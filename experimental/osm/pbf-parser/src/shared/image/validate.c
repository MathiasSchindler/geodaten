#include "image_internal.h"
#include "compression/crc32.h"

static void image_validation_set(ImageValidation *validation, ImageFormat format, int valid, const char *message) {
    if (validation == 0) {
        return;
    }
    validation->format = format;
    validation->valid = valid;
    validation->has_failure_offset = 0;
    validation->failure_offset = 0U;
    validation->message = message;
}

static void image_validation_set_offset(ImageValidation *validation, ImageFormat format, const char *message, size_t failure_offset) {
    if (validation == 0) {
        return;
    }
    validation->format = format;
    validation->valid = 0;
    validation->has_failure_offset = 1;
    validation->failure_offset = failure_offset;
    validation->message = message;
}

static int png_color_bit_depth_is_valid(unsigned char color_type, unsigned char bit_depth) {
    if (color_type == 0U) {
        return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U || bit_depth == 8U || bit_depth == 16U;
    }
    if (color_type == 2U) {
        return bit_depth == 8U || bit_depth == 16U;
    }
    if (color_type == 3U) {
        return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U || bit_depth == 8U;
    }
    if (color_type == 4U) {
        return bit_depth == 8U || bit_depth == 16U;
    }
    if (color_type == 6U) {
        return bit_depth == 8U || bit_depth == 16U;
    }
    return 0;
}

int image_validate_png(const unsigned char *data, size_t size, const ImageValidationOptions *options, ImageValidation *validation) {
    static const unsigned char signature[8] = {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1aU, '\n'};
    size_t offset;
    int seen_ihdr = 0;
    int seen_plte = 0;
    int seen_idat = 0;
    int seen_non_idat_after_idat = 0;
    int seen_iend = 0;
    unsigned char color_type = 0U;
    int strict = options != 0 && options->strict;

    if (size < 8U || !image_byte_arrays_equal(data, signature, sizeof(signature))) {
        image_validation_set(validation, IMAGE_FORMAT_UNKNOWN, 0, "not a PNG image");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_PNG, 0, "truncated PNG stream");
    offset = 8U;
    while (offset + 12U <= size) {
        unsigned int length = image_read_u32_be(data + offset);
        const unsigned char *type = data + offset + 4U;
        size_t payload = offset + 8U;
        size_t crc_offset;
        unsigned int expected_crc;
        unsigned int actual_crc;

        if ((size_t)length > size - offset - 12U) {
            image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "chunk length exceeds file size", offset);
            return -1;
        }
        crc_offset = payload + (size_t)length;
        expected_crc = image_read_u32_be(data + crc_offset);
        actual_crc = compression_crc32(type, (size_t)length + 4U);
        if (actual_crc != expected_crc) {
            image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "chunk CRC mismatch", offset);
            return -1;
        }
        if (!seen_ihdr) {
            if (!image_bytes_equal(type, "IHDR", 4U)) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "first chunk is not IHDR", offset + 4U);
                return -1;
            }
            if (length != 13U) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "IHDR chunk has invalid length", offset);
                return -1;
            }
            if (image_read_u32_be(data + payload) == 0U || image_read_u32_be(data + payload + 4U) == 0U) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "IHDR dimensions must be nonzero", payload);
                return -1;
            }
            color_type = data[payload + 9U];
            if (!png_color_bit_depth_is_valid(color_type, data[payload + 8U])) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "invalid PNG color type and bit depth combination", payload + 8U);
                return -1;
            }
            if (data[payload + 10U] != 0U || data[payload + 11U] != 0U || data[payload + 12U] > 1U) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "IHDR compression filter or interlace method is invalid", payload + 10U);
                return -1;
            }
            seen_ihdr = 1;
        } else if (image_bytes_equal(type, "IHDR", 4U)) {
            image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "duplicate IHDR chunk", offset + 4U);
            return -1;
        } else if (image_bytes_equal(type, "PLTE", 4U)) {
            if (seen_idat) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "PLTE chunk appears after IDAT", offset + 4U);
                return -1;
            }
            if (seen_plte) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "duplicate PLTE chunk", offset + 4U);
                return -1;
            }
            if (length == 0U || (length % 3U) != 0U || length > 768U) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "PLTE chunk has invalid length", offset);
                return -1;
            }
            seen_plte = 1;
        } else if (image_bytes_equal(type, "IDAT", 4U)) {
            if (seen_non_idat_after_idat) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "IDAT chunks are not consecutive", offset + 4U);
                return -1;
            }
            seen_idat = 1;
        } else if (image_bytes_equal(type, "tRNS", 4U)) {
            if (seen_idat) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "tRNS chunk appears after IDAT", offset + 4U);
                return -1;
            }
            if (color_type == 3U && !seen_plte) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "indexed tRNS chunk appears before PLTE", offset + 4U);
                return -1;
            }
        } else if (image_bytes_equal(type, "IEND", 4U)) {
            if (length != 0U) {
                image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "IEND chunk has invalid length", offset);
                return -1;
            }
            seen_iend = 1;
            offset = crc_offset + 4U;
            break;
        } else if (strict && seen_idat) {
            image_validation_set_offset(validation, IMAGE_FORMAT_PNG, "strict PNG rejects ancillary chunks after IDAT", offset + 4U);
            return -1;
        } else if (seen_idat) {
            seen_non_idat_after_idat = 1;
        }
        offset = crc_offset + 4U;
    }
    if (!seen_ihdr) {
        image_validation_set(validation, IMAGE_FORMAT_PNG, 0, "missing IHDR chunk");
        return -1;
    }
    if (color_type == 3U && !seen_plte) {
        image_validation_set(validation, IMAGE_FORMAT_PNG, 0, "indexed PNG is missing PLTE");
        return -1;
    }
    if (!seen_idat) {
        image_validation_set(validation, IMAGE_FORMAT_PNG, 0, "missing IDAT chunk");
        return -1;
    }
    if (!seen_iend) {
        image_validation_set(validation, IMAGE_FORMAT_PNG, 0, "missing IEND chunk");
        return -1;
    }
    if (offset != size) {
        image_validation_set(validation, IMAGE_FORMAT_PNG, 0, "trailing data after IEND");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_PNG, 1, "valid PNG image");
    return 0;
}

