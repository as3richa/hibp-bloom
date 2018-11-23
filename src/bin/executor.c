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

typedef struct st_command {
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
  void (*exec)(executor_t* ex, const struct st_command* command);
} command_t;

static void exec_status(executor_t* ex, const command_t* command);
static void exec_create(executor_t* ex, const command_t* command);
static void exec_create_maxmem(executor_t* ex, const command_t* command);
static void exec_create_falsepos(executor_t* ex, const command_t* command);
static void exec_load(executor_t* ex, const command_t* command);
static void exec_save(executor_t* ex, const command_t* command);
static void exec_unload(executor_t* ex, const command_t* command);
static void exec_insert(executor_t* ex, const command_t* command);
static void exec_insert_sha(executor_t* ex, const command_t* command);
static void exec_query(executor_t* ex, const command_t* command);
static void exec_query_sha(executor_t* ex, const command_t* command);
static void exec_falsepos(executor_t* ex, const command_t* command);
static void exec_sha(executor_t* ex, const command_t* command);
static void exec_help(executor_t* ex, const command_t* command);

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
    1, false, true, false,
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

#define HEX(v) ((0 <= (v) && (v) <= 9) ? ('0' + (v)) : ('a' + (v) - 10))

#define SHA1_BYTES 20

static inline void ex_error(executor_t* ex, executor_status_t status, const char* format, ...) {
  assert(status == EX_E_RECOVERABLE || status == EX_E_FATAL);

  ex->status = status;

  fprintf(
    stderr, "%s:%lu:%lu: ",
    ex->stream->name, (unsigned long)ex->token.line, (unsigned long)ex->token.column
  );

  va_list varargs;
  va_start(varargs, format);
  vfprintf(stderr, format, varargs);
  va_end(varargs);

  fputc('\n', stderr);
}

static inline void ex_arity_error(executor_t* ex, const command_t* command) {
  ex_error(
    ex, EX_E_RECOVERABLE,
    "%s takes %s %lu argument%s",
    command->name,
    (command->variadic ? "at least" : "exactly"),
    (unsigned long)command->min_arity,
    ((command->min_arity == 1) ? "" : "s")
  );
}

