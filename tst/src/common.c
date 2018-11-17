#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

void my_hassert(int ok, const char* filename, int line, const char* cond, const char* format, ...) {
  if(ok) {
    return;
  }

  fputs("========\n", stderr);
  fprintf(stderr, "assertion failed: %s:%d: (%s)\n", filename, line, cond);

  va_list varargs;
  va_start(varargs, format);
  vfprintf(stderr, format, varargs);
  va_end(varargs);

  fputs("\n========\n", stderr);

  exit(EXIT_FAILURE);
}

const char* status2str(hibp_status_t status) {
  switch(status) {
    case HIBP_E_NOMEM:    return "HIBP_E_NOMEM";
    case HIBP_E_VERSION:  return "HIBP_E_VERSION";
    case HIBP_E_IO:       return "HIBP_E_IO";
    case HIBP_E_CHECKSUM: return "HIBP_E_CHECKSUM";
    case HIBP_E_2BIG:     return "HIBP_E_2BIG";
    case HIBP_E_INVAL:    return "HIBP_E_INVAL";
    case HIBP_OK:         return "HIBP_OK";
  }

  hassert(0, "bad hibp_status_t: %d", (int)status);
  return NULL;
}

char* buffer2str(size_t size, hibp_byte_t* buffer) {
  char* str = (char*)malloc(size + 1);
  hassert(str != NULL, "malloc failed");

  memcpy(str, buffer, size + 1);
  return str;
}

byte* random_ascii_buffer(size_t size) {
  byte* buffer = (byte*)malloc(size);
  hassert(buffer != NULL, "malloc failed");

  for(size_t i = 0; i < size; i ++) {
    buffer[i] = (byte)((rand() % 95) + 32);
  }

  return buffer;
}

char* random_ascii_str(size_t length) {
  char* str = (char*)malloc(length + 1);
  hassert(str != NULL, "malloc failed");

  for(size_t i = 0; i < length; i ++) {
    str[i] = (byte)((rand() % 95) + 32);
  }

  str[length] = 0;

  return str;
}

byte* random_sha1(void) {
  byte* sha = (byte*)malloc(HIBP_SHA1_BYTES);
  hassert(sha != NULL, "malloc failed");

  for(size_t i = 0; i < HIBP_SHA1_BYTES; i ++) {
    sha[i] = rand() % 256;
  }

  return sha;
}
