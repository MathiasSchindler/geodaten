#include "compression/zstd.h"

#include "runtime.h"

#include <stdint.h>

static const uint32_t ZSTD_MAGIC_NUMBER = 0xFD2FB528u;
static const uint32_t ZSTD_SKIPPABLE_MAGIC_BASE = 0x184D2A50u;
static const uint32_t ZSTD_SKIPPABLE_MAGIC_MASK = 0xFFFFFFF0u;
static const size_t COMPRESSION_ZSTD_SIZE_MAX = (size_t)-1;

enum {
    COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS = 256,
    COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG = 12,
    COMPRESSION_ZSTD_HUFF_MAX_TABLE_SIZE = 1 << COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG
};

typedef struct {
    const unsigned char *src;
    size_t size;
    size_t byte_pos;
    int bit_pos;
} CompressionZstdRevBitReader;

typedef struct {
    uint64_t bit_container;
    unsigned bits_consumed;
    const unsigned char *ptr;
    const unsigned char *start;
    const unsigned char *limit_ptr;
} CompressionZstdBitDStream;

typedef enum {
    COMPRESSION_ZSTD_BIT_DSTREAM_UNFINISHED = 0,
    COMPRESSION_ZSTD_BIT_DSTREAM_END_OF_BUFFER = 1,
    COMPRESSION_ZSTD_BIT_DSTREAM_COMPLETED = 2,
    COMPRESSION_ZSTD_BIT_DSTREAM_OVERFLOW = 3
} CompressionZstdBitDStreamStatus;

typedef struct {
    const unsigned char *ptr;
    size_t size;
    unsigned char rle_byte;
    int is_rle;
    unsigned char *owned_ptr;
} CompressionZstdLiterals;

typedef struct {
    size_t header_size;
    size_t regenerated_size;
    size_t compressed_size;
    unsigned stream_count;
} CompressionZstdCompressedLiteralsHeader;

typedef struct {
    size_t description_size;
    unsigned symbol_count;
    unsigned table_log;
    unsigned char weights[COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS];
} CompressionZstdHuffmanTreeDescription;

typedef struct {
    const unsigned char *src;
    size_t size;
    size_t byte_pos;
    unsigned bit_pos;
} CompressionZstdFwdBitReader;

typedef struct {
    unsigned accuracy_log;
    unsigned symbol_count;
    size_t bytes_used;
    int16_t normalized_counts[256];
} CompressionZstdFseTableDescription;

typedef struct {
    unsigned accuracy_log;
    unsigned table_size;
    unsigned char symbols[512];
    unsigned char bits[512];
    uint16_t base[512];
} CompressionZstdFseDecodeTable;

typedef struct {
    unsigned table_log;
    unsigned table_size;
    unsigned char symbols[COMPRESSION_ZSTD_HUFF_MAX_TABLE_SIZE];
    unsigned char bits[COMPRESSION_ZSTD_HUFF_MAX_TABLE_SIZE];
} CompressionZstdHuffmanDecodeTable;

typedef struct {
    const unsigned char *src;
    size_t src_size;
    size_t src_pos;
    unsigned char *dst;
    size_t dst_size;
    size_t dst_pos;
    int has_checksum;
    uint32_t rep_offsets[3];
    int have_huf_table;
    CompressionZstdHuffmanDecodeTable huf_table;
    int have_seq_tables;
    CompressionZstdFseDecodeTable ll_table;
    CompressionZstdFseDecodeTable of_table;
    CompressionZstdFseDecodeTable ml_table;
} CompressionZstdFrameState;

static const unsigned char compression_zstd_ll_default_symbols[64] = {
    0, 0, 1, 3, 4, 6, 7, 9, 10, 12, 14, 16, 18, 19, 21, 22,
    24, 25, 26, 27, 29, 31, 0, 1, 2, 4, 5, 7, 8, 10, 11, 13,
    16, 17, 19, 20, 22, 23, 25, 25, 26, 28, 30, 0, 1, 2, 3, 5,
    6, 8, 9, 11, 12, 15, 17, 18, 20, 21, 23, 24, 35, 34, 33, 32
};

static const unsigned char compression_zstd_ll_default_bits[64] = {
    4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5,
    5, 5, 5, 6, 6, 6, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 4, 4, 5, 6, 6, 4, 4, 5, 5, 5,
    5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6
};

static const unsigned char compression_zstd_ll_default_base[64] = {
    0, 16, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 32, 0, 0, 0, 0, 32, 0, 0, 32, 0, 32, 0, 32, 0, 0,
    32, 0, 32, 0, 32, 0, 0, 16, 32, 0, 0, 48, 16, 32, 32, 32,
    32, 32, 32, 32, 32, 0, 32, 32, 32, 32, 32, 32, 0, 0, 0, 0
};

static const unsigned char compression_zstd_ml_default_symbols[64] = {
    0, 1, 2, 3, 5, 6, 8, 10, 13, 16, 19, 22, 25, 28, 31, 33,
    35, 37, 39, 41, 43, 45, 1, 2, 3, 4, 6, 7, 9, 12, 15, 18,
    21, 24, 27, 30, 32, 34, 36, 38, 40, 42, 44, 1, 1, 2, 4, 5,
    7, 8, 11, 14, 17, 20, 23, 26, 29, 52, 51, 50, 49, 48, 47, 46
};

static const unsigned char compression_zstd_ml_default_bits[64] = {
    6, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 4, 4, 5, 5,
    5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6
};

static const unsigned char compression_zstd_ml_default_base[64] = {
    0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 16, 0, 32, 0, 32, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 48, 16, 32, 32,
    32, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char compression_zstd_of_default_symbols[32] = {
    0, 6, 9, 15, 21, 3, 7, 12, 18, 23, 5, 8, 14, 20, 2, 7,
    11, 17, 22, 4, 8, 13, 19, 1, 6, 10, 16, 28, 27, 26, 25, 24
};

static const unsigned char compression_zstd_of_default_bits[32] = {
    5, 4, 5, 5, 5, 5, 4, 5, 5, 5, 5, 4, 5, 5, 5, 4,
    5, 5, 5, 5, 4, 5, 5, 5, 4, 5, 5, 5, 5, 5, 5, 5
};

static const unsigned char compression_zstd_of_default_base[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16,
    0, 0, 0, 0, 16, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0
};

static const uint32_t compression_zstd_ll_code_base[36] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u,
    16u, 18u, 20u, 22u, 24u, 28u, 32u, 40u, 48u, 64u, 128u, 256u, 512u, 1024u,
    2048u, 4096u, 8192u, 16384u, 32768u, 65536u
};

static const unsigned char compression_zstd_ll_code_bits[36] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    1u, 1u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 6u, 7u, 8u, 9u, 10u,
    11u, 12u, 13u, 14u, 15u, 16u
};

static const uint32_t compression_zstd_ml_code_base[53] = {
    3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u,
    19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u,
    33u, 34u, 35u, 37u, 39u, 41u, 43u, 47u, 51u, 59u, 67u, 83u, 99u, 131u,
    259u, 515u, 1027u, 2051u, 4099u, 8195u, 16387u, 32771u, 65539u
};

static const unsigned char compression_zstd_ml_code_bits[53] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u, 5u, 7u,
    8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u
};

static CompressionZstdResult compression_zstd_make_result(CompressionZstdStatus status, CompressionZstdBackend backend,
                                          const char *message) {
    CompressionZstdResult result;

    result.status = status;
    result.backend = backend;
    result.message = message;
    return result;
}

static void *compression_zstd_alloc(size_t size) {
    return rt_malloc(size == 0u ? 1u : size);
}

static void compression_zstd_free(void *ptr, size_t size) {
    (void)size;
    rt_free(ptr);
}

static void compression_zstd_init_state(CompressionZstdFrameState *state,
                                const void *src, size_t src_size,
                                void *dst, size_t dst_size) {
    state->src = (const unsigned char *)src;
    state->src_size = src_size;
    state->src_pos = 0u;
    state->dst = (unsigned char *)dst;
    state->dst_size = dst_size;
    state->dst_pos = 0u;
    state->has_checksum = 0;
    state->rep_offsets[0] = 1u;
    state->rep_offsets[1] = 4u;
    state->rep_offsets[2] = 8u;
    state->have_huf_table = 0;
    state->have_seq_tables = 0;
}

const char *compression_zstd_backend_name(CompressionZstdBackend backend) {
    switch (backend) {
        case COMPRESSION_ZSTD_BACKEND_CUSTOM:
            return "custom";
        case COMPRESSION_ZSTD_BACKEND_NONE:
        default:
            return "none";
    }
}

