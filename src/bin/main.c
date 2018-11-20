#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <assert.h>

#include "hibp-bloom.h"
#include "stream.h"
#include "tokenizer.h"

#include "walls-of-text.h"

static stream_t stream;
static int stdin_consumed = 0;
static int filter_initiaized;
static hibp_bloom_filter_t filter;
static token_t token;

static void prompt(void);

int main(int argc, char** argv) {
  if(argc > 3 || (argc == 3 && strcmp(argv[1], "-c") != 0)) {
    fprintf(stderr, usage, argv[0], argv[0], argv[0], argv[0]);
    return 1;
  }

  const int interactive = (argc == 1);

  if(argc == 1 || (argc == 2 && strcmp(argv[1], "-") == 0)) {
    stdin_consumed = 1;
    stream_new_file(&stream, stdin, "<standard input>");
    if(interactive) {
      stream.prompt = prompt;
    }
  } else if(argc == 2) {
    FILE* file = fopen(argv[1], "r");
    assert(file != NULL); /* FIXME */
    stream_new_file(&stream, file, argv[1]);
  } else {
    stream_new_str(&stream, argv[2], "<argv[2]>");
  }

  token_new(&token);

  if(interactive) {
    fputs(banner, stdout);
  }

  for(;;) {
    if(skip_to_command(&stream) == EOF) {
      break;
    }

    for(int i = 0;; i ++) {
      next_token(&token, &stream);

      printf("%d  ", i);
      for(size_t j = 0; j < token.length; j ++) {
        putchar(token.buffer[j]);
      }
      putchar('\n');

      if(token.last_of_command) {
        break;
      }
    }
  }

  token_free(&token);

  /* This may close stdin, but that's okay */
  stream_close(&stream);

  return 0;
}

static void prompt(void) {
  fputs(">> ", stdout);
}
