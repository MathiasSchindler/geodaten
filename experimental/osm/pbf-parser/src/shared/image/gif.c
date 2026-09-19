#include "image_internal.h"

int image_probe_gif(const unsigned char *data, size_t size, ImageInfo *info) {
    size_t offset;
    unsigned int frames = 0U;
    unsigned int duration_ms = 0U;
    unsigned int pending_delay_cs = 0U;
    int has_duration = 0;

    if (size < 13U || !image_bytes_equal(data, "GIF", 3U) || data[3] != '8' || (data[4] != '7' && data[4] != '9') || data[5] != 'a') {
        return 0;
    }

    info->format = IMAGE_FORMAT_GIF;
    image_set_variant(info, data[4] == '9' ? "GIF89a" : "GIF87a");
    image_set_compression(info, "LZW");
    image_set_dimensions(info, image_read_u16_le(data + 6), image_read_u16_le(data + 8));
    image_set_bit_depth(info, (unsigned int)((data[10] & 0x07U) + 1U));
    image_set_channels(info, 3U);
    image_set_color_model(info, "indexed-color");
    info->property_flags |= IMAGE_PROPERTY_PALETTE;

    offset = 13U;
    if ((data[10] & 0x80U) != 0U) {
        offset += (size_t)3U << ((data[10] & 0x07U) + 1U);
    }
    while (offset < size) {
        unsigned char block = data[offset++];

        if (block == 0x2cU) {
            if (offset + 9U > size) {
                break;
            }
            frames += 1U;
            if (duration_ms <= 0xffffffffU - pending_delay_cs * 10U) {
                duration_ms += pending_delay_cs * 10U;
                if (pending_delay_cs != 0U) {
                    has_duration = 1;
                }
            }
            pending_delay_cs = 0U;
            if ((data[offset + 8U] & 0x40U) != 0U) {
                info->property_flags |= IMAGE_PROPERTY_INTERLACED;
            }
            offset += 9U;
            if (offset > size) {
                break;
            }
            if ((data[offset - 1U] & 0x80U) != 0U) {
                offset += (size_t)3U << ((data[offset - 1U] & 0x07U) + 1U);
            }
            if (offset >= size) {
                break;
            }
            offset += 1U;
            while (offset < size) {
                unsigned int sub_size = data[offset++];
                if (sub_size == 0U) {
                    break;
                }
                if ((size_t)sub_size > size - offset) {
                    offset = size;
                    break;
                }
                offset += sub_size;
            }
        } else if (block == 0x21U) {
            unsigned char label;
            if (offset >= size) {
                break;
            }
            label = data[offset++];
            if (label == 0xf9U && offset + 5U <= size && data[offset] == 4U) {
                if ((data[offset + 1U] & 0x01U) != 0U) {
                    info->property_flags |= IMAGE_PROPERTY_ALPHA;
                }
                pending_delay_cs = image_read_u16_le(data + offset + 2U);
            } else if (label == 0xffU && offset + 12U <= size && data[offset] == 11U &&
                       image_bytes_equal((const unsigned char *)(data + offset + 1U), "NETSCAPE2.0", 11U)) {
                info->property_flags |= IMAGE_PROPERTY_LOOPING;
                if (offset + 16U <= size && data[offset + 12U] == 3U && data[offset + 13U] == 1U) {
                    image_set_loop_count(info, image_read_u16_le(data + offset + 14U));
                }
            }
            while (offset < size) {
                unsigned int sub_size = data[offset++];
                if (sub_size == 0U) {
                    break;
                }
                if ((size_t)sub_size > size - offset) {
                    offset = size;
                    break;
                }
                offset += sub_size;
            }
        } else if (block == 0x3bU) {
            break;
        } else {
            break;
        }
    }
    image_set_frames(info, frames);
    if (has_duration) {
        image_set_duration_ms(info, duration_ms);
    }
    return 1;
}
