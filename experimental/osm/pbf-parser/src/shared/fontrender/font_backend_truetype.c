#include "fontrender/font_backend.h"

#include <stdbool.h>
#include <stdint.h>

#include "fr_platform_internal.h"
#include "fontrender/fr_raster.h"
#include "fontrender/fr_ttf.h"

enum {
    FR_GLYPH_CACHE_CAP = 4096u,
    FR_GLYPH_CACHE_MASK = FR_GLYPH_CACHE_CAP - 1u,
    FR_OUTLINE_CACHE_CAP = 512u,
    FR_OUTLINE_CACHE_MASK = FR_OUTLINE_CACHE_CAP - 1u,
    FR_LOOKUP_CACHE_CAP = 1024u,
    FR_LOOKUP_CACHE_MASK = FR_LOOKUP_CACHE_CAP - 1u,
    FR_COMPOUND_RECURSION_LIMIT = 32u,

    FR_SIMPLE_FLAG_ON_CURVE = 0x01u,
    FR_SIMPLE_FLAG_X_SHORT = 0x02u,
    FR_SIMPLE_FLAG_Y_SHORT = 0x04u,
    FR_SIMPLE_FLAG_REPEAT = 0x08u,
    FR_SIMPLE_FLAG_X_SAME = 0x10u,
    FR_SIMPLE_FLAG_Y_SAME = 0x20u,
    FR_SIMPLE_FLAG_OVERLAP = 0x40u,

    FR_COMPOUND_ARG_WORDS = 0x0001u,
    FR_COMPOUND_ARGS_ARE_XY = 0x0002u,
    FR_COMPOUND_ROUND_XY = 0x0004u,
    FR_COMPOUND_HAVE_SCALE = 0x0008u,
    FR_COMPOUND_MORE_COMPONENTS = 0x0020u,
    FR_COMPOUND_HAVE_XY_SCALE = 0x0040u,
    FR_COMPOUND_HAVE_2X2 = 0x0080u,
    FR_COMPOUND_HAVE_INSTRUCTIONS = 0x0100u,
    FR_COMPOUND_USE_MY_METRICS = 0x0200u,
    FR_COMPOUND_OVERLAP = 0x0400u,
    FR_COMPOUND_SCALED_OFFSET = 0x0800u,
    FR_COMPOUND_UNSCALED_OFFSET = 0x1000u
};

_Static_assert((FR_GLYPH_CACHE_CAP & FR_GLYPH_CACHE_MASK) == 0u,
               "FR_GLYPH_CACHE_CAP must be a power of two");
_Static_assert((FR_OUTLINE_CACHE_CAP & FR_OUTLINE_CACHE_MASK) == 0u,
               "FR_OUTLINE_CACHE_CAP must be a power of two");
_Static_assert((FR_LOOKUP_CACHE_CAP & FR_LOOKUP_CACHE_MASK) == 0u,
               "FR_LOOKUP_CACHE_CAP must be a power of two");

typedef struct {
    int64_t multiplier;
    int64_t divisor;
} FrScale;

typedef struct {
    uint8_t flag;
    int32_t x;
    int32_t y;
} FrSimpleDecodePoint;

typedef struct {
    uint32_t requested_codepoint;
    FrGlyph glyph;
    unsigned char *bitmap_storage;
    bool occupied;
} FrGlyphCacheEntry;

typedef struct {
    uint16_t glyph_index;
    FrOutline outline;
    bool occupied;
} FrOutlineCacheEntry;

typedef struct {
    uint32_t requested_codepoint;
    uint32_t render_codepoint;
    uint16_t glyph_index;
    bool occupied;
} FrLookupCacheEntry;

struct FrFont {
    FrPlatform platform;
    void *mutex;
    FrTtfFont *ttf;
    FrTtfInfo info;
    uint16_t fallback_glyph_index;
    bool have_fallback_glyph;
    FrSimpleDecodePoint *simple_points;
    size_t simple_buffer_capacity;
    FrOutline scratch_outlines[FR_COMPOUND_RECURSION_LIMIT + 1u];
    FrGlyphCacheEntry entries[FR_GLYPH_CACHE_CAP];
    FrOutlineCacheEntry outline_entries[FR_OUTLINE_CACHE_CAP];
    FrLookupCacheEntry lookup_entries[FR_LOOKUP_CACHE_CAP];
};

#define FR_FONT_LOG(font, ...) \
    fr_platform_log_with(&(font)->platform, FR_PLATFORM_LOG_ERROR, "font_backend_truetype", __VA_ARGS__)

static void fr_font_lock(FrFont *font) {
    if (font != NULL && font->mutex != NULL && font->platform.mutex_lock != NULL) {
        font->platform.mutex_lock(font->platform.user_data, font->mutex);
    }
}

static void fr_font_unlock(FrFont *font) {
    if (font != NULL && font->mutex != NULL && font->platform.mutex_unlock != NULL) {
        font->platform.mutex_unlock(font->platform.user_data, font->mutex);
    }
}

static uint16_t fr_read_u16(const uint8_t *ptr) {
    return (uint16_t)(((uint16_t)ptr[0] << 8) | (uint16_t)ptr[1]);
}

static int16_t fr_read_s16(const uint8_t *ptr) {
    return (int16_t)fr_read_u16(ptr);
}

static int64_t fr_round_div_i64(int64_t value, int64_t divisor) {
    if (divisor <= 0) {
        return 0;
    }
    if (value >= 0) {
        return (value + divisor / 2) / divisor;
    }
    return -(((-value) + divisor / 2) / divisor);
}

