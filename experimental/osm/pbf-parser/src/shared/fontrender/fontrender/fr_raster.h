#ifndef FONTRENDER_FR_RASTER_H
#define FONTRENDER_FR_RASTER_H

#include <stdint.h>

#include "fr_outline.h"

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
    int stride;
    int left;
    int top;
} FrBitmap;

void fr_bitmap_init(FrBitmap *bitmap);
void fr_bitmap_free(FrBitmap *bitmap);
int fr_bitmap_alloc(FrBitmap *bitmap, int width, int height);
int fr_raster_outline_box(const FrOutline *outline, int *out_left, int *out_top, int *out_width, int *out_height);
int fr_raster_render(FrBitmap *bitmap, const FrOutline *outline);

#endif