static int jpeg_is_sof_marker(unsigned char marker) {
    return (marker >= 0xc0U && marker <= 0xc3U) ||
           (marker >= 0xc5U && marker <= 0xc7U) ||
           (marker >= 0xc9U && marker <= 0xcbU) ||
           (marker >= 0xcdU && marker <= 0xcfU);
}

static int jpeg_marker_has_no_payload(unsigned char marker) {
    return marker == 0x01U || (marker >= 0xd0U && marker <= 0xd9U);
}

static int jpeg_skip_scan_data(const unsigned char *data, size_t size, size_t *offset_io) {
    size_t offset = *offset_io;

    while (offset < size) {
        unsigned char marker;

        if (data[offset++] != 0xffU) {
            continue;
        }
        while (offset < size && data[offset] == 0xffU) {
            offset += 1U;
        }
        if (offset >= size) {
            return -1;
        }
        marker = data[offset++];
        if (marker == 0x00U || (marker >= 0xd0U && marker <= 0xd7U)) {
            continue;
        }
        if (marker == 0xd9U) {
            *offset_io = offset;
            return 0;
        }
        return -2;
    }
    return -1;
}

int image_validate_jpeg(const unsigned char *data, size_t size, ImageValidation *validation) {
    size_t offset = 2U;
    int saw_sof = 0;
    int saw_sos = 0;
    int saw_eoi = 0;

    if (size < 3U || data[0] != 0xffU || data[1] != 0xd8U || data[2] != 0xffU) {
        image_validation_set(validation, IMAGE_FORMAT_UNKNOWN, 0, "not a JPEG image");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "truncated JPEG stream");
    while (offset < size) {
        unsigned char marker;
        unsigned int segment_size;

        if (data[offset] != 0xffU) {
            image_validation_set_offset(validation, IMAGE_FORMAT_JPEG, "expected JPEG marker", offset);
            return -1;
        }
        while (offset < size && data[offset] == 0xffU) {
            offset += 1U;
        }
        if (offset >= size) {
            return -1;
        }
        marker = data[offset++];
        if (marker == 0x00U) {
            image_validation_set_offset(validation, IMAGE_FORMAT_JPEG, "unexpected stuffed JPEG byte outside scan data", offset - 1U);
            return -1;
        }
        if (marker == 0xd9U) {
            saw_eoi = 1;
            break;
        }
        if (jpeg_marker_has_no_payload(marker)) {
            continue;
        }
        if (offset + 2U > size) {
            return -1;
        }
        segment_size = image_read_u16_be(data + offset);
        if (segment_size < 2U || (size_t)segment_size > size - offset) {
            image_validation_set_offset(validation, IMAGE_FORMAT_JPEG, "JPEG segment length exceeds file size", offset);
            return -1;
        }
        if (jpeg_is_sof_marker(marker)) {
            if (segment_size < 8U) {
                image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "JPEG SOF segment is too short");
                return -1;
            }
            if (image_read_u16_be(data + offset + 3U) == 0U || image_read_u16_be(data + offset + 5U) == 0U ||
                data[offset + 7U] == 0U) {
                image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "JPEG SOF dimensions or component count are invalid");
                return -1;
            }
            if ((unsigned int)(8U + 3U * data[offset + 7U]) > segment_size) {
                image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "JPEG SOF component table is truncated");
                return -1;
            }
            saw_sof = 1;
        } else if (marker == 0xdaU) {
            int scan_result;

            if (segment_size < 6U) {
                image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "JPEG SOS segment is too short");
                return -1;
            }
            saw_sos = 1;
            offset += (size_t)segment_size;
            scan_result = jpeg_skip_scan_data(data, size, &offset);
            if (scan_result == -2) {
                image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "unexpected JPEG marker inside scan data");
                return -1;
            }
            if (scan_result != 0) {
                image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "missing JPEG EOI marker");
                return -1;
            }
            saw_eoi = 1;
            break;
        }
        offset += (size_t)segment_size;
    }
    if (!saw_sof) {
        image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "missing JPEG SOF segment");
        return -1;
    }
    if (!saw_sos) {
        image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "missing JPEG SOS segment");
        return -1;
    }
    if (!saw_eoi) {
        image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "missing JPEG EOI marker");
        return -1;
    }
    if (offset != size) {
        image_validation_set(validation, IMAGE_FORMAT_JPEG, 0, "trailing data after JPEG EOI");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_JPEG, 1, "valid JPEG image");
    return 0;
}

