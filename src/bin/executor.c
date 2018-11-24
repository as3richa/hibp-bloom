#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <openssl/sha.h>

#include "executor.h"

/* ================================================================
 * Command dispatch table
 * ================================================================ */

typedef struct {
  /* name by which the command is invoked */
  const char* name;

  /* Description of parameters like in a manpage, e.g. <filename> [<encoding>] */
  const char* usage;

  /* Plain-English description */
  const char* description;

  size_t min_arity;
  bool variadic;

  /* Fail if no filter loaded */
  bool filter_required;

  /* Fail if a filter _is_ loaded - so you don't clobber your work by accident */
  bool filter_unrequired;

  /* Callback */
  void (*exec)(executor_t* ex, size_t argc, const token_t* argv);
} command_t;

static void exec_status(executor_t* ex, size_t argc, const token_t* argv);
static void exec_create(executor_t* ex, size_t argc, const token_t* argv);
static void exec_create_maxmem(executor_t* ex, size_t argc, const token_t* argv);
static void exec_create_falsepos(executor_t* ex, size_t argc, const token_t* argv);
static void exec_load(executor_t* ex, size_t argc, const token_t* argv);
static void exec_save(executor_t* ex, size_t argc, const token_t* argv);
static void exec_unload(executor_t* ex, size_t argc, const token_t* argv);
static void exec_insert(executor_t* ex, size_t argc, const token_t* argv);
static void exec_insert_sha(executor_t* ex, size_t argc, const token_t* argv);
static void exec_query(executor_t* ex, size_t argc, const token_t* argv);
static void exec_query_sha(executor_t* ex, size_t argc, const token_t* argv);
static void exec_falsepos(executor_t* ex, size_t argc, const token_t* argv);
static void exec_sha(executor_t* ex, size_t argc, const token_t* argv);
static void exec_help(executor_t* ex, size_t argc, const token_t* argv);

#define N_COMMANDS (sizeof(commands) / sizeof(command_t))

static const command_t commands[] = {
  {
    "status",
    "",
    "Show information about the currently-loaded Bloom filter",
    0, false, true, false,
    exec_status
  },

  {
    "create",
    "<n_hash_functions> <log2_bits>",
    "Intialize a Bloom filter with n_hash_functions randomly-chosen hash functions and a bit vector of size (2**log2_bits).",
    2, false, false, true,
    exec_create
  },

  {
    "create-maxmem",
    "<count> <max_memory>",
    "Intialize a Bloom filter with an approximate memory limit, given the expected cardinality of the set.",
    2, false, false, true,
    exec_create_maxmem
  },

  {
    "create-falsepos",
    "<count> <rate>",
    "Initialize a Bloom filter with an approximate goal false positive rate, given the expected cardinality of the set.",
    2, false, false, true,
    exec_create_falsepos
  },

  {
    "load",
    "<filename>",
    "Load a previously-saved Bloom filter from disk.",
    1, false, false, true,
    exec_load
  },

  {
    "save",
    "<filename>",
    "Save the currently-loaded Bloom filter to disk.",
    1, false, true, false,
    exec_save
  },

  {
    "unload",
    "",
    "Unload the currently-loaded Bloom filter without persisting it to disk.",
    0, false, true, false,
    exec_unload
  },

  {
    "insert",
    "<string> [... <string>]",
    "Insert one or several string(s) into the Bloom filter.",
    1, true, true, false,
    exec_insert
  },

  {
    "insert-sha",
    "<hash> [... <hash>]",
    "Insert one or several string(s), encoded as SHA1 hashes, into the Bloom filter.",
    1, true, true, false,
    exec_insert_sha
  },

  {
    "query",
    "<string> [... <string>]",
    "Query for the presence of one or several string(s) in the Bloom filter.",
    1, true, true, false,
    exec_query
  },

  {
    "query-sha",
    "<hash> [... <hash>]",
    "Query for the presence of one or several string(s), encoded as SHA1 hashes, in the Bloom filter.",
    1, true, true, false,
    exec_query_sha
  },

  {
    "falsepos",
    "[<trials>]",
    "Empirically test the false positive rate of the currently-loaded Bloom filter by repeated random trials.",
    0, true, true, false,
    exec_falsepos
  },

  {
    "sha",
    "<string>",
    "Compute the SHA1 hash of the given string.",
    1, false, false, false,
    exec_sha
  },

  {
    "help",
    "[<command>]",
    "List available commands, or show detailed documentation for one command.",
    0, true, false, false,
    exec_help
  }
};

