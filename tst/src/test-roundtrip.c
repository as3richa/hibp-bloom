#include <stdio.h>
#include <stdlib.h>

#include "util.h"

/* Assert that Bloom filters survive the round trip of being written to and read
 * from disk. The important property is that, for any given Bloom filter, for any
 * given string, the resut of hibp_bf_query is unchanged after the filter is
 * written to and read from disk */

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
    int present[MAX_STRINGS];

    for(size_t i = 0; i < n_strings; i ++) {
      strings[i] = random_ascii_str(rand() % MAX_LENGTH);

      if(rand() % 2 == 0) {
        hibp_bf_insert_str(&bf, strings[i]);
      }
    }

    for(size_t i = 0; i < n_strings; i ++) {
      present[i] = hibp_bf_query_str(&bf, strings[i]);
    }

    char filename[99];
    sprintf(filename, "roundtrip.%d.bl", (int)(c + 1));

    FILE* outfile = fopen(filename, "wb");
    hassert0(outfile != NULL);
    hassert0(hibp_bf_save_file(&bf, outfile) == HIBP_OK);
    fclose(outfile);
    hibp_bf_destroy(&bf);

    FILE* infile = fopen(filename, "rb");
    hassert0(infile != NULL);
    hassert0(hibp_bf_load_file(&bf, infile) == HIBP_OK);
    fclose(infile);
    remove(filename);

    for(size_t i = 0; i < n_strings; i ++) {
      const int pr = hibp_bf_query_str(&bf, strings[i]);

      hassert(
        pr == present[i],

        "expected %s to %s in the Bloom filter, but it %s",
        strings[i],
        (present[i] ? "be present" : "not be present"),
        (pr ? "not present" : "present")
      );

      free(strings[i]);
    }

    hibp_bf_destroy(&bf);
  }

  return 0;
}