static int gif_color_table_size(unsigned char packed, size_t *size_out) {
    *size_out = (size_t)3U << ((packed & 0x07U) + 1U);
    return 0;
}

static int gif_skip_sub_blocks(const unsigned char *data, size_t size, size_t *offset_io) {
    size_t offset = *offset_io;

    while (offset < size) {
        unsigned int block_size = data[offset++];

        if (block_size == 0U) {
            *offset_io = offset;
            return 0;
        }
        if ((size_t)block_size > size - offset) {
            return -1;
        }
        offset += (size_t)block_size;
    }
    return -1;
}

int image_validate_gif(const unsigned char *data, size_t size, ImageValidation *validation) {
    size_t offset;
    size_t table_size;
    int saw_trailer = 0;
    unsigned int frame_count = 0U;

    if (size < 6U || !image_bytes_equal(data, "GIF", 3U) || data[3] != '8' ||
        (data[4] != '7' && data[4] != '9') || data[5] != 'a') {
        image_validation_set(validation, IMAGE_FORMAT_UNKNOWN, 0, "not a GIF image");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "truncated GIF stream");
    if (size < 13U) {
        return -1;
    }
    if (image_read_u16_le(data + 6U) == 0U || image_read_u16_le(data + 8U) == 0U) {
        image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "logical screen dimensions must be nonzero");
        return -1;
    }
    offset = 13U;
    if ((data[10] & 0x80U) != 0U) {
        gif_color_table_size(data[10], &table_size);
        if (table_size > size - offset) {
            image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "global color table is truncated");
            return -1;
        }
        offset += table_size;
    }
    while (offset < size) {
        unsigned char block = data[offset++];

        if (block == 0x3bU) {
            saw_trailer = 1;
            break;
        }
        if (block == 0x2cU) {
            unsigned char packed;

            if (offset + 9U > size) {
                image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "image descriptor is truncated");
                return -1;
            }
            if (image_read_u16_le(data + offset + 4U) == 0U || image_read_u16_le(data + offset + 6U) == 0U) {
                image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "image dimensions must be nonzero");
                return -1;
            }
            packed = data[offset + 8U];
            offset += 9U;
            if ((packed & 0x80U) != 0U) {
                gif_color_table_size(packed, &table_size);
                if (table_size > size - offset) {
                    image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "local color table is truncated");
                    return -1;
                }
                offset += table_size;
            }
            if (offset >= size) {
                image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "missing LZW minimum code size");
                return -1;
            }
            if (data[offset] < 2U || data[offset] > 12U) {
                image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "invalid LZW minimum code size");
                return -1;
            }
            offset += 1U;
            if (gif_skip_sub_blocks(data, size, &offset) != 0) {
                image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "image data sub-blocks are truncated");
                return -1;
            }
            frame_count += 1U;
        } else if (block == 0x21U) {
            unsigned char label;

            if (offset >= size) {
                image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "extension block is truncated");
                return -1;
            }
            label = data[offset++];
            if (label == 0xf9U) {
                if (offset + 6U > size || data[offset] != 4U || data[offset + 5U] != 0U) {
                    image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "graphic control extension is invalid");
                    return -1;
                }
                offset += 6U;
            } else if (gif_skip_sub_blocks(data, size, &offset) != 0) {
                image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "extension sub-blocks are truncated");
                return -1;
            }
        } else {
            image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "unknown GIF block type");
            return -1;
        }
    }
    if (!saw_trailer) {
        image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "missing GIF trailer");
        return -1;
    }
    if (offset != size) {
        image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "trailing data after GIF trailer");
        return -1;
    }
    if (frame_count == 0U) {
        image_validation_set(validation, IMAGE_FORMAT_GIF, 0, "missing GIF image frame");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_GIF, 1, "valid GIF image");
    return 0;
}

