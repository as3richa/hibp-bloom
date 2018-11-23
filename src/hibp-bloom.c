#include <openssl/sha.h>  /* SHA1 */
#include <openssl/rand.h> /* RAND_pseudo_bytes */
#include <stdio.h>        /* EOF, FILE, fread, fwrite */
#include <string.h>       /* memcpy, memcmp, strlen */
#include <math.h>         /* log, pow */
#include <assert.h>       /* assert */

#include "hibp-bloom.h"

/* ================================================================
 * Types and constants
 * ================================================================ */

/* Just for brevity */
typedef hibp_byte_t         byte;
typedef hibp_bloom_filter_t bloom_filter;
typedef hibp_status_t       status;
typedef hibp_prng_t         prng_t;
typedef hibp_getc_t         getc_t;
typedef hibp_putc_t         putc_t;

/* So the compile can populate some constants at compile time
 * (and hopefully elide them) */
#define MIN(x, y) (((x) <= (y)) ? (x) : (y))

/* SIZE_MAX isn't present on all platforms */
#undef SIZE_MAX
static const size_t SIZE_MAX = ~(size_t)0;

static const size_t SHA1_BYTES = HIBP_SHA1_BYTES;
static const size_t SHA1_BITS = 8 * HIBP_SHA1_BYTES;

/* Our implementations depends on being able to address 2**log2_bits bits, hence
 * necessarily log2_bits can't exceed the number of bits in a size_t. Moreover, it's
 * senseless to create a Bloom filter larger than the number of elements in the domain */
static const size_t LOG2_BITS_MAX = MIN(8 * sizeof(size_t), SHA1_BITS);

/* We encode the number of hash functions on disk as a 64 bit unsigned integer, so the
 * maximum number of hash functions is the minimum of SIZE_MAX and 2**64 - 1. I'm not
 * at all sure if this works on a compiler without a 64 bit type, but that's probably
 * not a serious concern */
static const size_t N_HASH_FUNCTIONS_MAX = MIN(SIZE_MAX, 0xffffffffffffffff);

/* In addition to the above restrictions we also require that the total allocated
 * size of the buffer doesn't exceed SIZE_MAX. OFC. on 64-bit systems this isn't
 * a practical concern, but I err on the side of strict correctness */

/* Magic version string. Appears at the start of every file */
static const byte VERSION[] = { 0xb1, 0x00, 0x13, 0x37 };

/* ================================================================
 * Internal utility functions
 * ================================================================ */

/* Each of the first bf->n_hash_functions slices of size bf->log2_bits of
 * bf->buffer encodes a Bloom filter hash function */
static inline byte* nth_hash_function(const bloom_filter* bf, size_t k) {
  return bf->buffer + k * bf->log2_bits;
}

/* Immediately following the last hash function is the Bloom filter bit vector */
static inline byte* bvector(const bloom_filter* bf) {
  return bf->buffer + bf->n_hash_functions * bf->log2_bits;
}

/* Evaluate the k'th hash function of bf against the given sha */
static inline size_t eval_nth_hash_function(const bloom_filter* bf, size_t k, const byte* sha) {
  const byte* indices = nth_hash_function(bf, k);

  /* For our purposes, a hash function takes some sha and concatenates together some
   * permutation of some subset of the bits of sha. In particular each hash function
   * yields a value log2_bits bits long. Each hash function is encoded as a simple array
   * of indices, each index indicating some bit to concatenate onto the hash value next */

  size_t value = 0;

  for(size_t i = 0; i < bf->log2_bits; i++) {
    const byte index = indices[i];
    assert(index < SHA1_BITS);

    /* Take the index'th bit of sha */
    const size_t bit = ((sha[index / 8] >> (index % 8)) & 1);

    /* Concatenate it onto the value */
    value |= (bit << i);
  }

  assert(value < (((size_t)1) << bf->log2_bits));

  return value;
}

