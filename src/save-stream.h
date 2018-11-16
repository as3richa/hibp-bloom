  /* The file format is layed out as follows ([bytes] description):
   * [4]          version string
   * [8]          n_hash_functions
   * [1]          log2_bits
   * [SHA1_BYTES] buffer SHA1 checksum
   * [...]        buffer
   * Buffer size is computed by check_size (it's not obvious) */

  /* ================================
   * Version string
   * ================================ */

  if(my_write(VERSION, sizeof(VERSION), ctx, putc) != 0) {
    return HIBP_E_IO;
  }

  /* ================================
   * n_hash_functions
   * ================================ */

  byte n_hash_functions_bytes[8];
  size_t_to_le_8_bytes(n_hash_functions_bytes, bf->n_hash_functions);

  if(my_write(n_hash_functions_bytes, 8, ctx, putc) != 0) {
    return HIBP_E_IO;
  }

  /* ================================
   * log2_bits
   * ================================ */

  assert(0 <= bf->log2_bits && bf->log2_bits <= 255);

  if(putc(bf->log2_bits, ctx) == EOF) {
    return HIBP_E_IO;
  }

  /* ================================
   * Checksum
   * ================================ */

  size_t buffer_size;
  const status st = compute_buffer_size(&buffer_size, bf->n_hash_functions, bf->log2_bits);

  assert(st == HIBP_OK);

  byte checksum[SHA1_BYTES];
  sha1(checksum, buffer_size, bf->buffer);

  if(my_write(checksum, SHA1_BYTES, ctx, putc) != 0) {
    return HIBP_E_IO;
  }

  /* ================================
   * buffer
   * ================================ */

  if(my_write(bf->buffer, buffer_size, ctx, putc) != 0) {
    return HIBP_E_IO;
  }

  return HIBP_OK;
