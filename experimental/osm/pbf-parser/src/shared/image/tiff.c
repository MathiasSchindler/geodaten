#include "image_internal.h"

static unsigned int tiff_read_u16(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u16_le(bytes) : image_read_u16_be(bytes);
}

static unsigned int tiff_read_u32(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u32_le(bytes) : image_read_u32_be(bytes);
}

static unsigned int tiff_read_u64_low(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u32_le(bytes) : image_read_u32_be(bytes + 4U);
}

static int tiff_u64_high_is_zero(const unsigned char *bytes, int little_endian) {
    return little_endian ? image_read_u32_le(bytes + 4U) == 0U : image_read_u32_be(bytes) == 0U;
}

static int tiff_value_u32(const unsigned char *data,
                          size_t size,
                          int little_endian,
                          unsigned int type,
                          unsigned int count,
                          const unsigned char *value_field,
                          unsigned int *value_out) {
    unsigned int offset;

    if (count == 0U) {
        return -1;
    }
    if (type == 3U) {
        if (count == 1U) {
            *value_out = tiff_read_u16(value_field, little_endian);
            return 0;
        }
        offset = tiff_read_u32(value_field, little_endian);
        if ((size_t)offset + 2U <= size) {
            *value_out = tiff_read_u16(data + offset, little_endian);
            return 0;
        }
    } else if (type == 4U) {
        if (count == 1U) {
            *value_out = tiff_read_u32(value_field, little_endian);
            return 0;
        }
        offset = tiff_read_u32(value_field, little_endian);
        if ((size_t)offset + 4U <= size) {
            *value_out = tiff_read_u32(data + offset, little_endian);
            return 0;
        }
    }
    return -1;
}

int image_tiff_extract_metadata(const unsigned char *data,
                                size_t size,
                                unsigned int *width_out,
                                unsigned int *height_out,
                                unsigned int *bit_depth_out,
                                unsigned int *channels_out,
                                unsigned int *compression_out,
                                unsigned int *photometric_out,
                                int *has_photometric_out,
                                unsigned int *orientation_out) {
    int little_endian;
    unsigned int ifd_offset;
    unsigned int entry_count;
    unsigned int i;

    if (width_out != 0) {
        *width_out = 0U;
    }
    if (height_out != 0) {
        *height_out = 0U;
    }
    if (bit_depth_out != 0) {
        *bit_depth_out = 0U;
    }
    if (channels_out != 0) {
        *channels_out = 0U;
    }
    if (compression_out != 0) {
        *compression_out = 0U;
    }
    if (photometric_out != 0) {
        *photometric_out = 0U;
    }
    if (has_photometric_out != 0) {
        *has_photometric_out = 0;
    }
    if (orientation_out != 0) {
        *orientation_out = 0U;
    }

    if (size < 8U) {
        return -1;
    }
    if (data[0] == 'I' && data[1] == 'I') {
        little_endian = 1;
    } else if (data[0] == 'M' && data[1] == 'M') {
        little_endian = 0;
    } else {
        return -1;
    }
    if (tiff_read_u16(data + 2U, little_endian) != 42U) {
        return -1;
    }

    ifd_offset = tiff_read_u32(data + 4U, little_endian);
    if ((size_t)ifd_offset + 2U > size) {
        return 0;
    }
    entry_count = tiff_read_u16(data + ifd_offset, little_endian);
    if ((size_t)entry_count > (size - (size_t)ifd_offset - 2U) / 12U) {
        entry_count = (unsigned int)((size - (size_t)ifd_offset - 2U) / 12U);
    }
    for (i = 0U; i < entry_count; ++i) {
        const unsigned char *entry = data + ifd_offset + 2U + (size_t)i * 12U;
        unsigned int tag = tiff_read_u16(entry, little_endian);
        unsigned int type = tiff_read_u16(entry + 2U, little_endian);
        unsigned int count = tiff_read_u32(entry + 4U, little_endian);
        unsigned int value = 0U;

        if (tiff_value_u32(data, size, little_endian, type, count, entry + 8U, &value) != 0) {
            continue;
        }
        if (tag == 256U && width_out != 0) {
            *width_out = value;
        } else if (tag == 257U && height_out != 0) {
            *height_out = value;
        } else if (tag == 258U && bit_depth_out != 0) {
            *bit_depth_out = value;
        } else if (tag == 259U && compression_out != 0) {
            *compression_out = value;
        } else if (tag == 262U && photometric_out != 0) {
            *photometric_out = value;
            if (has_photometric_out != 0) {
                *has_photometric_out = 1;
            }
        } else if (tag == 274U && orientation_out != 0) {
            *orientation_out = value;
        } else if (tag == 277U && channels_out != 0) {
            *channels_out = value;
        }
    }
    return 0;
}

