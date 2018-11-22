#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include "executor.h"
#include "stream.h"

#include "walls-of-text.h"

static void prompt(void);

int main(int argc, char** argv) {
  if(argc > 3 || (argc == 3 && strcmp(argv[1], "-c") != 0)) {
    fprintf(stderr, usage, argv[0], argv[0], argv[0], argv[0]);
    return 1;
  }

  const int interactive = (argc == 1);

  int stdin_consumed = 0;
  stream_t stream;

  if(argc == 1 || (argc == 2 && strcmp(argv[1], "-") == 0)) {
    /* `bin/hibp-bloom` or `bin/hibp-bloom -`. Read a script from the standard input.
     * In the first case we're in interactive mode */

    stdin_consumed = 1;
    stream_new_file(&stream, stdin, "<standard input>");

    /* Emit a prompt iff we're in interactive mode */
    if(interactive) {
      stream.prompt = prompt;
    }
  } else if(argc == 2) {
    /* `bin/hibp-bloom some-filename`. Read a script from the given file */

    FILE* file = fopen(argv[1], "r");

    if(file == NULL) {
      fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1], strerror(errno));
      return 1;
    }

    stream_new_file(&stream, file, argv[1]);
  } else {
    /* `bin/hibp-bloom -c 'some; script; text'`. Run the script given inline in argv */
    stream_new_str(&stream, argv[2], "<argv[2]>");
  }

  /* Show a pretty banner in interactive mode */
  if(interactive) {
    fputs(banner, stdout);
  }

  executor_t ex;
  executor_new(&ex, &stream, stdin_consumed);

  int exit_status = 0;

  for(;;) {
    executor_exec_one(&ex);

    /* OK; proceed to next command */
    if(ex.status == EX_OK) {
      continue;
    }

    /* EOF; terminate without emitting any errors */
    if(ex.status == EX_EOF) {
      break;
    }

    /* In interactive mode, we can recover from e.g. parse errors by draining the line and
     * continuing to chug along; in all other cases, errors are fatal */
    if(interactive && ex.status == EX_E_RECOVERABLE) {
      executor_drain_line(&ex);
      continue;
    }

    /* Die */
    exit_status = 1;
    break;
  }

  if(interactive) {
    /* Make sure the shell prompt goes on the next line */
    putchar('\n');
  }

  executor_destroy(&ex);

  /* This may close stdin, but that's okay */
  stream_close(&stream);

  return exit_status;
}

static void prompt(void) {
  fputs(">> ", stdout);
}
