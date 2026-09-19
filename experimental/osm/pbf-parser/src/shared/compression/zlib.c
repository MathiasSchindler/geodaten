#include "compression/zlib.h"

#include "runtime.h"

#define ZLIB_MAX_BITS 15U
#define ZLIB_MAX_TABLE_SIZE (1U << ZLIB_MAX_BITS)
#define ZLIB_MAX_CODE_TABLE_SIZE (1U << 7U)
#define ZLIB_MAX_LITERAL_SYMBOLS 288U
#define ZLIB_MAX_DISTANCE_SYMBOLS 32U
#define ZLIB_MAX_CODE_LENGTH_SYMBOLS 19U
#define ZLIB_LZ77_HASH_SLOTS 4U

static unsigned int compression_adler32(const unsigned char *data, size_t length) {
    unsigned int s1 = 1U;
    unsigned int s2 = 0U;

    while (length != 0U) {
        size_t chunk = length > 5552U ? 5552U : length;
        size_t index;

        for (index = 0U; index < chunk; ++index) {
            s1 += (unsigned int)data[index];
            s2 += s1;
        }
        s1 %= 65521U;
        s2 %= 65521U;
        data += chunk;
        length -= chunk;
    }
    return (s2 << 16U) | s1;
}

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t byte_offset;
    unsigned int bit_buffer;
    unsigned int bit_count;
} ZlibBitReader;

typedef struct {
    unsigned int table_bits;
    unsigned int table_size;
    unsigned short *symbols;
    unsigned char *lengths;
} ZlibHuffman;

typedef struct {
    unsigned char *data;
    size_t capacity;
    size_t byte_offset;
    unsigned int bit_buffer;
    unsigned int bit_count;
} ZlibBitWriter;

static void zlib_bit_reader_init(ZlibBitReader *reader, const unsigned char *data, size_t size) {
    reader->data = data;
    reader->size = size;
    reader->byte_offset = 0U;
    reader->bit_buffer = 0U;
    reader->bit_count = 0U;
}

static int zlib_ensure_bits(ZlibBitReader *reader, unsigned int count) {
    while (reader->bit_count < count) {
        if (reader->byte_offset >= reader->size) {
            return -1;
        }
        reader->bit_buffer |= ((unsigned int)reader->data[reader->byte_offset++]) << reader->bit_count;
        reader->bit_count += 8U;
    }
    return 0;
}

static int zlib_read_bits(ZlibBitReader *reader, unsigned int count, unsigned int *value_out) {
    unsigned int mask;

    if (count == 0U) {
        *value_out = 0U;
        return 0;
    }
    if (zlib_ensure_bits(reader, count) != 0) {
        return -1;
    }
    mask = (1U << count) - 1U;
    *value_out = reader->bit_buffer & mask;
    reader->bit_buffer >>= count;
    reader->bit_count -= count;
    return 0;
}

static void zlib_align_byte(ZlibBitReader *reader) {
    unsigned int drop = reader->bit_count & 7U;

    reader->bit_buffer >>= drop;
    reader->bit_count -= drop;
}

static unsigned int zlib_reverse_bits(unsigned int value, unsigned int count) {
    unsigned int reversed = 0U;
    unsigned int i;

    for (i = 0U; i < count; ++i) {
        reversed = (reversed << 1U) | (value & 1U);
        value >>= 1U;
    }
    return reversed;
}

static void zlib_bit_writer_init(ZlibBitWriter *writer, unsigned char *data, size_t capacity) {
    writer->data = data;
    writer->capacity = capacity;
    writer->byte_offset = 0U;
    writer->bit_buffer = 0U;
    writer->bit_count = 0U;
}

static int zlib_write_bits(ZlibBitWriter *writer, unsigned int value, unsigned int count) {
    if (count > 16U) return -1;
    writer->bit_buffer |= value << writer->bit_count;
    writer->bit_count += count;
    while (writer->bit_count >= 8U) {
        if (writer->byte_offset >= writer->capacity) return -1;
        writer->data[writer->byte_offset++] = (unsigned char)(writer->bit_buffer & 0xffU);
        writer->bit_buffer >>= 8U;
        writer->bit_count -= 8U;
    }
    return 0;
}