static void compression_zstd_copy(unsigned char *dst, const unsigned char *src, size_t len) {
#if defined(__GNUC__) || defined(__clang__)
    if (len != 0u) {
        __builtin_memcpy(dst, src, len);
    }
#else
    size_t i;

    for (i = 0u; i < len; ++i) {
        dst[i] = src[i];
    }
#endif
}

static void compression_zstd_fill(unsigned char *dst, unsigned char value, size_t len) {
#if defined(__GNUC__) || defined(__clang__)
    if (len != 0u) {
        __builtin_memset(dst, (int)value, len);
    }
#else
    size_t i;

    for (i = 0u; i < len; ++i) {
        dst[i] = value;
    }
#endif
}

static int compression_zstd_highest_set_bit(unsigned char value) {
    int bit;

    for (bit = 7; bit >= 0; --bit) {
        if (((unsigned)value >> bit) & 1u) {
            return bit;
        }
    }
    return -1;
}

static unsigned compression_zstd_floor_log2_u32(uint32_t value) {
    unsigned bit = 0u;

    while (value > 1u) {
        value >>= 1;
        bit += 1u;
    }
    return bit;
}

static uint32_t compression_zstd_read_le24(const unsigned char *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16);
}

static uint32_t compression_zstd_read_le32(const unsigned char *src) {
    return (uint32_t)src[0]
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16)
         | ((uint32_t)src[3] << 24);
}

static uint64_t compression_zstd_read_le64(const unsigned char *src) {
    return (uint64_t)src[0]
         | ((uint64_t)src[1] << 8)
         | ((uint64_t)src[2] << 16)
         | ((uint64_t)src[3] << 24)
         | ((uint64_t)src[4] << 32)
         | ((uint64_t)src[5] << 40)
         | ((uint64_t)src[6] << 48)
         | ((uint64_t)src[7] << 56);
}

static inline uint64_t compression_zstd_read_le64_partial(const unsigned char *src, size_t src_size) {
    uint64_t value = 0u;

    if (src_size >= sizeof(uint64_t)) {
        return compression_zstd_read_le64(src);
    }

    switch (src_size) {
        case 7u: value |= (uint64_t)src[6] << 48; /* fall through */
        case 6u: value |= (uint64_t)src[5] << 40; /* fall through */
        case 5u: value |= (uint64_t)src[4] << 32; /* fall through */
        case 4u: value |= (uint64_t)src[3] << 24; /* fall through */
        case 3u: value |= (uint64_t)src[2] << 16; /* fall through */
        case 2u: value |= (uint64_t)src[1] << 8; /* fall through */
        case 1u: value |= (uint64_t)src[0]; break;
        case 0u: break;
    }
    return value;
}

static inline uint32_t compression_zstd_read_bits_le_at(const unsigned char *src, size_t src_size,
                                                size_t bit_offset, unsigned bit_count) {
    size_t byte_offset = bit_offset >> 3;
    size_t remaining = src_size - byte_offset;
    unsigned bit_shift = (unsigned)(bit_offset & 7u);
    uint64_t value;
    uint64_t mask;

    if (bit_count == 0u || byte_offset >= src_size) {
        return 0u;
    }

    value = compression_zstd_read_le64_partial(src + byte_offset, remaining);
    mask = bit_count >= 32u ? 0xFFFFFFFFull : ((1ull << bit_count) - 1u);
    return (uint32_t)((value >> bit_shift) & mask);
}

static inline uint32_t compression_zstd_read_bits_backward_padded(const unsigned char *src, size_t src_size,
                                                          int *bit_offset_io, unsigned bit_count) {
    int new_offset = *bit_offset_io - (int)bit_count;
    size_t actual_offset;
    unsigned actual_bits;
    uint32_t value;

    actual_offset = (new_offset < 0) ? 0u : (size_t)new_offset;
    actual_bits = bit_count;
    if (new_offset < 0) {
        int available_bits = (int)bit_count + new_offset;

        actual_bits = available_bits > 0 ? (unsigned)available_bits : 0u;
    }

    value = 0u;
    if (actual_bits != 0u) {
        value = compression_zstd_read_bits_le_at(src, src_size, actual_offset, actual_bits);
    }
    if (new_offset < 0) {
        int missing = -new_offset;

        if ((unsigned)missing < 32u) {
            value <<= (unsigned)missing;
        } else {
            value = 0u;
        }
    }

    *bit_offset_io = new_offset;
    return value;
}

static CompressionZstdResult compression_zstd_revbit_init(CompressionZstdRevBitReader *reader,
                                          const unsigned char *src, size_t size) {
    int highest_bit;

    if (size == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "empty reverse bitstream");
    }
    highest_bit = compression_zstd_highest_set_bit(src[size - 1u]);
    if (highest_bit < 0) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid reverse bitstream terminator");
    }

    reader->src = src;
    reader->size = size;
    reader->byte_pos = size - 1u;
    reader->bit_pos = highest_bit - 1;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_revbit_read_bits(CompressionZstdRevBitReader *reader,
                                               unsigned bit_count, uint32_t *value_out) {
    uint32_t value = 0u;

    while (bit_count != 0u) {
        unsigned available;
        unsigned take;
        unsigned shift;
        uint32_t chunk;

        while (reader->bit_pos < 0) {
            if (reader->byte_pos == 0u) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "reverse bitstream exhausted");
            }
            reader->byte_pos -= 1u;
            reader->bit_pos = 7;
        }

        available = (unsigned)reader->bit_pos + 1u;
        take = bit_count < available ? bit_count : available;
        shift = available - take;
        chunk = ((uint32_t)reader->src[reader->byte_pos] >> shift) & ((1u << take) - 1u);
        value = (value << take) | chunk;
        reader->bit_pos -= (int)take;
        bit_count -= take;
    }

    *value_out = value;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_bitdstream_init(CompressionZstdBitDStream *stream,
                                              const unsigned char *src, size_t src_size) {
    unsigned char last_byte;
    size_t i;

    if (src_size == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "empty reverse bitstream");
    }

    stream->start = src;
    stream->limit_ptr = src + sizeof(uint64_t);
    if (src_size >= sizeof(uint64_t)) {
        stream->ptr = src + src_size - sizeof(uint64_t);
        stream->bit_container = compression_zstd_read_le64(stream->ptr);
    } else {
        stream->ptr = src;
        stream->bit_container = 0u;
        for (i = 0u; i < src_size; ++i) {
            stream->bit_container |= (uint64_t)src[i] << (i * 8u);
        }
    }

    last_byte = src[src_size - 1u];
    if (last_byte == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid reverse bitstream terminator");
    }

    stream->bits_consumed = (unsigned)(8 - compression_zstd_highest_set_bit(last_byte));
    if (src_size < sizeof(uint64_t)) {
        stream->bits_consumed += (unsigned)((sizeof(uint64_t) - src_size) * 8u);
    }
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static uint32_t compression_zstd_bitdstream_look_bits(const CompressionZstdBitDStream *stream, unsigned bit_count) {
    if (bit_count == 0u) {
        return 0u;
    }
    return (uint32_t)((stream->bit_container >> (64u - stream->bits_consumed - bit_count))
                      & ((1ull << bit_count) - 1u));
}

static uint32_t compression_zstd_bitdstream_read_bits(CompressionZstdBitDStream *stream, unsigned bit_count) {
    uint32_t value;

    value = compression_zstd_bitdstream_look_bits(stream, bit_count);
    stream->bits_consumed += bit_count;
    return value;
}

static CompressionZstdBitDStreamStatus compression_zstd_bitdstream_reload(CompressionZstdBitDStream *stream) {
    static const uint64_t zero_filled = 0u;

    if (stream->bits_consumed > 64u) {
        stream->ptr = (const unsigned char *)&zero_filled;
        return COMPRESSION_ZSTD_BIT_DSTREAM_OVERFLOW;
    }

    if (stream->ptr >= stream->limit_ptr) {
        stream->ptr -= stream->bits_consumed >> 3;
        stream->bits_consumed &= 7u;
        stream->bit_container = compression_zstd_read_le64(stream->ptr);
        return COMPRESSION_ZSTD_BIT_DSTREAM_UNFINISHED;
    }
    if (stream->ptr == stream->start) {
        if (stream->bits_consumed < 64u) {
            return COMPRESSION_ZSTD_BIT_DSTREAM_END_OF_BUFFER;
        }
        return COMPRESSION_ZSTD_BIT_DSTREAM_COMPLETED;
    }

    {
        unsigned nb_bytes = stream->bits_consumed >> 3;
        CompressionZstdBitDStreamStatus result = COMPRESSION_ZSTD_BIT_DSTREAM_UNFINISHED;

        if ((size_t)(stream->ptr - stream->start) < (size_t)nb_bytes) {
            nb_bytes = (unsigned)(stream->ptr - stream->start);
            result = COMPRESSION_ZSTD_BIT_DSTREAM_END_OF_BUFFER;
        }
        stream->ptr -= nb_bytes;
        stream->bits_consumed -= nb_bytes * 8u;
        stream->bit_container = compression_zstd_read_le64(stream->ptr);
        return result;
    }
}

