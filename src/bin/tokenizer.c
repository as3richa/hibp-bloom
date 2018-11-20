#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "tokenizer.h"
#include "stream.h"

/* Append a character to a given token */
static inline void token_pushc(token_t* token, int c) {
  assert(token->length <= token->capacity);
  assert(c != EOF);

  if(token->length == token->capacity) {
    if(token->capacity == 0) {
      token->capacity = 32;
    } else {
      token->capacity *= 2;
    }

    char* buffer = (char*)realloc(token->buffer, token->capacity);
    assert(buffer != NULL); /* FIXME */
    token->buffer = buffer;
  }

  token->buffer[token->length ++] = c;
}

void token_new(token_t* token) {
  token->buffer = NULL;
  token->length = 0;
  token->capacity = 0;
}

void token_free(token_t* token) {
  free(token->buffer);
}

int token_eq(const token_t* token, char* str) {
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
    const int c = stream_getc(stream);
    if(c == EOF || c == '\n') {
      break;
    }
  }
}

t10n_status_t next_token(token_t* token, stream_t* stream) {
  skip_to_command(stream);
  assert(stream_peek(stream) != EOF);

  token->line = stream->line;
  token->column = stream->column;
  token->length = 0;

  if(stream_peek(stream) == '"') {
    stream_getc(stream);

    for(;;) {
      const int c = stream_getc(stream);
      assert(c != EOF && c != '\n'); /* FIXME */

      if(c == '"') {
        break;
      }

      if(c == '\\') {
        switch(stream_getc(stream)) {
          case '\\':
            token_pushc(token, '\\');
            break;
          case '"':
            token_pushc(token, '"');
            break;
          case 'n':
            token_pushc(token, '\n');
            break;
          default:
            assert(0); /* FIXME */
        }
      } else {
        token_pushc(token, c);
      }
    }

    const int c = stream_peek(stream);
    assert(c == EOF || isspace(c) || c == ';'); (void)c; /* FIXME */
  } else {

    for(;;) {
      const int c = stream_peek(stream);

      if(c == EOF || isspace(c) || c == ';') {
        break;
      }

      stream_getc(stream);

      token_pushc(token, c);
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

  return T10N_OK;
}