static int fr_i64_to_i32(int64_t value, int32_t *out_value) {
    if (value < INT32_MIN || value > INT32_MAX) {
        return -1;
    }
    *out_value = (int32_t)value;
    return 0;
}

static int fr_scale_init(FrScale *scale, int pixel_size, uint16_t units_per_em) {
    if (scale == NULL || units_per_em == 0u || pixel_size <= 0) {
        return -1;
    }
    scale->multiplier = (int64_t)pixel_size;
    scale->divisor = (int64_t)units_per_em;
    return 0;
}

static inline int fr_scale_fixed(int32_t value, const FrScale *scale, int32_t *out_value) {
    int64_t scaled;

    if (scale == NULL || out_value == NULL) {
        return -1;
    }
    scaled = fr_round_div_i64((int64_t)value * scale->multiplier, scale->divisor);
    return fr_i64_to_i32(scaled, out_value);
}

static inline int fr_scale_units_round(int32_t value, const FrScale *scale, int *out_value) {
    int64_t scaled;

    if (scale == NULL || out_value == NULL) {
        return -1;
    }
    scaled = fr_round_div_i64((int64_t)value * scale->multiplier, scale->divisor);
    if (scaled < INT32_MIN || scaled > INT32_MAX) {
        return -1;
    }
    *out_value = (int)scaled;
    return 0;
}

static int fr_fixed_mul_f2dot14(int32_t value, int16_t coeff, int32_t *out_value) {
    int64_t product;

    if (out_value == NULL) {
        return -1;
    }
    product = fr_round_div_i64((int64_t)value * (int64_t)coeff, 1ll << 14);
    return fr_i64_to_i32(product, out_value);
}

static size_t fr_glyph_hash(uint32_t codepoint, uint16_t pixel_size, uint16_t style) {
    uint64_t mixed = ((uint64_t)codepoint * 11400714819323198485ull) ^ ((uint64_t)pixel_size << 16) ^ style;
    return (size_t)mixed & FR_GLYPH_CACHE_MASK;
}

static size_t fr_outline_hash(uint16_t glyph_index) {
    uint32_t mixed = (uint32_t)glyph_index * 2654435761u;
    return (size_t)mixed & FR_OUTLINE_CACHE_MASK;
}

static size_t fr_lookup_hash(uint32_t codepoint) {
    uint64_t mixed = ((uint64_t)codepoint * 11400714819323198485ull) ^ (uint64_t)(codepoint >> 16);
    return (size_t)mixed & FR_LOOKUP_CACHE_MASK;
}

static int fr_reserve_simple_decode_buffers(FrFont *font, size_t point_count) {
    size_t new_capacity;
    FrSimpleDecodePoint *grown_points;

    if (font == NULL) {
        return -1;
    }
    if (point_count <= font->simple_buffer_capacity) {
        return 0;
    }
    if (point_count > SIZE_MAX / sizeof(*font->simple_points)) {
        return -1;
    }

    new_capacity = font->simple_buffer_capacity != 0u ? font->simple_buffer_capacity : 256u;
    while (new_capacity < point_count) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = point_count;
            break;
        }
        new_capacity *= 2u;
    }

    grown_points = (FrSimpleDecodePoint *)fr_platform_realloc_with(&font->platform, font->simple_points,
                                                                   new_capacity * sizeof(*font->simple_points));
    if (grown_points == NULL) {
        return -1;
    }
    font->simple_points = grown_points;
    font->simple_buffer_capacity = new_capacity;
    return 0;
}

static int fr_scale_outline(FrOutline *outline, const FrScale *scale) {
    size_t i;

    if (outline == NULL || scale == NULL) {
        return -1;
    }
    if (scale->multiplier == scale->divisor) {
        return 0;
    }
    for (i = 0u; i < outline->point_count; ++i) {
        if (fr_scale_fixed(outline->points[i].x, scale, &outline->points[i].x) != 0 ||
            fr_scale_fixed(outline->points[i].y, scale, &outline->points[i].y) != 0) {
            return -1;
        }
    }
    return 0;
}

static int fr_outline_copy(FrOutline *dst, const FrOutline *src) {
    size_t point_bytes;
    size_t contour_bytes;

    if (dst == NULL || src == NULL) {
        return -1;
    }
    if (dst == src) {
        return 0;
    }
    if (src->point_count == 0u && src->contour_count == 0u) {
        dst->point_count = 0u;
        dst->contour_count = 0u;
        return 0;
    }
    if (fr_outline_reserve(dst, src->point_count, src->contour_count) != 0) {
        return -1;
    }

    point_bytes = src->point_count * sizeof(*src->points);
    contour_bytes = src->contour_count * sizeof(*src->contours);
    if (point_bytes != 0u) {
        fr_platform_memcpy(dst->points, src->points, point_bytes);
    }
    if (contour_bytes != 0u) {
        fr_platform_memcpy(dst->contours, src->contours, contour_bytes);
    }
    dst->point_count = src->point_count;
    dst->contour_count = src->contour_count;
    return 0;
}