static int compression_zstd_revbit_consumed(const CompressionZstdRevBitReader *reader) {
    return reader->byte_pos == 0u && reader->bit_pos < 0;
}

static CompressionZstdResult compression_zstd_fwdbit_read_bits(CompressionZstdFwdBitReader *reader,
                                               unsigned bit_count, uint32_t *value_out) {
    uint32_t value = 0u;
    unsigned value_shift = 0u;

    while (bit_count != 0u) {
        unsigned available;
        unsigned take;
        uint32_t chunk;

        if (reader->byte_pos >= reader->size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "forward bitstream exhausted");
        }

        available = 8u - reader->bit_pos;
        take = bit_count < available ? bit_count : available;
        chunk = ((uint32_t)reader->src[reader->byte_pos] >> reader->bit_pos)
              & ((1u << take) - 1u);
        value |= chunk << value_shift;
        value_shift += take;
        reader->bit_pos += take;
        if (reader->bit_pos == 8u) {
            reader->bit_pos = 0u;
            reader->byte_pos += 1u;
        }
        bit_count -= take;
    }

    *value_out = value;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static size_t compression_zstd_fwdbit_bytes_used(const CompressionZstdFwdBitReader *reader) {
    return reader->byte_pos + (reader->bit_pos != 0u ? 1u : 0u);
}

static CompressionZstdResult compression_zstd_parse_fse_table_description(const unsigned char *src,
                                                          size_t src_size,
                                                          unsigned max_accuracy_log,
                                                          unsigned max_symbol_count,
                                                          CompressionZstdFseTableDescription *table_out) {
    unsigned char scratch[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    CompressionZstdFwdBitReader reader;
    CompressionZstdResult result;
    uint32_t low4bits;
    unsigned remaining;
    unsigned threshold;
    unsigned bit_count;
    unsigned symbol;
    int previous_zero;
    unsigned i;

    if (src_size == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "missing FSE table description");
    }

    if (src_size < sizeof(scratch)) {
        compression_zstd_copy(scratch, src, src_size);
        reader.src = scratch;
        reader.size = sizeof(scratch);
    } else {
        reader.src = src;
        reader.size = src_size;
    }
    reader.byte_pos = 0u;
    reader.bit_pos = 0u;

    for (i = 0u; i < 256u; ++i) {
        table_out->normalized_counts[i] = 0;
    }

    result = compression_zstd_fwdbit_read_bits(&reader, 4u, &low4bits);
    if (result.status != COMPRESSION_ZSTD_OK) {
        return result;
    }
    table_out->accuracy_log = (unsigned)low4bits + 5u;
    if (table_out->accuracy_log > max_accuracy_log) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "FSE accuracy log exceeds allowed maximum");
    }

    remaining = (1u << table_out->accuracy_log) + 1u;
    threshold = 1u << table_out->accuracy_log;
    bit_count = table_out->accuracy_log + 1u;
    symbol = 0u;
    previous_zero = 0;
    while (remaining > 1u) {
        unsigned max;
        uint32_t raw_value;
        int count;

        if (previous_zero) {
            unsigned zero_run = 0u;

            for (;;) {
                uint32_t repeat_flag;

                result = compression_zstd_fwdbit_read_bits(&reader, 2u, &repeat_flag);
                if (result.status != COMPRESSION_ZSTD_OK) {
                    return result;
                }
                zero_run += (unsigned)repeat_flag;
                if (repeat_flag != 3u) {
                    break;
                }
            }

            if (symbol + zero_run > max_symbol_count) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "FSE zero run exceeds supported symbol count");
            }
            symbol += zero_run;
            previous_zero = 0;
            if (remaining <= 1u) {
                break;
            }
        }

        if (symbol >= max_symbol_count) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "FSE table uses more symbols than supported");
        }

        max = (2u * threshold - 1u) - remaining;

        result = compression_zstd_fwdbit_read_bits(&reader, bit_count - 1u, &raw_value);
        if (result.status != COMPRESSION_ZSTD_OK) {
            return result;
        }
        if (raw_value < max) {
            count = (int)raw_value;
        } else {
            uint32_t extra_bit;

            result = compression_zstd_fwdbit_read_bits(&reader, 1u, &extra_bit);
            if (result.status != COMPRESSION_ZSTD_OK) {
                return result;
            }
            count = (int)(raw_value + (extra_bit << (bit_count - 1u)));
            if ((unsigned)count >= threshold) {
                count -= (int)max;
            }
        }

        count -= 1;
        table_out->normalized_counts[symbol++] = (int16_t)count;
        if (count >= 0) {
            remaining -= (unsigned)count;
        } else {
            remaining += (unsigned)count;
        }
        previous_zero = (count == 0);

        if (remaining < threshold) {
            if (remaining <= 1u) {
                break;
            }
            bit_count = compression_zstd_floor_log2_u32(remaining) + 1u;
            threshold = 1u << (bit_count - 1u);
        }
    }

    if (remaining != 1u || symbol == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid FSE table description");
    }

    table_out->symbol_count = symbol;
    table_out->bytes_used = compression_zstd_fwdbit_bytes_used(&reader);
    if (table_out->bytes_used > src_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "FSE table description overruns input");
    }
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_build_fse_decode_table(const CompressionZstdFseTableDescription *desc,
                                                     CompressionZstdFseDecodeTable *table_out) {
    unsigned table_size;
    int16_t symbols[512];
    uint16_t next_state[256];
    unsigned high;
    unsigned step;
    unsigned pos;
    unsigned symbol;
    unsigned i;

    table_size = 1u << desc->accuracy_log;
    if (table_size > 512u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_UNSUPPORTED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "FSE table size exceeds local decoder capacity");
    }

    for (i = 0u; i < table_size; ++i) {
        symbols[i] = -1;
    }
    for (i = 0u; i < 256u; ++i) {
        next_state[i] = 0u;
    }

    high = table_size;
    for (symbol = 0u; symbol < desc->symbol_count; ++symbol) {
        if (desc->normalized_counts[symbol] == -1) {
            if (high == 0u) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "too many low-probability FSE symbols");
            }
            high -= 1u;
            symbols[high] = (int16_t)symbol;
            next_state[symbol] = 1u;
        } else if (desc->normalized_counts[symbol] > 0) {
            next_state[symbol] = (uint16_t)desc->normalized_counts[symbol];
        }
    }

    step = (table_size >> 1) + (table_size >> 3) + 3u;
    pos = 0u;
    for (symbol = 0u; symbol < desc->symbol_count; ++symbol) {
        int count;

        count = desc->normalized_counts[symbol];
        if (count <= 0) {
            continue;
        }
        while (count-- > 0) {
            while (symbols[pos] != -1) {
                pos = (pos + step) & (table_size - 1u);
            }
            symbols[pos] = (int16_t)symbol;
            pos = (pos + step) & (table_size - 1u);
        }
    }

    for (i = 0u; i < table_size; ++i) {
        uint32_t state_number;

        if (symbols[i] < 0) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "FSE decode table has unassigned states");
        }
        symbol = (unsigned)symbols[i];
        state_number = next_state[symbol];
        next_state[symbol] = (uint16_t)(state_number + 1u);
        table_out->symbols[i] = (unsigned char)symbol;
        table_out->bits[i] = (unsigned char)(desc->accuracy_log - compression_zstd_floor_log2_u32(state_number));
        table_out->base[i] = (uint16_t)((state_number << table_out->bits[i]) - table_size);
    }

    table_out->accuracy_log = desc->accuracy_log;
    table_out->table_size = table_size;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static void compression_zstd_init_predefined_ll_table(CompressionZstdFseDecodeTable *table) {
    unsigned i;

    table->accuracy_log = 6u;
    table->table_size = 64u;
    for (i = 0u; i < table->table_size; ++i) {
        table->symbols[i] = compression_zstd_ll_default_symbols[i];
        table->bits[i] = compression_zstd_ll_default_bits[i];
        table->base[i] = compression_zstd_ll_default_base[i];
    }
}

static void compression_zstd_init_predefined_of_table(CompressionZstdFseDecodeTable *table) {
    unsigned i;

    table->accuracy_log = 5u;
    table->table_size = 32u;
    for (i = 0u; i < table->table_size; ++i) {
        table->symbols[i] = compression_zstd_of_default_symbols[i];
        table->bits[i] = compression_zstd_of_default_bits[i];
        table->base[i] = compression_zstd_of_default_base[i];
    }
}

