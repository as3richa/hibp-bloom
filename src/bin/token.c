#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
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

bool token_eq(const token_t* token, const char* str) {
  const size_t length = strlen(str);

  if(token->length != length) {
    return false;
  }

  return (memcmp(token->buffer, str, length) == 0);
}

int token2double(double* value, const token_t* token) {
  if(token->length == 0) {
    return -1;
  }

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
  if(token->length == 0) {
    return -1;
  }

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

int token2memsize(size_t* value, const token_t* token) {
  if(token->length == 0) {
    return -1;
  }

  /* Extract the longest prefix of token that looks like a number */

  token_t numeric_part = *token;

  numeric_part.length = 0;

  while(numeric_part.length < token->length) {
    const int c = numeric_part.buffer[numeric_part.length];

    if(!isdigit(c) && c != '.') {
      break;
    }

    numeric_part.length ++;
  }

  /* Parse it */

  double magnitude;

  if(token2double(&magnitude, &numeric_part) == -1) {
    return -1;
  }

  /* The remainder of the token should be (case-insensitive) b, k, kb, m, mb, g, gb,
   * or the empty string */

  int multiplier_char;

  switch(token->length - numeric_part.length) {
    case 0:
      /* No suffix - implicitly, bytes */
      multiplier_char = 'b';
      break;

    case 1:
      /* Suffix is e.g. b, K, G */
      multiplier_char = token->buffer[token->length - 1];
      break;

    case 2:
    {
      /* Suffix is e.g. mb, Kb, GB */

      const int b = token->buffer[token->length - 1];

      if(b != 'b' && b != 'B') {
        return -1;
      }

      multiplier_char = token->buffer[token->length - 2];

      /* bb, BB, etc. aren't valid suffixes */
      if(multiplier_char == 'b' || multiplier_char == 'B') {
        return -1;
      }

      break;
    }

    default:
      return -1;
  }

  size_t multiplier;

  switch(multiplier_char) {
    case 'b':
    case 'B':
      multiplier = 1;
      break;

    case 'k':
    case 'K':
      multiplier = 1024;
      break;

    case 'm':
    case 'M':
      multiplier = 1024 * 1024;
      break;

    case 'g':
    case 'G':
      multiplier = 1024 * 1024 * 1024;
      break;

    default:
      return -1;
  }

  const double bytes = ceil(magnitude * multiplier);

  if(bytes > SIZE_MAX) {
    return -1;
  }

  (*value) = (size_t)bytes;

  return 0;
}

/* FIXME: dedupe this (tokenizer.c) */
static inline int hex2int(int hex) {
  if('0' <= hex && hex <= '9') {
    return hex - '0';
  } else if('a' <= hex && hex <= 'z')  {
    return 10 + hex - 'a';
  } else if('A' <= hex && hex <= 'Z') {
    return 10 + hex - 'A';
  } else {
    return -1;
  }
}

int token2sha(hibp_byte_t* sha, const token_t* token) {
  if(token->length != 40) {
    return -1;
  }

  for(size_t j = 0; j < 20; j ++) {
    int high = hex2int(token->buffer[2 * j]);

    if(high == -1) {
      return -1;
    }

    int low = hex2int(token->buffer[2 * j + 1]);

    if(low == -1) {
      return -1;
    }

    sha[j] = ((high << 4) | low);
  }

  return 0;
}
