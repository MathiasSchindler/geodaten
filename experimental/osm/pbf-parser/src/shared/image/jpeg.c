#include "image_internal.h"

static int is_jpeg_sof(unsigned char marker) {
    return (marker >= 0xc0U && marker <= 0xc3U) ||
           (marker >= 0xc5U && marker <= 0xc7U) ||
           (marker >= 0xc9U && marker <= 0xcbU) ||
           (marker >= 0xcdU && marker <= 0xcfU);
}

int image_probe_jpeg(const unsigned char *data, size_t size, ImageInfo *info) {
    size_t offset = 2U;

    if (size < 3U || data[0] != 0xffU || data[1] != 0xd8U || data[2] != 0xffU) {
        return 0;
    }

    info->format = IMAGE_FORMAT_JPEG;
    image_set_compression(info, "JPEG DCT");
    while (offset + 3U < size) {
        unsigned char marker;
        unsigned int segment_size;

        while (offset < size && data[offset] != 0xffU) {
            offset += 1U;
        }
        while (offset < size && data[offset] == 0xffU) {
            offset += 1U;
        }
        if (offset >= size) {
            break;
        }
        marker = data[offset++];
        if (marker == 0xd9U || marker == 0xdaU) {
            break;
        }
        if (marker == 0x01U || (marker >= 0xd0U && marker <= 0xd7U)) {
            continue;
        }
        if (offset + 2U > size) {
            break;
        }
        segment_size = image_read_u16_be(data + offset);
        if (segment_size < 2U) {
            break;
        }
        if (is_jpeg_sof(marker)) {
            if (offset + 8U <= size) {
                image_set_bit_depth(info, data[offset + 2U]);
                image_set_dimensions(info, image_read_u16_be(data + offset + 5U), image_read_u16_be(data + offset + 3U));
                image_set_channels(info, data[offset + 7U]);
                if (marker == 0xc0U) {
                    image_set_variant(info, "baseline DCT");
                } else if (marker == 0xc2U) {
                    image_set_variant(info, "progressive DCT");
                    info->property_flags |= IMAGE_PROPERTY_PROGRESSIVE;
                } else if (marker == 0xc1U) {
                    image_set_variant(info, "extended sequential DCT");
                } else if (marker == 0xc3U) {
                    image_set_variant(info, "lossless Huffman");
                    info->property_flags |= IMAGE_PROPERTY_LOSSLESS;
                } else {
                    image_set_variant(info, "JPEG");
                }
                if (data[offset + 7U] == 1U) {
                    image_set_color_model(info, "grayscale");
                } else if (data[offset + 7U] == 3U) {
                    image_set_color_model(info, "YCbCr/RGB");
                } else if (data[offset + 7U] == 4U) {
                    image_set_color_model(info, "CMYK/YCCK");
                }
            }
            break;
        }
        if (marker == 0xe1U && offset + 8U <= size) {
            if (image_bytes_equal(data + offset + 2U, "Exif", 4U)) {
                unsigned int orientation = 0U;
                info->property_flags |= IMAGE_PROPERTY_EXIF;
                if (offset + 8U < size) {
                    image_tiff_extract_metadata(data + offset + 8U, size - offset - 8U,
                                                0, 0, 0, 0, 0, 0, 0, &orientation);
                    image_set_orientation(info, orientation);
                }
            } else if (segment_size >= 31U && offset + 31U <= size &&
                       image_bytes_equal(data + offset + 2U, "http://ns.adobe.com/xap/1.0/", 29U)) {
                info->property_flags |= IMAGE_PROPERTY_XMP;
            }
        } else if (marker == 0xe2U && offset + 13U <= size && image_bytes_equal(data + offset + 2U, "ICC_PROFILE", 11U)) {
            info->property_flags |= IMAGE_PROPERTY_ICC;
        } else if (marker == 0xe0U && segment_size >= 16U && offset + 16U <= size &&
                   image_bytes_equal(data + offset + 2U, "JFIF", 4U) && data[offset + 6U] == 0U) {
            unsigned char units = data[offset + 9U];
            const char *unit_name = 0;

            if (units == 1U) {
                unit_name = "dpi";
            } else if (units == 2U) {
                unit_name = "dpcm";
            } else if (units == 0U) {
                unit_name = "pixel-aspect";
            }
            image_set_density(info, image_read_u16_be(data + offset + 10U), image_read_u16_be(data + offset + 12U), unit_name);
        }
        if ((size_t)segment_size > size - offset) {
            break;
        }
        offset += (size_t)segment_size;
    }
    return 1;
}