static void compression_zstd_init_predefined_ml_table(CompressionZstdFseDecodeTable *table) {
    unsigned i;

    table->accuracy_log = 6u;
    table->table_size = 64u;
    for (i = 0u; i < table->table_size; ++i) {
        table->symbols[i] = compression_zstd_ml_default_symbols[i];
        table->bits[i] = compression_zstd_ml_default_bits[i];
        table->base[i] = compression_zstd_ml_default_base[i];
    }
}

static void compression_zstd_init_rle_fse_table(CompressionZstdFseDecodeTable *table, unsigned symbol) {
    table->accuracy_log = 0u;
    table->table_size = 1u;
    table->symbols[0] = (unsigned char)symbol;
    table->bits[0] = 0u;
    table->base[0] = 0u;
}

static CompressionZstdResult compression_zstd_fse_init_state(CompressionZstdRevBitReader *reader,
                                             const CompressionZstdFseDecodeTable *table,
                                             uint32_t *state_out) {
    if (table->accuracy_log == 0u) {
        *state_out = 0u;
        return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
    }
    return compression_zstd_revbit_read_bits(reader, table->accuracy_log, state_out);
}

static CompressionZstdResult compression_zstd_decode_sequence_table(const unsigned char *src, size_t src_size,
                                                    unsigned mode, unsigned max_accuracy_log,
                                                    unsigned max_symbol_value,
                                                    const CompressionZstdFseDecodeTable *previous_table,
                                                    int have_previous,
                                                    void (*init_predefined)(CompressionZstdFseDecodeTable *),
                                                    CompressionZstdFseDecodeTable *table_out,
                                                    size_t *consumed_out) {
    CompressionZstdFseTableDescription desc;
    CompressionZstdResult result;

    if (mode == 0u) {
        init_predefined(table_out);
        *consumed_out = 0u;
        return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
    }
    if (mode == 1u) {
        if (src_size == 0u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated RLE sequence table");
        }
        if ((unsigned)src[0] > max_symbol_value) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "RLE sequence symbol is out of range");
        }
        compression_zstd_init_rle_fse_table(table_out, src[0]);
        *consumed_out = 1u;
        return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
    }
    if (mode == 2u) {
        result = compression_zstd_parse_fse_table_description(src, src_size, max_accuracy_log,
                                                      max_symbol_value + 1u, &desc);
        if (result.status != COMPRESSION_ZSTD_OK) {
            return result;
        }
        result = compression_zstd_build_fse_decode_table(&desc, table_out);
        if (result.status != COMPRESSION_ZSTD_OK) {
            return result;
        }
        *consumed_out = desc.bytes_used;
        return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
    }

    if (!have_previous) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "repeat sequence table has no previous table");
    }
    *table_out = *previous_table;
    *consumed_out = 0u;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_parse_compressed_literals_header(const unsigned char *src,
                                                               size_t src_size,
                                                               unsigned block_type,
                                                               unsigned size_format,
                                                               CompressionZstdCompressedLiteralsHeader *header_out) {
    uint32_t bits;

    if (size_format == 0u || size_format == 1u) {
        if (src_size < 3u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated compressed literals header");
        }
        bits = compression_zstd_read_le24(src);
        header_out->header_size = 3u;
        header_out->stream_count = (size_format == 0u) ? 1u : 4u;
        header_out->regenerated_size = (size_t)((bits >> 4) & 0x3FFu);
        header_out->compressed_size = (size_t)((bits >> 14) & 0x3FFu);
    } else if (size_format == 2u) {
        if (src_size < 4u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated compressed literals header");
        }
        bits = compression_zstd_read_le24(src) | ((uint32_t)src[3] << 24);
        header_out->header_size = 4u;
        header_out->stream_count = 4u;
        header_out->regenerated_size = (size_t)((bits >> 4) & 0x3FFFu);
        header_out->compressed_size = (size_t)((bits >> 18) & 0x3FFFu);
    } else {
        uint64_t bits64;

        if (src_size < 5u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated compressed literals header");
        }
        bits64 = (uint64_t)compression_zstd_read_le32(src) | ((uint64_t)src[4] << 32);
        header_out->header_size = 5u;
        header_out->stream_count = 4u;
        header_out->regenerated_size = (size_t)((bits64 >> 4) & 0x3FFFFu);
        header_out->compressed_size = (size_t)((bits64 >> 22) & 0x3FFFFu);
    }

    if (block_type == 3u && header_out->compressed_size == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "treeless literals block has empty payload");
    }
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_parse_huffman_tree_description(const unsigned char *src,
                                                             size_t src_size,
                                                             CompressionZstdHuffmanTreeDescription *tree_out) {
    unsigned char header;
    CompressionZstdFseDecodeTable decode_table;
    CompressionZstdFseTableDescription fse_table;
    CompressionZstdResult result;
    size_t explicit_weights;
    unsigned rank_stats[COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG + 1];
    unsigned weight_total;
    unsigned last_weight;
    unsigned table_log;
    unsigned i;

    if (src_size == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "missing Huffman tree description");
    }

    for (i = 0u; i <= COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG; ++i) {
        rank_stats[i] = 0u;
    }

    header = src[0];
    if (header < 128u) {
        size_t description_size = 1u + (size_t)header;
        size_t decoded_weights;
        const unsigned char *weights_src;
        size_t weights_src_size;

        if (description_size > src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated FSE-compressed Huffman tree description");
        }
        result = compression_zstd_parse_fse_table_description(src + 1u, (size_t)header,
                                  6u, 256u, &fse_table);
        if (result.status != COMPRESSION_ZSTD_OK) {
            return result;
        }
        result = compression_zstd_build_fse_decode_table(&fse_table, &decode_table);
        if (result.status != COMPRESSION_ZSTD_OK) {
            return result;
        }
        if (fse_table.bytes_used > (size_t)header) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "Huffman weight FSE table overruns description payload");
        }

        weights_src = src + 1u + fse_table.bytes_used;
        weights_src_size = (size_t)header - fse_table.bytes_used;
        {
            CompressionZstdBitDStream stream;
            CompressionZstdBitDStreamStatus status;
            uint32_t state1;
            uint32_t state2;

            if (weights_src_size == 0u) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "Huffman weight description is missing FSE payload");
            }

            result = compression_zstd_bitdstream_init(&stream, weights_src, weights_src_size);
            if (result.status != COMPRESSION_ZSTD_OK) {
                return result;
            }
            state1 = compression_zstd_bitdstream_read_bits(&stream, decode_table.accuracy_log);
            state2 = compression_zstd_bitdstream_read_bits(&stream, decode_table.accuracy_log);
            status = compression_zstd_bitdstream_reload(&stream);
            if (status == COMPRESSION_ZSTD_BIT_DSTREAM_OVERFLOW) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "Huffman weight FSE stream overflowed during initialization");
            }

            decoded_weights = 0u;
            while (status == COMPRESSION_ZSTD_BIT_DSTREAM_UNFINISHED) {
                if (decoded_weights >= COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS - 4u) {
                    return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "Huffman weight stream produces too many symbols");
                }
                if (state1 >= decode_table.table_size || state2 >= decode_table.table_size) {
                    return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "Huffman weight FSE state is out of range");
                }

                tree_out->weights[decoded_weights++] = decode_table.symbols[state1];
                state1 = (uint32_t)decode_table.base[state1]
                      + compression_zstd_bitdstream_read_bits(&stream, decode_table.bits[state1]);
                tree_out->weights[decoded_weights++] = decode_table.symbols[state2];
                state2 = (uint32_t)decode_table.base[state2]
                      + compression_zstd_bitdstream_read_bits(&stream, decode_table.bits[state2]);
                tree_out->weights[decoded_weights++] = decode_table.symbols[state1];
                state1 = (uint32_t)decode_table.base[state1]
                      + compression_zstd_bitdstream_read_bits(&stream, decode_table.bits[state1]);
                tree_out->weights[decoded_weights++] = decode_table.symbols[state2];
                state2 = (uint32_t)decode_table.base[state2]
                      + compression_zstd_bitdstream_read_bits(&stream, decode_table.bits[state2]);
                status = compression_zstd_bitdstream_reload(&stream);
            }

            while (1) {
                if (state1 >= decode_table.table_size || state2 >= decode_table.table_size) {
                    return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "Huffman weight FSE state is out of range");
                }
                if (decoded_weights >= COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS - 1u) {
                    return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "Huffman weight stream produces too many symbols");
                }

                tree_out->weights[decoded_weights++] = decode_table.symbols[state1];
                state1 = (uint32_t)decode_table.base[state1]
                      + compression_zstd_bitdstream_read_bits(&stream, decode_table.bits[state1]);
                if (compression_zstd_bitdstream_reload(&stream) == COMPRESSION_ZSTD_BIT_DSTREAM_OVERFLOW) {
                    if (decoded_weights >= COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS) {
                        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                    "Huffman weight stream produces too many symbols");
                    }
                    tree_out->weights[decoded_weights++] = decode_table.symbols[state2];
                    break;
                }

                if (decoded_weights >= COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS - 1u) {
                    return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "Huffman weight stream produces too many symbols");
                }
                tree_out->weights[decoded_weights++] = decode_table.symbols[state2];
                state2 = (uint32_t)decode_table.base[state2]
                      + compression_zstd_bitdstream_read_bits(&stream, decode_table.bits[state2]);
                if (compression_zstd_bitdstream_reload(&stream) == COMPRESSION_ZSTD_BIT_DSTREAM_OVERFLOW) {
                    if (decoded_weights >= COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS) {
                        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                    "Huffman weight stream produces too many symbols");
                    }
                    tree_out->weights[decoded_weights++] = decode_table.symbols[state1];
                    break;
                }
            }
        }

        tree_out->description_size = description_size;
        explicit_weights = decoded_weights;
    } else {
        explicit_weights = (size_t)header - 127u;
        {
            size_t weight_bytes = (explicit_weights + 1u) / 2u;
            size_t description_size = 1u + weight_bytes;

            if (description_size > src_size) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "truncated raw Huffman tree description");
            }
            if (explicit_weights >= COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "raw Huffman tree uses too many weights");
            }
            tree_out->description_size = description_size;
            for (i = 0u; i < explicit_weights; i += 2u) {
                unsigned char packed = src[1u + (i / 2u)];

                tree_out->weights[i] = (unsigned char)(packed >> 4);
                if (i + 1u < explicit_weights) {
                    tree_out->weights[i + 1u] = (unsigned char)(packed & 0x0Fu);
                }
            }
        }
    }

    if (explicit_weights == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "Huffman tree is missing weights");
    }

    weight_total = 0u;
    for (i = 0u; i < explicit_weights; ++i) {
        unsigned weight = tree_out->weights[i];

        if (weight > COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "Huffman weight exceeds supported maximum");
        }
        rank_stats[weight] += 1u;
        weight_total += (1u << weight) >> 1;
    }
    if (weight_total == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid empty Huffman tree");
    }

    table_log = compression_zstd_floor_log2_u32(weight_total) + 1u;
    if (table_log > COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "Huffman table log exceeds supported maximum");
    }
    {
        unsigned total = 1u << table_log;
        unsigned rest = total - weight_total;

        if (rest == 0u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "Huffman tree is missing implied last weight");
        }
        if ((rest & (rest - 1u)) != 0u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "implied Huffman weight is not a power of two");
        }
        last_weight = compression_zstd_floor_log2_u32(rest) + 1u;
    }

    if (last_weight > COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG || explicit_weights + 1u > COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "implied Huffman weight exceeds decoder limits");
    }

    tree_out->weights[explicit_weights] = (unsigned char)last_weight;
    rank_stats[last_weight] += 1u;
    if (rank_stats[1] < 2u || (rank_stats[1] & 1u) != 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "Huffman tree rank-1 weights are invalid");
    }

    tree_out->symbol_count = (unsigned)explicit_weights + 1u;
    tree_out->table_log = table_log;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_build_huffman_decode_table(const CompressionZstdHuffmanTreeDescription *tree,
                                                         CompressionZstdHuffmanDecodeTable *table_out) {
    unsigned rank_stats[COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG + 1];
    unsigned next_rank[COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG + 1];
    unsigned char sorted_symbols[COMPRESSION_ZSTD_HUFF_MAX_SYMBOLS];
    unsigned table_size;
    unsigned next;
    unsigned symbol_index;
    unsigned table_pos;
    unsigned weight;
    unsigned symbol;
    unsigned i;

    if (tree->table_log == 0u || tree->table_log > COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid Huffman table log");
    }

    for (i = 0u; i <= COMPRESSION_ZSTD_HUFF_MAX_TABLE_LOG; ++i) {
        rank_stats[i] = 0u;
        next_rank[i] = 0u;
    }

    for (symbol = 0u; symbol < tree->symbol_count; ++symbol) {
        weight = tree->weights[symbol];
        if (weight > tree->table_log) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "Huffman weight exceeds table log");
        }
        rank_stats[weight] += 1u;
    }

    next = 0u;
    for (weight = 0u; weight <= tree->table_log; ++weight) {
        next_rank[weight] = next;
        next += rank_stats[weight];
    }

    for (symbol = 0u; symbol < tree->symbol_count; ++symbol) {
        weight = tree->weights[symbol];
        sorted_symbols[next_rank[weight]++] = (unsigned char)symbol;
    }

    table_size = 1u << tree->table_log;
    symbol_index = rank_stats[0];
    table_pos = 0u;
    for (weight = 1u; weight <= tree->table_log; ++weight) {
        unsigned symbol_count = rank_stats[weight];
        unsigned repeat = 1u << (weight - 1u);
        unsigned bits = tree->table_log + 1u - weight;

        for (i = 0u; i < symbol_count; ++i) {
            unsigned j;
            unsigned char decoded_symbol = sorted_symbols[symbol_index + i];

            for (j = 0u; j < repeat; ++j) {
                if (table_pos + j >= table_size) {
                    return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "Huffman decode table overflows");
                }
                table_out->symbols[table_pos + j] = decoded_symbol;
                table_out->bits[table_pos + j] = (unsigned char)bits;
            }
            table_pos += repeat;
        }
        symbol_index += symbol_count;
    }

    if (table_pos != table_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "Huffman decode table is incomplete");
    }

    table_out->table_log = tree->table_log;
    table_out->table_size = table_size;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_decode_huffman_1stream(const unsigned char *src, size_t src_size,
                                                     const CompressionZstdHuffmanDecodeTable *table,
                                                     unsigned char *dst, size_t dst_size) {
    const unsigned char *symbols = table->symbols;
    const unsigned char *bits = table->bits;
    uint32_t table_mask = table->table_size - 1u;
    int padding;
    int bit_offset;
    uint32_t state;
    size_t i;

    if (src_size == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "empty Huffman bitstream");
    }
    if (src[src_size - 1u] == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid Huffman bitstream terminator");
    }

    padding = 8 - compression_zstd_highest_set_bit(src[src_size - 1u]);
    bit_offset = (int)(src_size * 8u) - padding;
    state = compression_zstd_read_bits_backward_padded(src, src_size, &bit_offset, table->table_log);
    if (state >= table->table_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "Huffman decode state is out of range");
    }

    for (i = 0u; i < dst_size; ++i) {
        unsigned bit_count = bits[state];
        uint32_t rest;

        dst[i] = symbols[state];
        rest = compression_zstd_read_bits_backward_padded(src, src_size, &bit_offset, bit_count);
        state = ((state << bit_count) + rest) & table_mask;
    }

    if (bit_offset != -(int)table->table_log) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "Huffman bitstream still has unread bits");
    }

    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_decode_huffman_4stream(const unsigned char *src, size_t src_size,
                                                     const CompressionZstdHuffmanDecodeTable *table,
                                                     unsigned char *dst, size_t dst_size) {
    size_t stream_sizes[4];
    size_t regen_sizes[4];
    size_t quarter;
    size_t offset;
    unsigned i;
    CompressionZstdResult result;

    if (src_size < 6u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "four-stream literals block is missing jump table");
    }

    stream_sizes[0] = (size_t)src[0] | ((size_t)src[1] << 8);
    stream_sizes[1] = (size_t)src[2] | ((size_t)src[3] << 8);
    stream_sizes[2] = (size_t)src[4] | ((size_t)src[5] << 8);
    if (stream_sizes[0] + stream_sizes[1] + stream_sizes[2] > src_size - 6u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "four-stream literals jump table overruns payload");
    }
    stream_sizes[3] = src_size - 6u - stream_sizes[0] - stream_sizes[1] - stream_sizes[2];

    quarter = (dst_size + 3u) / 4u;
    regen_sizes[0] = quarter;
    regen_sizes[1] = quarter;
    regen_sizes[2] = quarter;
    if (dst_size < regen_sizes[0] + regen_sizes[1] + regen_sizes[2]) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "four-stream literals regenerated size is invalid");
    }
    regen_sizes[3] = dst_size - regen_sizes[0] - regen_sizes[1] - regen_sizes[2];

    offset = 6u;
    for (i = 0u; i < 4u; ++i) {
        result = compression_zstd_decode_huffman_1stream(src + offset, stream_sizes[i], table,
                                                 dst, regen_sizes[i]);
        if (result.status != COMPRESSION_ZSTD_OK) {
            if (i == 0u) {
                return compression_zstd_make_result(result.status, result.backend,
                                            "four-stream literals stream 1 failed");
            }
            if (i == 1u) {
                return compression_zstd_make_result(result.status, result.backend,
                                            "four-stream literals stream 2 failed");
            }
            if (i == 2u) {
                return compression_zstd_make_result(result.status, result.backend,
                                            "four-stream literals stream 3 failed");
            }
            return compression_zstd_make_result(result.status, result.backend,
                                        "four-stream literals stream 4 failed");
        }
        offset += stream_sizes[i];
        dst += regen_sizes[i];
    }

    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static void compression_zstd_release_literals(CompressionZstdLiterals *literals) {
    if (literals->owned_ptr != NULL) {
        compression_zstd_free(literals->owned_ptr, literals->size == 0u ? 1u : literals->size);
        literals->owned_ptr = NULL;
    }
}