static int bmp_bit_depth_is_valid(unsigned int bits) {
    return bits == 1U || bits == 4U || bits == 8U || bits == 16U || bits == 24U || bits == 32U;
}

static int bmp_signed_u32_is_zero(unsigned int value) {
    return value == 0U;
}

static unsigned int bmp_abs_i32_as_u32(unsigned int value) {
    if ((value & 0x80000000U) == 0U) {
        return value;
    }
    return 0U - value;
}

static int bmp_row_stride(unsigned int width, unsigned int bits, size_t *stride_out) {
    size_t width_size = (size_t)width;
    size_t bits_size = (size_t)bits;
    size_t row_bits;

    if (width_size != 0U && bits_size > (((size_t)-1) - 31U) / width_size) {
        return -1;
    }
    row_bits = width_size * bits_size;
    *stride_out = ((row_bits + 31U) / 32U) * 4U;
    return 0;
}

static int bmp_validate_pixel_span(unsigned int width,
                                   unsigned int height,
                                   unsigned int bits,
                                   size_t pixel_offset,
                                   size_t size,
                                   ImageValidation *validation) {
    size_t stride;
    size_t needed;

    if (bmp_row_stride(width, bits, &stride) != 0 || height > ((size_t)-1) / stride) {
        image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP pixel array size overflows");
        return -1;
    }
    needed = stride * (size_t)height;
    if (pixel_offset > size || needed > size - pixel_offset) {
        image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP pixel array is truncated");
        return -1;
    }
    return 0;
}