static int zlib_flush_bits(ZlibBitWriter *writer) {
    if (writer->bit_count != 0U) {
        if (writer->byte_offset >= writer->capacity) return -1;
        writer->data[writer->byte_offset++] = (unsigned char)(writer->bit_buffer & 0xffU);
        writer->bit_buffer = 0U;
        writer->bit_count = 0U;
    }
    return 0;
}

static int zlib_write_fixed_symbol(ZlibBitWriter *writer, unsigned int symbol) {
    unsigned int code;
    unsigned int length;

    if (symbol <= 143U) {
        code = 0x30U + symbol;
        length = 8U;
    } else if (symbol <= 255U) {
        code = 0x190U + (symbol - 144U);
        length = 9U;
    } else if (symbol <= 279U) {
        code = symbol - 256U;
        length = 7U;
    } else if (symbol <= 287U) {
        code = 0xc0U + (symbol - 280U);
        length = 8U;
    } else {
        return -1;
    }
    return zlib_write_bits(writer, zlib_reverse_bits(code, length), length);
}

static int zlib_write_fixed_match(ZlibBitWriter *writer, unsigned int length, unsigned int distance) {
    static const unsigned short length_base[29] = {
        3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 13U, 15U, 17U, 19U, 23U, 27U, 31U,
        35U, 43U, 51U, 59U, 67U, 83U, 99U, 115U, 131U, 163U, 195U, 227U, 258U
    };
    static const unsigned char length_extra[29] = {
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 2U, 2U, 2U, 2U,
        3U, 3U, 3U, 3U, 4U, 4U, 4U, 4U, 5U, 5U, 5U, 5U, 0U
    };
    static const unsigned short dist_base[30] = {
        1U, 2U, 3U, 4U, 5U, 7U, 9U, 13U, 17U, 25U, 33U, 49U, 65U, 97U, 129U, 193U,
        257U, 385U, 513U, 769U, 1025U, 1537U, 2049U, 3073U, 4097U, 6145U, 8193U, 12289U, 16385U, 24577U
    };
    static const unsigned char dist_extra[30] = {
        0U, 0U, 0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U, 4U, 4U, 5U, 5U, 6U, 6U,
        7U, 7U, 8U, 8U, 9U, 9U, 10U, 10U, 11U, 11U, 12U, 12U, 13U, 13U
    };
    unsigned int length_index;
    unsigned int dist_index;

    if (length < 3U || length > 258U || distance == 0U || distance > 32768U) return -1;
    for (length_index = 0U; length_index < 29U; ++length_index) {
        unsigned int max_length = length_base[length_index] + ((1U << length_extra[length_index]) - 1U);
        if (length <= max_length) break;
    }
    if (length_index >= 29U) return -1;
    for (dist_index = 0U; dist_index < 30U; ++dist_index) {
        unsigned int max_distance = dist_base[dist_index] + ((1U << dist_extra[dist_index]) - 1U);
        if (distance <= max_distance) break;
    }
    if (dist_index >= 30U) return -1;
    if (zlib_write_fixed_symbol(writer, 257U + length_index) != 0) return -1;
    if (length_extra[length_index] != 0U && zlib_write_bits(writer, length - length_base[length_index], length_extra[length_index]) != 0) return -1;
    if (zlib_write_bits(writer, zlib_reverse_bits(dist_index, 5U), 5U) != 0) return -1;
    if (dist_extra[dist_index] != 0U && zlib_write_bits(writer, distance - dist_base[dist_index], dist_extra[dist_index]) != 0) return -1;
    return 0;
}

static void zlib_huffman_free(ZlibHuffman *huffman) {
    huffman->symbols = 0;
    huffman->lengths = 0;
    huffman->table_bits = 0U;
    huffman->table_size = 0U;
}

