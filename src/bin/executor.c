#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
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
  size_t max_arity;

  /* Fail if no filter loaded */
  bool filter_required;

  /* Fail if a filter _is_ loaded - so you don't clobber your work by accident */
  bool filter_unrequired;

  /* Callback */
  void (*exec)(executor_t* ex, size_t arity, const token_t* args);
} command_t;

static void exec_status(executor_t* ex, size_t arity, const token_t* args);
static void exec_create(executor_t* ex, size_t arity, const token_t* args);
static void exec_create_auto(executor_t* ex, size_t arity, const token_t* args);
static void exec_load(executor_t* ex, size_t arity, const token_t* args);
static void exec_save(executor_t* ex, size_t arity, const token_t* args);
static void exec_unload(executor_t* ex, size_t arity, const token_t* args);
static void exec_insert(executor_t* ex, size_t arity, const token_t* args);
static void exec_insert_sha(executor_t* ex, size_t arity, const token_t* args);
static void exec_insert_file(executor_t* ex, size_t arity, const token_t* args);
static void exec_query(executor_t* ex, size_t arity, const token_t* args);
static void exec_query_sha(executor_t* ex, size_t arity, const token_t* args);
static void exec_query_file(executor_t* ex, size_t arity, const token_t* args);
static void exec_falsepos(executor_t* ex, size_t arity, const token_t* args);
static void exec_sha(executor_t* ex, size_t arity, const token_t* args);
static void exec_help(executor_t* ex, size_t arity, const token_t* args);

#define N_COMMANDS (sizeof(commands) / sizeof(command_t))

static const command_t commands[] = {
  {
    "status",
    "",
    "Show information about the currently-loaded Bloom filter",
    0, 0,
    true, false,
    exec_status
  },

  {
    "create",
    "<n_hash_functions> <log2_bits>",
    (
      "Intialize a Bloom filter with n_hash_functions randomly-chosen hash functions\n"
      "and a bit vector of size 2**log2_bits. Tuning these values requires prior\n"
      "knowledge of the literature; to initialize a filter with sane defaults, use\n"
      "create-auto."
    ),
    2, 2,
    false, true,
    exec_create
  },

  {
    "create-auto",
    "<count> <rate> [<max_memory>]",
    (
      "Initialize a Bloom filter with an approximate goal false positive rate and an\n"
      "optional maximum permissable memory consumption (default 100MB), given the\n"
      "expected cardinality of the underlying set. If rate is not satisfiable within\n"
      "the given memory limit, the best-performing parameters within the memory limit\n"
      "shall be selected. max_memory can be given either as an integer number of\n"
      "bytes, or as a real number followed by a suffix indicating the units (e.g. 10M\n"
      "10gb, 0.5k, etc.). After creating a filter, try falsepos to empirically check\n"
      "the false positive rate."
    ),
    2, 2,
    false, true,
    exec_create_auto
  },

  {
    "load",
    "<filename>",
    "Load a previously-saved Bloom filter from disk.",
    1, 1,
    false, true,
    exec_load
  },

  {
    "save",
    "<filename>",
    "Save the currently-loaded Bloom filter to disk.",
    1, 1,
    true, false,
    exec_save
  },

  {
    "unload",
    "",
    "Unload the currently-loaded Bloom filter without persisting it to disk.",
    0, 0,
    true, false,
    exec_unload
  },

  {
    "insert",
    "<string> [... <string>]",
    "Insert one or several string(s) into the Bloom filter.",
    1, SIZE_MAX,
    true, false,
    exec_insert
  },

  {
    "insert-sha",
    "<hash> [... <hash>]",
    "Insert one or several string(s), encoded as SHA1 hashes, into the Bloom filter.",
    1, SIZE_MAX,
    true, false,
    exec_insert_sha
  },

  {
    "insert-file",
    "<filename> [<format>]",
    (
      "Insert a sequence of strings from the given file according to the specified\n"
      "format. format is either \"strings\" (default, whitespace-delimited strings),\n"
      "\"lines\" (full lines including leading/trailing whitespace), or \"shas\" (space-\n"
      "or comma-separated SHA1 hashes)."
    ),
    1, 2,
    true, false,
    exec_insert_file
  },

  {
    "query",
    "<string> [... <string>]",
    "Query for the presence of one or several string(s) in the Bloom filter.",
    1, SIZE_MAX,
    true, false,
    exec_query
  },

  {
    "query-sha",
    "<hash> [... <hash>]",
    (
      "Query for the presence of one or several string(s), encoded as SHA1 hashes,\n"
      "in the Bloom filter."
    ),
    1, SIZE_MAX,
    true, false,
    exec_query_sha
  },

  {
    "query-file",
    "<filename> [<format>]",
    (
      "Query for the presence of a sequence of strings from the given file according to\n"
      "the specified format. format is either \"strings\" (default, whitespace-delimited\n"
      "strings), \"lines\" (full lines including leading/trailing whitespace), or \"shas\"\n"
      "(space- or comma-separated SHA1 hashes)."
    ),
    1, 2,
    true, false,
    exec_query_file
  },

  {
    "falsepos",
    "[<trials>]",
    (
      "Empirically test the false positive rate of the currently-loaded Bloom\n"
      "filter by repeated random trials."
    ),
    0, 1,
    true, false,
    exec_falsepos
  },

  {
    "sha",
    "<string>",
    "Compute the SHA1 hash of the given string.",
    1, 1,
    false, false,
    exec_sha
  },

  {
    "help",
    "[<command>]",
    "List available commands, or show detailed documentation for one command.",
    0, 1,
    false, false,
    exec_help
  }
};

