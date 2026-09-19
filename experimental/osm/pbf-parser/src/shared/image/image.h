#ifndef NEWOS_IMAGE_H
#define NEWOS_IMAGE_H

#include <stddef.h>

#define IMAGE_INFO_HAS_DIMENSIONS (1U << 0)
#define IMAGE_INFO_HAS_BIT_DEPTH  (1U << 1)
#define IMAGE_INFO_HAS_CHANNELS   (1U << 2)
#define IMAGE_INFO_HAS_FRAMES     (1U << 3)
#define IMAGE_INFO_HAS_VARIANT    (1U << 4)
#define IMAGE_INFO_HAS_COLOR      (1U << 5)
#define IMAGE_INFO_HAS_COMPRESSION (1U << 6)
#define IMAGE_INFO_HAS_ORIENTATION (1U << 7)
#define IMAGE_INFO_HAS_DENSITY    (1U << 8)
#define IMAGE_INFO_HAS_DURATION_MS (1U << 9)
#define IMAGE_INFO_HAS_LOOP_COUNT (1U << 10)
#define IMAGE_INFO_HAS_C2PA       (1U << 11)

#define IMAGE_PROPERTY_ALPHA      (1U << 0)
#define IMAGE_PROPERTY_PALETTE    (1U << 1)
#define IMAGE_PROPERTY_INTERLACED (1U << 2)
#define IMAGE_PROPERTY_ANIMATED   (1U << 3)
#define IMAGE_PROPERTY_PROGRESSIVE (1U << 4)
#define IMAGE_PROPERTY_LOSSLESS   (1U << 5)
#define IMAGE_PROPERTY_EXIF       (1U << 6)
#define IMAGE_PROPERTY_ICC        (1U << 7)
#define IMAGE_PROPERTY_XMP        (1U << 8)
#define IMAGE_PROPERTY_TOP_DOWN   (1U << 9)
#define IMAGE_PROPERTY_LOOPING    (1U << 10)
#define IMAGE_PROPERTY_ORIENTATION (1U << 11)
#define IMAGE_PROPERTY_C2PA       (1U << 12)

typedef enum {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_GIF,
    IMAGE_FORMAT_TIFF,
    IMAGE_FORMAT_WEBP,
    IMAGE_FORMAT_BMP
} ImageFormat;

typedef struct {
    int present;
    int has_manifest_store;
    int jumbf_valid;
    int cbor_valid;
    int cose_valid;
    int content_hash_checked;
    int content_hash_matched;
    int content_hash_mismatched;
    int signature_checked;
    int signature_supported;
    int signature_valid;
    int trust_checked;
    int trust_supported;
    int trust_valid;
    const char *carrier;
    const char *signature_algorithm;
    unsigned int carrier_count;
    unsigned int recognized_carrier_count;
    unsigned int box_count;
    unsigned int invalid_box_count;
    unsigned int jumbf_box_count;
    unsigned int cbor_box_count;
    unsigned int cbor_valid_count;
    unsigned int manifest_count;
    unsigned int claim_count;
    unsigned int assertion_store_count;
    unsigned int assertion_count;
    unsigned int signature_count;
    unsigned int cose_signature_count;
    unsigned int signature_verified_count;
    unsigned int signature_invalid_count;
    unsigned int x509_cert_count;
    unsigned int x509_parseable_cert_count;
    unsigned int validation_failure_count;
    unsigned int ingredient_count;
    const char *status;
} ImageC2paInfo;

typedef struct {
    int trust_validation;
} ImageC2paOptions;

typedef struct {
    int c2pa_trust_validation;
} ImageProbeOptions;

typedef struct {
    ImageFormat format;
    unsigned int flags;
    unsigned int width;
    unsigned int height;
    unsigned int bit_depth;
    unsigned int channel_count;
    unsigned int frame_count;
    unsigned int orientation;
    unsigned int density_x;
    unsigned int density_y;
    unsigned int duration_ms;
    unsigned int loop_count;
    unsigned int property_flags;
    const char *variant;
    const char *color_model;
    const char *compression;
    const char *density_unit;
    ImageC2paInfo c2pa;
} ImageInfo;

typedef struct {
    ImageFormat format;
    int valid;
    int has_failure_offset;
    size_t failure_offset;
    const char *message;
} ImageValidation;

typedef struct {
    int strict;
} ImageValidationOptions;

void image_info_init(ImageInfo *info);
void image_c2pa_info_init(ImageC2paInfo *info);
int image_c2pa_analyze(const unsigned char *data, size_t size, ImageC2paInfo *info);
int image_c2pa_analyze_ex(const unsigned char *data, size_t size, const ImageC2paOptions *options, ImageC2paInfo *info);
int image_probe(const unsigned char *data, size_t size, ImageInfo *info_out);
int image_probe_ex(const unsigned char *data, size_t size, const ImageProbeOptions *options, ImageInfo *info_out);
int image_validate(const unsigned char *data, size_t size, ImageValidation *validation_out);
int image_validate_ex(const unsigned char *data, size_t size, const ImageValidationOptions *options, ImageValidation *validation_out);
const char *image_format_name(ImageFormat format);
const char *image_format_extension(ImageFormat format);
const char *image_format_mime(ImageFormat format);
const char *image_channel_description(const ImageInfo *info);
const char *image_property_name(unsigned int property);
const char *image_orientation_description(unsigned int orientation);

#endif
