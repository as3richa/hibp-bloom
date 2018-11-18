#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "util.h"

/* Assert that, for sane parameters, the false positive rate matches our expectations */

#define MAX_STRINGS 40000
#define LENGTH 100

typedef struct {
  size_t n_hash_functions;
  size_t log2_bits;
  size_t n_elements;
} case_t;

const case_t cases[] = {
  { 1, 10,  50 },
  { 5, 12,  500 },
  { 5, 15,  10000 },
  { 5, 20,  20000 },
  { 10, 20, 20000 },
  { 10, 24, 20000 },
  { 15, 24, 20000 }
};

const size_t n_cases = sizeof(cases) / sizeof(case_t);

int main(void) {
  for(size_t c = 0; c < n_cases; c ++) {
    const size_t n_hash_functions = cases[c].n_hash_functions;
    const size_t log2_bits = cases[c].log2_bits;
    const size_t n_elements = cases[c].n_elements;
    const size_t n_trials = 5 * n_elements;

    for(int k = 0; k < 3; k ++) {
      hibp_bloom_filter_t bf;
      hibp_status_t status;

      status = hibp_bf_new(&bf, n_hash_functions, log2_bits);
      hassert0(status == HIBP_OK);

      char *strings[MAX_STRINGS];

      for(size_t i = 0; i < n_elements; i ++) {
        strings[i] = random_ascii_str(LENGTH);
        hibp_bf_insert_str(&bf, strings[i]);
        free(strings[i]);
      }

      size_t positive = 0;

      for(size_t i = 0; i < n_trials; i ++) {
        char* string = random_ascii_str(LENGTH);
        positive += hibp_bf_query_str(&bf, string);
      }

      const double false_positive_rate = (double)positive / (double)n_trials;

      const size_t bits = (((size_t)1) << log2_bits);
      const double power = -1.0 * n_hash_functions * n_elements / bits;
      const double expected_false_positive_rate = pow(1 - exp(power), n_hash_functions);

      double maximum_false_positive_rate = 2 * expected_false_positive_rate;

      if(maximum_false_positive_rate < 1e-4) {
        maximum_false_positive_rate = 1e-4;
      }

      hassert(
        false_positive_rate <= maximum_false_positive_rate,
        "expected false positive rate to be ~%lf, but was %lf (case %lu, %lu, %lu)",
        expected_false_positive_rate, false_positive_rate,
        cases[c].n_hash_functions, cases[c].log2_bits, n_elements
      );

      hibp_bf_destroy(&bf);
    }
  }

  return 0;
}