static CompressionZstdResult compression_zstd_decode_literals_section(CompressionZstdFrameState *state,
                                                      const unsigned char *src, size_t src_size,
                                                      CompressionZstdLiterals *literals,
                                                      size_t *consumed_out) {
    unsigned char header;
    unsigned block_type;
    unsigned size_format;
    size_t regenerated_size;
    size_t header_size;
    CompressionZstdCompressedLiteralsHeader compressed_header;
    CompressionZstdHuffmanTreeDescription tree_description;
    CompressionZstdResult result;

    if (src_size == 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "missing literals section header");
    }

    literals->owned_ptr = NULL;

    header = src[0];
    block_type = (unsigned)(header & 0x3u);
    size_format = (unsigned)((header >> 2) & 0x3u);

    if (block_type == 0u || block_type == 1u) {
        if (size_format == 0u || size_format == 2u) {
            header_size = 1u;
            regenerated_size = (size_t)(header >> 3);
        } else if (size_format == 1u) {
            if (src_size < 2u) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "truncated literals header");
            }
            header_size = 2u;
            regenerated_size = (size_t)((header >> 4) | ((uint32_t)src[1] << 4));
        } else {
            if (src_size < 3u) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "truncated literals header");
            }
            header_size = 3u;
            regenerated_size = (size_t)((header >> 4)
                                        | ((uint32_t)src[1] << 4)
                                        | ((uint32_t)src[2] << 12));
        }

        if (block_type == 0u) {
            if (header_size + regenerated_size > src_size) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "truncated raw literals block");
            }
            literals->ptr = src + header_size;
            literals->size = regenerated_size;
            literals->rle_byte = 0u;
            literals->is_rle = 0;
            literals->owned_ptr = NULL;
            *consumed_out = header_size + regenerated_size;
            return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
        }

        if (header_size + 1u > src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated RLE literals block");
        }
        literals->ptr = NULL;
        literals->size = regenerated_size;
        literals->rle_byte = src[header_size];
        literals->is_rle = 1;
        literals->owned_ptr = NULL;
        *consumed_out = header_size + 1u;
        return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
    }

    result = compression_zstd_parse_compressed_literals_header(src, src_size, block_type, size_format,
                                                       &compressed_header);
    if (result.status != COMPRESSION_ZSTD_OK) {
        return result;
    }
    if (compressed_header.header_size + compressed_header.compressed_size > src_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "truncated compressed literals payload");
    }
    literals->ptr = NULL;
    literals->size = compressed_header.regenerated_size;
    literals->rle_byte = 0u;
    literals->is_rle = 0;
    literals->owned_ptr = NULL;
    *consumed_out = compressed_header.header_size + compressed_header.compressed_size;

    if (block_type == 2u || block_type == 3u) {
        CompressionZstdHuffmanDecodeTable huffman_table;
        const unsigned char *tree_src;
        const unsigned char *stream_src;
        size_t stream_size;

        tree_src = src + compressed_header.header_size;
        if (block_type == 2u) {
            result = compression_zstd_parse_huffman_tree_description(tree_src,
                                                             compressed_header.compressed_size,
                                                             &tree_description);
            if (result.status != COMPRESSION_ZSTD_OK) {
                return result;
            }
            if (tree_description.description_size == compressed_header.compressed_size) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "compressed literals block is missing Huffman stream data");
            }
            result = compression_zstd_build_huffman_decode_table(&tree_description, &huffman_table);
            if (result.status != COMPRESSION_ZSTD_OK) {
                return result;
            }
            state->huf_table = huffman_table;
            state->have_huf_table = 1;
            stream_src = tree_src + tree_description.description_size;
            stream_size = compressed_header.compressed_size - tree_description.description_size;
        } else {
            if (!state->have_huf_table) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "treeless literals block has no previous Huffman table");
            }
            huffman_table = state->huf_table;
            stream_src = tree_src;
            stream_size = compressed_header.compressed_size;
        }

        literals->owned_ptr = (unsigned char *)compression_zstd_alloc(literals->size);
        if (literals->owned_ptr == NULL) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_NOMEM, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "failed to allocate literals scratch buffer");
        }

        if (compressed_header.stream_count == 1u) {
            result = compression_zstd_decode_huffman_1stream(stream_src, stream_size,
                                                     &huffman_table,
                                                     literals->owned_ptr, literals->size);
        } else {
            result = compression_zstd_decode_huffman_4stream(stream_src, stream_size,
                                                     &huffman_table,
                                                     literals->owned_ptr, literals->size);
        }
        if (result.status != COMPRESSION_ZSTD_OK) {
            compression_zstd_release_literals(literals);
            return result;
        }

        literals->ptr = literals->owned_ptr;
        return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
    }

    return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                "invalid literals block type");
}