/* ================================================================
 * Types, constants, utilities
 * ================================================================ */

#define OUT_OF_MEMORY_MESSAGE "Out of memory"
#define BAD_SHA_MESSAGE "expected a SHA1 hash (40 hexademical digits)"

#define HEX(v) ((0 <= (v) && (v) <= 9) ? ('0' + (v)) : ('a' + (v) - 10))

#define SHA1_BYTES 20

static inline void fail(executor_t* ex, executor_status_t status, const token_t* token, const char* format, ...) {
  assert(status == EX_E_RECOVERABLE || status == EX_E_FATAL);

  ex->status = status;

  unsigned long line;
  unsigned long column;

  if(token != NULL) {
    line = (unsigned long)token->line;
    column = (unsigned long)token->column;
  } else {
    line = (unsigned long)ex->stream->line;
    column = (unsigned long)ex->stream->column;
  }

  fprintf(stderr, "%s:%lu:%lu: ", ex->stream->name, line, column);

  va_list varargs;
  va_start(varargs, format);
  vfprintf(stderr, format, varargs);
  va_end(varargs);

  fputc('\n', stderr);
}

static inline int ex_next_token(token_t* token, executor_t* ex) {
  token_new(token);

  const tokenization_status_t status = next_token(token, ex->stream);

  if(status == TS_OK) {
    return 0;
  }

  /* Allocation failure */
  if(status == TS_E_NOMEM) {
    fail(ex, EX_E_FATAL, NULL, OUT_OF_MEMORY_MESSAGE);
    return -1;
  }

  /* Parse error of some kind */

  char* message;
  switch(status) {
    case TS_E_BAD_ESCAPE:
      message = "Bad escape code in quoted token";
      break;
    case TS_E_MISSING_QUOTE:
      message = "Missing closing quote character";
      break;
    case TS_E_MISSING_SEP:
      message = "Expected a space after quoted token";
      break;
    default:
      message = NULL;
      assert(0);
  }

  fail(ex, EX_E_RECOVERABLE, token, message);
  return -1;
}

static inline const char* hibp_strerror(hibp_status_t status) {
  /* FIXME: think about these messages a bit more */
  switch(status) {
    case HIBP_E_NOMEM:
      return OUT_OF_MEMORY_MESSAGE;
    case HIBP_E_VERSION:
      return "Bad version string; file is not an hibp-bloom filter, or may be corrupted";
    case HIBP_E_IO:
      return "Unexpected end of file; file is likely corrupted";
    case HIBP_E_CHECKSUM:
      return "Failed checksum validation; file is likely corrupted";
    case HIBP_E_2BIG:
      return "Filter parameters exceed size limits";
    case HIBP_E_INVAL:
      return "Filter parameters are invalid; file is likely corrupted";
    default:
      assert(0);
      return NULL;
  }
}

/* Find a command by the name given in ex->token. Emit an error and return NULL if no matching
 * command exists */
const command_t* find_command(executor_t* ex, const token_t* token) {
  for(size_t i = 0; i < N_COMMANDS; i ++) {
    if(token_eq(token, commands[i].name)) {
      return &commands[i];
    }
  }

  /* No such command; emit an error */

  /* token isn't necessarily printable (and it's not null-terminated anyway) */
  char* str = token2str(token);

  /* Swallow any allocation errors from token2str, since we're already
   * dealing with one failure case */
  fail(
    ex, EX_E_RECOVERABLE, token,
    "No such command %s; try `help` to list available commands",
    ((str == NULL) ? "" : str)
  );

  free(str);

  return NULL;
}