static int zlib_huffman_build(
    ZlibHuffman *huffman,
    const unsigned char *lengths,
    unsigned int count,
    unsigned short *symbol_table,
    unsigned char *length_table,
    unsigned int table_capacity
) {
    unsigned int length_counts[ZLIB_MAX_BITS + 1U];
    unsigned int next_code[ZLIB_MAX_BITS + 1U];
    unsigned int code = 0U;
    unsigned int max_bits = 0U;
    unsigned int symbol;
    unsigned int bits;

    huffman->symbols = 0;
    huffman->lengths = 0;
    huffman->table_bits = 0U;
    huffman->table_size = 0U;
    rt_memset(length_counts, 0, sizeof(length_counts));
    rt_memset(next_code, 0, sizeof(next_code));

    for (symbol = 0U; symbol < count; ++symbol) {
        unsigned int length = lengths[symbol];
        if (length > ZLIB_MAX_BITS) return -1;
        if (length != 0U) {
            length_counts[length] += 1U;
            if (length > max_bits) max_bits = length;
        }
    }
    if (max_bits == 0U) return -1;
    for (bits = 1U; bits <= ZLIB_MAX_BITS; ++bits) {
        code = (code + length_counts[bits - 1U]) << 1U;
        next_code[bits] = code;
    }

    huffman->table_bits = max_bits;
    huffman->table_size = 1U << max_bits;
    if (huffman->table_size > table_capacity || symbol_table == 0 || length_table == 0) {
        return -1;
    }
    huffman->symbols = symbol_table;
    huffman->lengths = length_table;
    rt_memset(huffman->symbols, 0, sizeof(unsigned short) * huffman->table_size);
    rt_memset(huffman->lengths, 0, sizeof(unsigned char) * huffman->table_size);

    for (symbol = 0U; symbol < count; ++symbol) {
        unsigned int length = lengths[symbol];
        unsigned int reversed;
        unsigned int fill;
        unsigned int step;

        if (length == 0U) continue;
        reversed = zlib_reverse_bits(next_code[length], length);
        next_code[length] += 1U;
        step = 1U << length;
        for (fill = reversed; fill < huffman->table_size; fill += step) {
            huffman->symbols[fill] = (unsigned short)symbol;
            huffman->lengths[fill] = (unsigned char)length;
        }
    }
    return 0;
}

static int zlib_huffman_decode(ZlibBitReader *reader, const ZlibHuffman *huffman, unsigned int *symbol_out) {
    unsigned int key;
    unsigned int length;

    while (reader->bit_count < huffman->table_bits && reader->byte_offset < reader->size) {
        reader->bit_buffer |= ((unsigned int)reader->data[reader->byte_offset++]) << reader->bit_count;
        reader->bit_count += 8U;
    }
    if (reader->bit_count == 0U) {
        return -1;
    }
    key = reader->bit_buffer & (huffman->table_size - 1U);
    length = huffman->lengths[key];
    if (length == 0U || length > reader->bit_count) {
        return -1;
    }
    *symbol_out = (unsigned int)huffman->symbols[key];
    reader->bit_buffer >>= length;
    reader->bit_count -= length;
    return 0;
}

static int zlib_copy_output(unsigned char *output, size_t output_capacity, size_t *output_offset_io, unsigned int distance, unsigned int length) {
    size_t output_offset = *output_offset_io;
    unsigned int i;

    if (distance == 0U || distance > output_offset || output_offset + (size_t)length > output_capacity) {
        return -1;
    }
    for (i = 0U; i < length; ++i) {
        output[output_offset] = output[output_offset - (size_t)distance];
        output_offset += 1U;
    }
    *output_offset_io = output_offset;
    return 0;
}

