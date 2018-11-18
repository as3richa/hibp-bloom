#ifndef _COMMON_H_
#define _COMMON_H_

#include "hibp-bloom.h"

typedef hibp_byte_t byte;

#define SHA1_BYTES HIBP_SHA1_BYTES

void my_hassert(int ok, const char* filename, int line, const char* cond, const char* format, ...);

#define HSTRINGIFY(v) HSTRINGIFY_2(v)
#define HSTRINGIFY_2(v) #v

#define hassert(cond, ...) \
  my_hassert((cond), __FILE__, __LINE__, HSTRINGIFY(cond), __VA_ARGS__)

#define hassert0(cond) \
  hassert(cond, "expected the condition to hold")

const char* status2str(hibp_status_t status);
char* buffer2str(size_t size, byte* buffer);

byte* random_ascii_buffer(size_t size);
char* random_ascii_str(size_t length);

void sha1(byte* sha, size_t size, const byte* buffer);

#endif
