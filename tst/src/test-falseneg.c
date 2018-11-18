#include <stdio.h>
#include <stdlib.h>

#include "util.h"

/* Assert that no string that has been inserted into the set is ever misidentified
 * as being absent from the set */

#define MAX_LENGTH 100
#define MAX_STRINGS 10000

typedef struct {
  size_t n_hash_functions;
  size_t log2_bits;
  size_t n_strings;
} case_t;

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
    const size_t n_strings = cases[c].n_strings;

    hibp_bloom_filter_t bf;
    hibp_status_t status;

    status = hibp_bf_new(&bf, cases[c].n_hash_functions, cases[c].log2_bits);
    hassert0(status == HIBP_OK);

    char *strings[MAX_STRINGS];

    for(size_t i = 0; i < n_strings; i ++) {
      strings[i] = random_ascii_str(rand() % MAX_LENGTH);
      hibp_bf_insert_str(&bf, strings[i]);

      hassert(
        hibp_bf_query_str(&bf, strings[i]),
        "expected %s to be present in the Bloom filter, but it was not",
        strings[i]
      );
    }

    for(size_t i = 0; i < n_strings; i ++) {
      hassert(
        hibp_bf_query_str(&bf, strings[i]),
        "expected %s to be present in the Bloom filter, but it was not",
        strings[i]
      );

      free(strings[i]);
    }

    hibp_bf_destroy(&bf);
  }

  return 0;
}
