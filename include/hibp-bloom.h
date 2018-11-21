#ifndef _HIBP_BLOOM_H_
#define _HIBP_BLOOM_H_

#include <stddef.h>
#include <limits.h>

#if CHAR_BIT != 8
#error "Only platforms with 8 bit chars are supported"
#endif

typedef unsigned char hibp_byte_t;

#define HIBP_SHA1_BYTES 20

/* ================================================================
 * hibp_bloom_filter_t
 * ================================================================ */

/* ~All API functions operate on this structure. It implements a Bloom filter that can be
 * inserted into, searched, and persisted to disk. This structure's internals are private;
 * comments within are pedagogical and are not documentation */

typedef struct {
  /* How many hash functions does this Bloom filter use? */
  size_t n_hash_functions;

  /* Log base 2 of the number of bits in our filter. Our hash functions lend themselves
   * particularly well to vectors of power-of-2 length, and it's actually more useful
   * to know the log2_bits anyway */
  size_t log2_bits;

  /* Blob of data encoding the Bloom filter hash functions and the Bloom filter bit vector.
   * The first log2_bits * n_hash_functions bytes encode the hash functions;
   * the next ceil((2**log2_bits) / 8) bytes encode the bit vector. */
  hibp_byte_t* buffer;
} hibp_bloom_filter_t;

/* ================================================================
 * Error codes
 * ================================================================ */

typedef enum {
  /* Memory allocation failed */
  HIBP_E_NOMEM = -255, 

  /* Version string doesn't match expectation */
  HIBP_E_VERSION,

  /* IO error while reading or writing to a file or stream (incl. unexpected EOF) */
  HIBP_E_IO,

  /* File was read in its entirety, but checksum doesn't match */
  HIBP_E_CHECKSUM,

  /* A parameter exceeds an internal limit */
  HIBP_E_2BIG,

  /* A parameter has an invalid value */
  HIBP_E_INVAL,

  /* Everything is wonderful */
  HIBP_OK = 0
} hibp_status_t;

/* ================================================================
 * Callback types
 * ================================================================ */

/* By convention, any method that takes a callback function also takes a void* ctx, which
 * in turn is passed into the callback. This makes it possible to implement stateful
 * callbacks */

/* For passing a pseudo-random number generator into hibp_bf_new_prng. Given a ctx and an
 * upper bound u, return a uniformly distributed random integer in the range [0, u) */
typedef size_t (*hibp_prng_t)(void*, size_t);

/* For passing arbitrary streams into hibp_bf_load_stream. This can be used to load a
 * filter from somewhere other than the disk. Given a ctx, return the next byte of the
 * stream, or EOF if the stream is exhausted or if an error occurs. This signature is
 * isomorphic to fgetc from stdio */
typedef int (*hibp_getc_t)(void*);

/* For passing arbitrary streams into hibp_bf_save_stream. This can be used to save a
 * filter to somewhere other than the disk. Contract: given a byte and a ctx, write the
 * byte to the stream. EOF should be returned to signal an error; the return value is
 * otherwise ignored. This signature is isomorphic to fputc from stdio */
typedef int (*hibp_putc_t)(int, void*);

/* ================================================================
 * Public API
 * ================================================================ */

/* With the exception of hibp_bf_new* and hipb_bf_load*, every function that accepts a
 * hipb_bloom_filter_t* expects the structure therein to be fully initialized and
 * allocated. Calling such a function with an uninitialized structure is undefined
 * behavior */

/* == Utilities == */

/* Given the expected number of elements and an acceptable false positive rate,
 * calculate appropriate values of n_hash_functions and log2_bits to be passed
 * into hibp_bf_new (making no promises about memory consumption) */
void hibp_compute_optimal_params(size_t* n_hash_functions, size_t* log2_bits,
                                 size_t count, double fp);

/* Given the expected number of elements and a constraint on the total memory
 * consumption, calculate appropriate values of n_hash_functions and log2_bits
 * to be passed into hibp_bf_new. Allocator metadata isn't accounted for
 * in memory consumption. This function is best-effort and may exceed max_memory
 * if it can't reasonably be satisfied. Roughly speaking, this function picks the
 * largest possible log2_bits that would fit within max_memory, then chooses
 * an appropriate number of hash functions given that choice */
void hibp_compute_constrained_params(size_t* n_hash_functions, size_t* log2_bits,
                                     size_t count, size_t max_memory);

/* Given a 40-byte ASCII hexadecimal representation of a SHA1 hash, re-encode it
 * as 20 bytes of binary. Bail out at the first non-hex byte (so passing too-short
 * C strings does not induce undefined behavior). Returns HIBP_E_INVAL if the first
 * 40 characters of hex are not a valid hexadecimal string, HIBP_OK otherwise */
hibp_status_t hibp_sha1_hex2bin(hibp_byte_t* bin, const char* hex);

/* == Lifecyle == */

