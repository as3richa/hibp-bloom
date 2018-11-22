#ifndef _TOKENIZER_H_
#define _TOKENIZER_H_

/* Tokenization helper methods for parsing hibp-bloom commands */

#include <stddef.h>

#include "stream.h"
#include "token.h"

typedef enum {
  /* Allocation failed */
  TS_E_NOMEM = -255,

  /* Bad escape code */
  TS_E_BAD_ESCAPE,

  /* Missing closing quote in quoted token */
  TS_E_MISSING_QUOTE,

  /* Missing space after quoted token */
  TS_E_MISSING_SEP,

  /* Everything is wonderful */
  TS_OK = 0
} tokenization_status_t;

/* Skips leading whitespace, comments, and extraneous semicolons to reach either
 * EOF, or the beginning of a new command. Returns the first character of the
 * next command, or EOF if the stream is exhausted */
int skip_to_command(stream_t* stream);

/* Reads the next token, allocating additional memory as necessary and
 * returning a status code. stream must be at the first character of the next
 * token. This invariant is maintained by next_token itself
 * (until token->last_of_command) */
tokenization_status_t next_token(token_t* token, stream_t* stream);

/* Drains the stream up to and including a newline, or up to EOF */
void drain_line(stream_t* stream);

/* Convert a token to a human-readable, null-terminated string. If the token contains
 * only printable, non-space characters, copy it verbatim to a freshly-allocated
 * string; otherwise, wrap it in quotes and escape any non-printable characters.
 * Returns NULL on allocation failure. This lives here rather than in token.{c,h}
 * because it's basically the inverse of next_token */
char* token2str(const token_t* token);

#endif