static int bigtiff_value_u32(const unsigned char *data,
                             size_t size,
                             int little_endian,
                             unsigned int type,
                             unsigned int count,
                             const unsigned char *value_field,
                             unsigned int *value_out) {
    unsigned int offset;

    if (count == 0U) {
        return -1;
    }
    if (type == 3U) {
        if (count == 1U) {
            *value_out = tiff_read_u16(value_field, little_endian);
            return 0;
        }
        if (!tiff_u64_high_is_zero(value_field, little_endian)) {
            return -1;
        }
        offset = tiff_read_u64_low(value_field, little_endian);
        if ((size_t)offset + 2U <= size) {
            *value_out = tiff_read_u16(data + offset, little_endian);
            return 0;
        }
    } else if (type == 4U || type == 16U) {
        if (count == 1U) {
            *value_out = type == 16U ? tiff_read_u64_low(value_field, little_endian) : tiff_read_u32(value_field, little_endian);
            return 0;
        }
        if (!tiff_u64_high_is_zero(value_field, little_endian)) {
            return -1;
        }
        offset = tiff_read_u64_low(value_field, little_endian);
        if ((size_t)offset + 4U <= size) {
            *value_out = type == 16U ? tiff_read_u64_low(data + offset, little_endian) : tiff_read_u32(data + offset, little_endian);
            return 0;
        }
    }
    return -1;
}

static int image_bigtiff_extract_metadata(const unsigned char *data,
                                          size_t size,
                                          int little_endian,
                                          unsigned int *width_out,
                                          unsigned int *height_out,
                                          unsigned int *bit_depth_out,
                                          unsigned int *channels_out,
                                          unsigned int *compression_out,
                                          unsigned int *photometric_out,
                                          int *has_photometric_out,
                                          unsigned int *orientation_out) {
    unsigned int ifd_offset;
    unsigned int entry_count;
    unsigned int i;

    if (!tiff_u64_high_is_zero(data + 8U, little_endian)) {
        return 0;
    }
    ifd_offset = tiff_read_u64_low(data + 8U, little_endian);
    if ((size_t)ifd_offset + 8U > size) {
        return 0;
    }
    if (!tiff_u64_high_is_zero(data + ifd_offset, little_endian)) {
        entry_count = (unsigned int)((size - (size_t)ifd_offset - 8U) / 20U);
    } else {
        entry_count = tiff_read_u64_low(data + ifd_offset, little_endian);
    }
    if ((size_t)entry_count > (size - (size_t)ifd_offset - 8U) / 20U) {
        entry_count = (unsigned int)((size - (size_t)ifd_offset - 8U) / 20U);
    }
    for (i = 0U; i < entry_count; ++i) {
        const unsigned char *entry = data + ifd_offset + 8U + (size_t)i * 20U;
        unsigned int tag = tiff_read_u16(entry, little_endian);
        unsigned int type = tiff_read_u16(entry + 2U, little_endian);
        unsigned int count;
        unsigned int value = 0U;

        if (!tiff_u64_high_is_zero(entry + 4U, little_endian)) {
            continue;
        }
        count = tiff_read_u64_low(entry + 4U, little_endian);
        if (bigtiff_value_u32(data, size, little_endian, type, count, entry + 12U, &value) != 0) {
            continue;
        }
        if (tag == 256U && width_out != 0) {
            *width_out = value;
        } else if (tag == 257U && height_out != 0) {
            *height_out = value;
        } else if (tag == 258U && bit_depth_out != 0) {
            *bit_depth_out = value;
        } else if (tag == 259U && compression_out != 0) {
            *compression_out = value;
        } else if (tag == 262U && photometric_out != 0) {
            *photometric_out = value;
            if (has_photometric_out != 0) {
                *has_photometric_out = 1;
            }
        } else if (tag == 274U && orientation_out != 0) {
            *orientation_out = value;
        } else if (tag == 277U && channels_out != 0) {
            *channels_out = value;
        }
    }
    return 0;
}