/* Given n_hash_functions and log2_bits, determine whether the parameters are valid,
 * returning HIBP_E_INVAL, HIBP_E_2BIG, or HIBP_OK. buffer_size is populated with the
 * total size to allocate for the Bloom filter's buffer, if indeed the parameters were
 * valid */
static inline status compute_buffer_size(size_t* buffer_size, size_t n_hash_functions, size_t log2_bits) {
  if(n_hash_functions == 0) {
    return HIBP_E_INVAL;
  }

  if(log2_bits > LOG2_BITS_MAX || n_hash_functions > N_HASH_FUNCTIONS_MAX) {
    return HIBP_E_2BIG;
  }

  /* First, check if the hash functions by themselves already exceed SIZE_MAX */
  if(log2_bits > SIZE_MAX / n_hash_functions) {
    return HIBP_E_2BIG;
  }

  /* Won't overflow */
  const size_t hash_functions_size = log2_bits * n_hash_functions;

  /* Also won't overflow */
  const size_t vector_bits = (((size_t)1) << log2_bits);

  /* Can't do (vector_bits + 7) / 8 because the addition might overflow */
  const size_t vector_size = (vector_bits / 8) + (vector_bits % 8 != 0);

  if(hash_functions_size > SIZE_MAX - vector_size) {
    return HIBP_E_2BIG;
  }

  *buffer_size = hash_functions_size + vector_size;

  return HIBP_OK;
}

/* Plumbing for hibp_bf_load_stream */
static inline int my_read(byte* buffer, size_t size, void* ctx, getc_t getc) {
  for(size_t i = 0; i < size; i ++) {
    int c = getc(ctx);

    if(c == EOF) {
      return -1;
    }

    buffer[i] = (byte)c;
  }

  return 0;
}

/* Plumbing for hibp_bf_save_stream */
static inline int my_write(const byte* buffer, size_t size, void* ctx, putc_t putc) {
  for(size_t i = 0; i < size; i ++) {
    if(putc(buffer[i], ctx) == EOF) {
      return -1;
    }
  }

  return 0;
}

/* Given an 8-byte buffer encoding a little-endian unsigned integer, populate a size_t
 * with the value. Return -1 if the value does not fit into a size_t. Plumbing for
 * hipb_bf_load_* */
static inline int le_8_bytes_to_size_t(size_t* size, const byte* buffer) {
  if(sizeof(size_t) < 8) {
    for(size_t i = sizeof(size_t); i < 8; i ++) {
      if(buffer[i] != 0) {
        return -1;
      }
    }
  }

  (*size) = 0;

  for(size_t i = 0; i < sizeof(size_t); i ++) {
    (*size) |= (((size_t)buffer[i]) << (i * 8));
  }

  return 0;
}

/* Given a size_t, encode it as 8 bytes in little-endian order. Plumbing for
 * hipb_bf_save_* */
static inline void size_t_to_le_8_bytes(byte* buffer, size_t size) {
  for(size_t i = 0; i < 8; i ++) {
    buffer[i] = (size & 0xff);
    size >>= 8;
  }

  /* We should never be attempting to apply this function to a size_t
   * greater than 2**64 - 1 */
  assert(size == 0);
}

static inline void sha1(byte* sha, size_t size, const byte* buffer) {
  /* Strictly speaking, the contract of SHA1 (and SHA1_Init) dictates that it can
   * fail, but i) having audited the source, it cannot do so in practice in the current
   * implementation; ii) it's hard to imagine a world where it could */
  const int okay = (SHA1(buffer, size, sha) != NULL);
  (void)okay;
  assert(okay);
}

/* Returns a pseudo-random number (nominally) uniformly distributed on [0, SIZE_MAX].
 * If *use_openssl, try OpenSSL's RAND_pseudo_bytes first (setting *use_openssl = 0
 * if it fails). Use stdlib's rand if the initial call to RAND_pseudo_bytes fails, or
 * if *use_openssl was zero in the first place */