static int zlib_inflate_codes(ZlibBitReader *reader, const ZlibHuffman *litlen, const ZlibHuffman *dist, unsigned char *output, size_t output_capacity, size_t *output_offset_io) {
    static const unsigned short length_base[29] = {
        3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 13U, 15U, 17U, 19U, 23U, 27U, 31U,
        35U, 43U, 51U, 59U, 67U, 83U, 99U, 115U, 131U, 163U, 195U, 227U, 258U
    };
    static const unsigned char length_extra[29] = {
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 2U, 2U, 2U, 2U,
        3U, 3U, 3U, 3U, 4U, 4U, 4U, 4U, 5U, 5U, 5U, 5U, 0U
    };
    static const unsigned short dist_base[30] = {
        1U, 2U, 3U, 4U, 5U, 7U, 9U, 13U, 17U, 25U, 33U, 49U, 65U, 97U, 129U, 193U,
        257U, 385U, 513U, 769U, 1025U, 1537U, 2049U, 3073U, 4097U, 6145U, 8193U, 12289U, 16385U, 24577U
    };
    static const unsigned char dist_extra[30] = {
        0U, 0U, 0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U, 4U, 4U, 5U, 5U, 6U, 6U,
        7U, 7U, 8U, 8U, 9U, 9U, 10U, 10U, 11U, 11U, 12U, 12U, 13U, 13U
    };

    for (;;) {
        unsigned int symbol;

        if (zlib_huffman_decode(reader, litlen, &symbol) != 0) return -1;
        if (symbol < 256U) {
            if (*output_offset_io >= output_capacity) return -1;
            output[(*output_offset_io)++] = (unsigned char)symbol;
        } else if (symbol == 256U) {
            return 0;
        } else if (symbol <= 285U) {
            unsigned int length_index = symbol - 257U;
            unsigned int length = length_base[length_index];
            unsigned int distance_symbol;
            unsigned int distance;
            unsigned int extra;

            if (length_index >= 29U) return -1;
            if (length_extra[length_index] != 0U) {
                if (zlib_read_bits(reader, length_extra[length_index], &extra) != 0) return -1;
                length += extra;
            }
            if (zlib_huffman_decode(reader, dist, &distance_symbol) != 0 || distance_symbol >= 30U) return -1;
            distance = dist_base[distance_symbol];
            if (dist_extra[distance_symbol] != 0U) {
                if (zlib_read_bits(reader, dist_extra[distance_symbol], &extra) != 0) return -1;
                distance += extra;
            }
            if (zlib_copy_output(output, output_capacity, output_offset_io, distance, length) != 0) return -1;
        } else {
            return -1;
        }
    }
}

static int zlib_inflate_stored(ZlibBitReader *reader, unsigned char *output, size_t output_capacity, size_t *output_offset_io) {
    unsigned int len;
    unsigned int nlen;

    zlib_align_byte(reader);
    if (reader->byte_offset + 4U > reader->size) return -1;
    len = (unsigned int)reader->data[reader->byte_offset] | ((unsigned int)reader->data[reader->byte_offset + 1U] << 8U);
    nlen = (unsigned int)reader->data[reader->byte_offset + 2U] | ((unsigned int)reader->data[reader->byte_offset + 3U] << 8U);
    reader->byte_offset += 4U;
    if (((len ^ 0xffffU) & 0xffffU) != nlen || reader->byte_offset + (size_t)len > reader->size || *output_offset_io + (size_t)len > output_capacity) return -1;
    memcpy(output + *output_offset_io, reader->data + reader->byte_offset, (size_t)len);
    reader->byte_offset += (size_t)len;
    *output_offset_io += (size_t)len;
    return 0;
}

static int zlib_inflate_fixed(ZlibBitReader *reader, unsigned char *output, size_t output_capacity, size_t *output_offset_io) {
    unsigned char lit_lengths[ZLIB_MAX_LITERAL_SYMBOLS];
    unsigned char dist_lengths[ZLIB_MAX_DISTANCE_SYMBOLS];
    unsigned short lit_symbols[512U];
    unsigned char lit_table_lengths[512U];
    unsigned short dist_symbols[32U];
    unsigned char dist_table_lengths[32U];
    ZlibHuffman litlen;
    ZlibHuffman dist;
    unsigned int i;
    int result;

    for (i = 0U; i <= 143U; ++i) lit_lengths[i] = 8U;
    for (; i <= 255U; ++i) lit_lengths[i] = 9U;
    for (; i <= 279U; ++i) lit_lengths[i] = 7U;
    for (; i < ZLIB_MAX_LITERAL_SYMBOLS; ++i) lit_lengths[i] = 8U;
    for (i = 0U; i < ZLIB_MAX_DISTANCE_SYMBOLS; ++i) dist_lengths[i] = 5U;
    if (zlib_huffman_build(&litlen, lit_lengths, ZLIB_MAX_LITERAL_SYMBOLS, lit_symbols, lit_table_lengths, 512U) != 0) return -1;
    if (zlib_huffman_build(&dist, dist_lengths, ZLIB_MAX_DISTANCE_SYMBOLS, dist_symbols, dist_table_lengths, 32U) != 0) {
        zlib_huffman_free(&litlen);
        return -1;
    }
    result = zlib_inflate_codes(reader, &litlen, &dist, output, output_capacity, output_offset_io);
    zlib_huffman_free(&litlen);
    zlib_huffman_free(&dist);
    return result;
}