/* Initialize the Bloom filter pointed to by bf, generating a new set of random hash
 * functions and allocating all requisite memory. The filter will have n_hash_functions
 * unique hash functions and will be backed by a bit vector of size 2**log2_bits; the
 * utility functions can be used to compute sane defaults for these values. Uses the
 * default PRNG, which uses OpenSSL's RAND_pseudo_bytes as a PRNG if available, falling
 * back to stdlib's rand otherwise. If strong randomness guarantees or particular seeding
 * behavior are required, hibp_bf_new_prng should be used instead with a custom PRNG.
 * Returns:
 * - HIBP_E_INVAL if either n_hash_functions or log2_bits is zero
 * - HIBP_E_2BIG if n_hash_functions or log2_bits exceeds the limits of the implementation
 * - HIBP_E_NOMEM if memory allocation fails
 * - HIBP_E_CHECKSUM if the file was read in its entirety but its checksum doesn't match
 * - HIBP_OK otherwise
 * In all cases except the last, no call to hibp_bf_destroy is necessary */
hibp_status_t hibp_bf_new(hibp_bloom_filter_t* bf, size_t n_hash_functions, size_t log2_bits);

/* Initialize the Bloom filter pointer to by bf, generating a new set of random hash
 * functions and allocating all requisite memory. prng is used as the PRNG for hash
 * function generation; this function has otherwise-identical semantics to hibp_bf_new */
hibp_status_t hibp_bf_new_prng(hibp_bloom_filter_t* bf, size_t n_hash_functions, size_t log2_bits,
                               void* ctx, hibp_prng_t prng);

/* Deallocate any dynamically-allocated memory associated with the Bloom filter bf. Every
 * call to hibp_bf_new* or hibp_bf_load* should have a corresponding call to
 * hibp_bf_destroy to avoid leaking memory. A Bloom filter cannot be used after being
 * destroyed */
void hibp_bf_destroy(hibp_bloom_filter_t* bf);

/* == IO == */

/* Read, allocate, and initialize a previously-saved Bloom filter from the given file.
 * Returns:
 * - HIBP_E_VERSION if the version string of the given file doesn't match the expectation
 * - HIBP_E_IO in the case of an IO error (including but not limited to premature EOF)
 * - HIBP_E_INVAL if n_hash_functions or log2_bits is zero
 * - HIBP_E_2BIG if n_hash_functions or log2_bits exceeds the limits of the implementation
 * - HIBP_E_NOMEM if memory allocated fails
 * - HIBP_OK otherwise
 * In all cases except the last, no call to hibp_bf_destroy is necessary.
 * In general, files are portable across machines and architectures, but a machine with
 * 32-bit size_t could yield HIBP_E_2BIG for a very large filter generate on a 64-bit
 * machine. Discounting this edge case, and assuming an uncorrupted storage medium,
 * HIBP_E_{VERSION,INVAL,2BIG} will not occur for a file proced by hibp_bf_save_file */
hibp_status_t hibp_bf_load_file(hibp_bloom_filter_t* bf, FILE* file);

/* Read, allocate, and initialize a previously-saved Bloom filter from the given stream.
 * getc is used repeatedly to extract bytes from the stream as specified in the comment
 * for hibp_getc_t. This function is otherwise semantically identical to
 * hipb_bf_load_file */
hibp_status_t hibp_bf_load_stream(hibp_bloom_filter_t* bf, void* ctx, hibp_getc_t getc);

/* Persist a Bloom filter by writing its representation to the given file. Returns
 * HIBP_E_IO in the event of an IO error, HIBP_OK otherwise. */
hibp_status_t hibp_bf_save_file(const hibp_bloom_filter_t* bf, FILE* file);

/* Persist a Bloom filter by writing its representation to the given stream.
 * Individual bytes are written to the stream by calling putc as specified in the
 * comment for hibp_putc_t. Recall in particular that any IO errors must be signalled
 * by returning EOF from putc. In all other respects, this function is semantically
 * identical to hipb_bf_save_file */
hibp_status_t hibp_bf_save_stream(const hibp_bloom_filter_t* bf, void* ctx, hibp_putc_t putc);

/* == Insertion == */

/* Given a string encoded as a byte buffer, insert it into the set */
void hibp_bf_insert(hibp_bloom_filter_t* bf, size_t size, const hibp_byte_t* buffer);

/* Given a null-terminated C-style string, insert it into the set */
void hibp_bf_insert_str(hibp_bloom_filter_t* bf, const char* str);

/* Given the 20-byte binary SHA1 hash of some string, insert that string into the set */
void hibp_bf_insert_sha1(hibp_bloom_filter_t* bf, const hibp_byte_t* sha);

/* == Querying == */

/* All query functions have the same semantics: given (a representation of) some string,
 * return 0 if the string is certainly not in the set, or 1 if the string is in the set
 * with high probability. The false positive rate for non-adversarial data can be tuned to
 * be arbitrarily low with a good choice of n_hash_functions and log2_bits. In general,
 * more bits means fewer false positives (and for any particular number of bits there
 * exists some optimal number of hash functions) */

/* Given a string encoded as a byte buffer, determine whether it is present in the set
 * with high probability */
int hibp_bf_query(const hibp_bloom_filter_t* bf, size_t size, const hibp_byte_t* buffer);

/* Given a null-terminated C-style string, determine whether it is present in the set
 * with high probability */
int hibp_bf_query_str(const hibp_bloom_filter_t* bf, const char* str);

/* Given the 20-byte binary SHA1 hash of some string, determine whether that string is
 * present in the set with high probability */
int hibp_bf_query_sha1(const hibp_bloom_filter_t* bf, const hibp_byte_t* sha);

#endif /* _HIBP_BLOOM_H_ */
