#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "tokenizer.h"
#include "stream.h"

/* ================================================================
 * Plumbing
 * ================================================================ */

/* Append a character to a given token. Return -1 on allocation failure */
static inline int token_pushc(token_t* token, int c) {
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
      /* FIXME: log error condition */
      return -1;
    }

    token->buffer = buffer;
  }

  assert(token->length < token->capacity);

  token->buffer[token->length ++] = c;

  return 0;
}

/* Given a character, return its value in hexidecimal, or -1 if the character
 * isn't hexademical */
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

/* Parse an escape sequence for a quoted token (assuming the backslash was
 * already read). Return EOF on parse error */
static inline int parse_escape_sequence(stream_t* stream) {
  switch(stream_getc(stream)) {
    case '"':
      return '"';
    case '\'':
      return '\'';
    case '\\':
      return '\\';
    case 'n':
      return '\n';
    case 'x':
    {
      const int high = hex2int(stream_getc(stream));

      if(high == -1) {
        return EOF;
      }

      const int low = hex2int(stream_getc(stream));

      if(low == -1) {
        return EOF;
      }

      return ((high << 4) | low);
    }
    default:
      return -1;
  }
}

/* Parse a quoted token from the given stream. Return TS_E_UNRECOVERABLE for
 * allocation failure */
static inline tokenization_status_t parse_quoted_token(token_t* token, stream_t* stream) {
  const int quote = stream_getc(stream);
  assert(quote == '"' || quote == '\'');

  for(;;) {
    int c = stream_peek(stream);

    if(c == EOF || c == '\n') {
      return TS_E_MISSING_QUOTE;
    }

    stream_getc(stream);

    if(c == quote) {
      break;
    }

    if(c == '\\') {
      c = parse_escape_sequence(stream);

      if(c == EOF) {
        return TS_E_BAD_ESCAPE;
      }
    }
    
    if(token_pushc(token, c) == -1) {
      return TS_E_NOMEM;
    } 
  }

  const int c = stream_peek(stream);

  if(!(c == EOF || isspace(c) || c == ';')) {
    return TS_E_MISSING_SEP;
  }

  return TS_OK;
}

/* ================================================================
 * Public interface
 * ================================================================ */

void token_new(token_t* token) {
  token->buffer = NULL;
  token->length = 0;
  token->capacity = 0;
}

void token_destroy(token_t* token) {
  free(token->buffer);
}

int token_eq(const token_t* token, const char* str) {
  const size_t length = strlen(str);

  if(token->length != length) {
    return 0;
  }

  return (memcmp(token->buffer, str, length) == 0);
}

int skip_to_command(stream_t* stream) {
  for(;;) {
    const int c = stream_peek(stream);

    /* Skip any whitespace or semicolons */
    if(c == EOF || (!isspace(c) && c != ';')) {
      break;
    }

    stream_getc(stream);
  }

  /* Read all the way to the end of the line or EOF in the case of a comment */
  if(stream_peek(stream) == '#') {
    for(;;) {
      const int c = stream_getc(stream);

      if(c == EOF || c == '\n') {
        break;
      }
    }
  }

  /* Return EOF iff stream is exhausted */
  return stream_peek(stream);
}

void drain_line(stream_t* stream) {
  for(;;) {
    const int c = stream_peek(stream);

    if(c == EOF || c == '\n') {
      break;
    }

    stream_getc(stream);
  }
}

tokenization_status_t next_token(token_t* token, stream_t* stream) {
  skip_to_command(stream);
  assert(stream_peek(stream) != EOF);

  token->line = stream->line;
  token->column = stream->column;
  token->length = 0;

  const int maybe_quote = stream_peek(stream);

  if(maybe_quote == '"' || maybe_quote == '\'') {
    const tokenization_status_t status = parse_quoted_token(token, stream);

    if(status != TS_OK) {
      return status;
    }
  } else {

    for(;;) {
      const int c = stream_peek(stream);

      if(c == EOF || isspace(c) || c == ';') {
        break;
      }

      stream_getc(stream);

      if(token_pushc(token, c) == -1) {
        return TS_E_NOMEM;
      }
    }
  }

  /* Drain trailing spaces */
  for(;;) {
    const int c = stream_peek(stream);

    if(c == EOF || c == '\n' || !isspace(c)) {
      break;
    }

    stream_getc(stream);
  }

  /* Check if this is the last token in the ongoing command */
  const int c = stream_peek(stream);
  token->last_of_command = (c == EOF || c == '\n' || c == ';' || c == '#');

  return TS_OK;
}