static int fr_outline_copy_scaled(FrOutline *dst, const FrOutline *src, const FrScale *scale) {
    size_t contour_bytes;
    size_t i;

    if (dst == NULL || src == NULL || scale == NULL) {
        return -1;
    }
    if (dst == src) {
        return fr_scale_outline(dst, scale);
    }
    if (scale->multiplier == scale->divisor) {
        return fr_outline_copy(dst, src);
    }
    if (src->point_count == 0u && src->contour_count == 0u) {
        dst->point_count = 0u;
        dst->contour_count = 0u;
        return 0;
    }
    if (fr_outline_reserve(dst, src->point_count, src->contour_count) != 0) {
        return -1;
    }

    for (i = 0u; i < src->point_count; ++i) {
        const FrOutlinePoint *src_point = &src->points[i];
        FrOutlinePoint *dst_point = &dst->points[i];

        if (fr_scale_fixed(src_point->x, scale, &dst_point->x) != 0 ||
            fr_scale_fixed(src_point->y, scale, &dst_point->y) != 0) {
            return -1;
        }
        dst_point->flags = src_point->flags;
        dst_point->reserved[0] = src_point->reserved[0];
        dst_point->reserved[1] = src_point->reserved[1];
        dst_point->reserved[2] = src_point->reserved[2];
    }

    contour_bytes = src->contour_count * sizeof(*src->contours);
    if (contour_bytes != 0u) {
        fr_platform_memcpy(dst->contours, src->contours, contour_bytes);
    }
    dst->point_count = src->point_count;
    dst->contour_count = src->contour_count;
    return 0;
}

static FrGlyphCacheEntry *fr_find_glyph_cache_entry(FrFont *font, uint32_t codepoint,
                                                    uint16_t pixel_size, uint16_t style,
                                                    FrGlyphCacheEntry **out_victim) {
    FrGlyphCacheEntry *start;
    FrGlyphCacheEntry *entry;
    FrGlyphCacheEntry *end;

    if (font == NULL || out_victim == NULL) {
        return NULL;
    }

    start = &font->entries[fr_glyph_hash(codepoint, pixel_size, style)];
    entry = start;
    end = font->entries + FR_GLYPH_CACHE_CAP;
    do {
        if (!entry->occupied) {
            *out_victim = entry;
            return NULL;
        }
        if (entry->requested_codepoint == codepoint &&
            entry->glyph.pixel_size == pixel_size &&
            entry->glyph.style == style) {
            *out_victim = entry;
            return entry;
        }
        ++entry;
        if (entry == end) {
            entry = font->entries;
        }
    } while (entry != start);

    *out_victim = start;
    return NULL;
}

static FrOutlineCacheEntry *fr_find_outline_cache_entry(FrFont *font, uint16_t glyph_index,
                                                        FrOutlineCacheEntry **out_victim) {
    FrOutlineCacheEntry *start;
    FrOutlineCacheEntry *entry;
    FrOutlineCacheEntry *end;

    if (font == NULL || out_victim == NULL) {
        return NULL;
    }

    start = &font->outline_entries[fr_outline_hash(glyph_index)];
    entry = start;
    end = font->outline_entries + FR_OUTLINE_CACHE_CAP;
    do {
        if (!entry->occupied) {
            *out_victim = entry;
            return NULL;
        }
        if (entry->glyph_index == glyph_index) {
            *out_victim = entry;
            return entry;
        }
        ++entry;
        if (entry == end) {
            entry = font->outline_entries;
        }
    } while (entry != start);

    *out_victim = start;
    return NULL;
}

static FrLookupCacheEntry *fr_find_lookup_cache_entry(FrFont *font, uint32_t requested_codepoint,
                                                      FrLookupCacheEntry **out_victim) {
    FrLookupCacheEntry *start;
    FrLookupCacheEntry *entry;
    FrLookupCacheEntry *end;

    if (font == NULL || out_victim == NULL) {
        return NULL;
    }

    start = &font->lookup_entries[fr_lookup_hash(requested_codepoint)];
    entry = start;
    end = font->lookup_entries + FR_LOOKUP_CACHE_CAP;
    do {
        if (!entry->occupied) {
            *out_victim = entry;
            return NULL;
        }
        if (entry->requested_codepoint == requested_codepoint) {
            *out_victim = entry;
            return entry;
        }
        ++entry;
        if (entry == end) {
            entry = font->lookup_entries;
        }
    } while (entry != start);

    *out_victim = start;
    return NULL;
}

static int fr_outline_append_transformed(FrOutline *dst, const FrOutline *src,
                                         int16_t m00, int16_t m01, int16_t m10, int16_t m11,
                                         int32_t dx, int32_t dy) {
    size_t base_point;
    size_t base_contour;
    size_t i;

    if (dst == NULL || src == NULL) {
        return -1;
    }
    if (fr_outline_reserve(dst,
                           dst->point_count + src->point_count,
                           dst->contour_count + src->contour_count) != 0) {
        return -1;
    }

    base_point = dst->point_count;
    base_contour = dst->contour_count;
    for (i = 0u; i < src->point_count; ++i) {
        const FrOutlinePoint *point = &src->points[i];
        FrOutlinePoint *dst_point = &dst->points[base_point + i];
        int32_t xx;
        int32_t xy;
        int32_t yx;
        int32_t yy;
        int32_t x;
        int32_t y;

        if (fr_fixed_mul_f2dot14(point->x, m00, &xx) != 0 ||
            fr_fixed_mul_f2dot14(point->y, m01, &xy) != 0 ||
            fr_fixed_mul_f2dot14(point->x, m10, &yx) != 0 ||
            fr_fixed_mul_f2dot14(point->y, m11, &yy) != 0) {
            return -1;
        }
        if (fr_i64_to_i32((int64_t)xx + (int64_t)xy + (int64_t)dx, &x) != 0 ||
            fr_i64_to_i32((int64_t)yx + (int64_t)yy + (int64_t)dy, &y) != 0) {
            return -1;
        }
        dst_point->x = x;
        dst_point->y = y;
        dst_point->flags = point->flags;
        dst_point->reserved[0] = 0u;
        dst_point->reserved[1] = 0u;
        dst_point->reserved[2] = 0u;
    }
    dst->point_count += src->point_count;

    for (i = 0u; i < src->contour_count; ++i) {
        FrOutlineContour *dst_contour = &dst->contours[base_contour + i];

        dst_contour->first_point = base_point + src->contours[i].first_point;
        dst_contour->point_count = src->contours[i].point_count;
    }
    dst->contour_count += src->contour_count;
    return 0;
}