int image_validate_bmp(const unsigned char *data, size_t size, ImageValidation *validation) {
    unsigned int file_size;
    unsigned int pixel_offset;
    unsigned int dib_size;
    unsigned int bits;
    unsigned int width;
    unsigned int height;
    size_t min_pixel_offset;
    size_t color_table_entries = 0U;
    size_t color_table_bytes = 0U;

    if (size < 2U || data[0] != 'B' || data[1] != 'M') {
        image_validation_set(validation, IMAGE_FORMAT_UNKNOWN, 0, "not a BMP image");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "truncated BMP stream");
    if (size < 26U) {
        return -1;
    }
    file_size = image_read_u32_le(data + 2U);
    pixel_offset = image_read_u32_le(data + 10U);
    dib_size = image_read_u32_le(data + 14U);
    if (file_size != 0U && (size_t)file_size != size) {
        image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP file size field does not match input size");
        return -1;
    }
    if (dib_size == 12U) {
        unsigned int planes;

        if (size < 26U) {
            return -1;
        }
        width = image_read_u16_le(data + 18U);
        height = image_read_u16_le(data + 20U);
        planes = image_read_u16_le(data + 22U);
        bits = image_read_u16_le(data + 24U);
        if (width == 0U || height == 0U) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP dimensions must be nonzero");
            return -1;
        }
        if (planes != 1U) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP plane count must be one");
            return -1;
        }
        if (!bmp_bit_depth_is_valid(bits)) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP bit depth is unsupported");
            return -1;
        }
        if (bits <= 8U) {
            color_table_entries = (size_t)1U << bits;
            color_table_bytes = color_table_entries * 3U;
        }
        min_pixel_offset = 14U + 12U + color_table_bytes;
        if ((size_t)pixel_offset < min_pixel_offset || (size_t)pixel_offset > size) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP pixel offset is invalid");
            return -1;
        }
        if (bmp_validate_pixel_span(width, height, bits, (size_t)pixel_offset, size, validation) != 0) {
            return -1;
        }
    } else if (dib_size >= 40U) {
        unsigned int planes;
        unsigned int compression;
        unsigned int image_size;
        unsigned int colors_used = 0U;
        size_t masks_size = 0U;

        if (dib_size > size - 14U || size < 54U) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP DIB header is truncated");
            return -1;
        }
        width = image_read_u32_le(data + 18U);
        height = image_read_u32_le(data + 22U);
        planes = image_read_u16_le(data + 26U);
        bits = image_read_u16_le(data + 28U);
        compression = image_read_u32_le(data + 30U);
        image_size = image_read_u32_le(data + 34U);
        if (dib_size >= 40U) {
            colors_used = image_read_u32_le(data + 46U);
        }
        if (bmp_signed_u32_is_zero(width) || bmp_signed_u32_is_zero(height)) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP dimensions must be nonzero");
            return -1;
        }
        width = bmp_abs_i32_as_u32(width);
        height = bmp_abs_i32_as_u32(height);
        if (planes != 1U) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP plane count must be one");
            return -1;
        }
        if (!bmp_bit_depth_is_valid(bits)) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP bit depth is unsupported");
            return -1;
        }
        if ((compression == 1U && bits != 8U) || (compression == 2U && bits != 4U) ||
            (compression == 3U && bits != 16U && bits != 32U) || compression > 5U) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP compression is incompatible with bit depth");
            return -1;
        }
        if (compression == 3U && dib_size == 40U) {
            masks_size = 12U;
        }
        if (bits <= 8U) {
            size_t maximum_entries = (size_t)1U << bits;

            color_table_entries = colors_used == 0U ? maximum_entries : (size_t)colors_used;
            if (color_table_entries > maximum_entries) {
                image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP color table is too large for bit depth");
                return -1;
            }
            color_table_bytes = color_table_entries * 4U;
        }
        min_pixel_offset = 14U + (size_t)dib_size + masks_size + color_table_bytes;
        if ((size_t)pixel_offset < min_pixel_offset || (size_t)pixel_offset > size) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP pixel offset is invalid");
            return -1;
        }
        if (compression == 0U || compression == 3U) {
            if (bmp_validate_pixel_span(width, height, bits, (size_t)pixel_offset, size, validation) != 0) {
                return -1;
            }
        } else if (image_size != 0U && ((size_t)pixel_offset > size || (size_t)image_size > size - (size_t)pixel_offset)) {
            image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP compressed image data is truncated");
            return -1;
        }
    } else {
        image_validation_set(validation, IMAGE_FORMAT_BMP, 0, "BMP DIB header size is unsupported");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_BMP, 1, "valid BMP image and pixel array");
    return 0;
}

