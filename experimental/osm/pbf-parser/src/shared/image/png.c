#include "image_internal.h"

static size_t png_payload_end(size_t offset, unsigned int length, size_t size) {
    if ((size_t)length > size - offset) {
        return size;
    }
    return offset + (size_t)length;
}

int image_probe_png(const unsigned char *data, size_t size, ImageInfo *info) {
    static const unsigned char signature[8] = {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1aU, '\n'};
    unsigned char color_type;
    unsigned int channels = 0U;
    size_t offset;
    unsigned int duration_ms = 0U;
    int has_duration = 0;

    if (size < 8U || !image_byte_arrays_equal(data, signature, sizeof(signature))) {
        return 0;
    }

    info->format = IMAGE_FORMAT_PNG;
    image_set_variant(info, "PNG");
    image_set_compression(info, "deflate");
    if (size >= 33U && image_read_u32_be(data + 8) == 13U && image_bytes_equal(data + 12, "IHDR", 4U)) {
        image_set_dimensions(info, image_read_u32_be(data + 16), image_read_u32_be(data + 20));
        image_set_bit_depth(info, data[24]);
        color_type = data[25];
        if (color_type == 0U) {
            channels = 1U;
            image_set_color_model(info, "grayscale");
        } else if (color_type == 2U) {
            channels = 3U;
            image_set_color_model(info, "truecolor");
        } else if (color_type == 3U) {
            channels = 1U;
            info->property_flags |= IMAGE_PROPERTY_PALETTE;
            image_set_color_model(info, "indexed-color");
        } else if (color_type == 4U) {
            channels = 2U;
            info->property_flags |= IMAGE_PROPERTY_ALPHA;
            image_set_color_model(info, "grayscale-alpha");
        } else if (color_type == 6U) {
            channels = 4U;
            info->property_flags |= IMAGE_PROPERTY_ALPHA;
            image_set_color_model(info, "truecolor-alpha");
        }
        image_set_channels(info, channels);
        if (data[28] != 0U) {
            info->property_flags |= IMAGE_PROPERTY_INTERLACED;
        }
    }
    offset = 8U;
    while (offset + 12U <= size) {
        unsigned int length = image_read_u32_be(data + offset);
        const unsigned char *type = data + offset + 4U;
        size_t payload = offset + 8U;

        if (image_bytes_equal(type, "tRNS", 4U)) {
            info->property_flags |= IMAGE_PROPERTY_ALPHA;
        } else if (image_bytes_equal(type, "iCCP", 4U)) {
            info->property_flags |= IMAGE_PROPERTY_ICC;
        } else if (image_bytes_equal(type, "eXIf", 4U)) {
            info->property_flags |= IMAGE_PROPERTY_EXIF;
            if (payload < size) {
                unsigned int orientation = 0U;
                image_tiff_extract_metadata(data + payload, png_payload_end(payload, length, size) - payload,
                                            0, 0, 0, 0, 0, 0, 0, &orientation);
                image_set_orientation(info, orientation);
            }
        } else if (image_bytes_equal(type, "pHYs", 4U) && length >= 9U && payload + 9U <= size) {
            image_set_density(info, image_read_u32_be(data + payload), image_read_u32_be(data + payload + 4U),
                              data[payload + 8U] == 1U ? "pixels/meter" : "pixels/unit");
        } else if (image_bytes_equal(type, "acTL", 4U)) {
            if (payload + 8U <= png_payload_end(payload, length, size)) {
                image_set_frames(info, image_read_u32_be(data + payload));
                image_set_loop_count(info, image_read_u32_be(data + payload + 4U));
            }
        } else if (image_bytes_equal(type, "fcTL", 4U)) {
            if (payload + 26U <= png_payload_end(payload, length, size)) {
                unsigned int delay_num = image_read_u16_be(data + payload + 20U);
                unsigned int delay_den = image_read_u16_be(data + payload + 22U);
                unsigned int frame_duration;

                if (delay_den == 0U) {
                    delay_den = 100U;
                }
                frame_duration = (delay_num * 1000U) / delay_den;
                if (duration_ms <= 0xffffffffU - frame_duration) {
                    duration_ms += frame_duration;
                    has_duration = 1;
                }
            }
        } else if (image_bytes_equal(type, "IEND", 4U)) {
            break;
        }
        if ((size_t)length > size - offset - 12U) {
            break;
        }
        offset += 12U + (size_t)length;
    }
    if (has_duration) {
        image_set_duration_ms(info, duration_ms);
    }
    return 1;
}
