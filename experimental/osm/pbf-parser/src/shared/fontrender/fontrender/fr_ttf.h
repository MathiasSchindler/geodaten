#ifndef FONTRENDER_FR_TTF_H
#define FONTRENDER_FR_TTF_H

#include <stddef.h>
#include <stdint.h>

typedef struct FrTtfFont FrTtfFont;

typedef enum {
    FR_TTF_OK = 0,
    FR_TTF_ERR_INVALID_ARGUMENT = -1,
    FR_TTF_ERR_IO = -2,
    FR_TTF_ERR_NO_MEMORY = -3,
    FR_TTF_ERR_UNSUPPORTED_SFNT = -4,
    FR_TTF_ERR_MALFORMED = -5,
    FR_TTF_ERR_MISSING_TABLE = -6,
    FR_TTF_ERR_UNSUPPORTED_CMAP = -7,
    FR_TTF_ERR_INVALID_GLYPH = -8,
    FR_TTF_ERR_NOT_FOUND = -9
} FrTtfResult;

typedef struct {
    uint16_t units_per_em;
    int16_t ascender;
    int16_t descender;
    int16_t line_gap;
    int16_t index_to_loc_format;
    uint16_t num_glyphs;
    uint16_t number_of_h_metrics;
} FrTtfInfo;

typedef struct {
    uint16_t advance_width;
    int16_t left_side_bearing;
} FrTtfHMetric;

typedef struct {
    uint16_t glyph_index;
    int16_t number_of_contours;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    const uint8_t *data;
    size_t data_length;
} FrTtfGlyph;

FrTtfResult fr_ttf_open(FrTtfFont **out_font, const char *path);
FrTtfResult fr_ttf_open_memory(FrTtfFont **out_font, const void *data, size_t size);
void fr_ttf_close(FrTtfFont *font);

const char *fr_ttf_result_string(FrTtfResult result);

FrTtfResult fr_ttf_get_info(const FrTtfFont *font, FrTtfInfo *out_info);
FrTtfResult fr_ttf_lookup_glyph(const FrTtfFont *font, uint32_t codepoint, uint16_t *out_glyph_index);
FrTtfResult fr_ttf_get_hmetric(const FrTtfFont *font, uint16_t glyph_index, FrTtfHMetric *out_metric);
FrTtfResult fr_ttf_get_glyph(const FrTtfFont *font, uint16_t glyph_index, FrTtfGlyph *out_glyph);

#endif