static size_t tiff_type_size(unsigned int type) {
    switch (type) {
        case 1U:
        case 2U:
        case 6U:
        case 7U:
            return 1U;
        case 3U:
        case 8U:
            return 2U;
        case 4U:
        case 9U:
        case 11U:
            return 4U;
        case 5U:
        case 10U:
        case 12U:
        case 16U:
        case 17U:
        case 18U:
            return 8U;
        default:
            return 0U;
    }
}

static unsigned int tiff_validate_read_u16(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u16_le(bytes) : image_read_u16_be(bytes);
}

static unsigned int tiff_validate_read_u32(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u32_le(bytes) : image_read_u32_be(bytes);
}

static int tiff_validate_classic_ifd(const unsigned char *data, size_t size, int little_endian, size_t ifd_offset, ImageValidation *validation) {
    unsigned int entry_count;
    unsigned int index;
    int has_width = 0;
    int has_height = 0;

    if (ifd_offset + 2U > size) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "TIFF first IFD offset exceeds file size", ifd_offset);
        return -1;
    }
    entry_count = tiff_validate_read_u16(data + ifd_offset, little_endian);
    if ((size_t)entry_count > (size - ifd_offset - 2U) / 12U) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "TIFF IFD entry table is truncated", ifd_offset);
        return -1;
    }
    if (ifd_offset + 2U + (size_t)entry_count * 12U + 4U > size) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "TIFF next IFD pointer is truncated", ifd_offset + 2U + (size_t)entry_count * 12U);
        return -1;
    }
    for (index = 0U; index < entry_count; ++index) {
        const unsigned char *entry = data + ifd_offset + 2U + (size_t)index * 12U;
        unsigned int tag = tiff_validate_read_u16(entry, little_endian);
        unsigned int type = tiff_validate_read_u16(entry + 2U, little_endian);
        unsigned int count = tiff_validate_read_u32(entry + 4U, little_endian);
        size_t unit_size = tiff_type_size(type);
        size_t value_size;

        if (unit_size == 0U || count == 0U || (size_t)count > ((size_t)-1) / unit_size) {
            image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "TIFF IFD entry has invalid type or count", ifd_offset + 2U + (size_t)index * 12U);
            return -1;
        }
        value_size = unit_size * (size_t)count;
        if (value_size > 4U) {
            unsigned int value_offset = tiff_validate_read_u32(entry + 8U, little_endian);

            if ((size_t)value_offset > size || value_size > size - (size_t)value_offset) {
                image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "TIFF IFD value offset exceeds file size", ifd_offset + 2U + (size_t)index * 12U + 8U);
                return -1;
            }
        }
        if (tag == 256U) {
            has_width = 1;
        } else if (tag == 257U) {
            has_height = 1;
        }
    }
    if (!has_width || !has_height) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "TIFF first IFD is missing image dimensions", ifd_offset);
        return -1;
    }
    return 0;
}

