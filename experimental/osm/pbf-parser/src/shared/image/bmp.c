#include "image_internal.h"

int image_probe_bmp(const unsigned char *data, size_t size, ImageInfo *info) {
    unsigned int dib_size;
    unsigned int width;
    unsigned int height;
    unsigned int bits;

    if (size < 26U || data[0] != 'B' || data[1] != 'M') {
        return 0;
    }

    info->format = IMAGE_FORMAT_BMP;
    dib_size = image_read_u32_le(data + 14U);
    if (dib_size == 12U && size >= 26U) {
        image_set_variant(info, "OS/2 bitmap core header");
        image_set_compression(info, "uncompressed");
        width = image_read_u16_le(data + 18U);
        height = image_read_u16_le(data + 20U);
        bits = image_read_u16_le(data + 24U);
        image_set_dimensions(info, width, height);
        image_set_bit_depth(info, bits);
        if (bits == 24U) {
            image_set_channels(info, 3U);
            image_set_color_model(info, "rgb");
        } else if (bits == 8U || bits == 4U || bits == 1U) {
            image_set_channels(info, 1U);
            image_set_color_model(info, "indexed-color");
            info->property_flags |= IMAGE_PROPERTY_PALETTE;
        }
    } else if (dib_size >= 40U && size >= 30U) {
        unsigned int compression = size >= 34U ? image_read_u32_le(data + 30U) : 0U;
        image_set_variant(info, dib_size == 40U ? "Windows bitmap info header" : "extended Windows bitmap header");
        width = image_read_u32_le(data + 18U);
        height = image_read_u32_le(data + 22U);
        if ((height & 0x80000000U) != 0U) {
            height = (unsigned int)(0U - height);
            info->property_flags |= IMAGE_PROPERTY_TOP_DOWN;
        }
        bits = image_read_u16_le(data + 28U);
        image_set_dimensions(info, width, height);
        image_set_bit_depth(info, bits);
        if (compression == 0U) {
            image_set_compression(info, "uncompressed");
        } else if (compression == 1U) {
            image_set_compression(info, "RLE8");
        } else if (compression == 2U) {
            image_set_compression(info, "RLE4");
        } else if (compression == 3U) {
            image_set_compression(info, "bitfields");
        } else if (compression == 4U) {
            image_set_compression(info, "JPEG");
        } else if (compression == 5U) {
            image_set_compression(info, "PNG");
        } else {
            image_set_compression(info, "other");
        }
        if (bits == 32U) {
            image_set_channels(info, 4U);
            image_set_color_model(info, "rgba");
            info->property_flags |= IMAGE_PROPERTY_ALPHA;
        } else if (bits == 24U) {
            image_set_channels(info, 3U);
            image_set_color_model(info, "rgb");
        } else if (bits == 8U || bits == 4U || bits == 1U) {
            image_set_channels(info, 1U);
            image_set_color_model(info, "indexed-color");
            info->property_flags |= IMAGE_PROPERTY_PALETTE;
        }
    }
    return 1;
}
