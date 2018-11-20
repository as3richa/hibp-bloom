#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <assert.h>

#include "stream.h"

typedef struct {
  int line;
  int column;
  char* buffer;
  size_t length;
  size_t capacity;
} token_t;

const char* usage =
  "OVERVIEW: command-line tool for building and querying Bloom filters\n\n"
  "USAGE:\n\n"
  "  %s                start an interactive session\n"
  "  %s -              read and run a script from the standard input\n"
  "  %s <filename>     read run the script specified by filename\n"
  "  %s -c <commands>  run given sequence of commands from the second argument\n\n"
  "Run `help` in an interactive session to learn about the hibp-bloom scripting language\n";

static inline int skip_whitespace_and_comments(stream_t* stream);
static inline int next_token(token_t* token, stream_t* stream);
static inline void token_pushc(token_t* token, int c);

int main(int argc, char** argv) {
  if(argc > 3 || (argc == 3 && strcmp(argv[1], "-c") != 0)) {
    fprintf(stderr, usage, argv[0], argv[0], argv[0], argv[0]);
    return 1;
  }

  stream_t stream;

  const int interactive = (argc == 1);
  int stdin_consumed = 0;

  if(argc == 1 || (argc == 2 && strcmp(argv[1], "-") == 0)) {
    stdin_consumed = 1;
    stream_new_file(&stream, stdin, "<standard input>");
  } else if(argc == 2) {
    FILE* file = fopen(argv[1], "r");
    assert(file != NULL); /* FIXME */
    stream_new_file(&stream, file, argv[1]);
  } else {
    stream_new_str(&stream, argv[2], "<argv[2]>");
  }

  token_t token = { -1, -1, NULL, 0, 0 };

  for(;;) {
    if(interactive) {
      fputs(">> ", stdout);
      fflush(stdout);
    }

    if(skip_whitespace_and_comments(&stream) == EOF) {
      break;
    }

    for(int i = 0;; i ++) {
      const int more = next_token(&token, &stream);

      printf("%d  ", i);
      for(size_t j = 0; j < token.length; j ++) {
        putchar(token.buffer[j]);
      }
      putchar('\n');

      if(!more) {
        break;
      }
    }
  }

  /* This may close stdin, but that's okay */
  stream_close(&stream);

  return 0;
}

static inline int skip_whitespace_and_comments(stream_t* stream) {
  for(;;) {
    const int c = stream_peek(stream);

    if(c == EOF) {
      break;
    }

    /* Skip any extraneous semicolons too */
    if(!isspace(c) && c != ';') {
      break;
    }

    stream_getc(stream);
  }

  if(stream_peek(stream) == '#') {
    for(;;) {
      const int c = stream_getc(stream);

      if(c == EOF || c == '\n') {
        break;
      }
    }
  }

  return stream_peek(stream);
}

static inline int next_token(token_t* token, stream_t* stream) {
  skip_whitespace_and_comments(stream);
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
    assert(c == EOF || isspace(c) || c == ';'); /* FIXME */
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

  /* Return 1 if there are more tokens in this command, 0 otherwise */
  const int c = stream_peek(stream);
  return !(c == EOF || c == '\n' || c == ';' || c == '#');
}

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
