#include "image_internal.h"

unsigned int image_read_u16_le(const unsigned char *bytes) {
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8);
}

unsigned int image_read_u16_be(const unsigned char *bytes) {
    return ((unsigned int)bytes[0] << 8) | (unsigned int)bytes[1];
}

unsigned int image_read_u24_le(const unsigned char *bytes) {
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8) | ((unsigned int)bytes[2] << 16);
}

unsigned int image_read_u32_le(const unsigned char *bytes) {
    return (unsigned int)bytes[0] |
           ((unsigned int)bytes[1] << 8) |
           ((unsigned int)bytes[2] << 16) |
           ((unsigned int)bytes[3] << 24);
}

unsigned int image_read_u32_be(const unsigned char *bytes) {
    return ((unsigned int)bytes[0] << 24) |
           ((unsigned int)bytes[1] << 16) |
           ((unsigned int)bytes[2] << 8) |
           (unsigned int)bytes[3];
}

int image_bytes_equal(const unsigned char *bytes, const char *text, size_t length) {
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != (unsigned char)text[index]) {
            return 0;
        }
    }
    return 1;
}

int image_byte_arrays_equal(const unsigned char *left, const unsigned char *right, size_t length) {
    size_t index;

    for (index = 0; index < length; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

void image_info_init(ImageInfo *info) {
    if (info == 0) {
        return;
    }
    info->format = IMAGE_FORMAT_UNKNOWN;
    info->flags = 0U;
    info->width = 0U;
    info->height = 0U;
    info->bit_depth = 0U;
    info->channel_count = 0U;
    info->frame_count = 0U;
    info->orientation = 0U;
    info->density_x = 0U;
    info->density_y = 0U;
    info->duration_ms = 0U;
    info->loop_count = 0U;
    info->property_flags = 0U;
    info->variant = 0;
    info->color_model = 0;
    info->compression = 0;
    info->density_unit = 0;
    image_c2pa_info_init(&info->c2pa);
}

void image_set_dimensions(ImageInfo *info, unsigned int width, unsigned int height) {
    if (width != 0U && height != 0U) {
        info->width = width;
        info->height = height;
        info->flags |= IMAGE_INFO_HAS_DIMENSIONS;
    }
}

void image_set_bit_depth(ImageInfo *info, unsigned int bit_depth) {
    if (bit_depth != 0U) {
        info->bit_depth = bit_depth;
        info->flags |= IMAGE_INFO_HAS_BIT_DEPTH;
    }
}

void image_set_channels(ImageInfo *info, unsigned int channel_count) {
    if (channel_count != 0U) {
        info->channel_count = channel_count;
        info->flags |= IMAGE_INFO_HAS_CHANNELS;
    }
}

void image_set_frames(ImageInfo *info, unsigned int frame_count) {
    if (frame_count != 0U) {
        info->frame_count = frame_count;
        info->flags |= IMAGE_INFO_HAS_FRAMES;
        if (frame_count > 1U) {
            info->property_flags |= IMAGE_PROPERTY_ANIMATED;
        }
    }
}

void image_set_duration_ms(ImageInfo *info, unsigned int duration_ms) {
    if (duration_ms != 0U) {
        info->duration_ms = duration_ms;
        info->flags |= IMAGE_INFO_HAS_DURATION_MS;
    }
}

void image_set_loop_count(ImageInfo *info, unsigned int loop_count) {
    info->loop_count = loop_count;
    info->flags |= IMAGE_INFO_HAS_LOOP_COUNT;
    if (loop_count == 0U) {
        info->property_flags |= IMAGE_PROPERTY_LOOPING;
    }
}

void image_set_variant(ImageInfo *info, const char *variant) {
    if (variant != 0 && variant[0] != '\0') {
        info->variant = variant;
        info->flags |= IMAGE_INFO_HAS_VARIANT;
    }
}

void image_set_color_model(ImageInfo *info, const char *color_model) {
    if (color_model != 0 && color_model[0] != '\0') {
        info->color_model = color_model;
        info->flags |= IMAGE_INFO_HAS_COLOR;
    }
}

void image_set_compression(ImageInfo *info, const char *compression) {
    if (compression != 0 && compression[0] != '\0') {
        info->compression = compression;
        info->flags |= IMAGE_INFO_HAS_COMPRESSION;
    }
}

void image_set_orientation(ImageInfo *info, unsigned int orientation) {
    if (orientation >= 1U && orientation <= 8U) {
        info->orientation = orientation;
        info->flags |= IMAGE_INFO_HAS_ORIENTATION;
        if (orientation != 1U) {
            info->property_flags |= IMAGE_PROPERTY_ORIENTATION;
        }
    }
}

void image_set_density(ImageInfo *info, unsigned int density_x, unsigned int density_y, const char *unit) {
    if (density_x != 0U && density_y != 0U && unit != 0) {
        info->density_x = density_x;
        info->density_y = density_y;
        info->density_unit = unit;
        info->flags |= IMAGE_INFO_HAS_DENSITY;
    }
}

void image_set_c2pa(ImageInfo *info, const ImageC2paInfo *c2pa) {
    if (info != 0 && c2pa != 0 && c2pa->present) {
        info->c2pa = *c2pa;
        info->flags |= IMAGE_INFO_HAS_C2PA;
        info->property_flags |= IMAGE_PROPERTY_C2PA;
    }
}

int image_probe_ex(const unsigned char *data, size_t size, const ImageProbeOptions *options, ImageInfo *info_out) {
    ImageInfo info;

    if (data == 0 || info_out == 0) {
        return -1;
    }
    image_info_init(&info);

    if (image_probe_png(data, size, &info) ||
        image_probe_jpeg(data, size, &info) ||
        image_probe_gif(data, size, &info) ||
        image_probe_tiff(data, size, &info) ||
        image_probe_webp(data, size, &info) ||
        image_probe_bmp(data, size, &info)) {
        ImageC2paInfo c2pa;
        ImageC2paOptions c2pa_options;

        c2pa_options.trust_validation = options != 0 && options->c2pa_trust_validation;
        if (image_c2pa_analyze_ex(data, size, &c2pa_options, &c2pa) == 0) {
            image_set_c2pa(&info, &c2pa);
        }
        *info_out = info;
        return 0;
    }

    *info_out = info;
    return -1;
}

int image_probe(const unsigned char *data, size_t size, ImageInfo *info_out) {
    return image_probe_ex(data, size, 0, info_out);
}

int image_validate_ex(const unsigned char *data, size_t size, const ImageValidationOptions *options, ImageValidation *validation_out) {
    ImageInfo info;

    if (validation_out != 0) {
        validation_out->format = IMAGE_FORMAT_UNKNOWN;
        validation_out->valid = 0;
        validation_out->has_failure_offset = 0;
        validation_out->failure_offset = 0U;
        validation_out->message = "unsupported image format";
    }
    if (data == 0) {
        return -1;
    }
    if (image_validate_png(data, size, options, validation_out) == 0) {
        return 0;
    }
    if (validation_out != 0 && validation_out->format == IMAGE_FORMAT_PNG) {
        return -1;
    }
    if (image_validate_jpeg(data, size, validation_out) == 0) {
        return 0;
    }
    if (validation_out != 0 && validation_out->format == IMAGE_FORMAT_JPEG) {
        return -1;
    }
    if (image_validate_gif(data, size, validation_out) == 0) {
        return 0;
    }
    if (validation_out != 0 && validation_out->format == IMAGE_FORMAT_GIF) {
        return -1;
    }
    if (image_validate_bmp(data, size, validation_out) == 0) {
        return 0;
    }
    if (validation_out != 0 && validation_out->format == IMAGE_FORMAT_BMP) {
        return -1;
    }
    if (image_validate_tiff(data, size, validation_out) == 0) {
        return 0;
    }
    if (validation_out != 0 && validation_out->format == IMAGE_FORMAT_TIFF) {
        return -1;
    }
    if (image_validate_webp(data, size, validation_out) == 0) {
        return 0;
    }
    if (validation_out != 0 && validation_out->format == IMAGE_FORMAT_WEBP) {
        return -1;
    }
    if (image_probe(data, size, &info) == 0) {
        if (validation_out != 0) {
            validation_out->format = info.format;
            validation_out->valid = 1;
            validation_out->has_failure_offset = 0;
            validation_out->failure_offset = 0U;
            validation_out->message = "recognized; deep validation not implemented";
        }
        return 0;
    }
    if (validation_out != 0) {
        validation_out->format = IMAGE_FORMAT_UNKNOWN;
        validation_out->valid = 0;
        validation_out->has_failure_offset = 0;
        validation_out->failure_offset = 0U;
        validation_out->message = "unsupported image format";
    }
    return -1;
}

int image_validate(const unsigned char *data, size_t size, ImageValidation *validation_out) {
    return image_validate_ex(data, size, 0, validation_out);
}

const char *image_format_name(ImageFormat format) {
    switch (format) {
        case IMAGE_FORMAT_PNG: return "PNG";
        case IMAGE_FORMAT_JPEG: return "JPEG";
        case IMAGE_FORMAT_GIF: return "GIF";
        case IMAGE_FORMAT_TIFF: return "TIFF";
        case IMAGE_FORMAT_WEBP: return "WebP";
        case IMAGE_FORMAT_BMP: return "BMP";
        default: return "unknown";
    }
}

const char *image_format_extension(ImageFormat format) {
    switch (format) {
        case IMAGE_FORMAT_PNG: return "png";
        case IMAGE_FORMAT_JPEG: return "jpeg";
        case IMAGE_FORMAT_GIF: return "gif";
        case IMAGE_FORMAT_TIFF: return "tiff";
        case IMAGE_FORMAT_WEBP: return "webp";
        case IMAGE_FORMAT_BMP: return "bmp";
        default: return "unknown";
    }
}

const char *image_format_mime(ImageFormat format) {
    switch (format) {
        case IMAGE_FORMAT_PNG: return "image/png";
        case IMAGE_FORMAT_JPEG: return "image/jpeg";
        case IMAGE_FORMAT_GIF: return "image/gif";
        case IMAGE_FORMAT_TIFF: return "image/tiff";
        case IMAGE_FORMAT_WEBP: return "image/webp";
        case IMAGE_FORMAT_BMP: return "image/bmp";
        default: return "application/octet-stream";
    }
}

const char *image_channel_description(const ImageInfo *info) {
    if (info == 0 || (info->flags & IMAGE_INFO_HAS_CHANNELS) == 0U) {
        return "unknown";
    }
    if (info->format == IMAGE_FORMAT_PNG && info->channel_count == 1U) {
        return "gray/indexed";
    }
    if (info->channel_count == 1U) {
        return "gray";
    }
    if (info->channel_count == 2U) {
        return "gray-alpha";
    }
    if (info->channel_count == 3U) {
        return "rgb";
    }
    if (info->channel_count == 4U) {
        return "rgba";
    }
    return "multi-channel";
}

const char *image_property_name(unsigned int property) {
    switch (property) {
        case IMAGE_PROPERTY_ALPHA: return "alpha";
        case IMAGE_PROPERTY_PALETTE: return "palette";
        case IMAGE_PROPERTY_INTERLACED: return "interlaced";
        case IMAGE_PROPERTY_ANIMATED: return "animated";
        case IMAGE_PROPERTY_PROGRESSIVE: return "progressive";
        case IMAGE_PROPERTY_LOSSLESS: return "lossless";
        case IMAGE_PROPERTY_EXIF: return "exif";
        case IMAGE_PROPERTY_ICC: return "icc-profile";
        case IMAGE_PROPERTY_XMP: return "xmp";
        case IMAGE_PROPERTY_TOP_DOWN: return "top-down";
        case IMAGE_PROPERTY_LOOPING: return "looping";
        case IMAGE_PROPERTY_ORIENTATION: return "orientation";
        case IMAGE_PROPERTY_C2PA: return "c2pa";
        default: return 0;
    }
}

const char *image_orientation_description(unsigned int orientation) {
    switch (orientation) {
        case 1U: return "normal";
        case 2U: return "mirrored horizontal";
        case 3U: return "rotated 180";
        case 4U: return "mirrored vertical";
        case 5U: return "mirrored horizontal then rotated 270";
        case 6U: return "rotated 90 clockwise";
        case 7U: return "mirrored horizontal then rotated 90";
        case 8U: return "rotated 270 clockwise";
        default: return "unknown";
    }
}