/* ================================================================
 * Types, constants, utilities
 * ================================================================ */

#define OUT_OF_MEMORY_MESSAGE "Out of memory"
#define BAD_SHA_MESSAGE "expected a SHA1 hash (40 hexademical digits)"

static const char* help_footer =
  "Try `help <command>` to view detailed documentation for a command.\n\n"
  "Commands are delimited by newlines or semicolons; individual tokens (i.e.\n"
  "command names or parameters) are delimited by whitespace. To pass a parameter\n"
  "containing whitespace or a semicolon, use single or double quotes. Quoted tokens\n"
  "support a limited set of escape sequences: \"\\n\", \"\\xhh\" (for any hexadecimal\n"
  "digits h), \"\\\"\", '\\''.\n\n"
  "Commands accepting a filename parameter can be directed to read from standard\n"
  "input or write to standard output by passing \"-\" (the hyphen character) as the\n"
  "filename. Because reading from standard input exhausts the stream, standard\n"
  "input can only be used as a parameter once per script run, and not at all if the\n"
  "session is interactive or if the script is given on the standard input.";

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

  fail(ex, EX_E_RECOVERABLE, NULL, message);
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
    fail(ex, EX_E_RECOVERABLE, token, "%s", strerror(errno));
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

typedef enum {
  SF_FORMAT_STRINGS,
  SF_FORMAT_LINES,
  SF_FORMAT_SHAS
} stringfile_format_t;

static inline int ex_token2format(stringfile_format_t* format, executor_t* ex, const token_t* token) {
  if(token_eq(token, "strings")) {
    (*format) = SF_FORMAT_STRINGS;
    return 0;
  }

  if(token_eq(token, "lines")) {
    (*format) = SF_FORMAT_LINES;
    return 0;
  }

  if(token_eq(token, "shas")) {
    (*format) = SF_FORMAT_SHAS;
    return 0;
  }

  char* str = token2str(token);

  /* Swallow any allocation errors from token2str */
  fail(
    ex, EX_E_RECOVERABLE, token,
    "Invalid format %s; expected strings, lines, or shas",
    ((str == NULL) ? "" : str)
  );

  free(str);

  return -1;
}

static inline int ex_open_stringfile(stream_t* stream, executor_t* ex, const token_t* filename) {
  FILE* file = ex_fopen(ex, filename, true, false);

  if(file == NULL) {
    return -1;
  }

  char* name = (file == stdin) ? strdup("<standard input>") : token2str(filename);

  if(name == NULL) {
    fail(ex, EX_E_FATAL, NULL, OUT_OF_MEMORY_MESSAGE);
    return -1;
  }

  stream_new_file(stream, file, name);

  return 0;
}