static inline FILE* ex_fopen(executor_t* ex, const token_t* token, bool in, bool binary) {
  const char* mode;

  if(binary) {
    mode = (in ? "rb" : "wb");
  } else {
    mode = (in ? "r" : "w");
  }

  /* stdin / stdout */
  if(token_eq(token, "-")) {
    FILE* file;

    if(in) {
      /* We can only consume stdin once */
      if(ex->stdin_consumed) {
        fail(ex, EX_E_RECOVERABLE, token, "standard input has already been consumed");
        return NULL;
      }

      file = freopen(NULL, mode, stdin);
    } else {
      file = freopen(NULL, mode, stdout);
    }

    /* FIXME: freopen doesn't necessarily set errno */
    if(file == NULL) {
      fail(ex, EX_E_FATAL, token, "%s", strerror(errno));
      return NULL;
    }

    return file;
  }

  /* Regular file */

  for(size_t i = 0; i < token->length; i ++) {
    if(token->buffer[i] == 0) {
      fail(ex, EX_E_RECOVERABLE, token, "null byte in filename");
      return NULL;
    }
  }

  char* filename = (char*)malloc(token->length + 1);

  if(filename == NULL) {
    fail(ex, EX_E_FATAL, token, OUT_OF_MEMORY_MESSAGE);
    return NULL;
  }

  memcpy(filename, token->buffer, token->length);
  filename[token->length] = 0;

  FILE* file = fopen(filename, mode);

  free(filename);

  /* FIXME: fopen doesn't necessarily set errno */
  if(file == NULL) {
    fail(ex, EX_E_FATAL, token, "%s", strerror(errno));
    return NULL;
  }

  return file;
}

static inline void ex_fclose(FILE* file) {
  if(file == stdin || file == stdout) {
    return;
  }

  /* FIXME: check for errors */
  fclose(file);
}

/* ================================================================
 * Command callbacks
 * ================================================================ */

static void exec_status(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc == 0);
  (void)argc;
  (void)argv;

  const char* format =
    "n_hash_functions:  %u\n"
    "log2_bits:         %u\n"
    "Bits:              %u\n"
    "Total memory:      %.2lf MB\n";

  hibp_filter_info_t info;
  hibp_bf_get_info(&info, &ex->filter);

  printf(
    format,
    (unsigned long)info.n_hash_functions,
    (unsigned long)info.log2_bits,
    (unsigned long)info.bits,
    info.memory / (double)(1024 * 1024)
  );
}

static void exec_create(executor_t* ex, size_t argc, const token_t* argv) {
  assert(!ex->filter_initialized);
  assert(argc == 2);

  size_t n_hash_functions;
  size_t log2_bits;

  /* Parse parameters */

  if(token2size(&n_hash_functions, &argv[0]) == -1 || n_hash_functions == 0) {
    fail(ex, EX_E_RECOVERABLE, &argv[0], "n_hash_functions must be a positive integer");
    return;
  }

  if(token2size(&log2_bits, &argv[1]) == -1 || n_hash_functions == 0) {
    fail(ex, EX_E_RECOVERABLE, &argv[1], "log2_bits must be a positive integer");
    return;
  }

  /* Initialize filter */

  hibp_status_t status = hibp_bf_new(&ex->filter, n_hash_functions, log2_bits);

  if(status == HIBP_OK) {
    ex->filter_initialized = true;
    return;
  }

  fail(
    ex, ((status == HIBP_E_NOMEM) ? EX_E_FATAL : EX_E_RECOVERABLE), &argv[1],
    hibp_strerror(status)
  );
}

static void exec_create_maxmem(executor_t* ex, size_t argc, const token_t* argv) {
  (void)ex;
  (void)argc;
  (void)argv;
}

static void exec_create_falsepos(executor_t* ex, size_t argc, const token_t* argv) {
  (void)ex;
  (void)argc;
  (void)argv;
}