static int fr_decode_simple_glyph(FrFont *font, const FrTtfGlyph *glyph, FrOutline *outline) {
    const uint8_t *ptr;
    const uint8_t *end;
    const uint8_t *end_pts;
    FrSimpleDecodePoint *simple_points = NULL;
    size_t point_count = 0u;
    size_t contour_count = 0u;
    size_t i;
    int status = -1;

    if (font == NULL || glyph == NULL || outline == NULL ||
        glyph->number_of_contours < 0 || glyph->data_length < 10u) {
        return -1;
    }

    contour_count = (size_t)glyph->number_of_contours;
    ptr = glyph->data + 10u;
    end = glyph->data + glyph->data_length;

    if (contour_count != 0u) {
        uint16_t prev_end = 0u;

        if ((size_t)(end - ptr) < contour_count * 2u) {
            goto cleanup;
        }
        end_pts = ptr;
        for (i = 0u; i < contour_count; ++i) {
            uint16_t end_point = fr_read_u16(end_pts + i * 2u);

            if ((i != 0u && end_point <= prev_end) || end_point == UINT16_MAX) {
                goto cleanup;
            }
            prev_end = end_point;
        }
        point_count = (size_t)fr_read_u16(end_pts + (contour_count - 1u) * 2u) + 1u;
        ptr += contour_count * 2u;
    } else {
        end_pts = ptr;
    }

    if ((size_t)(end - ptr) < 2u) {
        goto cleanup;
    }
    {
        uint16_t instruction_length = fr_read_u16(ptr);
        ptr += 2u;
        if ((size_t)(end - ptr) < instruction_length) {
            goto cleanup;
        }
        ptr += instruction_length;
    }

    if (point_count != 0u) {
        size_t flag_index = 0u;
        int64_t current_x = 0;
        int64_t current_y = 0;

        if (fr_reserve_simple_decode_buffers(font, point_count) != 0) {
            goto cleanup;
        }
        simple_points = font->simple_points;

        while (flag_index < point_count) {
            uint8_t flag;
            size_t repeat = 1u;

            if (ptr >= end) {
                goto cleanup;
            }
            flag = *ptr++;
            if ((flag & (uint8_t)~0x7Fu) != 0u) {
                goto cleanup;
            }
            if ((flag & FR_SIMPLE_FLAG_REPEAT) != 0u) {
                if (ptr >= end) {
                    goto cleanup;
                }
                repeat += *ptr++;
            }
            if (repeat > point_count - flag_index) {
                goto cleanup;
            }
            while (repeat-- != 0u) {
                simple_points[flag_index++].flag = flag;
            }
        }

        for (i = 0u; i < point_count; ++i) {
            int32_t delta = 0;
            uint8_t flag = simple_points[i].flag;

            if ((flag & FR_SIMPLE_FLAG_X_SHORT) != 0u) {
                if (ptr >= end) {
                    goto cleanup;
                }
                delta = (flag & FR_SIMPLE_FLAG_X_SAME) != 0u ? (int32_t)*ptr : -(int32_t)*ptr;
                ptr += 1u;
            } else if ((flag & FR_SIMPLE_FLAG_X_SAME) == 0u) {
                if ((size_t)(end - ptr) < 2u) {
                    goto cleanup;
                }
                delta = fr_read_s16(ptr);
                ptr += 2u;
            }

            current_x += delta;
            if (current_x < INT32_MIN || current_x > INT32_MAX) {
                goto cleanup;
            }
            simple_points[i].x = (int32_t)current_x;
        }

        for (i = 0u; i < point_count; ++i) {
            int32_t delta = 0;
            uint8_t flag = simple_points[i].flag;

            if ((flag & FR_SIMPLE_FLAG_Y_SHORT) != 0u) {
                if (ptr >= end) {
                    goto cleanup;
                }
                delta = (flag & FR_SIMPLE_FLAG_Y_SAME) != 0u ? (int32_t)*ptr : -(int32_t)*ptr;
                ptr += 1u;
            } else if ((flag & FR_SIMPLE_FLAG_Y_SAME) == 0u) {
                if ((size_t)(end - ptr) < 2u) {
                    goto cleanup;
                }
                delta = fr_read_s16(ptr);
                ptr += 2u;
            }

            current_y += delta;
            if (current_y < INT32_MIN || current_y > INT32_MAX) {
                goto cleanup;
            }
            simple_points[i].y = (int32_t)current_y;
        }

        if (fr_outline_reserve(outline,
                               outline->point_count + point_count,
                               outline->contour_count + contour_count) != 0) {
            goto cleanup;
        }
        {
            size_t base_point = outline->point_count;
            FrOutlinePoint *dst_points = outline->points + base_point;

            for (i = 0u; i < point_count; ++i) {
                dst_points[i].x = simple_points[i].x * FR_OUTLINE_ONE;
                dst_points[i].y = simple_points[i].y * FR_OUTLINE_ONE;
                dst_points[i].flags =
                    (simple_points[i].flag & FR_SIMPLE_FLAG_ON_CURVE) != 0u ? FR_OUTLINE_POINT_ON_CURVE : 0u;
                dst_points[i].reserved[0] = 0u;
                dst_points[i].reserved[1] = 0u;
                dst_points[i].reserved[2] = 0u;
            }
            outline->point_count += point_count;
        }
    }

    {
        size_t base_point = outline->point_count - point_count;
        size_t contour_start = 0u;
        size_t base_contour = outline->contour_count;

        for (i = 0u; i < contour_count; ++i) {
            size_t contour_end = (size_t)fr_read_u16(end_pts + i * 2u);
            size_t contour_points = contour_end + 1u - contour_start;

            outline->contours[base_contour + i].first_point = base_point + contour_start;
            outline->contours[base_contour + i].point_count = contour_points;
            contour_start = contour_end + 1u;
        }
        outline->contour_count += contour_count;
    }

    status = 0;

cleanup:
    return status;
}

