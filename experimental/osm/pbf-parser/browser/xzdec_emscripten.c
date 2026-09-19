#include <lzma.h>

#include <stdio.h>
#include <stdlib.h>

#define XZDEC_BUFFER_SIZE 65536U

static int decode_file(const char *input_path, const char *output_path) {
    FILE *input = 0;
    FILE *output = 0;
    unsigned char *in_buffer = 0;
    unsigned char *out_buffer = 0;
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret;
    int result = 1;

    input = fopen(input_path, "rb");
    if (input == 0) {
        fprintf(stderr, "xzdec: could not open input\n");
        goto cleanup;
    }
    output = fopen(output_path, "wb");
    if (output == 0) {
        fprintf(stderr, "xzdec: could not open output\n");
        goto cleanup;
    }

    in_buffer = (unsigned char *)malloc(XZDEC_BUFFER_SIZE);
    out_buffer = (unsigned char *)malloc(XZDEC_BUFFER_SIZE);
    if (in_buffer == 0 || out_buffer == 0) {
        fprintf(stderr, "xzdec: out of memory\n");
        goto cleanup;
    }

    ret = lzma_stream_decoder(&stream, UINT64_MAX, 0);
    if (ret != LZMA_OK) {
        fprintf(stderr, "xzdec: could not initialize decoder: %u\n", (unsigned)ret);
        goto cleanup;
    }

    stream.avail_in = 0;
    stream.next_in = 0;
    stream.next_out = out_buffer;
    stream.avail_out = XZDEC_BUFFER_SIZE;

    for (;;) {
        if (stream.avail_in == 0 && !feof(input)) {
            stream.next_in = in_buffer;
            stream.avail_in = fread(in_buffer, 1, XZDEC_BUFFER_SIZE, input);
            if (ferror(input)) {
                fprintf(stderr, "xzdec: input read failed\n");
                goto cleanup;
            }
        }

        ret = lzma_code(&stream, feof(input) ? LZMA_FINISH : LZMA_RUN);
        if (stream.avail_out == 0 || ret == LZMA_STREAM_END) {
            size_t write_size = XZDEC_BUFFER_SIZE - stream.avail_out;
            if (write_size != 0 && fwrite(out_buffer, 1, write_size, output) != write_size) {
                fprintf(stderr, "xzdec: output write failed\n");
                goto cleanup;
            }
            stream.next_out = out_buffer;
            stream.avail_out = XZDEC_BUFFER_SIZE;
        }

        if (ret == LZMA_STREAM_END) {
            result = 0;
            break;
        }
        if (ret != LZMA_OK) {
            fprintf(stderr, "xzdec: decode failed: %u\n", (unsigned)ret);
            goto cleanup;
        }
    }

cleanup:
    lzma_end(&stream);
    free(out_buffer);
    free(in_buffer);
    if (output != 0) fclose(output);
    if (input != 0) fclose(input);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: xzdec INPUT.xz OUTPUT\n");
        return 1;
    }
    return decode_file(argv[1], argv[2]);
}
