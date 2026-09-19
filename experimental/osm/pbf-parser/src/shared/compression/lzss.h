#ifndef NEWOS_COMPRESSION_LZSS_H
#define NEWOS_COMPRESSION_LZSS_H

#include <stddef.h>

#define COMPRESSION_LZSS_WINDOW_SIZE 8192U
#define COMPRESSION_LZSS_MIN_MATCH 3U
#define COMPRESSION_LZSS_MAX_MATCH 10U

#define COMPRESSION_LZSS_PROFILE_WIDE_WINDOW 0U
#define COMPRESSION_LZSS_PROFILE_WIDE_MATCH 1U
#define COMPRESSION_LZSS_PROFILE_MEDIUM_MATCH 2U
#define COMPRESSION_LZSS_PROFILE_LONG_MATCH 3U
#define COMPRESSION_LZSS_PROFILE_DEFAULT COMPRESSION_LZSS_PROFILE_WIDE_WINDOW

size_t compression_lzss_bound(size_t input_size);
int compression_lzss_compress(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);
int compression_lzss_decompress(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);
int compression_lzss_compress_profile(unsigned int profile_id, const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);
int compression_lzss_decompress_profile(unsigned int profile_id, const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out);

#endif