static CompressionZstdResult compression_zstd_literals_copy(const CompressionZstdLiterals *literals, size_t offset,
                                            unsigned char *dst, size_t len) {
    if (offset + len > literals->size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "sequence consumes more literals than available");
    }
    if (literals->is_rle) {
        compression_zstd_fill(dst, literals->rle_byte, len);
        return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
    }
    compression_zstd_copy(dst, literals->ptr + offset, len);
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static void compression_zstd_update_repeat_offsets(CompressionZstdFrameState *state,
                                           uint32_t offset_value,
                                           size_t lit_length,
                                           uint32_t offset) {
    uint32_t rep1;
    uint32_t rep2;
    uint32_t rep3;

    rep1 = state->rep_offsets[0];
    rep2 = state->rep_offsets[1];
    rep3 = state->rep_offsets[2];

    if (offset_value > 3u || (offset_value == 3u && lit_length == 0u)) {
        state->rep_offsets[2] = rep2;
        state->rep_offsets[1] = rep1;
        state->rep_offsets[0] = offset;
        return;
    }

    if (lit_length == 0u) {
        if (offset_value == 1u) {
            state->rep_offsets[0] = rep2;
            state->rep_offsets[1] = rep1;
        } else if (offset_value == 2u) {
            state->rep_offsets[0] = rep3;
            state->rep_offsets[1] = rep1;
            state->rep_offsets[2] = rep2;
        }
        return;
    }

    if (offset_value == 2u) {
        state->rep_offsets[0] = rep2;
        state->rep_offsets[1] = rep1;
    } else if (offset_value == 3u) {
        state->rep_offsets[0] = rep3;
        state->rep_offsets[1] = rep1;
        state->rep_offsets[2] = rep2;
    }
}