static inline int ex_next_token(executor_t* ex) {
  stream_t* stream = ex->stream;

  const tokenization_status_t status = next_token(&ex->token, stream);

  if(status == TS_OK) {
    return 0;
  }

  /* Allocation failure */
  if(status == TS_E_NOMEM) {
    ex_error(ex, EX_E_FATAL, "%s", OUT_OF_MEMORY_MESSAGE);
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

  ex_error(ex, EX_E_RECOVERABLE, "%s", message);
  return -1;
}

inline const char* hibp_strerror(hibp_status_t status) {
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
const command_t* find_command(executor_t* ex) {
  const token_t* token = &ex->token;

  for(size_t i = 0; i < N_COMMANDS; i ++) {
    if(token_eq(&ex->token, commands[i].name)) {
      return &commands[i];
    }
  }

  /* No such command; emit an error */

  /* token isn't necessarily printable (and it's not null-terminated anyway) */
  char* str = token2str(token);

  /* Swallow any allocation errors from token2str, since we're already
   * dealing with one failure case */
  ex_error(
    ex, EX_E_RECOVERABLE,
    "No such command %s; try `help` to list available commands",
    ((str == NULL) ? "" : str)
  );

  free(str);

  return NULL;
}

FILE* ex_fopen(executor_t* ex, bool in, bool binary) {
  const char* mode;

  if(binary) {
    mode = (in ? "rb" : "wb");
  } else {
    mode = (in ? "r" : "w");
  }

  /* stdin / stdout */
  if(token_eq(&ex->token, "-")) {
    FILE* file;

    if(in) {
      /* We can only consume stdin once */
      if(ex->stdin_consumed) {
        ex_error(ex, EX_E_RECOVERABLE, "standard input has already been consumed");
        return NULL;
      }

      file = freopen(NULL, mode, stdin);
    } else {
      file = freopen(NULL, mode, stdout);
    }

    /* FIXME: freopen doesn't necessarily set errno */
    if(file == NULL) {
      ex_error(ex, EX_E_FATAL, "%s", strerror(errno));
      return NULL;
    }

    return file;
  }

  /* Regular file */

  for(size_t i = 0; i < ex->token.length; i ++) {
    if(ex->token.buffer[i] == 0) {
      ex_error(ex, EX_E_RECOVERABLE, "null byte in filename");
      return NULL;
    }
  }

  char* filename = (char*)malloc(ex->token.length + 1);

  if(filename == NULL) {
    ex_error(ex, EX_E_FATAL, OUT_OF_MEMORY_MESSAGE);
    return NULL;
  }

  memcpy(filename, ex->token.buffer, ex->token.length);
  filename[ex->token.length] = 0;

  FILE* file = fopen(filename, mode);

  free(filename);

  /* FIXME: fopen doesn't necessarily set errno */
  if(file == NULL) {
    ex_error(ex, EX_E_FATAL, "%s", strerror(errno));
    return NULL;
  }

  return file;
}

void ex_fclose(FILE* file) {
  if(file == stdin || file == stdout) {
    return;
  }

  /* FIXME: check for errors */
  fclose(file);
}

/* ================================================================
 * Command callbacks
 * ================================================================ */

static void exec_status(executor_t* ex, const command_t* command) {
  assert(ex->filter_initialized);

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

static void exec_create(executor_t* ex, const command_t* command) {
  assert(!ex->filter_initialized);

  size_t n_hash_functions;
  size_t log2_bits;

  /* Read n_hash_functions */

  if(ex_next_token(ex) == -1) {
    return;
  }

  if(ex->token.last_of_command) {
    ex_arity_error(ex, command);
    return;
  }

  if(token2size(&n_hash_functions, &ex->token) == -1 || n_hash_functions == 0) {
    ex_error(ex, EX_E_RECOVERABLE, "n_hash_functions must be a positive integer");
    return;
  }

  /* Read log2_bits */

  if(ex_next_token(ex) == -1) {
    return;
  }

  if(!ex->token.last_of_command) {
    ex_arity_error(ex, command);
    return;
  }

  if(token2size(&log2_bits, &ex->token) == -1 || n_hash_functions == 0) {
    ex_error(ex, EX_E_RECOVERABLE, "log2_bits must be a positive integer");
    return;
  }

  /* Initialize filter */

  hibp_status_t status = hibp_bf_new(&ex->filter, n_hash_functions, log2_bits);

  if(status == HIBP_OK) {
    ex->filter_initialized = true;
    return;
  }

  ex_error(
    ex, ((status == HIBP_E_NOMEM) ? EX_E_FATAL : EX_E_RECOVERABLE),
    hibp_strerror(status)
  );
}

static void exec_create_maxmem(executor_t* ex, const command_t* command) {
  (void)ex;
}

static void exec_create_falsepos(executor_t* ex, const command_t* command) {
  (void)ex;
}

static void exec_load(executor_t* ex, const command_t* command) {
  assert(!ex->filter_initialized);

  if(ex_next_token(ex) == -1) {
    return;
  }

  if(!ex->token.last_of_command) {
    ex_arity_error(ex, command);
    return;
  }

  FILE* file = ex_fopen(ex, true, true);

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
  ex_error(ex, EX_E_RECOVERABLE, "%s", strerror(errno));
}

static void exec_save(executor_t* ex, const command_t* command) {
  assert(ex->filter_initialized);

  if(ex_next_token(ex) == -1) {
    return;
  }

  if(!ex->token.last_of_command) {
    ex_arity_error(ex, command);
    return;
  }

  FILE* file = ex_fopen(ex, false, true);

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
  ex_error(ex, EX_E_RECOVERABLE, "%s", strerror(errno));
}

static void exec_unload(executor_t* ex, const command_t* command) {
  (void)ex;
}

static void exec_insert(executor_t* ex, const command_t* command) {
  (void)command;

  assert(ex->filter_initialized);

  do {
    if(ex_next_token(ex) == -1) {
      return;
    }

    hibp_bf_insert(&ex->filter, ex->token.length, (hibp_byte_t*)ex->token.buffer);
  } while(!ex->token.last_of_command);
}

static void exec_insert_sha(executor_t* ex, const command_t* command) {
 (void)ex;
}

static void exec_query(executor_t* ex, const command_t* command) {
  (void)ex;

  assert(ex->filter_initialized);

  do {
    if(ex_next_token(ex) == -1) {
      return;
    }

    const bool found = hibp_bf_query(&ex->filter, ex->token.length, (hibp_byte_t*)ex->token.buffer);

    char* str = token2str(&ex->token);

    if(str == NULL) {
      ex_error(ex, EX_E_FATAL, OUT_OF_MEMORY_MESSAGE);
      return;
    }

    printf("%s", str);

    for(size_t i = strlen(str); i < 40; i ++) {
      putchar(' ');
    }

    free(str);

    puts(found ? "true" : "false");

  } while(!ex->token.last_of_command);
}

static void exec_query_sha(executor_t* ex, const command_t* command) {
  (void)ex;
}

static void exec_falsepos(executor_t* ex, const command_t* command) {
  (void)ex;
}


static void exec_sha(executor_t* ex, const command_t* command) {
  if(ex_next_token(ex) == -1) {
    return;
  }

  if(!ex->token.last_of_command) {
    ex_arity_error(ex, command);
    return;
  }

  unsigned char sha[SHA1_BYTES];
  SHA1((unsigned char*)ex->token.buffer, ex->token.length, sha);

  for(size_t i = 0; i < sizeof(sha); i ++) {
    putchar(HEX(sha[i] >> 4));
    putchar(HEX(sha[i] & 0xf));
  }
  putchar('\n');
}

static void exec_help(executor_t* ex, const command_t* command) {
  if(ex->token.last_of_command) {
    /* Unary version; list all available commands */

    puts("Available commands:");

    for(size_t i = 0; i < N_COMMANDS; i ++) {
      printf("~ %s %s\n", commands[i].name, commands[i].usage);
    }

    puts("Try `help <command>` to view detailed documentation for a command");

    return;
  }

  /* Binary version; give help for one particular command */

  if(ex_next_token(ex) == -1) {
    return;
  }

  if(!ex->token.last_of_command) {
    ex_error(ex, EX_E_RECOVERABLE, "%s", "help takes at most 1 argument");
    return;
  }

  const command_t* requested_command = find_command(ex);

  if(requested_command == NULL) {
    return;
  }

  printf(
    "\n  USAGE: %s %s\n  %s\n\n",
    requested_command->name, requested_command->usage, requested_command->description
  );
}

/* ================================================================
 * Public interface
 * ================================================================ */

void executor_new(executor_t* ex, stream_t* stream, int stdin_consumed) {
  ex->stream = stream;
  token_new(&ex->token);
  ex->stdin_consumed = stdin_consumed;
  ex->filter_initialized = 0;
  ex->status = EX_OK;
}

void executor_destroy(executor_t* ex) {
  token_destroy(&ex->token);
}

void executor_exec_one(executor_t* ex) {
  assert(ex->status == EX_OK);

  stream_t* stream = ex->stream;

  if(skip_to_command(stream) == EOF) {
    ex->status = EX_EOF;
    return;
  }

  if(ex_next_token(ex) == -1) {
    return;
  }

  const command_t* command = find_command(ex);

  if(command == NULL) {
    return;
  }

  const bool nullary = ex->token.last_of_command;

  if(nullary && command->min_arity > 0) {
    ex_arity_error(ex, command);
    return;
  }

  if(command->filter_required && !ex->filter_initialized) {
    ex_error(
      ex, EX_E_RECOVERABLE,
      "%s requires a loaded Bloom filter; try `help` to learn how to create or load a filter",
      command->name
    );
    return;
  }

  if(command->filter_unrequired && ex->filter_initialized) {
    ex_error(
      ex, EX_E_RECOVERABLE,
      "%s would overwrite the already-loaded filter; run `save` and `unload` first",
      command->name
    );
    return;
  }

  /* Just for an assertion below. NB. copy the value, not the pointer */
  const token_t prev_token = ex->token;
  (void)prev_token;

  command->exec(ex, command);

  /* Sanity checks: if the command was nullary, it shouldn't have read any new tokens at
   * all; irrespective of arity, if the command succeeded then the last-read token
   * should be the last token of the command */

  assert(!nullary || (ex->token.line == prev_token.line && ex->token.column == prev_token.column));
  assert(ex->status != EX_OK || ex->token.last_of_command);
}

void executor_drain_line(executor_t* ex) {
  ex->status = EX_OK;
  drain_line(ex->stream);
}