static inline void close_stringfile(stream_t* stream) {
  free((void*)stream->name);
  stream_close(stream);
}

static inline int stringfile_skip(stream_t* stream, stringfile_format_t format) {
  switch(format) {
    case SF_FORMAT_STRINGS:
      for(;;) {
        const int c = stream_peek(stream);

        if(c == EOF || !isspace(c)) {
          break;
        }

        stream_getc(stream);
      }
      break;
    case SF_FORMAT_SHAS:
      for(;;) {
        const int c = stream_peek(stream);

        if(c == EOF || (!isspace(c) && c != ',')) {
          break;
        }

        stream_getc(stream);
      }
      break;
    default:
      assert(format == SF_FORMAT_LINES);
      break;
  }

  return stream_peek(stream);
}

static inline int ex_stringfile_next_string(token_t* string, executor_t* ex, stream_t* stream) {
  for(;;) {
    const int c = stream_peek(stream);

    if(c == EOF || isspace(c)) {
      break;
    }

    if(token_pushc(string, c) == -1) {
      fail(ex, EX_E_FATAL, NULL, OUT_OF_MEMORY_MESSAGE);
      return -1;
    }

    stream_getc(stream);
  }

  assert(string->length > 0);

  return 0;
}

static inline int ex_stringfile_next_line(token_t* line, executor_t* ex, stream_t* stream) {
  for(;;) {
    const int c = stream_peek(stream);

    if(c == EOF || c == '\n') {
      break;
    }

    if(token_pushc(line, c) == -1) {
      fail(ex, EX_E_FATAL, NULL, OUT_OF_MEMORY_MESSAGE);
      return -1;
    }

    stream_getc(stream);
  }

  assert(line->length > 0);

  return 0;
}

static inline int ex_stringfile_next_sha(hibp_byte_t* sha, executor_t* ex, stream_t* stream) {
  for(size_t i = 0; i < 2 * SHA1_BYTES; i ++) {
    int nybble;

    const int c = stream_getc(stream);

    if('0' <= c && c <= '9') {
      nybble = c - '0';
    } else if('a' <= c && c <= 'f') {
      nybble = 10 + c - 'a';
    } else if('A' <= c && c <= 'F') {
      nybble = 10 + c - 'A';
    } else {
      fail(
        ex, EX_E_RECOVERABLE, NULL,
        "malformed SHA1 hash in %s at %lu:%lu",
        stream->name, stream->line, stream->column
      );
      return -1;
    }

    if(i % 2 == 0) {
      sha[i / 2] = (nybble << 4);
    } else {
      sha[i / 2] |= nybble;
    }
  }

  return 0;
}

/* ================================================================
 * Command callbacks
 * ================================================================ */

static void exec_status(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity == 0);
  (void)arity;
  (void)args;

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

static void exec_create(executor_t* ex, size_t arity, const token_t* args) {
  assert(!ex->filter_initialized);
  assert(arity == 2);
  (void)arity;

  size_t n_hash_functions;
  size_t log2_bits;

  /* Parse parameters */

  if(token2size(&n_hash_functions, &args[0]) == -1 || n_hash_functions == 0) {
    fail(ex, EX_E_RECOVERABLE, &args[0], "n_hash_functions must be a positive integer");
    return;
  }

  if(token2size(&log2_bits, &args[1]) == -1 || n_hash_functions == 0) {
    fail(ex, EX_E_RECOVERABLE, &args[1], "log2_bits must be a positive integer");
    return;
  }

  /* Initialize filter */

  hibp_status_t status = hibp_bf_new(&ex->filter, n_hash_functions, log2_bits);

  if(status == HIBP_OK) {
    ex->filter_initialized = true;
    return;
  }

  fail(
    ex, ((status == HIBP_E_NOMEM) ? EX_E_FATAL : EX_E_RECOVERABLE), NULL,
    hibp_strerror(status)
  );
}