int image_probe_tiff(const unsigned char *data, size_t size, ImageInfo *info) {
    int little_endian;
    unsigned int width = 0U;
    unsigned int height = 0U;
    unsigned int bit_depth = 0U;
    unsigned int channels = 0U;
    unsigned int compression = 0U;
    unsigned int photometric = 0U;
    int has_photometric = 0;
    unsigned int orientation = 0U;

    if (size < 8U) {
        return 0;
    }
    if (data[0] == 'I' && data[1] == 'I') {
        little_endian = 1;
    } else if (data[0] == 'M' && data[1] == 'M') {
        little_endian = 0;
    } else {
        return 0;
    }
    if (tiff_read_u16(data + 2U, little_endian) == 43U) {
        if (size < 16U || tiff_read_u16(data + 4U, little_endian) != 8U || tiff_read_u16(data + 6U, little_endian) != 0U) {
            return 0;
        }
        info->format = IMAGE_FORMAT_TIFF;
        image_set_variant(info, little_endian ? "BigTIFF little-endian" : "BigTIFF big-endian");
        image_bigtiff_extract_metadata(data, size, little_endian, &width, &height, &bit_depth, &channels, &compression,
                                       &photometric, &has_photometric, &orientation);
    } else if (tiff_read_u16(data + 2U, little_endian) == 42U) {
        info->format = IMAGE_FORMAT_TIFF;
        image_set_variant(info, little_endian ? "classic TIFF little-endian" : "classic TIFF big-endian");
        image_tiff_extract_metadata(data, size, &width, &height, &bit_depth, &channels, &compression,
                                    &photometric, &has_photometric, &orientation);
    } else {
        return 0;
    }
    image_set_dimensions(info, width, height);
    image_set_bit_depth(info, bit_depth);
    image_set_channels(info, channels);
    if (compression == 1U) {
        image_set_compression(info, "uncompressed");
    } else if (compression == 5U) {
        image_set_compression(info, "LZW");
    } else if (compression == 6U || compression == 7U) {
        image_set_compression(info, "JPEG");
    } else if (compression == 8U) {
        image_set_compression(info, "deflate");
    } else if (compression != 0U) {
        image_set_compression(info, "other");
    }
    if (has_photometric && photometric == 0U) {
        image_set_color_model(info, "white-is-zero grayscale");
    } else if (has_photometric && photometric == 1U) {
        image_set_color_model(info, "black-is-zero grayscale");
    } else if (has_photometric && photometric == 2U) {
        image_set_color_model(info, "rgb");
    } else if (has_photometric && photometric == 3U) {
        image_set_color_model(info, "palette");
        info->property_flags |= IMAGE_PROPERTY_PALETTE;
    } else if (has_photometric && photometric == 5U) {
        image_set_color_model(info, "cmyk");
    } else if (has_photometric && photometric == 6U) {
        image_set_color_model(info, "YCbCr");
    }
    image_set_orientation(info, orientation);
    return 1;
}
