#include <stdio.h>
#include <assert.h>

#include "stream.h"

/* ================================================================
 * Plumbing
 * ================================================================ */

/* Common initialization for both stream constructors */
static inline void stream_new(stream_t* stream, const char* name) {
  stream->c = EOF;
  stream->eof = 0;
  stream->name = name;
  stream->line = 1;
  stream->column = 1;
  stream->prompt = NULL;
}

/* Given a stream backed a by a null-terminated string, extract a character from
 * the string and increment the pointer */
static int stream_str_getc(stream_t* stream) {
  const char* str = (const char*)stream->ctx;
  const int c = *str;

  if(c == 0) {
    return EOF;
  }

  stream->ctx = (void*)(str + 1);
  return c;
}

/* Given a stream backed by a FILE, extract a character from the file */
static int stream_file_getc(stream_t* stream) {
  return fgetc((FILE*)stream->ctx);
}

/* Given a stream backed by a FILE, destroy the FILE */
static void stream_file_close(stream_t* stream) {
  fclose((FILE*)stream->ctx);
}

/* ================================================================
 * Public interface
 * ================================================================ */

void stream_new_file(stream_t* stream, FILE* file, const char* name) {
  stream_new(stream, name);
  stream->ctx = (void*)file;
  stream->my_getc = stream_file_getc;
  stream->my_close = stream_file_close;
}

void stream_new_str(stream_t* stream, const char* str, const char* name) {
  stream_new(stream, name);
  stream->ctx = (void*)str;
  stream->my_getc = stream_str_getc;
  stream->my_close = NULL;
}

void stream_close(stream_t* stream) {
  if(stream->my_close == NULL) {
    return;
  }
  stream->my_close(stream);
}

int stream_peek(stream_t* stream) {
  /* Don't try to read again if we've already hit an EOF */
  if(stream->eof) {
    return EOF;
  }

  /* Return the buffer character, if any. We use EOF to indicate the absence of a
   * buffered character - it's not necessarily an actual end-of-file */
  if(stream->c != EOF) {
    return stream->c;
  }

  /* Dispatch prompt hook if necessary */
  if(stream->prompt && stream->column == 1) {
    stream->prompt();
  }

  /* Fetch and buffer a character */
  stream->c = stream->my_getc(stream);

  /* Real EOF */
  if(stream->c == EOF) {
    stream->eof = 1;
  }

  return stream->c;
}

int stream_getc(stream_t* stream) {
  assert(!stream->eof);

  /* Take the buffered character, or possibly read a new one */
  const int c = stream_peek(stream);

  /* Positional updates */
  if(c == '\n') {
    stream->line ++;
    stream->column = 1;
  } else if(c != EOF) {
    stream->column ++;
  }

  /* Remove the currently-buffered character */
  stream->c = EOF;

  return c;
}