static int fr_get_glyph_outline(FrFont *font, uint16_t glyph_index, FrOutline *outline,
                                uint16_t *stack, size_t depth);

static int fr_decode_compound_glyph(FrFont *font, const FrTtfGlyph *glyph, FrOutline *outline,
                                    uint16_t *stack, size_t depth) {
    const uint8_t *ptr;
    const uint8_t *end;
    uint16_t flags = 0u;
    bool have_instructions = false;

    if (font == NULL || glyph == NULL || outline == NULL || glyph->data_length < 10u) {
        return -1;
    }

    ptr = glyph->data + 10u;
    end = glyph->data + glyph->data_length;

    do {
        uint16_t component_glyph_index;
        int16_t arg1;
        int16_t arg2;
        int16_t m00 = 1 << 14;
        int16_t m01 = 0;
        int16_t m10 = 0;
        int16_t m11 = 1 << 14;
        int32_t dx;
        int32_t dy;
        FrOutline *component_outline;

        if ((size_t)(end - ptr) < 4u) {
            return -1;
        }
        flags = fr_read_u16(ptr);
        component_glyph_index = fr_read_u16(ptr + 2u);
        ptr += 4u;
        have_instructions = have_instructions || (flags & FR_COMPOUND_HAVE_INSTRUCTIONS) != 0u;

        if (component_glyph_index >= font->info.num_glyphs) {
            FR_FONT_LOG(font, "compound glyph references invalid component %u\n",
                    (unsigned int)component_glyph_index);
            return -1;
        }
        if ((flags & FR_COMPOUND_ARGS_ARE_XY) == 0u) {
            FR_FONT_LOG(font, "matched-point compound glyphs are not supported yet\n");
            return -1;
        }
        if ((flags & FR_COMPOUND_SCALED_OFFSET) != 0u &&
            (flags & FR_COMPOUND_UNSCALED_OFFSET) != 0u) {
            FR_FONT_LOG(font, "invalid compound glyph offset flags\n");
            return -1;
        }

        if ((flags & FR_COMPOUND_ARG_WORDS) != 0u) {
            if ((size_t)(end - ptr) < 4u) {
                return -1;
            }
            arg1 = fr_read_s16(ptr);
            arg2 = fr_read_s16(ptr + 2u);
            ptr += 4u;
        } else {
            if ((size_t)(end - ptr) < 2u) {
                return -1;
            }
            arg1 = (int8_t)ptr[0];
            arg2 = (int8_t)ptr[1];
            ptr += 2u;
        }

        if ((flags & FR_COMPOUND_HAVE_SCALE) != 0u) {
            if ((size_t)(end - ptr) < 2u) {
                return -1;
            }
            m00 = fr_read_s16(ptr);
            m11 = m00;
            ptr += 2u;
        } else if ((flags & FR_COMPOUND_HAVE_XY_SCALE) != 0u) {
            if ((size_t)(end - ptr) < 4u) {
                return -1;
            }
            m00 = fr_read_s16(ptr);
            m11 = fr_read_s16(ptr + 2u);
            ptr += 4u;
        } else if ((flags & FR_COMPOUND_HAVE_2X2) != 0u) {
            if ((size_t)(end - ptr) < 8u) {
                return -1;
            }
            m00 = fr_read_s16(ptr);
            m01 = fr_read_s16(ptr + 2u);
            m10 = fr_read_s16(ptr + 4u);
            m11 = fr_read_s16(ptr + 6u);
            ptr += 8u;
        }

        dx = (int32_t)arg1 * FR_OUTLINE_ONE;
        dy = (int32_t)arg2 * FR_OUTLINE_ONE;
        if ((flags & FR_COMPOUND_SCALED_OFFSET) != 0u) {
            int32_t scaled_dx_x;
            int32_t scaled_dx_y;
            int32_t scaled_dy_x;
            int32_t scaled_dy_y;

            if (fr_fixed_mul_f2dot14(dx, m00, &scaled_dx_x) != 0 ||
                fr_fixed_mul_f2dot14(dy, m01, &scaled_dx_y) != 0 ||
                fr_fixed_mul_f2dot14(dx, m10, &scaled_dy_x) != 0 ||
                fr_fixed_mul_f2dot14(dy, m11, &scaled_dy_y) != 0 ||
                fr_i64_to_i32((int64_t)scaled_dx_x + (int64_t)scaled_dx_y, &dx) != 0 ||
                fr_i64_to_i32((int64_t)scaled_dy_x + (int64_t)scaled_dy_y, &dy) != 0) {
                return -1;
            }
        }

        component_outline = &font->scratch_outlines[depth];
        fr_outline_reset(component_outline);
        if (fr_get_glyph_outline(font, component_glyph_index, component_outline, stack, depth) != 0) {
            FR_FONT_LOG(font, "failed to decode compound component %u\n",
                    (unsigned int)component_glyph_index);
            return -1;
        }
        if (fr_outline_append_transformed(outline, component_outline, m00, m01, m10, m11, dx, dy) != 0) {
            return -1;
        }
    } while ((flags & FR_COMPOUND_MORE_COMPONENTS) != 0u);

    if (have_instructions) {
        uint16_t instruction_length;

        if ((size_t)(end - ptr) < 2u) {
            return -1;
        }
        instruction_length = fr_read_u16(ptr);
        ptr += 2u;
        if ((size_t)(end - ptr) < instruction_length) {
            return -1;
        }
        ptr += instruction_length;
    }

    return 0;
}

