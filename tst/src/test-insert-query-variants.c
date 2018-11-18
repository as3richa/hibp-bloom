#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* Assert that the 3 variants of insert and query are semantically equivalent
 * and interoperable */

#define MAX_LENGTH 100
#define MAX_INPUTS 10000

typedef struct {
  size_t n_hash_functions;
  size_t log2_bits;
  size_t n_inputs;
} case_t;

typedef struct {
  char* string;
  size_t size;
  byte* buffer;
  byte sha[SHA1_BYTES];
} input_t;

const case_t cases[] = {
  { 1,  0,  1 },
  { 1,  1,  1 },
  { 5,  5,  50 },
  { 5,  5,  1000 },
  { 5,  10, 10000 },
  { 10, 10, 10000 },
  { 15, 20, 10000 }
};

const size_t n_cases = sizeof(cases) / sizeof(case_t);

int main(void) {
  for(size_t c = 0; c < n_cases; c ++) {
    const size_t n_inputs = cases[c].n_inputs;

    hibp_bloom_filter_t bf;
    hibp_status_t status;

    status = hibp_bf_new(&bf, cases[c].n_hash_functions, cases[c].log2_bits);
    hassert0(status == HIBP_OK);

    input_t inputs[MAX_INPUTS];

    for(size_t i = 0; i < n_inputs; i ++) {
      inputs[i].string = random_ascii_str(rand() % MAX_LENGTH);

      inputs[i].size = strlen(inputs[i].string);
      inputs[i].buffer = (byte*)inputs[i].string;

      sha1(inputs[i].sha, inputs[i].size, inputs[i].buffer);

      if(rand() % 2 == 0) {
        switch(i % 3) {
        case 0:
          hibp_bf_insert_str(&bf, inputs[i].string);
          break;
        case 1:
          hibp_bf_insert(&bf, inputs[i].size, inputs[i].buffer);
          break;
        default:
          hibp_bf_insert_sha1(&bf, inputs[i].sha);
        }
      }
    }

    for(size_t i = 0; i < n_inputs; i ++) {
      const int q1 = hibp_bf_query_str(&bf, inputs[i].string);
      const int q2 = hibp_bf_query(&bf, inputs[i].size, inputs[i].buffer);
      const int q3 = hibp_bf_query_sha1(&bf, inputs[i].sha);

      hassert(
        q1 == q2 && q2 == q3,
        "expected hibp_bf_{query,query_str,query_sha1} to be equivalent for %s (got %d, %d, %d)",
        inputs[i].string, q1, q2, q3
      );

      free(inputs[i].string);
    }

    hibp_bf_destroy(&bf);
  }

  return 0;
}