static int zlib_inflate_dynamic(ZlibBitReader *reader, unsigned char *output, size_t output_capacity, size_t *output_offset_io) {
    static const unsigned char code_order[ZLIB_MAX_CODE_LENGTH_SYMBOLS] = { 16U, 17U, 18U, 0U, 8U, 7U, 9U, 6U, 10U, 5U, 11U, 4U, 12U, 3U, 13U, 2U, 14U, 1U, 15U };
    unsigned char code_lengths[ZLIB_MAX_CODE_LENGTH_SYMBOLS];
    unsigned char lit_lengths[ZLIB_MAX_LITERAL_SYMBOLS];
    unsigned char dist_lengths[ZLIB_MAX_DISTANCE_SYMBOLS];
    unsigned short code_symbols[ZLIB_MAX_CODE_TABLE_SIZE];
    unsigned char code_table_lengths[ZLIB_MAX_CODE_TABLE_SIZE];
    unsigned short lit_symbols[ZLIB_MAX_TABLE_SIZE];
    unsigned char lit_table_lengths[ZLIB_MAX_TABLE_SIZE];
    unsigned short dist_symbols[ZLIB_MAX_TABLE_SIZE];
    unsigned char dist_table_lengths[ZLIB_MAX_TABLE_SIZE];
    unsigned int hlit;
    unsigned int hdist;
    unsigned int hclen;
    unsigned int value;
    unsigned int index = 0U;
    ZlibHuffman code_huff;
    ZlibHuffman litlen;
    ZlibHuffman dist;
    int result;

    rt_memset(code_lengths, 0, sizeof(code_lengths));
    rt_memset(lit_lengths, 0, sizeof(lit_lengths));
    rt_memset(dist_lengths, 0, sizeof(dist_lengths));
    if (zlib_read_bits(reader, 5U, &value) != 0) return -1;
    hlit = value + 257U;
    if (zlib_read_bits(reader, 5U, &value) != 0) return -1;
    hdist = value + 1U;
    if (zlib_read_bits(reader, 4U, &value) != 0) return -1;
    hclen = value + 4U;
    if (hlit > ZLIB_MAX_LITERAL_SYMBOLS || hdist > ZLIB_MAX_DISTANCE_SYMBOLS) return -1;
    for (index = 0U; index < hclen; ++index) {
        if (zlib_read_bits(reader, 3U, &value) != 0) return -1;
        code_lengths[code_order[index]] = (unsigned char)value;
    }
    if (zlib_huffman_build(&code_huff, code_lengths, ZLIB_MAX_CODE_LENGTH_SYMBOLS, code_symbols, code_table_lengths, ZLIB_MAX_CODE_TABLE_SIZE) != 0) return -1;

    index = 0U;
    while (index < hlit + hdist) {
        unsigned int symbol;
        unsigned int repeat;
        unsigned int previous = 0U;
        unsigned char *target;

        if (zlib_huffman_decode(reader, &code_huff, &symbol) != 0) {
            zlib_huffman_free(&code_huff);
            return -1;
        }
        target = index < hlit ? lit_lengths : dist_lengths;
        if (index != 0U) {
            previous = index <= hlit ? lit_lengths[index - 1U] : dist_lengths[index - hlit - 1U];
        }
        if (symbol <= 15U) {
            target[index < hlit ? index : index - hlit] = (unsigned char)symbol;
            index += 1U;
        } else if (symbol == 16U) {
            if (index == 0U || zlib_read_bits(reader, 2U, &value) != 0) {
                zlib_huffman_free(&code_huff);
                return -1;
            }
            repeat = value + 3U;
            while (repeat-- != 0U && index < hlit + hdist) {
                target = index < hlit ? lit_lengths : dist_lengths;
                target[index < hlit ? index : index - hlit] = (unsigned char)previous;
                index += 1U;
            }
        } else if (symbol == 17U || symbol == 18U) {
            unsigned int extra_bits = symbol == 17U ? 3U : 7U;
            unsigned int base = symbol == 17U ? 3U : 11U;
            if (zlib_read_bits(reader, extra_bits, &value) != 0) {
                zlib_huffman_free(&code_huff);
                return -1;
            }
            repeat = value + base;
            while (repeat-- != 0U && index < hlit + hdist) {
                target = index < hlit ? lit_lengths : dist_lengths;
                target[index < hlit ? index : index - hlit] = 0U;
                index += 1U;
            }
        } else {
            zlib_huffman_free(&code_huff);
            return -1;
        }
    }
    zlib_huffman_free(&code_huff);
    if (index != hlit + hdist) return -1;
    if (zlib_huffman_build(&litlen, lit_lengths, hlit, lit_symbols, lit_table_lengths, ZLIB_MAX_TABLE_SIZE) != 0) return -1;
    if (zlib_huffman_build(&dist, dist_lengths, hdist, dist_symbols, dist_table_lengths, ZLIB_MAX_TABLE_SIZE) != 0) {
        zlib_huffman_free(&litlen);
        return -1;
    }
    result = zlib_inflate_codes(reader, &litlen, &dist, output, output_capacity, output_offset_io);
    zlib_huffman_free(&litlen);
    zlib_huffman_free(&dist);
    return result;
}

