#ifndef NEWOS_COMPRESSION_ZLIB_H
#define NEWOS_COMPRESSION_ZLIB_H

#include <stddef.h>

size_t compression_zlib_store_bound(size_t input_size);
int compression_zlib_store(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);
size_t compression_zlib_fixed_rle_bound(size_t input_size);
int compression_zlib_fixed_rle(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);
size_t compression_zlib_fixed_lz77_bound(size_t input_size);
int compression_zlib_fixed_lz77(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);
int compression_zlib_inflate(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);

#endif