static CompressionZstdResult compression_zstd_execute_sequence(CompressionZstdFrameState *state,
                                               const CompressionZstdLiterals *literals,
                                               size_t *literal_pos_io,
                                               size_t lit_length,
                                               uint32_t offset_value,
                                               size_t match_length) {
    size_t literal_pos;
    uint32_t offset;
    size_t i;
    CompressionZstdResult result;

    literal_pos = *literal_pos_io;

    if (literal_pos + lit_length > literals->size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "literal length exceeds literals buffer");
    }
    if (state->dst_pos + lit_length > state->dst_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "sequence literals exceed output buffer");
    }

    result = compression_zstd_literals_copy(literals, literal_pos, state->dst + state->dst_pos, lit_length);
    if (result.status != COMPRESSION_ZSTD_OK) {
        return result;
    }
    state->dst_pos += lit_length;
    literal_pos += lit_length;
    *literal_pos_io = literal_pos;

    if (offset_value > 3u) {
        offset = offset_value - 3u;
    } else if (lit_length == 0u) {
        if (offset_value == 1u) {
            offset = state->rep_offsets[1];
        } else if (offset_value == 2u) {
            offset = state->rep_offsets[2];
        } else {
            offset = state->rep_offsets[0] - 1u;
        }
    } else {
        offset = state->rep_offsets[offset_value - 1u];
    }

    if (offset == 0u || (size_t)offset > state->dst_pos) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid match offset");
    }
    if (state->dst_pos + match_length > state->dst_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "match exceeds output buffer");
    }

    if (match_length <= (size_t)offset) {
        compression_zstd_copy(state->dst + state->dst_pos,
                      state->dst + state->dst_pos - (size_t)offset,
                      match_length);
    } else {
        for (i = 0u; i < match_length; ++i) {
            state->dst[state->dst_pos + i] = state->dst[state->dst_pos - (size_t)offset + i];
        }
    }
    state->dst_pos += match_length;
    compression_zstd_update_repeat_offsets(state, offset_value, lit_length, offset);

    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_finish_literals(CompressionZstdFrameState *state,
                                              const CompressionZstdLiterals *literals,
                                              size_t literal_pos) {
    CompressionZstdResult result;

    if (literal_pos < literals->size) {
        size_t trailing = literals->size - literal_pos;

        if (state->dst_pos + trailing > state->dst_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "trailing literals exceed output buffer");
        }
        result = compression_zstd_literals_copy(literals, literal_pos, state->dst + state->dst_pos, trailing);
        if (result.status != COMPRESSION_ZSTD_OK) {
            return result;
        }
        state->dst_pos += trailing;
    }

    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_decode_compressed_block(CompressionZstdFrameState *state,
                                                      const unsigned char *src, size_t src_size) {
    CompressionZstdLiterals literals;
    CompressionZstdResult result;
    CompressionZstdResult final_result;
    size_t literals_consumed;
    const unsigned char *seq_src;
    size_t seq_size;
    size_t num_sequences;
    size_t seq_header_size;

    result = compression_zstd_decode_literals_section(state, src, src_size, &literals, &literals_consumed);
    if (result.status != COMPRESSION_ZSTD_OK) {
        return result;
    }

    final_result = compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);

    seq_src = src + literals_consumed;
    seq_size = src_size - literals_consumed;
    if (seq_size == 0u) {
        final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "missing sequences section");
        goto cleanup;
    }

    if (seq_src[0] == 0u) {
        num_sequences = 0u;
        seq_header_size = 1u;
    } else if (seq_src[0] < 128u) {
        num_sequences = seq_src[0];
        seq_header_size = 1u;
    } else if (seq_src[0] < 255u) {
        if (seq_size < 2u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated sequences header");
        }
        num_sequences = ((size_t)(seq_src[0] - 128u) << 8) + (size_t)seq_src[1];
        seq_header_size = 2u;
    } else {
        if (seq_size < 3u) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated sequences header");
        }
        num_sequences = (size_t)seq_src[1] + ((size_t)seq_src[2] << 8) + 0x7F00u;
        seq_header_size = 3u;
    }

    if (num_sequences == 0u) {
        if (seq_size != 1u) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_UNSUPPORTED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "zero-sequence block has unsupported trailing content");
            goto cleanup;
        }
        if (state->dst_pos + literals.size > state->dst_size) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "literals exceed output buffer");
            goto cleanup;
        }
        result = compression_zstd_literals_copy(&literals, 0u, state->dst + state->dst_pos, literals.size);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }
        state->dst_pos += literals.size;
        goto cleanup;
    }

    if (num_sequences == 1u) {
        num_sequences = 1u;
    }

    {
        CompressionZstdRevBitReader reader;
        unsigned char modes;
        unsigned ll_mode;
        unsigned of_mode;
        unsigned ml_mode;
        size_t seq_tables_capacity;
        CompressionZstdFseDecodeTable ll_table;
        CompressionZstdFseDecodeTable of_table;
        CompressionZstdFseDecodeTable ml_table;
        const unsigned char *ll_symbols;
        const unsigned char *ll_bits_table;
        const uint16_t *ll_base_table;
        const unsigned char *of_symbols;
        const unsigned char *of_bits_table;
        const uint16_t *of_base_table;
        const unsigned char *ml_symbols;
        const unsigned char *ml_bits_table;
        const uint16_t *ml_base_table;
        uint32_t ll_state;
        uint32_t of_state;
        uint32_t ml_state;
        size_t seq_tables_size = 0u;
        size_t literal_pos = 0u;
        size_t seq_index;

        if (seq_size < seq_header_size + 1u) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "truncated sequence modes byte");
            goto cleanup;
        }
        modes = seq_src[seq_header_size];
        if ((modes & 0x3u) != 0u) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "reserved sequence mode bits are set");
            goto cleanup;
        }

        ll_mode = (unsigned)((modes >> 6) & 0x3u);
        of_mode = (unsigned)((modes >> 4) & 0x3u);
        ml_mode = (unsigned)((modes >> 2) & 0x3u);
    seq_tables_capacity = seq_size - seq_header_size - 1u;

        result = compression_zstd_decode_sequence_table(seq_src + seq_header_size + 1u,
                        seq_tables_capacity,
                                                ll_mode, 9u, 35u,
                                                &state->ll_table, state->have_seq_tables,
                                                compression_zstd_init_predefined_ll_table,
                                                &ll_table, &seq_tables_size);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }
        if (seq_tables_size > seq_tables_capacity) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "sequence tables overrun section");
            goto cleanup;
        }
        result = compression_zstd_decode_sequence_table(seq_src + seq_header_size + 1u + seq_tables_size,
                                                seq_tables_capacity - seq_tables_size,
                                                of_mode, 8u, 31u,
                                                &state->of_table, state->have_seq_tables,
                                                compression_zstd_init_predefined_of_table,
                                                &of_table, &literals_consumed);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }
        seq_tables_size += literals_consumed;
        if (seq_tables_size > seq_tables_capacity) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "sequence tables overrun section");
            goto cleanup;
        }

        result = compression_zstd_decode_sequence_table(seq_src + seq_header_size + 1u + seq_tables_size,
                                                seq_tables_capacity - seq_tables_size,
                                                ml_mode, 9u, 52u,
                                                &state->ml_table, state->have_seq_tables,
                                                compression_zstd_init_predefined_ml_table,
                                                &ml_table, &literals_consumed);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }
        seq_tables_size += literals_consumed;
        if (seq_tables_size > seq_tables_capacity) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "sequence tables overrun section");
            goto cleanup;
        }

        result = compression_zstd_revbit_init(&reader, seq_src + seq_header_size + 1u + seq_tables_size,
                                      seq_tables_capacity - seq_tables_size);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }

        result = compression_zstd_fse_init_state(&reader, &ll_table, &ll_state);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }
        result = compression_zstd_fse_init_state(&reader, &of_table, &of_state);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }
        result = compression_zstd_fse_init_state(&reader, &ml_table, &ml_state);
        if (result.status != COMPRESSION_ZSTD_OK) {
            final_result = result;
            goto cleanup;
        }

        ll_symbols = ll_table.symbols;
        ll_bits_table = ll_table.bits;
        ll_base_table = ll_table.base;
        of_symbols = of_table.symbols;
        of_bits_table = of_table.bits;
        of_base_table = of_table.base;
        ml_symbols = ml_table.symbols;
        ml_bits_table = ml_table.bits;
        ml_base_table = ml_table.base;

        for (seq_index = 0u; seq_index < num_sequences; ++seq_index) {
            uint32_t offset_extra;
            unsigned ll_code;
            unsigned of_code;
            unsigned ml_code;
            size_t lit_length_base;
            size_t match_length_base;
            unsigned lit_length_bits;
            unsigned match_length_bits;
            uint32_t lit_length_extra;
            uint32_t match_length_extra;

            if (ll_state >= ll_table.table_size
                || of_state >= of_table.table_size
                || ml_state >= ml_table.table_size) {
                final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                    "FSE state is out of range");
                goto cleanup;
            }
            ll_code = ll_symbols[ll_state];
            of_code = of_symbols[of_state];
            ml_code = ml_symbols[ml_state];
            if (ll_code > 35u || of_code > 31u || ml_code > 52u) {
                final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                    "sequence code exceeds decoder limits");
                goto cleanup;
            }

            lit_length_base = (size_t)compression_zstd_ll_code_base[ll_code];
            lit_length_bits = compression_zstd_ll_code_bits[ll_code];
            match_length_base = (size_t)compression_zstd_ml_code_base[ml_code];
            match_length_bits = compression_zstd_ml_code_bits[ml_code];

            result = compression_zstd_revbit_read_bits(&reader, of_code, &offset_extra);
            if (result.status != COMPRESSION_ZSTD_OK) {
                final_result = result;
                goto cleanup;
            }
            result = compression_zstd_revbit_read_bits(&reader, match_length_bits, &match_length_extra);
            if (result.status != COMPRESSION_ZSTD_OK) {
                final_result = result;
                goto cleanup;
            }
            result = compression_zstd_revbit_read_bits(&reader, lit_length_bits, &lit_length_extra);
            if (result.status != COMPRESSION_ZSTD_OK) {
                final_result = result;
                goto cleanup;
            }

            result = compression_zstd_execute_sequence(state, &literals, &literal_pos,
                                               lit_length_base + (size_t)lit_length_extra,
                                               ((uint32_t)1u << of_code) + offset_extra,
                                               match_length_base + (size_t)match_length_extra);
            if (result.status != COMPRESSION_ZSTD_OK) {
                final_result = result;
                goto cleanup;
            }

            if (seq_index + 1u < num_sequences) {
                uint32_t extra;

                extra = 0u;
                if (ll_bits_table[ll_state] != 0u) {
                    result = compression_zstd_revbit_read_bits(&reader, ll_bits_table[ll_state], &extra);
                    if (result.status != COMPRESSION_ZSTD_OK) {
                        final_result = result;
                        goto cleanup;
                    }
                }
                ll_state = (uint32_t)ll_base_table[ll_state] + extra;

                extra = 0u;
                if (ml_bits_table[ml_state] != 0u) {
                    result = compression_zstd_revbit_read_bits(&reader, ml_bits_table[ml_state], &extra);
                    if (result.status != COMPRESSION_ZSTD_OK) {
                        final_result = result;
                        goto cleanup;
                    }
                }
                ml_state = (uint32_t)ml_base_table[ml_state] + extra;

                extra = 0u;
                if (of_bits_table[of_state] != 0u) {
                    result = compression_zstd_revbit_read_bits(&reader, of_bits_table[of_state], &extra);
                    if (result.status != COMPRESSION_ZSTD_OK) {
                        final_result = result;
                        goto cleanup;
                    }
                }
                of_state = (uint32_t)of_base_table[of_state] + extra;
            }
        }

        if (!compression_zstd_revbit_consumed(&reader)) {
            final_result = compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                                "sequence bitstream still has unread bits");
            goto cleanup;
        }
        state->ll_table = ll_table;
        state->of_table = of_table;
        state->ml_table = ml_table;
        state->have_seq_tables = 1;
        final_result = compression_zstd_finish_literals(state, &literals, literal_pos);
    }