int compression_zlib_inflate(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    ZlibBitReader reader;
    size_t output_offset = 0U;
    int final_block = 0;
    unsigned int expected_adler;

    if (input == 0 || output == 0 || output_size_out == 0 || input_size < 6U) {
        return -1;
    }
    if ((input[0] & 0x0fU) != 8U || (((unsigned int)input[0] << 8U) + (unsigned int)input[1]) % 31U != 0U || (input[1] & 0x20U) != 0U) {
        return -1;
    }
    zlib_bit_reader_init(&reader, input + 2U, input_size - 6U);
    while (!final_block) {
        unsigned int value;
        unsigned int block_type;

        if (zlib_read_bits(&reader, 1U, &value) != 0) return -1;
        final_block = value != 0U;
        if (zlib_read_bits(&reader, 2U, &block_type) != 0) return -1;
        if (block_type == 0U) {
            if (zlib_inflate_stored(&reader, output, output_capacity, &output_offset) != 0) return -1;
        } else if (block_type == 1U) {
            if (zlib_inflate_fixed(&reader, output, output_capacity, &output_offset) != 0) return -1;
        } else if (block_type == 2U) {
            if (zlib_inflate_dynamic(&reader, output, output_capacity, &output_offset) != 0) return -1;
        } else {
            return -1;
        }
    }
    expected_adler = ((unsigned int)input[input_size - 4U] << 24U) |
                     ((unsigned int)input[input_size - 3U] << 16U) |
                     ((unsigned int)input[input_size - 2U] << 8U) |
                     (unsigned int)input[input_size - 1U];
    if (compression_adler32(output, output_offset) != expected_adler) {
        return -1;
    }
    *output_size_out = output_offset;
    return 0;
}

size_t compression_zlib_store_bound(size_t input_size) {
    size_t block_count = input_size / 65535U + 1U;

    if (input_size > ((size_t)-1) - 6U || block_count > (((size_t)-1) - input_size - 6U) / 5U) {
        return 0U;
    }
    return 2U + input_size + block_count * 5U + 4U;
}

int compression_zlib_store(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    size_t bound = compression_zlib_store_bound(input_size);
    size_t input_offset = 0U;
    size_t output_offset = 0U;
    unsigned int adler;

    if (input == 0 || output == 0 || output_size_out == 0 || bound == 0U || output_capacity < bound) {
        return -1;
    }
    output[output_offset++] = 0x78U;
    output[output_offset++] = 0x01U;
    while (input_offset < input_size || input_size == 0U) {
        size_t remaining = input_size - input_offset;
        unsigned int block_size = remaining > 65535U ? 65535U : (unsigned int)remaining;
        int final_block = input_offset + (size_t)block_size == input_size;
        unsigned int nlen = (~block_size) & 0xffffU;

        output[output_offset++] = final_block ? 0x01U : 0x00U;
        output[output_offset++] = (unsigned char)(block_size & 0xffU);
        output[output_offset++] = (unsigned char)((block_size >> 8U) & 0xffU);
        output[output_offset++] = (unsigned char)(nlen & 0xffU);
        output[output_offset++] = (unsigned char)((nlen >> 8U) & 0xffU);
        if (block_size != 0U) {
            size_t index;

            for (index = 0U; index < (size_t)block_size; ++index) {
                output[output_offset + index] = input[input_offset + index];
            }
            output_offset += (size_t)block_size;
        }
        input_offset += (size_t)block_size;
        if (final_block) {
            break;
        }
    }
    adler = compression_adler32(input, input_size);
    output[output_offset++] = (unsigned char)((adler >> 24U) & 0xffU);
    output[output_offset++] = (unsigned char)((adler >> 16U) & 0xffU);
    output[output_offset++] = (unsigned char)((adler >> 8U) & 0xffU);
    output[output_offset++] = (unsigned char)(adler & 0xffU);
    *output_size_out = output_offset;
    return 0;
}

