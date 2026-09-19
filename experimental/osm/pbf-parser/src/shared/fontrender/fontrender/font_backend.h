#ifndef FONTRENDER_FONT_BACKEND_H
#define FONTRENDER_FONT_BACKEND_H

#include <stddef.h>
#include <stdint.h>

typedef struct FrFont FrFont;

typedef struct {
    uint32_t codepoint;
    uint16_t pixel_size;
    uint16_t style;
    int advance;
    int left;
    int top;
    int width;
    int height;
    const unsigned char *bitmap;
} FrGlyph;

int fr_font_open(FrFont **out_font, const char *font_path);
int fr_font_open_memory(FrFont **out_font, const void *data, size_t size);
void fr_font_close(FrFont *font);
const FrGlyph *fr_font_get_glyph(FrFont *font, uint32_t codepoint, int pixel_size, uint16_t style);
int fr_font_prefetch_glyph(FrFont *font, uint32_t codepoint, int pixel_size, uint16_t style);
int fr_font_prefetch_codepoints(FrFont *font, const uint32_t *codepoints, size_t codepoint_count,
                                int pixel_size, uint16_t style);
int fr_font_line_height(FrFont *font, int pixel_size);

#endif