static int fr_decode_glyph_outline_uncached(FrFont *font, uint16_t glyph_index, FrOutline *outline,
                                            uint16_t *stack, size_t depth) {
    FrTtfGlyph glyph;
    FrTtfResult result;
    size_t i;

    if (font == NULL || outline == NULL || stack == NULL) {
        return -1;
    }
    if (depth >= FR_COMPOUND_RECURSION_LIMIT) {
        FR_FONT_LOG(font, "compound glyph recursion limit exceeded\n");
        return -1;
    }
    for (i = 0u; i < depth; ++i) {
        if (stack[i] == glyph_index) {
            FR_FONT_LOG(font, "cyclic compound glyph reference for glyph %u\n",
                    (unsigned int)glyph_index);
            return -1;
        }
    }
    stack[depth] = glyph_index;

    result = fr_ttf_get_glyph(font->ttf, glyph_index, &glyph);
    if (result != FR_TTF_OK) {
        FR_FONT_LOG(font, "fr_ttf_get_glyph(%u) failed: %s\n",
                (unsigned int)glyph_index, fr_ttf_result_string(result));
        return -1;
    }
    if (glyph.data == NULL || glyph.data_length == 0u || glyph.number_of_contours == 0) {
        return 0;
    }
    if (glyph.number_of_contours > 0) {
        return fr_decode_simple_glyph(font, &glyph, outline);
    }
    if (glyph.number_of_contours == -1) {
        return fr_decode_compound_glyph(font, &glyph, outline, stack, depth + 1u);
    }

    FR_FONT_LOG(font, "unsupported glyph contour type %d for glyph %u\n",
            glyph.number_of_contours, (unsigned int)glyph_index);
    return -1;
}

static int fr_get_glyph_outline(FrFont *font, uint16_t glyph_index, FrOutline *outline,
                                uint16_t *stack, size_t depth) {
    FrOutlineCacheEntry *entry;
    FrOutlineCacheEntry *victim;

    if (font == NULL || outline == NULL) {
        return -1;
    }

    entry = fr_find_outline_cache_entry(font, glyph_index, &victim);
    if (entry != NULL) {
        return fr_outline_copy(outline, &entry->outline);
    }

    if (fr_decode_glyph_outline_uncached(font, glyph_index, outline, stack, depth) != 0) {
        return -1;
    }
    if (fr_outline_copy(&victim->outline, outline) == 0) {
        victim->glyph_index = glyph_index;
        victim->occupied = true;
    }
    return 0;
}

static int fr_load_glyph_outline_for_render(FrFont *font, uint16_t glyph_index, FrOutline *outline,
                                            const FrScale *scale, uint16_t *stack) {
    FrOutlineCacheEntry *entry;
    FrOutlineCacheEntry *victim;

    if (font == NULL || outline == NULL || scale == NULL || stack == NULL) {
        return -1;
    }

    entry = fr_find_outline_cache_entry(font, glyph_index, &victim);
    if (entry != NULL) {
        return fr_outline_copy_scaled(outline, &entry->outline, scale);
    }

    if (fr_decode_glyph_outline_uncached(font, glyph_index, outline, stack, 0u) != 0) {
        return -1;
    }
    if (fr_outline_copy(&victim->outline, outline) == 0) {
        victim->glyph_index = glyph_index;
        victim->occupied = true;
    }
    return fr_scale_outline(outline, scale);
}