static int tiff_u64_high_is_zero_for_validation(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u32_le(bytes + 4U) == 0U : image_read_u32_be(bytes) == 0U;
}

static unsigned int tiff_read_u64_low_for_validation(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u32_le(bytes) : image_read_u32_be(bytes + 4U);
}

static int tiff_validate_big_ifd(const unsigned char *data, size_t size, int little_endian, size_t ifd_offset, ImageValidation *validation) {
    unsigned int entry_count;
    unsigned int index;
    int has_width = 0;
    int has_height = 0;

    if (ifd_offset + 8U > size) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF first IFD offset exceeds file size", ifd_offset);
        return -1;
    }
    if (!tiff_u64_high_is_zero_for_validation(data + ifd_offset, little_endian)) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF IFD entry count exceeds supported range", ifd_offset);
        return -1;
    }
    entry_count = tiff_read_u64_low_for_validation(data + ifd_offset, little_endian);
    if ((size_t)entry_count > (size - ifd_offset - 8U) / 20U) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF IFD entry table is truncated", ifd_offset);
        return -1;
    }
    if (ifd_offset + 8U + (size_t)entry_count * 20U + 8U > size) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF next IFD pointer is truncated", ifd_offset + 8U + (size_t)entry_count * 20U);
        return -1;
    }
    for (index = 0U; index < entry_count; ++index) {
        const unsigned char *entry = data + ifd_offset + 8U + (size_t)index * 20U;
        unsigned int tag = tiff_validate_read_u16(entry, little_endian);
        unsigned int type = tiff_validate_read_u16(entry + 2U, little_endian);
        unsigned int count;
        size_t unit_size = tiff_type_size(type);
        size_t value_size;

        if (!tiff_u64_high_is_zero_for_validation(entry + 4U, little_endian)) {
            image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF IFD entry count exceeds supported range", ifd_offset + 8U + (size_t)index * 20U + 4U);
            return -1;
        }
        count = tiff_read_u64_low_for_validation(entry + 4U, little_endian);
        if (unit_size == 0U || count == 0U || (size_t)count > ((size_t)-1) / unit_size) {
            image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF IFD entry has invalid type or count", ifd_offset + 8U + (size_t)index * 20U);
            return -1;
        }
        value_size = unit_size * (size_t)count;
        if (value_size > 8U) {
            unsigned int value_offset;

            if (!tiff_u64_high_is_zero_for_validation(entry + 12U, little_endian)) {
                image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF IFD value offset exceeds supported range", ifd_offset + 8U + (size_t)index * 20U + 12U);
                return -1;
            }
            value_offset = tiff_read_u64_low_for_validation(entry + 12U, little_endian);
            if ((size_t)value_offset > size || value_size > size - (size_t)value_offset) {
                image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF IFD value offset exceeds file size", ifd_offset + 8U + (size_t)index * 20U + 12U);
                return -1;
            }
        }
        if (tag == 256U) {
            has_width = 1;
        } else if (tag == 257U) {
            has_height = 1;
        }
    }
    if (!has_width || !has_height) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF first IFD is missing image dimensions", ifd_offset);
        return -1;
    }
    return 0;
}

