#ifndef _STREAM_H_
#define _STREAM_H_

#include <stddef.h>

/* Stream structure designed to abstract away the differences between parsing from
 * a file versus parsing from a null-terminated string. Also, handles line/column
 * values for error messaging, etc. */

typedef struct st_stream {
  /* If only there was some language that had polymorphism and vtables... :) */
  void* ctx;
  int (*my_getc)(struct st_stream*);
  void (*my_close)(struct st_stream*);

  /* Character buffered by stream_peek */
  int c;

  /* EOF indicator */
  int eof;

  /* Everything below is public */

  /* name is informational. It's passed into the constructors */
  const char* name;

  size_t line;
  size_t column;

  /* Hook to be executed immediately before the first read of a new line */
  void (*prompt)(void);
} stream_t;

/* Public interface. Hopefully self-explanatory */

void stream_new_file(stream_t* stream, FILE* file, const char* name);
void stream_new_str(stream_t* stream, const char* str, const char* name);
void stream_close(stream_t* stream);

int stream_peek(stream_t* stream);
int stream_getc(stream_t* stream);

#endif
