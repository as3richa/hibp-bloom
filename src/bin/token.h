#ifndef _TOKEN_H_
#define _TOKEN_H_

#include <stddef.h>

#include "bool.h"

typedef struct {
  size_t capacity;

  /* Everything below is public */

  size_t line;
  size_t column;

  char* buffer;
  size_t length;

  /* Is this the last token of the current command? */
  bool last_of_command;
} token_t;

/* Memory is allocated lazily, so token_new can't fail */
void token_new(token_t* token);
void token_destroy(token_t* token);

/* Append a character to a given token. Return -1 on allocation failure */
int token_pushc(token_t* token, int c);

/* Equality comparison with a null-terminated string */
int token_eq(const token_t* token, const char* str);

/* These return -1 on parse failure. Unlike the standard library functions they don't
 * tolerate trailing characters not part of the value */
int token2double(double* value, const token_t* token);
int token2size(size_t* value, const token_t* token);

#endif
