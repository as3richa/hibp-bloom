#include <stdlib.h>
#include <assert.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

/* The sole purpose of this program is to use as a guinea pig for valgrind.
 * valgrind finds a lot of errors in system libraries over which we have no
 * control, so in order to get clean information about the correctness of our own
 * library, we need to supply a list of suppressions (i.e. errors we don't care
 * about to valgrind). We can generate such a list by writing a trivial, known-
 * correct program that exercises the relevant functionality of system APIs,
 * then running it under valgrind with --gen-suppressions */

const size_t SIZE = 1024 * 1024;
const size_t SHA1_BYTES = 20;

int main(void) {
  unsigned char* buffer = malloc(SIZE);

  assert(buffer);
  assert(RAND_pseudo_bytes(buffer, SIZE) != -1);
  free(buffer);

  buffer = calloc(SIZE, 1);
  assert(buffer);

  unsigned char* sha = malloc(SHA1_BYTES);
  assert(sha);

  assert(SHA1(buffer, SIZE, sha) != NULL);

  free(buffer);
  free(sha);

  return 0;
}
