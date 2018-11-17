  /* The file format is layed out as follows ([bytes] description):
   * [4]          version string
   * [8]          n_hash_functions
   * [1]          log2_bits
   * [SHA1_BYTES] buffer SHA1 checksum
   * [...]        buffer */

  /* ================================
   * Version string
   * ================================ */

  byte this_version[sizeof(VERSION)];

  if(my_read(this_version, sizeof(VERSION), ctx, getc) != 0) {
    return HIBP_E_IO;
  }

  if (memcmp(this_version, VERSION, sizeof(VERSION)) != 0) {
    return HIBP_E_VERSION;
  }

  /* ================================
   * n_hash_functions
   * ================================ */

  byte n_hash_functions_bytes[8];

  if(my_read(n_hash_functions_bytes, 8, ctx, getc) != 0) {
    return HIBP_E_IO;
  }

  if(le_8_bytes_to_size_t(&bf->n_hash_functions, n_hash_functions_bytes) != 0) {
    return HIBP_E_2BIG;
  }

  /* ================================
   * log2_bits
   * ================================ */

  int c = getc(ctx);

  if(c == EOF) {
    return HIBP_E_IO;
  }

  assert(c == (c & 0xff));

  /* Cast away signedness */
  bf->log2_bits = (byte)c;

  /* Can sanity check sizes and compute buffer size now */

  size_t buffer_size;
  const status st = compute_buffer_size(&buffer_size, bf->n_hash_functions, bf->log2_bits);

  if(st != HIBP_OK) {
    return st;
  }

  /* ================================
   * Checksum
   * ================================ */

  byte checksum[SHA1_BYTES];

  if(my_read(checksum, SHA1_BYTES, ctx, getc) != 0) {
    return HIBP_E_IO;
  }

  /* ================================
   * buffer
   * ================================ */

  bf->buffer = (byte*)malloc(buffer_size);

  if(bf->buffer == NULL) {
    return HIBP_E_NOMEM;
  }

  if(my_read(bf->buffer, buffer_size, ctx, getc) != 0) {
    free(bf->buffer);
    return HIBP_E_IO;
  }

  /* Assert that the checksum actually matches */
  byte true_checksum[SHA1_BYTES];
  sha1(true_checksum, buffer_size, bf->buffer);

  if(memcmp(checksum, true_checksum, SHA1_BYTES) != 0) {
    free(bf->buffer);
    return HIBP_E_CHECKSUM;
  }

  return HIBP_OK;