static int fr_lookup_render_glyph(FrFont *font, uint32_t requested_codepoint,
                                  uint16_t *out_glyph_index, uint32_t *out_render_codepoint) {
    FrTtfResult result;
    uint16_t glyph_index;
    FrLookupCacheEntry *entry;
    FrLookupCacheEntry *victim;

    if (font == NULL || out_glyph_index == NULL || out_render_codepoint == NULL) {
        return -1;
    }

    entry = fr_find_lookup_cache_entry(font, requested_codepoint, &victim);
    if (entry != NULL) {
        *out_glyph_index = entry->glyph_index;
        *out_render_codepoint = entry->render_codepoint;
        return 0;
    }

    result = fr_ttf_lookup_glyph(font->ttf, requested_codepoint, &glyph_index);
    if (result == FR_TTF_OK) {
        victim->requested_codepoint = requested_codepoint;
        victim->render_codepoint = requested_codepoint;
        victim->glyph_index = glyph_index;
        victim->occupied = true;
        *out_glyph_index = glyph_index;
        *out_render_codepoint = requested_codepoint;
        return 0;
    }
    if (result != FR_TTF_ERR_NOT_FOUND) {
        FR_FONT_LOG(font, "glyph lookup for U+%04X failed: %s\n",
                (unsigned int)requested_codepoint, fr_ttf_result_string(result));
        return -1;
    }
    if (!font->have_fallback_glyph) {
        FR_FONT_LOG(font, "font has no '?' fallback for missing U+%04X\n",
                (unsigned int)requested_codepoint);
        return -1;
    }

    victim->requested_codepoint = requested_codepoint;
    victim->render_codepoint = '?';
    victim->glyph_index = font->fallback_glyph_index;
    victim->occupied = true;
    *out_glyph_index = font->fallback_glyph_index;
    *out_render_codepoint = '?';
    return 0;
}

static int fr_load_cached_glyph(FrFont *font, FrGlyphCacheEntry *entry,
                                uint32_t requested_codepoint, int pixel_size, uint16_t style) {
    FrScale scale;
    uint16_t glyph_index;
    uint32_t render_codepoint;
    FrTtfHMetric metric;
    FrOutline *outline;
    FrBitmap bitmap;
    uint16_t stack[FR_COMPOUND_RECURSION_LIMIT];
    int advance;
    int status = -1;

    if (font == NULL || entry == NULL) {
        return -1;
    }
    if (fr_scale_init(&scale, pixel_size, font->info.units_per_em) != 0) {
        return -1;
    }
    if (fr_lookup_render_glyph(font, requested_codepoint, &glyph_index, &render_codepoint) != 0) {
        return -1;
    }
    if (fr_ttf_get_hmetric(font->ttf, glyph_index, &metric) != FR_TTF_OK) {
        FR_FONT_LOG(font, "failed to load hmetric for glyph %u\n",
                (unsigned int)glyph_index);
        return -1;
    }
    if (fr_scale_units_round((int32_t)metric.advance_width, &scale, &advance) != 0) {
        FR_FONT_LOG(font, "failed to scale advance for glyph %u\n",
                (unsigned int)glyph_index);
        return -1;
    }

    outline = &font->scratch_outlines[0];
    fr_outline_reset(outline);
    fr_bitmap_init(&bitmap);

    if (fr_load_glyph_outline_for_render(font, glyph_index, outline, &scale, stack) != 0) {
        FR_FONT_LOG(font, "failed to load outline for glyph %u for U+%04X\n",
                (unsigned int)glyph_index, (unsigned int)requested_codepoint);
        goto cleanup;
    }
    if (outline->point_count == 0u) {
        fr_platform_free_with(&font->platform, entry->bitmap_storage);
        entry->bitmap_storage = NULL;
        entry->requested_codepoint = requested_codepoint;
        entry->glyph.codepoint = render_codepoint;
        entry->glyph.pixel_size = (uint16_t)pixel_size;
        entry->glyph.style = style;
        entry->glyph.advance = advance;
        entry->glyph.left = 0;
        entry->glyph.top = 0;
        entry->glyph.width = 0;
        entry->glyph.height = 0;
        entry->glyph.bitmap = NULL;
        entry->occupied = true;
        status = 0;
        goto cleanup;
    }
    if (fr_raster_render(&bitmap, outline) != 0) {
        FR_FONT_LOG(font, "failed to rasterize glyph %u for U+%04X\n",
                (unsigned int)glyph_index, (unsigned int)requested_codepoint);
        goto cleanup;
    }

    fr_platform_free_with(&font->platform, entry->bitmap_storage);
    entry->bitmap_storage = bitmap.pixels;
    bitmap.pixels = NULL;
    entry->requested_codepoint = requested_codepoint;
    entry->glyph.codepoint = render_codepoint;
    entry->glyph.pixel_size = (uint16_t)pixel_size;
    entry->glyph.style = style;
    entry->glyph.advance = advance;
    entry->glyph.left = bitmap.left;
    entry->glyph.top = bitmap.top;
    entry->glyph.width = bitmap.width;
    entry->glyph.height = bitmap.height;
    entry->glyph.bitmap = entry->bitmap_storage;
    entry->occupied = true;
    status = 0;

cleanup:
    fr_bitmap_free(&bitmap);
    fr_outline_reset(outline);
    return status;
}