static void exec_load(executor_t* ex, size_t argc, const token_t* argv) {
  assert(!ex->filter_initialized);
  assert(argc == 1);

  FILE* file = ex_fopen(ex, &argv[0], true, true);

  if(file == NULL) {
    return;
  }

  const hibp_status_t status = hibp_bf_load_file(&ex->filter, file);

  ex_fclose(file);

  if(status == HIBP_OK) {
    ex->filter_initialized = 1;
    return;
  }

  assert(status == HIBP_E_IO);

  /* FIXME: errno isn't necessarily set by fwrite and friends */
  fail(ex, EX_E_RECOVERABLE, &argv[0], "%s", strerror(errno));
}

static void exec_save(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc == 1);

  FILE* file = ex_fopen(ex, &argv[0], false, true);

  if(file == NULL) {
    return;
  }

  const hibp_status_t status = hibp_bf_save_file(&ex->filter, file);

  ex_fclose(file);

  if(status == HIBP_OK) {
    return;
  }

  assert(status == HIBP_E_IO);

  /* FIXME: errno isn't necessarily set by fwrite and friends */
  fail(ex, EX_E_RECOVERABLE, &argv[0], "%s", strerror(errno));
}

static void exec_unload(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc == 0);
  (void)argc;
  (void)argv;

  hibp_bf_destroy(&ex->filter);
  ex->filter_initialized = false;
}

static void exec_insert(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc > 0);

  for(size_t i = 0; i < argc; i ++) {
    const token_t* token = &argv[i];
    hibp_bf_insert(&ex->filter, token->length, (hibp_byte_t*)token->buffer);
  }
}

static void exec_insert_sha(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc > 0);

  for(size_t i = 0; i < argc; i ++) {
    const token_t* token = &argv[i];

    hibp_byte_t sha[SHA1_BYTES];

    if(token2sha(sha, token) == -1) {
      fail(ex, EX_E_RECOVERABLE, token, BAD_SHA_MESSAGE);
    }

    hibp_bf_insert_sha1(&ex->filter, sha);
  }
}

static void exec_query(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc > 0);

  for(size_t i = 0; i < argc; i ++) {
    const token_t* token = &argv[i];
    const bool found = hibp_bf_query(&ex->filter, token->length, (hibp_byte_t*)token->buffer);

    char* str = token2str(token);

    if(str == NULL) {
      fail(ex, EX_E_FATAL, NULL, OUT_OF_MEMORY_MESSAGE);
      return;
    }

    printf("%s  %s\n", str, (found ? "true" : "false"));

    free(str);
  }
}

static void exec_query_sha(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc > 0);

  for(size_t i = 0; i < argc; i ++) {
    const token_t* token = &argv[i];

    hibp_byte_t sha[SHA1_BYTES];

    if(token2sha(sha, token) == -1) {
      fail(ex, EX_E_RECOVERABLE, token, BAD_SHA_MESSAGE);
      return;
    }

    const bool found = hibp_bf_query_sha1(&ex->filter, sha);

    /* No need for token2str shenanigans, because we know for sure that it's
     * printable verbatim */
    fwrite(token->buffer, 1, token->length, stdout);
    printf("  %s\n", (found ? "true" : "false"));
  }
}

static void exec_falsepos(executor_t* ex, size_t argc, const token_t* argv) {
  assert(ex->filter_initialized);
  assert(argc == 0 || argc == 1);

  size_t trials = 10000;

  if(argc == 1) {
    if(token2size(&trials, &argv[0]) == -1) {
      fail(ex, EX_E_RECOVERABLE, &argv[0], "trials must be a positive integer");
      return;
    }
  }

  size_t positive = 0;

  for(size_t i = 0; i < trials; i ++) {
    hibp_byte_t value[100];

    for(size_t j = 0; j < sizeof(value); j ++) {
      value[j] = rand() % 256;
    }

    positive += hibp_bf_query(&ex->filter, sizeof(value), value);
  }

  printf("%lf\n", (double)positive / trials);
}


