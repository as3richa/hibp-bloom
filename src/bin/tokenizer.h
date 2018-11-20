#ifndef _TOKENIZER_H_
#define _TOKENIZER_H_

/* Token structure and tokenization helper methods for parsing hibp-bloom commands */

#include <stddef.h>

#include "stream.h"

typedef struct {
  size_t capacity;

  /* Everything below is public */

  size_t line;
  size_t column;

  char* buffer;
  size_t length;

  /* Is this the last token of the current command? */
  int last_of_command;
} token_t;

/* t10n, like /^t[a-b]{10}n$/ :) */
typedef enum {
  T10N_E_UNRECOVERABLE = -255,
  T10N_E_RECOVERABLE,
  T10N_OK = 0
} t10n_status_t;

/* The scripting language is simple to the point that we never actually need to
 * hold more than a single token in memory when parsing and executing a script -
 * hence we can use a single token_t object, reusing its allocated storage with
 * each read */

/* Memory is allocated lazily, so token_new can't fail */
void token_new(token_t* token);
void token_free(token_t* token);

/* Case-insensitive equality comparison with a null-terminated string */
int token_eq_ins(const token_t* token, char* str);

/* Skips leading whitespace, comments, and extraneous semicolons to reach either
 * EOF, or the beginning of a new command. Returns EOF if the stream is exhausted
 * after the skip, non-EOF otherwise */
int skip_to_command(stream_t* stream);

/* Drains the stream up to and including a newline, or up to EOF */
void drain_line(stream_t* stream);

/* Reads the next token, returning a status code */
t10n_status_t next_token(token_t* token, stream_t* stream);

#endif
