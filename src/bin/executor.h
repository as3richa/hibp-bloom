#ifndef _EXECUTOR_H_
#define _EXECUTOR_H_

#include "hibp-bloom.h"
#include "stream.h"
#include "tokenizer.h"

typedef enum {
  EX_E_RECOVERABLE = -255,
  EX_E_FATAL,
  EX_EOF,
  EX_OK = 0
} executor_status_t;

typedef struct {
  stream_t* stream;

  /* The scripting language is simple to the point that we never actually need to
   * hold more than a single token in memory when parsing and executing a script -
   * hence we can use a single token_t object, reusing its allocated storage with
   * each read */
  token_t token;

  /* Is stdin still available to be slurped by some command? */
  bool stdin_consumed;

  /* Does filter refer to a properly-initialized Bloom filter? */
  bool filter_initialized;

  hibp_bloom_filter_t filter;

  /* Populated by executor_exec_one and handled by main */
  executor_status_t status;
} executor_t;

void executor_new(executor_t* ex, stream_t* stream, int stdin_consumed);
void executor_destroy(executor_t* ex);

void executor_exec_one(executor_t* ex);
void executor_drain_line(executor_t* ex);

#endif