size_t compression_zlib_fixed_rle_bound(size_t input_size) {
    size_t bit_bytes;

    if (input_size > (((size_t)-1) / 9U) - 32U) return 0U;
    bit_bytes = (input_size * 9U + 7U) / 8U;
    if (bit_bytes > ((size_t)-1) - 16U) return 0U;
    return bit_bytes + 16U;
}

int compression_zlib_fixed_rle(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    ZlibBitWriter writer;
    size_t input_offset = 0U;
    unsigned int adler;

    if (input == 0 || output == 0 || output_size_out == 0 || compression_zlib_fixed_rle_bound(input_size) == 0U || output_capacity < compression_zlib_fixed_rle_bound(input_size)) return -1;
    if (output_capacity < 6U) return -1;
    output[0] = 0x78U;
    output[1] = 0x01U;
    zlib_bit_writer_init(&writer, output + 2U, output_capacity - 6U);
    if (zlib_write_bits(&writer, 1U, 1U) != 0 || zlib_write_bits(&writer, 1U, 2U) != 0) return -1;
    while (input_offset < input_size) {
        size_t run = 1U;

        while (input_offset + run < input_size && run < 259U && input[input_offset + run] == input[input_offset]) run += 1U;
        if (run >= 4U) {
            size_t remaining;

            if (zlib_write_fixed_symbol(&writer, input[input_offset]) != 0) return -1;
            input_offset += 1U;
            remaining = run - 1U;
            while (remaining != 0U) {
                unsigned int length = remaining > 258U ? 258U : (unsigned int)remaining;
                if (length < 3U) {
                    unsigned int index;
                    for (index = 0U; index < length; ++index) {
                        if (zlib_write_fixed_symbol(&writer, input[input_offset + index]) != 0) return -1;
                    }
                    input_offset += (size_t)length;
                    remaining = 0U;
                } else {
                    if (zlib_write_fixed_match(&writer, length, 1U) != 0) return -1;
                    input_offset += (size_t)length;
                    remaining -= (size_t)length;
                }
            }
        } else {
            if (zlib_write_fixed_symbol(&writer, input[input_offset]) != 0) return -1;
            input_offset += 1U;
        }
    }
    if (zlib_write_fixed_symbol(&writer, 256U) != 0 || zlib_flush_bits(&writer) != 0) return -1;
    *output_size_out = 2U + writer.byte_offset;
    if (*output_size_out + 4U > output_capacity) return -1;
    adler = compression_adler32(input, input_size);
    output[(*output_size_out)++] = (unsigned char)((adler >> 24U) & 0xffU);
    output[(*output_size_out)++] = (unsigned char)((adler >> 16U) & 0xffU);
    output[(*output_size_out)++] = (unsigned char)((adler >> 8U) & 0xffU);
    output[(*output_size_out)++] = (unsigned char)(adler & 0xffU);
    return 0;
}

size_t compression_zlib_fixed_lz77_bound(size_t input_size) {
    if (input_size > 0xffffffffULL) return 0U;
    return compression_zlib_fixed_rle_bound(input_size);
}

static unsigned int zlib_lz77_hash(const unsigned char *input) {
    unsigned int value = ((unsigned int)input[0] << 16U) ^ ((unsigned int)input[1] << 8U) ^ (unsigned int)input[2];

    value ^= value >> 9U;
    value *= 2654435761U;
    return (value >> 16U) & 65535U;
}

