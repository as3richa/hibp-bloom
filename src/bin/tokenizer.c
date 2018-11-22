#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "tokenizer.h"
#include "stream.h"

/* ================================================================
 * Plumbing
 * ================================================================ */

/* Given a character, return its value in hexidecimal, or -1 if the character
 * isn't hexademical */
static inline int hex2int(int hex) {
  if('0' <= hex && hex <= '9') {
    return hex - '0';
  } else if('a' <= hex && hex <= 'z')  {
    return 10 + hex - 'a';
  } else if('A' <= hex && hex <= 'Z') {
    return 10 + hex - 'A';
  } else {
    return -1;
  }
}

/* Given an integer from 0 through 15, return the corresponding hexademical character */
static inline int int2hex(int i) {
  assert(0 <= i && i < 16);
  if(i < 10) {
    return '0' + i;
  } else {
    return 'a' + (i - 10);
  }
}

/* Parse an escape sequence for a quoted token (assuming the backslash was
 * already read). Return EOF on parse error */
static inline int parse_escape_sequence(stream_t* stream) {
  switch(stream_getc(stream)) {
    case '"':
      return '"';
    case '\'':
      return '\'';
    case '\\':
      return '\\';
    case 'n':
      return '\n';
    case 'x':
    {
      const int high = hex2int(stream_getc(stream));

      if(high == -1) {
        return EOF;
      }

      const int low = hex2int(stream_getc(stream));

      if(low == -1) {
        return EOF;
      }

      return ((high << 4) | low);
    }
    default:
      return -1;
  }
}

/* Parse a quoted token from the given stream */
static inline tokenization_status_t parse_quoted_token(token_t* token, stream_t* stream) {
  const int quote = stream_getc(stream);
  assert(quote == '"' || quote == '\'');

  for(;;) {
    int c = stream_peek(stream);

    if(c == EOF || c == '\n') {
      return TS_E_MISSING_QUOTE;
    }

    stream_getc(stream);

    if(c == quote) {
      break;
    }

    if(c == '\\') {
      c = parse_escape_sequence(stream);

      if(c == EOF) {
        return TS_E_BAD_ESCAPE;
      }
    }
    
    if(token_pushc(token, c) == -1) {
      return TS_E_NOMEM;
    } 
  }

  const int c = stream_peek(stream);

  if(!(c == EOF || isspace(c) || c == ';')) {
    return TS_E_MISSING_SEP;
  }

  return TS_OK;
}

/* ================================================================
 * Public interface
 * ================================================================ */

int skip_to_command(stream_t* stream) {
  for(;;) {
    /* Skip whitespace and semicolons */
    for(;;) {
      const int c = stream_peek(stream);

      if(c == EOF || (!isspace(c) && c != ';')) {
        break;
      }

      stream_getc(stream);
    }

    /* Drain to the end of the line or EOF in the case of a comment */
    if(stream_peek(stream) == '#') {
      drain_line(stream);
    }

    const int c = stream_peek(stream);

    /* If we slurped up a comment we may have landed on another line with skippable
     * characters, in which case we need to do another iteration */

    if(c == EOF || (!isspace(c) && c != ';' && c != '#')) {
      return c;
    }
  }
}

tokenization_status_t next_token(token_t* token, stream_t* stream) {
  bool quoted;

  {
    const int c = stream_peek(stream);

    /* Command is not exhausted */
    assert(c != EOF && c != '\n' && c != ';' && c != '#');

    /* stream is pointing at the first character of the next token
     * (i.e. no leading spaces) */
    assert(!isspace(c));

    quoted = (c == '"' || c == '\'');
  }

  token->line = stream->line;
  token->column = stream->column;

  token->length = 0;

  if(quoted) {
    const tokenization_status_t status = parse_quoted_token(token, stream);

    if(status != TS_OK) {
      return status;
    }
  } else {
    for(;;) {
      const int c = stream_peek(stream);

      if(c == EOF || isspace(c) || c == ';' || c == '#') {
        break;
      }

      stream_getc(stream);

      if(token_pushc(token, c) == -1) {
        return TS_E_NOMEM;
      }
    }
  }

  /* Drain trailing (non-newline) spaces */
  for(;;) {
    const int c = stream_peek(stream);

    if(c == EOF || c == '\n' || !isspace(c)) {
      break;
    }

    stream_getc(stream);
  }

  /* Check if this is the last token in the ongoing command */
  const int c = stream_peek(stream);
  token->last_of_command = (c == EOF || c == '\n' || c == ';' || c == '#');

  return TS_OK;
}

void drain_line(stream_t* stream) {
  for(;;) {
    const int c = stream_peek(stream);

    if(c == EOF || c == '\n') {
      break;
    }

    stream_getc(stream);
  }
}

char* token2str(const token_t* token) {
  if(token->length == 0) {
    return strdup("\"\"");
  }

  int needs_quoting = 0;
  size_t str_length = 0;

  for(size_t i = 0; i < token->length; i ++) {
    const int c = token->buffer[i];

    if(c == '"' || c == '\n') {
      needs_quoting = 1;
      str_length += 2; /* \", \n */
    } else if(c == '\'' || c == ' ' || c == ';' || c == '#') {
      needs_quoting = 1;
      str_length ++; /* verbatim */
    } else if(!isprint(c)) {
      needs_quoting = 1;
      str_length += 4; /* \xhh */
    } else {
      str_length ++;
    }
  }

  if(needs_quoting) {
    str_length += 2;
  }

  char* str = (char*)malloc(str_length + 1);

  if(str == NULL) {
    return NULL;
  }

  if(needs_quoting) {
    str[0] = '"';

    size_t k = 1;

    for(size_t i = 0; i < token->length; i ++) {
      const int c = token->buffer[i];

      if(c == '"') {
        str[k ++] = '\\';
        str[k ++] = '"';
      } else if(c == '\n') {
        str[k ++] = '\\';
        str[k ++] = 'n';
      } else if(!isprint(c)) {
        str[k ++] = '\\';
        str[k ++] = 'x';
        str[k ++] = int2hex((c >> 4) & 0x0f);
        str[k ++] = int2hex(c & 0x0f);
      } else {
        str[k ++] = c;
      }
    }

    assert(k == str_length - 1);
    str[k] = '"';
  } else {
    assert(str_length == token->length);
    memcpy(str, token->buffer, str_length);
  }

  str[str_length] = 0;

  return str;
}
