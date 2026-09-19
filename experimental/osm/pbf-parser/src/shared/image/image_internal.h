#ifndef NEWOS_IMAGE_INTERNAL_H
#define NEWOS_IMAGE_INTERNAL_H

#include "image/image.h"

unsigned int image_read_u16_le(const unsigned char *bytes);
unsigned int image_read_u16_be(const unsigned char *bytes);
unsigned int image_read_u24_le(const unsigned char *bytes);
unsigned int image_read_u32_le(const unsigned char *bytes);
unsigned int image_read_u32_be(const unsigned char *bytes);
int image_bytes_equal(const unsigned char *bytes, const char *text, size_t length);
int image_byte_arrays_equal(const unsigned char *left, const unsigned char *right, size_t length);

void image_set_dimensions(ImageInfo *info, unsigned int width, unsigned int height);
void image_set_bit_depth(ImageInfo *info, unsigned int bit_depth);
void image_set_channels(ImageInfo *info, unsigned int channel_count);
void image_set_frames(ImageInfo *info, unsigned int frame_count);
void image_set_duration_ms(ImageInfo *info, unsigned int duration_ms);
void image_set_loop_count(ImageInfo *info, unsigned int loop_count);
void image_set_variant(ImageInfo *info, const char *variant);
void image_set_color_model(ImageInfo *info, const char *color_model);
void image_set_compression(ImageInfo *info, const char *compression);
void image_set_orientation(ImageInfo *info, unsigned int orientation);
void image_set_density(ImageInfo *info, unsigned int density_x, unsigned int density_y, const char *unit);
void image_set_c2pa(ImageInfo *info, const ImageC2paInfo *c2pa);

int image_probe_png(const unsigned char *data, size_t size, ImageInfo *info);
int image_probe_gif(const unsigned char *data, size_t size, ImageInfo *info);
int image_probe_jpeg(const unsigned char *data, size_t size, ImageInfo *info);
int image_probe_tiff(const unsigned char *data, size_t size, ImageInfo *info);
int image_probe_webp(const unsigned char *data, size_t size, ImageInfo *info);
int image_probe_bmp(const unsigned char *data, size_t size, ImageInfo *info);

int image_tiff_extract_metadata(const unsigned char *data,
                                size_t size,
                                unsigned int *width_out,
                                unsigned int *height_out,
                                unsigned int *bit_depth_out,
                                unsigned int *channels_out,
                                unsigned int *compression_out,
                                unsigned int *photometric_out,
                                int *has_photometric_out,
                                unsigned int *orientation_out);

int image_validate_png(const unsigned char *data, size_t size, const ImageValidationOptions *options, ImageValidation *validation);
int image_validate_jpeg(const unsigned char *data, size_t size, ImageValidation *validation);
int image_validate_gif(const unsigned char *data, size_t size, ImageValidation *validation);
int image_validate_bmp(const unsigned char *data, size_t size, ImageValidation *validation);
int image_validate_tiff(const unsigned char *data, size_t size, ImageValidation *validation);
int image_validate_webp(const unsigned char *data, size_t size, ImageValidation *validation);

#endif