static int fr_font_open_impl(FrFont **out_font, const char *font_path, const void *data, size_t size, bool from_memory) {
    FrFont *font;
    FrPlatform platform;
    FrTtfResult result;

    if (out_font == NULL || (!from_memory && font_path == NULL) || (from_memory && data == NULL)) {
        return -1;
    }
    *out_font = NULL;

    fr_platform_resolve(&platform);
    font = (FrFont *)fr_platform_calloc_with(&platform, 1u, sizeof(*font));
    if (font == NULL) {
        return -1;
    }
    font->platform = platform;
    if (font->platform.mutex_create != NULL) {
        font->mutex = font->platform.mutex_create(font->platform.user_data);
        if (font->mutex == NULL) {
            FR_FONT_LOG(font, "failed to create optional font mutex\n");
            fr_platform_free_with(&font->platform, font);
            return -1;
        }
    }

    result = from_memory ? fr_ttf_open_memory(&font->ttf, data, size) : fr_ttf_open(&font->ttf, font_path);
    if (result != FR_TTF_OK) {
        FR_FONT_LOG(font, "failed to open %s: %s\n",
                    from_memory ? "<memory>" : font_path, fr_ttf_result_string(result));
        if (font->mutex != NULL) {
            font->platform.mutex_destroy(font->platform.user_data, font->mutex);
        }
        fr_platform_free_with(&font->platform, font);
        return -1;
    }
    result = fr_ttf_get_info(font->ttf, &font->info);
    if (result != FR_TTF_OK) {
        FR_FONT_LOG(font, "failed to read font info: %s\n",
                    fr_ttf_result_string(result));
        fr_ttf_close(font->ttf);
        if (font->mutex != NULL) {
            font->platform.mutex_destroy(font->platform.user_data, font->mutex);
        }
        fr_platform_free_with(&font->platform, font);
        return -1;
    }
    if (fr_ttf_lookup_glyph(font->ttf, '?', &font->fallback_glyph_index) == FR_TTF_OK) {
        font->have_fallback_glyph = true;
    }

    *out_font = font;
    return 0;
}

int fr_font_open(FrFont **out_font, const char *font_path) {
    return fr_font_open_impl(out_font, font_path, NULL, 0u, false);
}

int fr_font_open_memory(FrFont **out_font, const void *data, size_t size) {
    return fr_font_open_impl(out_font, NULL, data, size, true);
}

void fr_font_close(FrFont *font) {
    size_t i;

    if (font == NULL) {
        return;
    }
    for (i = 0u; i < FR_GLYPH_CACHE_CAP; ++i) {
        fr_platform_free_with(&font->platform, font->entries[i].bitmap_storage);
        font->entries[i].bitmap_storage = NULL;
        font->entries[i].glyph.bitmap = NULL;
    }
    for (i = 0u; i < FR_OUTLINE_CACHE_CAP; ++i) {
        fr_outline_free(&font->outline_entries[i].outline);
    }
    for (i = 0u; i < sizeof(font->scratch_outlines) / sizeof(font->scratch_outlines[0]); ++i) {
        fr_outline_free(&font->scratch_outlines[i]);
    }
    fr_platform_free_with(&font->platform, font->simple_points);
    fr_ttf_close(font->ttf);
    if (font->mutex != NULL) {
        font->platform.mutex_destroy(font->platform.user_data, font->mutex);
    }
    fr_platform_free_with(&font->platform, font);
}

static const FrGlyph *fr_font_get_glyph_unlocked(FrFont *font, uint32_t codepoint, int pixel_size, uint16_t style) {
    FrGlyphCacheEntry *entry;
    FrGlyphCacheEntry *victim;
    uint16_t pixel_size_u16;

    if (font == NULL || pixel_size <= 0 || pixel_size > 65535 || codepoint > 0x10FFFFu) {
        return NULL;
    }

    pixel_size_u16 = (uint16_t)pixel_size;
    entry = fr_find_glyph_cache_entry(font, codepoint, pixel_size_u16, style, &victim);
    if (entry != NULL) {
        return &entry->glyph;
    }

    if (fr_load_cached_glyph(font, victim, codepoint, pixel_size, style) != 0) {
        return NULL;
    }
    return &victim->glyph;
}

const FrGlyph *fr_font_get_glyph(FrFont *font, uint32_t codepoint, int pixel_size, uint16_t style) {
    const FrGlyph *glyph;

    fr_font_lock(font);
    glyph = fr_font_get_glyph_unlocked(font, codepoint, pixel_size, style);
    fr_font_unlock(font);
    return glyph;
}

int fr_font_prefetch_glyph(FrFont *font, uint32_t codepoint, int pixel_size, uint16_t style) {
    int status;

    fr_font_lock(font);
    status = fr_font_get_glyph_unlocked(font, codepoint, pixel_size, style) != NULL ? 0 : -1;
    fr_font_unlock(font);
    return status;
}

int fr_font_prefetch_codepoints(FrFont *font, const uint32_t *codepoints, size_t codepoint_count,
                                int pixel_size, uint16_t style) {
    size_t i;

    if (font == NULL || (codepoint_count != 0u && codepoints == NULL)) {
        return -1;
    }
    fr_font_lock(font);
    for (i = 0u; i < codepoint_count; ++i) {
        if (fr_font_get_glyph_unlocked(font, codepoints[i], pixel_size, style) == NULL) {
            fr_font_unlock(font);
            return -1;
        }
    }
    fr_font_unlock(font);
    return 0;
}

int fr_font_line_height(FrFont *font, int pixel_size) {
    FrScale scale;
    int32_t line_units;
    int line_height;

    if (font == NULL || pixel_size <= 0 || font->info.units_per_em == 0u) {
        return pixel_size > 0 ? pixel_size + 6 : 6;
    }

    fr_font_lock(font);
    line_units = (int32_t)font->info.ascender - (int32_t)font->info.descender + (int32_t)font->info.line_gap;
    if (line_units <= 0) {
        line_units = (int32_t)font->info.ascender - (int32_t)font->info.descender;
    }
    if (line_units <= 0) {
        fr_font_unlock(font);
        return pixel_size + 6;
    }
    if (fr_scale_init(&scale, pixel_size, font->info.units_per_em) != 0 ||
        fr_scale_units_round(line_units, &scale, &line_height) != 0 ||
        line_height <= 0) {
        fr_font_unlock(font);
        return pixel_size + 6;
    }
    fr_font_unlock(font);
    return line_height;
}