static void exec_sha(executor_t* ex, size_t argc, const token_t* argv) {
  assert(argc == 1);
  (void)ex;
  (void)argc;

  const token_t* token = &argv[0];

  unsigned char sha[SHA1_BYTES];
  SHA1((unsigned char*)token->buffer, token->length, sha);

  for(size_t i = 0; i < sizeof(sha); i ++) {
    putchar(HEX(sha[i] >> 4));
    putchar(HEX(sha[i] & 0xf));
  }

  putchar('\n');
}

static void exec_help(executor_t* ex, size_t argc, const token_t* argv) {
  assert(argc == 0 || argc == 1);

  if(argc == 0) {
    /* Nullary version; list all available commands */

    puts("Available commands:");

    for(size_t i = 0; i < N_COMMANDS; i ++) {
      printf("~ %s %s\n", commands[i].name, commands[i].usage);
    }

    puts("Try `help <command>` to view detailed documentation for a command");

    return;
  }

  /* Unary version; give help for one particular command */

  const command_t* command = find_command(ex, &argv[0]);

  if(command == NULL) {
    return;
  }

  printf(
    "\n  USAGE: %s %s\n  %s\n\n",
    command->name, command->usage, command->description
  );
}

/* ================================================================
 * Public interface
 * ================================================================ */

void executor_new(executor_t* ex, stream_t* stream, int stdin_consumed) {
  ex->stream = stream;
  ex->stdin_consumed = stdin_consumed;
  ex->filter_initialized = 0;
  ex->status = EX_OK;
}

void executor_destroy(executor_t* ex) {
  (void)ex;
}

void executor_exec_one(executor_t* ex) {
  assert(ex->status == EX_OK);

  if(skip_to_command(ex->stream) == EOF) {
    ex->status = EX_EOF;
    return;
  }

  token_t command_name;

  size_t argc = 0;
  token_t* argv = NULL;

  if(ex_next_token(&command_name, ex) == -1) {
    return;
  }

  const bool nullary = (command_name.last_of_command);

  if(!nullary) {
    size_t capacity = 0;

    do {
      assert(argc <= capacity);

      if(argc == capacity) {
        capacity = ((capacity == 0) ? 8 : (2 * capacity));
        token_t* next = (token_t*)realloc(argv, sizeof(token_t) * capacity);

        if(next == NULL) {
          fail(ex, EX_E_FATAL, NULL, OUT_OF_MEMORY_MESSAGE);
          free(argv);
          return;
        }

        argv = next;
      }

      assert(argc < capacity);
      assert(argv != NULL);

      if(ex_next_token(&argv[argc ++], ex) == -1) {
        free(argv);
        return;
      }
    } while(!argv[argc - 1].last_of_command);

    /* Shrink argv, just for hygiene */
    if(argc < capacity) {
      token_t* next = realloc(argv, sizeof(token_t) * capacity);

      /* Swallow any error since we can keep chugging along */
      if(next != NULL) {
        argv = next;
      }
    }
  }

  const command_t* command = find_command(ex, &command_name);

  if(command == NULL) {
    free(argv);
    return;
  }

  if(nullary && command->min_arity > 0) {
    fail(
      ex,
      EX_E_RECOVERABLE,
      &command_name,
      "%s takes %s %lu argument%s",
      command->name,
      (command->variadic ? "at least" : "exactly"),
      (unsigned long)command->min_arity,
      ((command->min_arity == 1) ? "" : "s")
    );
    free(argv);
    return;
  }

  if(command->filter_required && !ex->filter_initialized) {
    fail(
      ex, EX_E_RECOVERABLE, &command_name,
      "%s requires a loaded Bloom filter; try `help` to learn how to create or load a filter",
      command->name
    );
    free(argv);
    return;
  }

  if(command->filter_unrequired && ex->filter_initialized) {
    fail(
      ex, EX_E_RECOVERABLE, &command_name,
      "%s would overwrite the already-loaded filter; run `save` and `unload` first",
      command->name
    );
    free(argv);
    return;
  }

  command->exec(ex, argc, argv);

  free(argv);
}

void executor_drain_line(executor_t* ex) {
  ex->status = EX_OK;
  drain_line(ex->stream);
}