int image_validate_tiff(const unsigned char *data, size_t size, ImageValidation *validation) {
    int little_endian;
    unsigned int magic;
    unsigned int ifd_offset;

    if (size < 4U || !((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M'))) {
        image_validation_set(validation, IMAGE_FORMAT_UNKNOWN, 0, "not a TIFF image");
        return -1;
    }
    little_endian = data[0] == 'I';
    magic = tiff_validate_read_u16(data + 2U, little_endian);
    if (magic == 43U) {
        if (size < 16U || tiff_validate_read_u16(data + 4U, little_endian) != 8U || tiff_validate_read_u16(data + 6U, little_endian) != 0U) {
            image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF header fields are invalid", 4U);
            return -1;
        }
        if (!tiff_u64_high_is_zero_for_validation(data + 8U, little_endian)) {
            image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "BigTIFF first IFD offset exceeds supported range", 8U);
            return -1;
        }
        if (tiff_validate_big_ifd(data, size, little_endian, (size_t)tiff_read_u64_low_for_validation(data + 8U, little_endian), validation) != 0) {
            return -1;
        }
        image_validation_set(validation, IMAGE_FORMAT_TIFF, 1, "valid BigTIFF header and first IFD");
        return 0;
    }
    if (magic != 42U) {
        image_validation_set_offset(validation, IMAGE_FORMAT_TIFF, "TIFF magic number is invalid", 2U);
        return -1;
    }
    if (size < 8U) {
        image_validation_set(validation, IMAGE_FORMAT_TIFF, 0, "truncated TIFF header");
        return -1;
    }
    ifd_offset = tiff_validate_read_u32(data + 4U, little_endian);
    if (tiff_validate_classic_ifd(data, size, little_endian, (size_t)ifd_offset, validation) != 0) {
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_TIFF, 1, "valid TIFF header and first IFD");
    return 0;
}

int image_validate_webp(const unsigned char *data, size_t size, ImageValidation *validation) {
    size_t offset = 12U;
    unsigned int riff_size;
    int saw_image_chunk = 0;

    if (size < 12U || !image_bytes_equal(data, "RIFF", 4U) || !image_bytes_equal(data + 8U, "WEBP", 4U)) {
        image_validation_set(validation, IMAGE_FORMAT_UNKNOWN, 0, "not a WebP image");
        return -1;
    }
    riff_size = image_read_u32_le(data + 4U);
    if ((size_t)riff_size + 8U != size) {
        image_validation_set_offset(validation, IMAGE_FORMAT_WEBP, "WebP RIFF size does not match input size", 4U);
        return -1;
    }
    while (offset + 8U <= size) {
        const unsigned char *type = data + offset;
        unsigned int chunk_size = image_read_u32_le(data + offset + 4U);
        size_t chunk_total = 8U + (size_t)chunk_size + ((chunk_size & 1U) != 0U ? 1U : 0U);

        if (chunk_total < 8U || chunk_total > size - offset) {
            image_validation_set_offset(validation, IMAGE_FORMAT_WEBP, "WebP chunk length exceeds file size", offset);
            return -1;
        }
        if (image_bytes_equal(type, "VP8X", 4U)) {
            if (chunk_size < 10U) {
                image_validation_set_offset(validation, IMAGE_FORMAT_WEBP, "WebP VP8X chunk is too short", offset);
                return -1;
            }
        } else if (image_bytes_equal(type, "VP8 ", 4U) || image_bytes_equal(type, "VP8L", 4U)) {
            saw_image_chunk = 1;
        } else if (image_bytes_equal(type, "ANMF", 4U)) {
            if (chunk_size < 16U) {
                image_validation_set_offset(validation, IMAGE_FORMAT_WEBP, "WebP ANMF chunk is too short", offset);
                return -1;
            }
            saw_image_chunk = 1;
        } else if (image_bytes_equal(type, "ANIM", 4U) && chunk_size < 6U) {
            image_validation_set_offset(validation, IMAGE_FORMAT_WEBP, "WebP ANIM chunk is too short", offset);
            return -1;
        }
        offset += chunk_total;
    }
    if (offset != size) {
        image_validation_set_offset(validation, IMAGE_FORMAT_WEBP, "WebP chunk table is truncated", offset);
        return -1;
    }
    if (!saw_image_chunk) {
        image_validation_set(validation, IMAGE_FORMAT_WEBP, 0, "WebP image chunk is missing");
        return -1;
    }
    image_validation_set(validation, IMAGE_FORMAT_WEBP, 1, "valid WebP RIFF image");
    return 0;
}
