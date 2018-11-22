#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* SIZE_MAX isn't available on every compiler */
#undef SIZE_MAX
#define SIZE_MAX (~(size_t)0)

#include "token.h"

void token_new(token_t* token) {
  token->buffer = NULL;
  token->length = 0;
  token->capacity = 0;
}

void token_destroy(token_t* token) {
  free(token->buffer);
}

int token_pushc(token_t* token, int c) {
  assert(token->length <= token->capacity);
  assert(c != EOF);

  if(token->length == token->capacity) {
    if(token->capacity == 0) {
      token->capacity = 32;
    } else {
      token->capacity *= 2;
    }

    char* buffer = (char*)realloc(token->buffer, token->capacity);

    if(buffer == NULL) {
      return -1;
    }

    token->buffer = buffer;
  }

  assert(token->length < token->capacity);

  token->buffer[token->length ++] = c;

  return 0;
}

int token_eq(const token_t* token, const char* str) {
  const size_t length = strlen(str);

  if(token->length != length) {
    return 0;
  }

  return (memcmp(token->buffer, str, length) == 0);
}

int token2double(double* value, const token_t* token) {
  assert(token->length > 0);

  size_t i;

  *value = 0.0;

  for(i = 0; i < token->length && token->buffer[i] != '.'; i ++) {
    if(!isdigit(token->buffer[i])) {
      return -1;
    }
    *value = 10.0 * (*value) + (token->buffer[i] - '0');
  }

  if(i >= token->length) {
    return 0;
  }

  assert(token->buffer[i] == '.');

  i ++;

  for(double exponent = 0.1; i < token->length; i ++, exponent /= 10.0) {
    if(!isdigit(token->buffer[i])) {
      return -1;
    }
    *value += exponent * (token->buffer[i] - '0');
  }

  return 0;
}

int token2size(size_t* value, const token_t* token) {
  assert(token->length > 0);

  *value = 0;

  for(size_t i = 0; i < token->length; i ++) {
    if(!isdigit(token->buffer[i])) {
      return -1;
    }

    /* Bounds check */
    if(*value > (SIZE_MAX - (token->buffer[i] - '0')) / 10) {
      return -1;
    }

    *value = 10 * (*value) + (token->buffer[i] - '0');
  }

  return 0;
}