static inline size_t my_rand(int* use_openssl) {
  size_t number;

  if(*use_openssl) {
    number = 0; /* Prevent a spurious valgrind error */
    *use_openssl = (RAND_pseudo_bytes((byte*)&number, sizeof(size_t)) != -1);
  }

  if(!*use_openssl) {
    number = 0;

    /* This deliberately clobbers previously-written bits. I don't know if that's
     * actually useful, but YOLO */
    for(size_t i = 0; i < sizeof(size_t); i ++) {
      number = ((number << 8) | (size_t)rand());
    }
  }

  return number;
}

/* Default prng for hibp_bf_new */
static size_t default_prng(void* ctx, size_t upper_bound) {
  (void)ctx;
  assert(upper_bound > 0);

  /* Flag indicating whether to attempt to use the OpenSSL PRNG. If and when the OpenSSL
   * PRNG fails, my_rand will set the flag to zero and all subsequent RNG will be done
   * with sdtlib's rand */
  int use_openssl = 1;

  if(upper_bound == SIZE_MAX) {
    return my_rand(&use_openssl);
  }

  /* Because SIZE_MAX isn't necessarily divisible by upper_bound, my_rand(use_openssl) %
   * upper_bound isn't necessarily uniformly distributed (there's potentially a slight
   * bias to lower numbers). We can rectify this by discarding the highest numbers from
   * the range of my_rand */

  /* This is just a rounding down to the nearest multiple of upper_bound */
  const size_t limit = SIZE_MAX / upper_bound * upper_bound;

  size_t number;

  /* Find some random number uniformly distributed on [0, limit) */
  do {
    number = my_rand(&use_openssl);
  } while(number >= limit);

  /* Since [0, limit) has cardinality that is a multiple of upper_bound, and since number
   * is uniformly distributed on that range, number % upper_bound is uniformly
   * distributed on [0, upper_bound) */
  return number % upper_bound;
}

static inline size_t min(size_t x, size_t y) {
  return (x <= y) ? x : y;
}

/* ================================================================
 * Public API
 * ================================================================ */

/* FIXME */
void hibp_bf_get_info(hibp_filter_info_t* info, const hibp_bloom_filter_t* bf) {
  size_t buffer_size;
  compute_buffer_size(&buffer_size, bf->n_hash_functions, bf->log2_bits);

  info->n_hash_functions = bf->n_hash_functions;
  info->log2_bits = bf->log2_bits;
  info->bits = (((size_t)1) << bf->log2_bits);
  info->memory = sizeof(*bf) + buffer_size;
}

/* == Utilities == */

void hibp_compute_optimal_params(size_t* n_hash_functions, size_t* log2_bits, size_t count, double fp) {
  /* From Wikipedia:
   * ... The optimal number of bits per element is -1.44 log_2 p
   * ... with the corresponding number of hash functions ... - log_2 p */

  const double log_of_2 = log(2);
  const double log_fp = log(fp) / log_of_2;

  const double bits_per_elem = -1 * 1.44 * log_fp;
  const double bits = bits_per_elem * count;
  const double double_log2_bits = ceil(log(bits) / log_of_2 + 1e-6);

  *log2_bits = (double_log2_bits > LOG2_BITS_MAX)
    ? LOG2_BITS_MAX
    : (size_t)double_log2_bits;

  const double double_n_hash_functions = ceil(-1 * log_fp);

  *n_hash_functions = (double_n_hash_functions > N_HASH_FUNCTIONS_MAX)
     ? N_HASH_FUNCTIONS_MAX
     : (size_t)double_n_hash_functions;
}