static void zlib_lz77_insert(const unsigned char *input, size_t input_size, unsigned int *head, size_t offset) {
    unsigned int *bucket;
    unsigned int slot;

    if (offset + 2U >= input_size) return;
    bucket = head + (size_t)zlib_lz77_hash(input + offset) * ZLIB_LZ77_HASH_SLOTS;
    for (slot = ZLIB_LZ77_HASH_SLOTS - 1U; slot > 0U; --slot) bucket[slot] = bucket[slot - 1U];
    bucket[0] = (unsigned int)offset;
}

int compression_zlib_fixed_lz77(const unsigned char *input, size_t input_size, unsigned char *output, size_t output_capacity, size_t *output_size_out) {
    ZlibBitWriter writer;
    unsigned int *head;
    size_t input_offset = 0U;
    unsigned int adler;
    unsigned int index;
    int result = -1;

    if (input == 0 || output == 0 || output_size_out == 0 || compression_zlib_fixed_lz77_bound(input_size) == 0U || output_capacity < compression_zlib_fixed_lz77_bound(input_size)) return -1;
    if (output_capacity < 6U) return -1;
    head = (unsigned int *)rt_malloc(sizeof(unsigned int) * 65536U * ZLIB_LZ77_HASH_SLOTS);
    if (head == 0) return -1;
    for (index = 0U; index < 65536U * ZLIB_LZ77_HASH_SLOTS; ++index) head[index] = 0xffffffffU;
    output[0] = 0x78U;
    output[1] = 0x01U;
    zlib_bit_writer_init(&writer, output + 2U, output_capacity - 6U);
    if (zlib_write_bits(&writer, 1U, 1U) != 0 || zlib_write_bits(&writer, 1U, 2U) != 0) goto cleanup;
    while (input_offset < input_size) {
        unsigned int best_length = 0U;
        unsigned int best_distance = 0U;

        if (input_offset + 2U < input_size) {
            unsigned int hash = zlib_lz77_hash(input + input_offset);
            unsigned int *bucket = head + (size_t)hash * ZLIB_LZ77_HASH_SLOTS;
            unsigned int slot;

            for (slot = 0U; slot < ZLIB_LZ77_HASH_SLOTS; ++slot) {
                unsigned int candidate = bucket[slot];
                unsigned int length = 0U;
                unsigned int max_length = input_size - input_offset > 258U ? 258U : (unsigned int)(input_size - input_offset);

                if (candidate == 0xffffffffU || (size_t)candidate >= input_offset || input_offset - (size_t)candidate > 32768U) continue;
                while (length < max_length && input[(size_t)candidate + (size_t)length] == input[input_offset + (size_t)length]) length += 1U;
                if (length > best_length && length >= 3U) {
                    best_length = length;
                    best_distance = (unsigned int)(input_offset - (size_t)candidate);
                    if (best_length == max_length) break;
                }
            }
            zlib_lz77_insert(input, input_size, head, input_offset);
        }
        if (best_length >= 3U) {
            unsigned int step;

            if (zlib_write_fixed_match(&writer, best_length, best_distance) != 0) goto cleanup;
            for (step = 1U; step < best_length; ++step) zlib_lz77_insert(input, input_size, head, input_offset + (size_t)step);
            input_offset += (size_t)best_length;
        } else {
            if (zlib_write_fixed_symbol(&writer, input[input_offset]) != 0) goto cleanup;
            input_offset += 1U;
        }
    }
    if (zlib_write_fixed_symbol(&writer, 256U) != 0 || zlib_flush_bits(&writer) != 0) goto cleanup;
    *output_size_out = 2U + writer.byte_offset;
    if (*output_size_out + 4U > output_capacity) goto cleanup;
    adler = compression_adler32(input, input_size);
    output[(*output_size_out)++] = (unsigned char)((adler >> 24U) & 0xffU);
    output[(*output_size_out)++] = (unsigned char)((adler >> 16U) & 0xffU);
    output[(*output_size_out)++] = (unsigned char)((adler >> 8U) & 0xffU);
    output[(*output_size_out)++] = (unsigned char)(adler & 0xffU);
    result = 0;

cleanup:
    rt_free(head);
    return result;
}
