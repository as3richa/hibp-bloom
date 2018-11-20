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
  token_t token;

  int stdin_consumed: 1;
  int filter_initialized: 1;

  hibp_bloom_filter_t filter;

  /* Everything below is public */

  executor_status_t status;
  char error_str[256 + 1];
} executor_t;

void executor_new(executor_t* ex, stream_t* stream, int stdin_consumed);
void executor_destroy(executor_t* ex);
void executor_exec_one(executor_t* ex);
void executor_drain_line(executor_t* ex);

#endif