void hibp_compute_constrained_params(size_t* n_hash_functions, size_t* log2_bits, size_t count, size_t max_memory) {
  /* We could do something highly intelligent here with a binary search or some such, but
   * given that we only have to iterate over < 40 values let's not bother */

  /* In any practical application the memory consumed by the bit vector is going to be
   * much greater than the memory consumed by the hash functions, so let's just tune that
   * knob. Iterate over possible values of log2_bits, selecting at each iteration the
   * optimal number of hash functions to minimize false positives; compute the size of the
   * requisite buffer, and select the largest value log2_bits that is within the constraint.
   * At minimum, set log2_bits to 8 (even if that doesn't satisfy the constriant) */

  const double log_of_2 = log(2);

  for(size_t candidate_log2_bits = 8;; candidate_log2_bits ++) {
    /* From Wikipedia:
     * ... for a given m and n, the value of k that minimizes the false positive probability is
     * k = m / n * ln 2
     * k is the number of hash functions, m is the number of bits, n is the number of elements */

    double double_candidate_n_hash_functions = ceil(pow(2, candidate_log2_bits) / count * log_of_2 + 1e-6);

    size_t candidate_n_hash_functions = (double_candidate_n_hash_functions > N_HASH_FUNCTIONS_MAX)
      ? N_HASH_FUNCTIONS_MAX
      : double_candidate_n_hash_functions;

    size_t buffer_size;

    if(compute_buffer_size(&buffer_size, candidate_log2_bits, candidate_n_hash_functions) != HIBP_OK) {
      buffer_size = SIZE_MAX;
    }

    if(buffer_size > max_memory && candidate_log2_bits > 8) {
      break;
    }

    *n_hash_functions = candidate_n_hash_functions;
    *log2_bits = candidate_log2_bits;
  }
}

status hibp_sha1_hex2bin(byte* bin, const char* hex) {
  for(int i = 0; i < 20; i ++) {
    bin[i] = 0;

    for(int k = i; k <= i + 1; k ++) {
      bin[i] <<= 4;

      if('0' <= hex[k] && hex[k] <= '9') {
        bin[i] |= (hex[k] - '0');
      } else if('a' <= hex[k] && hex[k] <= 'f') {
        bin[i] |= (0xa + hex[k] - 'a');
      } else if('A' <= hex[k] && hex[k] <= 'F') {
        bin[i] |= (0xa + hex[k] - 'A');
      } else {
        return HIBP_E_INVAL;
      }
    }
  }

  return HIBP_OK;
}

/* == Lifecyle == */

status hibp_bf_new(bloom_filter* bf, size_t n_hash_functions, size_t log2_bits) {
  return hibp_bf_new_prng(bf, n_hash_functions, log2_bits, NULL, default_prng);
}

status hibp_bf_new_prng(bloom_filter* bf, size_t n_hash_functions, size_t log2_bits, void* ctx, prng_t prng) {
  size_t buffer_size;
  const status st = compute_buffer_size(&buffer_size, n_hash_functions, log2_bits);

  if(st != HIBP_OK) {
    return st;
  }

  bf->n_hash_functions = n_hash_functions;
  bf->log2_bits = log2_bits;

  /* calloc to save us memsetting the bit vector. On some systems it's actually faster */
  bf->buffer = (byte*)calloc(buffer_size, 1);

  if(bf->buffer == NULL) {
    return HIBP_E_NOMEM;
  }

  byte* hash_functions = nth_hash_function(bf, 0);
  const size_t hash_functions_size = bf->log2_bits * bf->n_hash_functions;

  /* Intuitively, if we have a small number of hash functions, we probably don't want
   * them all to include the same bits over and over again when there are other bits
   * of the SHA1 hash that aren't covered by any hash function. We can satisfy this
   * criteria by making sure that all SHA1_BITS bits are covered before we reuse any;
   * this can be accomplished easily by selecting indices from a permutation until it's
   * exhausted, and repeating with a new permutation untill all the indices of all the
   * hash functions have been selected.
   *
   * As an added bonus, because we encode the hash functions in a single flat array
   * we can just repeatedly generate and memcpy permutations without worrying about
   * the dileneation between the different hash functions */

  /* Initialize the permutation */
  byte permutation[SHA1_BITS];
  for(size_t i = 0; i < SHA1_BITS; i++) {
    permutation[i] = i;
  }

  /* Repeatedly generate a permutation with the Fisher-Yates shuffle, and copy as many
   * indices as necessary into hash_functions. We need hash_functions_size indices
   * in total, and each iteration generates SHA1_BITS of them */

  for(size_t generated = 0; generated < hash_functions_size; generated += SHA1_BITS) {
    for(size_t i = SHA1_BITS - 1; i > 0; i --) {
      const size_t j = prng(ctx, i + 1);

      const byte swap = permutation[i];
      permutation[i] = permutation[j];
      permutation[j] = swap;
    }

    const size_t copy_size = min(hash_functions_size - generated, SHA1_BITS);
    memcpy(hash_functions + generated, permutation, copy_size);
  }

  return HIBP_OK;
}

