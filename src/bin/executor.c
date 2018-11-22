#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <openssl/sha.h>

#include "executor.h"
#include "command-defns.h"

/* ================================================================
 * Plumbing
 * ================================================================ */

#define OUT_OF_MEMORY_MESSAGE "Out of memory"
#define NO_SUCH_COMMAND_MESSAGE "No such command %s; try `help` to list available commands"

#define HEX(v) ((0 <= (v) && (v) <= 9) ? ('0' + (v)) : ('a' + (v) - 10))

#define SHA1_BYTES 20

static inline void error(executor_t* ex, executor_status_t status, const char* format, ...) {
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

static inline int my_next_token(executor_t* ex) {
  stream_t* stream = ex->stream;

  const tokenization_status_t status = next_token(&ex->token, stream);

  if(status == TS_OK) {
    return 0;
  }

  /* Allocation failure */
  if(status == TS_E_NOMEM) {
    error(ex, EX_E_FATAL, "%s", OUT_OF_MEMORY_MESSAGE);
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

  error(ex, EX_E_RECOVERABLE, "%s", message);
  return -1;
}

/* ================================================================
 * Commands
 * ================================================================ */

static void exec_create(executor_t* ex) {
  (void)ex;
}

static void exec_create_maxmem(executor_t* ex) {
  (void)ex;
}

static void exec_create_falsepos(executor_t* ex) {
  (void)ex;
}

static void exec_load(executor_t* ex) {
  (void)ex;
}

static void exec_save(executor_t* ex) {
  (void)ex;
}

static void exec_unload(executor_t* ex) {
  (void)ex;
}

static void exec_insert(executor_t* ex) {
  (void)ex;
}

static void exec_insert_sha(executor_t* ex) {
 (void)ex;
}

static void exec_query(executor_t* ex) {
  (void)ex;
}

static void exec_query_sha(executor_t* ex) {
  (void)ex;
}

static void exec_falsepos(executor_t* ex) {
  (void)ex;
}


static void exec_sha(executor_t* ex) {
  if(my_next_token(ex) == -1) {
    return;
  }

  if(!ex->token.last_of_command) {
    error(ex, EX_E_RECOVERABLE, "%s", "sha takes exactly 1 argument");
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

static void exec_help(executor_t* ex) {
  if(ex->token.last_of_command) {
    /* Unary version; list all available commands */

    puts("Available commands:");

    for(size_t i = 0; i < n_commands; i ++) {
      const command_defn_t* command = &command_defns[i];
      printf("~ %s %s\n", command->name, command->usage);
    }

    puts("Try `help <command>` to view detailed documentation for a command");

    return;
  }

  /* Binary version; give help for one particular command */

  if(my_next_token(ex) == -1) {
    return;
  }

  const token_t* token = &ex->token;
  const command_defn_t* command = NULL;

  if(!token->last_of_command) {
    error(ex, EX_E_RECOVERABLE, "%s", "help takes at most 1 argument");
    return;
  }

  for(size_t i = 0; i < n_commands; i ++) {
    if(token_eq(token, command_defns[i].name)) {
      command = &command_defns[i];
      break;
    }
  }

  if(command == NULL) {
    char* pretty_token = token2str(token);

    /* Swallow allocation error from token2str (if any) since we're already
     * handling something */
    error(ex, EX_E_RECOVERABLE, NO_SUCH_COMMAND_MESSAGE, (pretty_token == NULL) ? "" : pretty_token);

    free(pretty_token);

    return;
  }

  printf("\n  USAGE: %s %s\n  %s\n\n", command->name, command->usage, command->description);
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

  if(my_next_token(ex) == -1) {
    return;
  }

  const command_defn_t* command = NULL;

  for(size_t i = 0; i < n_commands; i ++) {
    if(token_eq(&ex->token, command_defns[i].name)) {
      command = &command_defns[i];
      break;
    }
  }

  if(command == NULL) {
    char* pretty_token = token2str(&ex->token);

    /* Swallow allocation error from token2str (if any) since we're already
     * handling something */
    error(ex, EX_E_RECOVERABLE, NO_SUCH_COMMAND_MESSAGE, (pretty_token == NULL) ? "" : pretty_token);

    free(pretty_token);

    return;
  }

  const bool nullary = ex->token.last_of_command;

  if(nullary && command->min_arity > 0) {
    error(
      ex, EX_E_RECOVERABLE,
      "%s takes %s %lu argument%s",
      command->name,
      ((command->flags & VARIADIC) ? "at least" : "exactly"),
      (unsigned long)command->min_arity,
      ((command->min_arity == 1) ? "" : "s")
    );
    return;
  }

  /* Just for the assertion below */
  const token_t prev_token = ex->token;
  (void)prev_token;

  command->exec(ex);

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