cleanup:
    compression_zstd_release_literals(&literals);
    return final_result;
}

static CompressionZstdResult compression_zstd_parse_frame_header(CompressionZstdFrameState *state, size_t *frame_content_size_out) {
    uint32_t magic;
    unsigned char descriptor;
    unsigned fcs_flag;
    unsigned single_segment;
    unsigned dict_id_flag;
    unsigned reserved_bit;
    size_t frame_content_size = 0u;

    if (state->src_size < 5u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "truncated frame header");
    }

    magic = compression_zstd_read_le32(state->src);
    if (magic == ZSTD_MAGIC_NUMBER) {
        state->src_pos = 4u;
    } else if ((magic & ZSTD_SKIPPABLE_MAGIC_MASK) == ZSTD_SKIPPABLE_MAGIC_BASE) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_UNSUPPORTED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "skippable frames are not implemented yet");
    } else {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "invalid zstd frame magic");
    }

    descriptor = state->src[state->src_pos++];
    fcs_flag = (unsigned)(descriptor >> 6);
    single_segment = (unsigned)((descriptor >> 5) & 1u);
    reserved_bit = (unsigned)((descriptor >> 3) & 1u);
    state->has_checksum = (int)((descriptor >> 2) & 1u);
    dict_id_flag = (unsigned)(descriptor & 0x3u);

    if (reserved_bit != 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "reserved frame header bit is set");
    }
    if (dict_id_flag != 0u) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_UNSUPPORTED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "dictionary frames are not implemented yet");
    }

    if (!single_segment) {
        if (state->src_pos >= state->src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated window descriptor");
        }
        state->src_pos += 1u;
    }

    if (fcs_flag == 0u) {
        if (single_segment) {
            if (state->src_pos >= state->src_size) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "truncated frame content size");
            }
            frame_content_size = (size_t)state->src[state->src_pos++];
        }
    } else if (fcs_flag == 1u) {
        if (state->src_pos + 2u > state->src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated frame content size");
        }
        frame_content_size = (size_t)((uint32_t)state->src[state->src_pos]
                            | ((uint32_t)state->src[state->src_pos + 1u] << 8));
        frame_content_size += 256u;
        state->src_pos += 2u;
    } else if (fcs_flag == 2u) {
        if (state->src_pos + 4u > state->src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated frame content size");
        }
        frame_content_size = (size_t)compression_zstd_read_le32(state->src + state->src_pos);
        state->src_pos += 4u;
    } else {
        uint64_t frame_content_size64;

        if (state->src_pos + 8u > state->src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated frame content size");
        }
        frame_content_size64 = compression_zstd_read_le64(state->src + state->src_pos);
        if (frame_content_size64 > (uint64_t)COMPRESSION_ZSTD_SIZE_MAX) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_UNSUPPORTED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "frame content size is too large");
        }
        frame_content_size = (size_t)frame_content_size64;
        state->src_pos += 8u;
    }

    *frame_content_size_out = frame_content_size;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_decode_raw_block(CompressionZstdFrameState *state, size_t block_size) {
    if (state->src_pos + block_size > state->src_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "truncated raw block");
    }
    if (state->dst_pos + block_size > state->dst_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "raw block exceeds output buffer");
    }

    compression_zstd_copy(state->dst + state->dst_pos, state->src + state->src_pos, block_size);
    state->src_pos += block_size;
    state->dst_pos += block_size;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_decode_rle_block(CompressionZstdFrameState *state, size_t block_size) {
    unsigned char value;

    if (state->src_pos >= state->src_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "truncated RLE block");
    }
    if (state->dst_pos + block_size > state->dst_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "RLE block exceeds output buffer");
    }

    value = state->src[state->src_pos++];
    compression_zstd_fill(state->dst + state->dst_pos, value, block_size);
    state->dst_pos += block_size;
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

static CompressionZstdResult compression_zstd_try_custom(void *dst, size_t dst_size,
                                         const void *src, size_t src_size,
                                         size_t *written_out) {
    CompressionZstdFrameState state;
    CompressionZstdResult result;
    size_t frame_content_size = 0u;
    int last_block = 0;

    compression_zstd_init_state(&state, src, src_size, dst, dst_size);

    result = compression_zstd_parse_frame_header(&state, &frame_content_size);
    if (result.status != COMPRESSION_ZSTD_OK) {
        return result;
    }

    while (!last_block) {
        uint32_t block_header;
        unsigned block_type;
        size_t block_size;

        if (state.src_pos + 3u > state.src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated block header");
        }

        block_header = compression_zstd_read_le24(state.src + state.src_pos);
        state.src_pos += 3u;

        last_block = (int)(block_header & 1u);
        block_type = (unsigned)((block_header >> 1) & 0x3u);
        block_size = (size_t)(block_header >> 3);

        if (block_type == 0u) {
            result = compression_zstd_decode_raw_block(&state, block_size);
        } else if (block_type == 1u) {
            result = compression_zstd_decode_rle_block(&state, block_size);
        } else if (block_type == 2u) {
            if (state.src_pos + block_size > state.src_size) {
                return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                            "truncated compressed block");
            }
            result = compression_zstd_decode_compressed_block(&state, state.src + state.src_pos, block_size);
            state.src_pos += block_size;
        } else {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "reserved block type");
        }

        if (result.status != COMPRESSION_ZSTD_OK) {
            return result;
        }
    }

    if (state.has_checksum) {
        if (state.src_pos + 4u > state.src_size) {
            return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_TRUNCATED, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                        "truncated content checksum");
        }
        state.src_pos += 4u;
    }

    if (state.src_pos != state.src_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "unexpected trailing bytes after frame");
    }
    if (frame_content_size != 0u && state.dst_pos != frame_content_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "decoded size does not match frame content size");
    }
    if (state.dst_pos != dst_size) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "decoded size does not match caller output size");
    }

    if (written_out != NULL) {
        *written_out = state.dst_pos;
    }
    return compression_zstd_make_result(COMPRESSION_ZSTD_OK, COMPRESSION_ZSTD_BACKEND_CUSTOM, NULL);
}

CompressionZstdResult compression_zstd_decompress_frame(void *dst, size_t dst_size,
                                        const void *src, size_t src_size,
                                        size_t *written_out) {
    CompressionZstdResult result;

    result = compression_zstd_try_custom(dst, dst_size, src, src_size, written_out);
    if (result.status == COMPRESSION_ZSTD_OK) {
        return result;
    }
    return result;
}

CompressionZstdResult compression_zstd_frame_content_size(const void *src, size_t src_size,
                                         size_t *frame_content_size_out) {
    CompressionZstdFrameState state;

    if (frame_content_size_out == NULL) {
        return compression_zstd_make_result(COMPRESSION_ZSTD_ERR_CORRUPT, COMPRESSION_ZSTD_BACKEND_CUSTOM,
                                    "frame content size output pointer is null");
    }

    compression_zstd_init_state(&state, src, src_size, NULL, 0u);
    return compression_zstd_parse_frame_header(&state, frame_content_size_out);
}
