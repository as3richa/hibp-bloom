#include <stdio.h>
#include <assert.h>
#include <openssl/sha.h>

#include "executor.h"
#include "command-defns.h"

static const char* out_of_memory = "Out of memory";

/* ================================================================
 * Plumbing
 * ================================================================ */

#define eprintf(ex, ...) \
  snprintf((ex)->error_str, sizeof((ex)->error_str), __VA_ARGS__)

static inline int my_next_token(executor_t* ex) {
  stream_t* stream = ex->stream;

  const tokenization_status_t status = next_token(&ex->token, stream);

  if(status == TS_OK) {
    return 0;
  }

  if(status == TS_E_NOMEM) {
    ex->status = EX_E_FATAL;
    eprintf(ex, "%s", out_of_memory);
    return -1;
  }

  /* Parse error of some kind */

  ex->status = EX_E_RECOVERABLE;

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

  eprintf(
    ex, "%s:%lu:%lu: %s",
    stream->name, (unsigned long)stream->line, (unsigned long)stream->column,
    message
  );

  return -1;
}

static void exec_sha(executor_t* ex) {
  if(my_next_token(ex) == -1) {
    return;
  }

  if(!ex->token.last_of_command) {
    ex->status = EX_E_RECOVERABLE;

    eprintf(
      ex, "%s:%lu:%lu: sha takes at most 1 argument",
      ex->stream->name, (unsigned long)ex->token.line, (unsigned long)ex->token.column
    );

    return;
  }

  unsigned char sha[20];
  const int okay = (SHA1((unsigned char*)ex->token.buffer, ex->token.length, sha) != NULL);
  (void)okay;
  assert(okay);

  for(size_t i = 0; i < sizeof(sha); i ++) {
    const unsigned char high = sha[i] >> 4;
    const unsigned char low = (sha[i] & 0x0f);
    putchar((0 <= high && high <= 9) ? ('0' + high) : ('a' + high - 10));
    putchar((0 <= low && low <= 9) ? ('0' + low) : ('a' + low - 10));
  }

  putchar('\n');
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

  const token_t* token = &ex->token;
  const command_defn_t* command = NULL;

  for(size_t i = 0; i < n_commands; i ++) {
    if(token_eq(token, command_defns[i].name)) {
      command = &command_defns[i];
      break;
    }
  }

  if(command == NULL) {
    ex->status = EX_E_RECOVERABLE;

    eprintf(
      ex, "%s:%lu:%lu: No such command; try `help` to list available commands",
      stream->name, (unsigned long)token->line, (unsigned long)token->column
    );

    return;
  }

  if(token->last_of_command && command->min_arity > 0) {
    ex->status = EX_E_RECOVERABLE;

    eprintf(
      ex, "%s:%lu:%lu: %s takes at least %lu argument%s",
      stream->name, (unsigned long)token->line, (unsigned long)token->column,
      command->name,
      (unsigned long)command->min_arity,
      ((command->min_arity == 1) ? "" : "s")
    );

    return;
  }

  command->exec(ex);
}

void executor_drain_line(executor_t* ex) {
  ex->status = EX_OK;
  drain_line(ex->stream);
}