void hibp_bf_destroy(bloom_filter* bf) {
  free(bf->buffer);
}

/* == IO == */

/* It's trivial to just implement hibp_bf_load_file using hibp_bf_load_stream
 * as a building block, but then you're using fgetc rather than fread to read
 * a potentially huge buffer which seems like a serious tax to be paying.
 * We could also just implement the two separately, but it'd be pretty easy
 * to screw up. Instead, let's just stick the function body for hibp_bf_load_stream
 * in a header file, and use some macros to use it for hibp_bf_load_file
 * at no performance cost */
status hibp_bf_load_file(bloom_filter* bf, FILE* file) {

  #define ctx file
  #define getc fgetc
  #define my_read(buffer, size, file, _unused) (fread((buffer), 1, (size), (file)) != (size))

  #include "load-stream.h"

  #undef ctx
  #undef getc
  #undef my_read

}

status hibp_bf_load_stream(bloom_filter* bf, void* ctx, getc_t getc) {
  #include "load-stream.h"
}

/* Same as above - put the body of hibp_bf_save_stream in a header, then use it
 * for both implementations with some preprocessor magic */
status hibp_bf_save_file(const bloom_filter* bf, FILE* file) {

  #define ctx file
  #define putc fputc
  #define my_write(buffer, size, file, _unused) (fwrite((buffer), 1, (size), (file)) != (size))

  #include "save-stream.h"

  #undef ctx
  #undef putc
  #undef my_write

}

status hibp_bf_save_stream(const bloom_filter* bf, void* ctx, putc_t putc) {
  #include "save-stream.h"
}

/* == Insertion == */

void hibp_bf_insert(bloom_filter* bf, size_t size, const byte* buffer) {
  byte sha[SHA1_BYTES];
  sha1(sha, size, buffer);
  hibp_bf_insert_sha1(bf, sha);
}

void hibp_bf_insert_str(bloom_filter* bf, const char* str) {
  hibp_bf_insert(bf, strlen(str), (const byte*)str);
}

void hibp_bf_insert_sha1(bloom_filter* bf, const byte* sha) {
  /* For every hash function h, set the bit h(sha) in the Bloom filter vector */

  byte* vector = bvector(bf);

  for(size_t i = 0; i < bf->n_hash_functions; i++) {
    const size_t k = eval_nth_hash_function(bf, i, sha);
    vector[k / 8] |= (1 << (k % 8));
  }
}

/* == Querying == */

int hibp_bf_query(const bloom_filter* bf, size_t size, const byte* buffer) {
  byte sha[SHA1_BYTES];
  sha1(sha, size, buffer);
  return hibp_bf_query_sha1(bf, sha);
}

int hibp_bf_query_str(const bloom_filter* bf, const char* str) {
  return hibp_bf_query(bf, strlen(str), (const byte*)str);
}

int hibp_bf_query_sha1(const bloom_filter* bf, const byte* sha) {
  /* If, for some hash function h, the bit h(sha) is unset in the Bloom filter
   * vector, then sha is guaranteed not to be present in the set. Otherwise, sha
   * is present _with high probability_ */

  const byte* vector = bvector(bf);

  for(size_t i = 0; i < bf->n_hash_functions; i++) {
    const size_t k = eval_nth_hash_function(bf, i, sha);

    assert(k < (((size_t)1) << bf->log2_bits));

    if(((vector[k / 8] >> (k % 8)) & 1) == 0) {
      return 0;
    }
  }

  return 1;
}