static void exec_create_auto(executor_t* ex, size_t arity, const token_t* args) {
  assert(!ex->filter_initialized);
  assert(arity == 2 || arity == 3);

  size_t count;
  double rate;
  size_t maxmem;

  /* Parse parameters */

  if(token2size(&count, &args[0]) == -1) {
    fail(ex, EX_E_RECOVERABLE, &args[0], "count must be a non-negative integer");
    return;
  }

  if(token2double(&rate, &args[1]) == -1) {
    fail(ex, EX_E_RECOVERABLE, &args[1], "rate must be a non-negative real number");
  }

  if(arity == 3) {
    if(token2memsize(&maxmem, &args[2]) == -1) {
      fail(ex, EX_E_RECOVERABLE, &args[2], "maxmem must be a quantity of memory");
      return;
    }
  } else {
    maxmem = 100 * 1024 * 1024;
  }

  size_t n_hash_functions;
  size_t log2_bits;

  /* Compute the parameters that would (with high probability) give a false positive
   * rate of rate (assuming that the cardinality of the underlying set is count) */

  hibp_compute_optimal_params(&n_hash_functions, &log2_bits, count, rate);

  const size_t memory = hibp_compute_total_size(n_hash_functions, log2_bits);

  /* If satisfying rate would eat too much memory, fall back on the best possible
   * false positive rate that fits within the limit */

  if(memory > maxmem) {
    hibp_compute_constrained_params(&n_hash_functions, &log2_bits, count, maxmem);
  }

  /* Initialize filter */

  hibp_status_t status = hibp_bf_new(&ex->filter, n_hash_functions, log2_bits);

  if(status == HIBP_OK) {
    ex->filter_initialized = true;
    return;
  }

  fail(
    ex, ((status == HIBP_E_NOMEM) ? EX_E_FATAL : EX_E_RECOVERABLE), NULL,
    hibp_strerror(status)
  );
}

static void exec_load(executor_t* ex, size_t arity, const token_t* args) {
  assert(!ex->filter_initialized);
  assert(arity == 1);
  (void)arity;

  FILE* file = ex_fopen(ex, &args[0], true, true);

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
  fail(ex, EX_E_RECOVERABLE, &args[0], "%s", strerror(errno));
}

static void exec_save(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity == 1);
  (void)arity;

  FILE* file = ex_fopen(ex, &args[0], false, true);

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
  fail(ex, EX_E_RECOVERABLE, &args[0], "%s", strerror(errno));
}

static void exec_unload(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity == 0);
  (void)arity;
  (void)args;

  hibp_bf_destroy(&ex->filter);
  ex->filter_initialized = false;
}

static void exec_insert(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity > 0);

  for(size_t i = 0; i < arity; i ++) {
    const token_t* token = &args[i];
    hibp_bf_insert(&ex->filter, token->length, (hibp_byte_t*)token->buffer);
  }
}

static void exec_insert_sha(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity > 0);

  for(size_t i = 0; i < arity; i ++) {
    const token_t* token = &args[i];

    hibp_byte_t sha[SHA1_BYTES];

    if(token2sha(sha, token) == -1) {
      fail(ex, EX_E_RECOVERABLE, token, BAD_SHA_MESSAGE);
    }

    hibp_bf_insert_sha1(&ex->filter, sha);
  }
}

static void exec_insert_file(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity == 1 || arity == 2);

  stringfile_format_t format;

  if(arity == 2) {
    if(ex_token2format(&format, ex, &args[1]) == -1) {
      return;
    }
  } else {
    format = SF_FORMAT_STRINGS;
  }

  stream_t stream;

  if(ex_open_stringfile(&stream, ex, &args[0]) == -1) {
    return;
  }

  size_t inserted = 0;

  for(;;) {
    if(stringfile_skip(&stream, format) == EOF) {
      break;
    }

    if(format == SF_FORMAT_SHAS) {
      hibp_byte_t sha[SHA1_BYTES];

      if(ex_stringfile_next_sha(sha, ex, &stream) == -1) {
        break;
      }

      hibp_bf_insert_sha1(&ex->filter, sha);
    } else {
      token_t token;
      token_new(&token);

      if(format == SF_FORMAT_STRINGS) {
        if(ex_stringfile_next_string(&token, ex, &stream) == -1) {
          token_destroy(&token);
          break;
        }
      } else {
        assert(format == SF_FORMAT_LINES);

        if(ex_stringfile_next_line(&token, ex, &stream) == -1) {
          token_destroy(&token);
          break;
        }
      }

      hibp_bf_insert(&ex->filter, token.length, (hibp_byte_t*)token.buffer);

      token_destroy(&token);
    }

    inserted ++;
  }

  /* FIXME: every size_t => unsigned long cast is suspicious. Wish C stdlib sucked less */
  printf(
    "insert-file: inserted %lu %s%s from %s.\n",
    (unsigned long)inserted,
    ((format == SF_FORMAT_SHAS) ? "SHA" : "string"),
    ((inserted == 1) ? "" : "s"),
    stream.name
  );

  close_stringfile(&stream);
}

static void exec_query(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity > 0);

  for(size_t i = 0; i < arity; i ++) {
    const token_t* token = &args[i];
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

static void exec_query_sha(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity > 0);

  for(size_t i = 0; i < arity; i ++) {
    const token_t* token = &args[i];

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

static void exec_query_file(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity == 1 || arity == 2);

  stringfile_format_t format;

  if(arity == 2) {
    if(ex_token2format(&format, ex, &args[1]) == -1) {
      return;
    }
  } else {
    format = SF_FORMAT_STRINGS;
  }

  stream_t stream;

  if(ex_open_stringfile(&stream, ex, &args[0]) == -1) {
    return;
  }

  for(;;) {
    if(stringfile_skip(&stream, format) == EOF) {
      close_stringfile(&stream);
      return;
    }

    if(format == SF_FORMAT_SHAS) {
      hibp_byte_t sha[SHA1_BYTES];

      if(ex_stringfile_next_sha(sha, ex, &stream) == -1) {
        close_stringfile(&stream);
        return;
      }

      const bool found = hibp_bf_query_sha1(&ex->filter, sha);

      for(size_t i = 0; i < SHA1_BYTES; i ++) {
        putchar(HEX(sha[i] >> 4));
        putchar(HEX(sha[i] & 0xf));
      }

      puts(found ? "  true" : "  false");
    } else {
      token_t token;
      token_new(&token);

      if(format == SF_FORMAT_STRINGS) {
        if(ex_stringfile_next_string(&token, ex, &stream) == -1) {
          token_destroy(&token);
          close_stringfile(&stream);
          return;
        }
      } else {
        assert(format == SF_FORMAT_LINES);

        if(ex_stringfile_next_line(&token, ex, &stream) == -1) {
          token_destroy(&token);
          close_stringfile(&stream);
          return;
        }
      }

      char* str = token2str(&token);

      if(str == NULL) {
        token_destroy(&token);
        close_stringfile(&stream);
        return;
      }

      const bool found = hibp_bf_query(&ex->filter, token.length, (hibp_byte_t*)token.buffer);

      printf("%s  %s\n", str, (found ? "true" : "false"));

      free(str);
      token_destroy(&token);
    }
  }
}

static void exec_falsepos(executor_t* ex, size_t arity, const token_t* args) {
  assert(ex->filter_initialized);
  assert(arity == 0 || arity == 1);

  size_t trials = 10000;

  if(arity == 1) {
    if(token2size(&trials, &args[0]) == -1) {
      fail(ex, EX_E_RECOVERABLE, &args[0], "trials must be a positive integer");
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


static void exec_sha(executor_t* ex, size_t arity, const token_t* args) {
  assert(arity == 1);
  (void)ex;
  (void)arity;

  const token_t* token = &args[0];

  unsigned char sha[SHA1_BYTES];
  SHA1((unsigned char*)token->buffer, token->length, sha);

  for(size_t i = 0; i < sizeof(sha); i ++) {
    putchar(HEX(sha[i] >> 4));
    putchar(HEX(sha[i] & 0xf));
  }

  putchar('\n');
}

static void exec_help(executor_t* ex, size_t arity, const token_t* args) {
  assert(arity == 0 || arity == 1);

  if(arity == 0) {
    /* Nullary version; list all available commands */

    puts("\nAvailable commands:");

    for(size_t i = 0; i < N_COMMANDS; i ++) {
      printf("  %s %s\n", commands[i].name, commands[i].usage);
    }

    putchar('\n');
    puts(help_footer);
    putchar('\n');

    return;
  }

  /* Unary version; give help for one particular command */

  const command_t* command = find_command(ex, &args[0]);

  if(command == NULL) {
    return;
  }

  printf(
    "\n  USAGE: %s %s\n\n%s\n\n",
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

  size_t arity = 0;
  token_t* args = NULL;

  if(ex_next_token(&command_name, ex) == -1) {
    return;
  }

  const bool nullary = (command_name.last_of_command);

  if(!nullary) {
    size_t capacity = 0;

    do {
      assert(arity <= capacity);

      if(arity == capacity) {
        capacity = ((capacity == 0) ? 8 : (2 * capacity));
        token_t* next = (token_t*)realloc(args, sizeof(token_t) * capacity);

        if(next == NULL) {
          fail(ex, EX_E_FATAL, NULL, OUT_OF_MEMORY_MESSAGE);

          while(arity --) {
            token_destroy(&args[arity]);
          }
          free(args);

          return;
        }

        args = next;
      }

      assert(arity < capacity);
      assert(args != NULL);

      if(ex_next_token(&args[arity ++], ex) == -1) {
        while(arity --) {
          token_destroy(&args[arity]);
        }
        free(args);

        return;
      }
    } while(!args[arity - 1].last_of_command);

    /* Shrink args, just for hygiene */
    if(arity < capacity) {
      token_t* next = realloc(args, sizeof(token_t) * capacity);

      /* Swallow any error since we can keep chugging along */
      if(next != NULL) {
        args = next;
      }
    }
  }

  const command_t* command = find_command(ex, &command_name);

  if(command == NULL) {
    while(arity --) {
      token_destroy(&args[arity]);
    }
    free(args);

    return;
  }

  if(arity < command->min_arity) {
    const bool variadic = (command->min_arity < command->max_arity);

    fail(
      ex,
      EX_E_RECOVERABLE,
      &command_name,
      "%s takes %s %lu argument%s",
      command->name,
      (variadic ? "at least" : "exactly"),
      (unsigned long)command->min_arity,
      ((command->min_arity == 1) ? "" : "s")
    );

    free(args);
    return;
  }

  if(arity > command->max_arity) {
    const bool variadic = (command->min_arity < command->max_arity);

    fail(
      ex,
      EX_E_RECOVERABLE,
      &command_name,
      "%s takes %s %lu argument%s",
      command->name,
      (variadic ? "at most" : "exactly"),
      (unsigned long)command->max_arity,
      ((command->min_arity == 1) ? "" : "s")
    );

    while(arity --) {
      token_destroy(&args[arity]);
    }
    free(args);

    return;
  }

  if(command->filter_required && !ex->filter_initialized) {
    fail(
      ex, EX_E_RECOVERABLE, &command_name,
      "%s requires a loaded Bloom filter; try `help` to learn how to create or load a filter",
      command->name
    );

    while(arity --) {
      token_destroy(&args[arity]);
    }
    free(args);

    return;
  }

  if(command->filter_unrequired && ex->filter_initialized) {
    fail(
      ex, EX_E_RECOVERABLE, &command_name,
      "%s would overwrite the already-loaded filter; run `save` and `unload` first",
      command->name
    );

    while(arity --) {
      token_destroy(&args[arity]);
    }
    free(args);

    return;
  }

  command->exec(ex, arity, args);

  while(arity --) {
    token_destroy(&args[arity]);
  }
  free(args);
}

void executor_drain_line(executor_t* ex) {
  ex->status = EX_OK;
  drain_line(ex->stream);
}
