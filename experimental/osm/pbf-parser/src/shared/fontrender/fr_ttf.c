#include "fontrender/fr_ttf.h"

#include <stdbool.h>

#include "fr_platform_internal.h"

#define FR_TTF_TAG(a, b, c, d) \
    ((((uint32_t)(uint8_t)(a)) << 24) | (((uint32_t)(uint8_t)(b)) << 16) | (((uint32_t)(uint8_t)(c)) << 8) | ((uint32_t)(uint8_t)(d)))

enum {
    FR_TTF_HEADER_SIZE = 12u,
    FR_TTF_TABLE_RECORD_SIZE = 16u,
    FR_TTF_MAX_TABLES = 4096u
};

typedef struct {
    uint32_t tag;
    uint32_t offset;
    uint32_t length;
} FrTtfTable;

typedef struct {
    const uint8_t *data;
    size_t length;
    uint16_t seg_count;
} FrTtfCmap4;

typedef struct {
    const uint8_t *data;
    size_t length;
    uint32_t n_groups;
} FrTtfCmap12;

struct FrTtfFont {
    const uint8_t *data;
    size_t data_size;
    FrPlatform platform;
    FrPlatformFile loaded_file;

    const uint8_t *head;
    size_t head_length;
    const uint8_t *maxp;
    size_t maxp_length;
    const uint8_t *hhea;
    size_t hhea_length;
    const uint8_t *hmtx;
    size_t hmtx_length;
    const uint8_t *cmap;
    size_t cmap_length;
    const uint8_t *loca;
    size_t loca_length;
    const uint8_t *glyf;
    size_t glyf_length;
    const uint8_t *name;
    size_t name_length;

    FrTtfCmap4 cmap4;
    FrTtfCmap12 cmap12;
    FrTtfInfo info;
};

static uint16_t fr_ttf_read_u16(const uint8_t *ptr) {
    return (uint16_t)(((uint16_t)ptr[0] << 8) | (uint16_t)ptr[1]);
}

static int16_t fr_ttf_read_s16(const uint8_t *ptr) {
    return (int16_t)fr_ttf_read_u16(ptr);
}

static uint32_t fr_ttf_read_u32(const uint8_t *ptr) {
    return ((uint32_t)ptr[0] << 24) |
           ((uint32_t)ptr[1] << 16) |
           ((uint32_t)ptr[2] << 8) |
           (uint32_t)ptr[3];
}

static bool fr_ttf_range_valid(size_t size, uint32_t offset, uint32_t length) {
    return (size_t)offset <= size && (size_t)length <= size - (size_t)offset;
}

static bool fr_ttf_mul_overflow(size_t a, size_t b, size_t *out) {
    if (a != 0u && b > SIZE_MAX / a) {
        return true;
    }
    *out = a * b;
    return false;
}

static bool fr_ttf_add_overflow(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) {
        return true;
    }
    *out = a + b;
    return false;
}

static bool fr_ttf_ranges_overlap(const FrTtfTable *a, const FrTtfTable *b) {
    uint64_t a_end;
    uint64_t b_end;

    if (a->length == 0u || b->length == 0u) {
        return false;
    }

    a_end = (uint64_t)a->offset + (uint64_t)a->length;
    b_end = (uint64_t)b->offset + (uint64_t)b->length;
    return (uint64_t)a->offset < b_end && (uint64_t)b->offset < a_end;
}

static FrTtfResult fr_ttf_load_file(FrTtfFont *font, const char *path) {
    FrPlatformFileResult file_result;

    file_result = fr_platform_load_file_with(&font->platform, path, &font->loaded_file);
    if (file_result == FR_PLATFORM_FILE_ERR_NO_MEMORY) {
        return FR_TTF_ERR_NO_MEMORY;
    }
    if (file_result != FR_PLATFORM_FILE_OK) {
        return FR_TTF_ERR_IO;
    }

    font->data = font->loaded_file.data;
    font->data_size = font->loaded_file.size;
    return FR_TTF_OK;
}

static const FrTtfTable *fr_ttf_find_table(const FrTtfTable *tables, uint16_t num_tables, uint32_t tag) {
    uint16_t i;

    for (i = 0u; i < num_tables; ++i) {
        if (tables[i].tag == tag) {
            return &tables[i];
        }
    }
    return NULL;
}

static FrTtfResult fr_ttf_parse_directory(FrTtfFont *font, FrTtfTable **out_tables, uint16_t *out_num_tables) {
    FrTtfTable *tables;
    uint32_t sfnt_version;
    uint16_t num_tables;
    size_t table_bytes;
    uint16_t i;
    uint16_t j;

    if (font->data_size < FR_TTF_HEADER_SIZE) {
        return FR_TTF_ERR_MALFORMED;
    }

    sfnt_version = fr_ttf_read_u32(font->data);
    if (sfnt_version != 0x00010000u && sfnt_version != FR_TTF_TAG('t', 'r', 'u', 'e')) {
        return FR_TTF_ERR_UNSUPPORTED_SFNT;
    }

    num_tables = fr_ttf_read_u16(font->data + 4u);
    if (num_tables == 0u || num_tables > FR_TTF_MAX_TABLES) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (fr_ttf_mul_overflow((size_t)num_tables, FR_TTF_TABLE_RECORD_SIZE, &table_bytes) ||
        fr_ttf_add_overflow(FR_TTF_HEADER_SIZE, table_bytes, &table_bytes) ||
        table_bytes > font->data_size) {
        return FR_TTF_ERR_MALFORMED;
    }

    tables = (FrTtfTable *)fr_platform_calloc_with(&font->platform, (size_t)num_tables, sizeof(*tables));
    if (tables == NULL) {
        return FR_TTF_ERR_NO_MEMORY;
    }

    for (i = 0u; i < num_tables; ++i) {
        const uint8_t *entry = font->data + FR_TTF_HEADER_SIZE + (size_t)i * FR_TTF_TABLE_RECORD_SIZE;
        uint32_t offset = fr_ttf_read_u32(entry + 8u);
        uint32_t length = fr_ttf_read_u32(entry + 12u);

        tables[i].tag = fr_ttf_read_u32(entry);
        tables[i].offset = offset;
        tables[i].length = length;

        if (!fr_ttf_range_valid(font->data_size, offset, length)) {
            fr_platform_free_with(&font->platform, tables);
            return FR_TTF_ERR_MALFORMED;
        }
        if (length != 0u && offset < table_bytes) {
            fr_platform_free_with(&font->platform, tables);
            return FR_TTF_ERR_MALFORMED;
        }
        for (j = 0u; j < i; ++j) {
            if (tables[j].tag == tables[i].tag) {
                fr_platform_free_with(&font->platform, tables);
                return FR_TTF_ERR_MALFORMED;
            }
        }
    }

    for (i = 0u; i < num_tables; ++i) {
        for (j = (uint16_t)(i + 1u); j < num_tables; ++j) {
            if (fr_ttf_ranges_overlap(&tables[i], &tables[j])) {
                fr_platform_free_with(&font->platform, tables);
                return FR_TTF_ERR_MALFORMED;
            }
        }
    }

    *out_tables = tables;
    *out_num_tables = num_tables;
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_assign_table(FrTtfFont *font, const FrTtfTable *table, const uint8_t **out_data, size_t *out_length) {
    if (table == NULL) {
        return FR_TTF_ERR_MISSING_TABLE;
    }
    if (!fr_ttf_range_valid(font->data_size, table->offset, table->length)) {
        return FR_TTF_ERR_MALFORMED;
    }
    *out_data = font->data + table->offset;
    *out_length = table->length;
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_parse_head(FrTtfFont *font) {
    uint16_t units_per_em;
    int16_t index_to_loc_format;
    int16_t glyph_data_format;

    if (font->head_length < 54u) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (fr_ttf_read_u32(font->head + 12u) != 0x5F0F3CF5u) {
        return FR_TTF_ERR_MALFORMED;
    }

    units_per_em = fr_ttf_read_u16(font->head + 18u);
    index_to_loc_format = fr_ttf_read_s16(font->head + 50u);
    glyph_data_format = fr_ttf_read_s16(font->head + 52u);

    if (units_per_em < 16u || units_per_em > 16384u) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (index_to_loc_format != 0 && index_to_loc_format != 1) {
        return FR_TTF_ERR_UNSUPPORTED_SFNT;
    }
    if (glyph_data_format != 0) {
        return FR_TTF_ERR_UNSUPPORTED_SFNT;
    }

    font->info.units_per_em = units_per_em;
    font->info.index_to_loc_format = index_to_loc_format;
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_parse_maxp(FrTtfFont *font) {
    uint32_t version;
    uint16_t num_glyphs;

    if (font->maxp_length < 6u) {
        return FR_TTF_ERR_MALFORMED;
    }

    version = fr_ttf_read_u32(font->maxp);
    if (version != 0x00010000u) {
        return FR_TTF_ERR_UNSUPPORTED_SFNT;
    }

    num_glyphs = fr_ttf_read_u16(font->maxp + 4u);
    if (num_glyphs == 0u) {
        return FR_TTF_ERR_MALFORMED;
    }

    font->info.num_glyphs = num_glyphs;
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_parse_hhea(FrTtfFont *font) {
    uint16_t number_of_h_metrics;

    if (font->hhea_length < 36u) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (fr_ttf_read_s16(font->hhea + 32u) != 0) {
        return FR_TTF_ERR_UNSUPPORTED_SFNT;
    }

    number_of_h_metrics = fr_ttf_read_u16(font->hhea + 34u);
    if (number_of_h_metrics == 0u || number_of_h_metrics > font->info.num_glyphs) {
        return FR_TTF_ERR_MALFORMED;
    }

    font->info.ascender = fr_ttf_read_s16(font->hhea + 4u);
    font->info.descender = fr_ttf_read_s16(font->hhea + 6u);
    font->info.line_gap = fr_ttf_read_s16(font->hhea + 8u);
    font->info.number_of_h_metrics = number_of_h_metrics;
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_validate_hmtx(FrTtfFont *font) {
    size_t required;
    size_t long_metric_bytes;
    size_t extra_lsb_bytes;

    if (fr_ttf_mul_overflow((size_t)font->info.number_of_h_metrics, 4u, &long_metric_bytes)) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (fr_ttf_mul_overflow((size_t)(font->info.num_glyphs - font->info.number_of_h_metrics), 2u, &extra_lsb_bytes) ||
        fr_ttf_add_overflow(long_metric_bytes, extra_lsb_bytes, &required)) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (font->hmtx_length < required) {
        return FR_TTF_ERR_MALFORMED;
    }
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_validate_cmap4(const uint8_t *subtable, size_t available, FrTtfCmap4 *out_cmap4) {
    uint16_t length;
    uint16_t seg_count_x2;
    uint16_t seg_count;
    size_t min_length;
    size_t end_codes_off;
    size_t start_codes_off;
    size_t id_delta_off;
    size_t id_range_off;
    uint16_t i;
    uint32_t previous_end = 0u;

    if (available < 16u) {
        return FR_TTF_ERR_MALFORMED;
    }

    length = fr_ttf_read_u16(subtable + 2u);
    seg_count_x2 = fr_ttf_read_u16(subtable + 6u);
    if (length < 16u || length > available || seg_count_x2 == 0u || (seg_count_x2 & 1u) != 0u) {
        return FR_TTF_ERR_MALFORMED;
    }

    seg_count = (uint16_t)(seg_count_x2 / 2u);
    if (fr_ttf_mul_overflow((size_t)seg_count, 8u, &min_length) ||
        fr_ttf_add_overflow(16u, min_length, &min_length) ||
        min_length > length) {
        return FR_TTF_ERR_MALFORMED;
    }

    end_codes_off = 14u;
    start_codes_off = 16u + (size_t)seg_count * 2u;
    id_delta_off = start_codes_off + (size_t)seg_count * 2u;
    id_range_off = id_delta_off + (size_t)seg_count * 2u;

    if (fr_ttf_read_u16(subtable + 14u + (size_t)seg_count * 2u) != 0u) {
        return FR_TTF_ERR_MALFORMED;
    }

    for (i = 0u; i < seg_count; ++i) {
        uint16_t start_code = fr_ttf_read_u16(subtable + start_codes_off + (size_t)i * 2u);
        uint16_t end_code = fr_ttf_read_u16(subtable + end_codes_off + (size_t)i * 2u);
        uint16_t id_range_offset = fr_ttf_read_u16(subtable + id_range_off + (size_t)i * 2u);
        size_t range_word_off = id_range_off + (size_t)i * 2u;
        size_t glyph_off;
        size_t glyph_span;
        size_t glyph_limit;

        if (start_code > end_code) {
            return FR_TTF_ERR_MALFORMED;
        }
        if (i != 0u && ((uint32_t)start_code <= previous_end || (uint32_t)end_code < previous_end)) {
            return FR_TTF_ERR_MALFORMED;
        }
        previous_end = end_code;

        if (id_range_offset != 0u) {
            if (fr_ttf_add_overflow(range_word_off, id_range_offset, &glyph_off)) {
                return FR_TTF_ERR_MALFORMED;
            }
            if (fr_ttf_add_overflow(glyph_off, 2u, &glyph_limit) || glyph_limit > length) {
                return FR_TTF_ERR_MALFORMED;
            }
            glyph_span = (size_t)(end_code - start_code) * 2u;
            if (glyph_span > length ||
                fr_ttf_add_overflow(glyph_off, glyph_span, &glyph_limit) ||
                fr_ttf_add_overflow(glyph_limit, 2u, &glyph_limit) ||
                glyph_limit > length) {
                return FR_TTF_ERR_MALFORMED;
            }
        }
    }

    if (fr_ttf_read_u16(subtable + end_codes_off + (size_t)(seg_count - 1u) * 2u) != 0xFFFFu) {
        return FR_TTF_ERR_MALFORMED;
    }

    out_cmap4->data = subtable;
    out_cmap4->length = length;
    out_cmap4->seg_count = seg_count;
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_validate_cmap12(const uint8_t *subtable, size_t available, FrTtfCmap12 *out_cmap12) {
    uint32_t length;
    uint32_t n_groups;
    size_t required;
    uint32_t i;
    uint32_t previous_end = 0u;

    if (available < 16u) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (fr_ttf_read_u16(subtable + 2u) != 0u) {
        return FR_TTF_ERR_MALFORMED;
    }

    length = fr_ttf_read_u32(subtable + 4u);
    n_groups = fr_ttf_read_u32(subtable + 12u);
    if (length < 16u || length > available) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (fr_ttf_mul_overflow((size_t)n_groups, 12u, &required) ||
        fr_ttf_add_overflow(16u, required, &required) ||
        required > length) {
        return FR_TTF_ERR_MALFORMED;
    }

    for (i = 0u; i < n_groups; ++i) {
        const uint8_t *group = subtable + 16u + (size_t)i * 12u;
        uint32_t start_char = fr_ttf_read_u32(group);
        uint32_t end_char = fr_ttf_read_u32(group + 4u);
        uint32_t start_glyph = fr_ttf_read_u32(group + 8u);
        uint64_t last_glyph = (uint64_t)start_glyph + (uint64_t)(end_char - start_char);

        if (start_char > end_char || end_char > 0x10FFFFu) {
            return FR_TTF_ERR_MALFORMED;
        }
        if (i != 0u && start_char <= previous_end) {
            return FR_TTF_ERR_MALFORMED;
        }
        if (last_glyph > 0xFFFFu) {
            return FR_TTF_ERR_MALFORMED;
        }
        previous_end = end_char;
    }

    out_cmap12->data = subtable;
    out_cmap12->length = length;
    out_cmap12->n_groups = n_groups;
    return FR_TTF_OK;
}

static int fr_ttf_cmap_score(uint16_t platform_id, uint16_t encoding_id, uint16_t format) {
    if (format == 12u) {
        if (platform_id == 0u) {
            return 30;
        }
        if (platform_id == 3u && encoding_id == 10u) {
            return 20;
        }
        return -1;
    }
    if (format == 4u) {
        if (platform_id == 0u) {
            return 10;
        }
        if (platform_id == 3u && encoding_id == 1u) {
            return 9;
        }
        return -1;
    }
    return -1;
}

static FrTtfResult fr_ttf_parse_cmap(FrTtfFont *font) {
    uint16_t num_subtables;
    size_t header_bytes;
    int best_score4 = -1;
    int best_score12 = -1;
    uint16_t i;

    if (font->cmap_length < 4u) {
        return FR_TTF_ERR_MALFORMED;
    }
    if (fr_ttf_read_u16(font->cmap) != 0u) {
        return FR_TTF_ERR_UNSUPPORTED_CMAP;
    }

    num_subtables = fr_ttf_read_u16(font->cmap + 2u);
    if (fr_ttf_mul_overflow((size_t)num_subtables, 8u, &header_bytes) ||
        fr_ttf_add_overflow(4u, header_bytes, &header_bytes) ||
        header_bytes > font->cmap_length) {
        return FR_TTF_ERR_MALFORMED;
    }

    for (i = 0u; i < num_subtables; ++i) {
        const uint8_t *record = font->cmap + 4u + (size_t)i * 8u;
        uint16_t platform_id = fr_ttf_read_u16(record);
        uint16_t encoding_id = fr_ttf_read_u16(record + 2u);
        uint32_t offset = fr_ttf_read_u32(record + 4u);
        uint16_t format;
        int score;

        if (offset > font->cmap_length - 2u) {
            return FR_TTF_ERR_MALFORMED;
        }

        format = fr_ttf_read_u16(font->cmap + offset);
        score = fr_ttf_cmap_score(platform_id, encoding_id, format);
        if (score < 0) {
            continue;
        }

        if (format == 4u) {
            FrTtfCmap4 cmap4;
            FrTtfResult result = fr_ttf_validate_cmap4(font->cmap + offset, font->cmap_length - offset, &cmap4);
            if (result != FR_TTF_OK) {
                return result;
            }
            if (score > best_score4) {
                font->cmap4 = cmap4;
                best_score4 = score;
            }
        } else if (format == 12u) {
            FrTtfCmap12 cmap12;
            FrTtfResult result = fr_ttf_validate_cmap12(font->cmap + offset, font->cmap_length - offset, &cmap12);
            if (result != FR_TTF_OK) {
                return result;
            }
            if (score > best_score12) {
                font->cmap12 = cmap12;
                best_score12 = score;
            }
        }
    }

    if (best_score4 < 0 && best_score12 < 0) {
        return FR_TTF_ERR_UNSUPPORTED_CMAP;
    }
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_get_loca_offset(const FrTtfFont *font, uint32_t glyph_slot, uint32_t *out_offset) {
    if (glyph_slot > font->info.num_glyphs) {
        return FR_TTF_ERR_INVALID_GLYPH;
    }

    if (font->info.index_to_loc_format == 0) {
        *out_offset = (uint32_t)fr_ttf_read_u16(font->loca + (size_t)glyph_slot * 2u) * 2u;
        return FR_TTF_OK;
    }
    if (font->info.index_to_loc_format == 1) {
        *out_offset = fr_ttf_read_u32(font->loca + (size_t)glyph_slot * 4u);
        return FR_TTF_OK;
    }
    return FR_TTF_ERR_UNSUPPORTED_SFNT;
}

static FrTtfResult fr_ttf_validate_loca_glyf(FrTtfFont *font) {
    size_t required_loca_bytes;
    uint32_t i;
    uint32_t previous_offset = 0u;

    if (font->info.index_to_loc_format == 0) {
        if (fr_ttf_mul_overflow((size_t)font->info.num_glyphs + 1u, 2u, &required_loca_bytes)) {
            return FR_TTF_ERR_MALFORMED;
        }
    } else if (font->info.index_to_loc_format == 1) {
        if (fr_ttf_mul_overflow((size_t)font->info.num_glyphs + 1u, 4u, &required_loca_bytes)) {
            return FR_TTF_ERR_MALFORMED;
        }
    } else {
        return FR_TTF_ERR_UNSUPPORTED_SFNT;
    }
    if (font->loca_length < required_loca_bytes) {
        return FR_TTF_ERR_MALFORMED;
    }

    for (i = 0u; i <= font->info.num_glyphs; ++i) {
        uint32_t offset;
        FrTtfResult result = fr_ttf_get_loca_offset(font, i, &offset);
        if (result != FR_TTF_OK) {
            return result;
        }
        if (i != 0u && offset < previous_offset) {
            return FR_TTF_ERR_MALFORMED;
        }
        if (offset > font->glyf_length) {
            return FR_TTF_ERR_MALFORMED;
        }
        previous_offset = offset;
    }

    for (i = 0u; i < font->info.num_glyphs; ++i) {
        uint32_t glyph_offset;
        uint32_t next_offset;
        size_t glyph_length;
        const uint8_t *glyph_data;
        int16_t number_of_contours;
        int16_t x_min;
        int16_t y_min;
        int16_t x_max;
        int16_t y_max;
        FrTtfResult result = fr_ttf_get_loca_offset(font, i, &glyph_offset);
        if (result != FR_TTF_OK) {
            return result;
        }
        result = fr_ttf_get_loca_offset(font, i + 1u, &next_offset);
        if (result != FR_TTF_OK) {
            return result;
        }
        if (next_offset < glyph_offset) {
            return FR_TTF_ERR_MALFORMED;
        }
        glyph_length = (size_t)(next_offset - glyph_offset);
        if (glyph_length == 0u) {
            continue;
        }
        if (glyph_length < 10u) {
            return FR_TTF_ERR_MALFORMED;
        }

        glyph_data = font->glyf + glyph_offset;
        number_of_contours = fr_ttf_read_s16(glyph_data);
        x_min = fr_ttf_read_s16(glyph_data + 2u);
        y_min = fr_ttf_read_s16(glyph_data + 4u);
        x_max = fr_ttf_read_s16(glyph_data + 6u);
        y_max = fr_ttf_read_s16(glyph_data + 8u);

        if (number_of_contours < -1 || x_min > x_max || y_min > y_max) {
            return FR_TTF_ERR_MALFORMED;
        }
    }

    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_parse(FrTtfFont *font) {
    FrTtfTable *tables = NULL;
    uint16_t num_tables = 0u;
    const FrTtfTable *table;
    FrTtfResult result;

    result = fr_ttf_parse_directory(font, &tables, &num_tables);
    if (result != FR_TTF_OK) {
        return result;
    }

    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('h', 'e', 'a', 'd'));
    result = fr_ttf_assign_table(font, table, &font->head, &font->head_length);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, tables);
        return result;
    }
    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('m', 'a', 'x', 'p'));
    result = fr_ttf_assign_table(font, table, &font->maxp, &font->maxp_length);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, tables);
        return result;
    }
    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('h', 'h', 'e', 'a'));
    result = fr_ttf_assign_table(font, table, &font->hhea, &font->hhea_length);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, tables);
        return result;
    }
    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('h', 'm', 't', 'x'));
    result = fr_ttf_assign_table(font, table, &font->hmtx, &font->hmtx_length);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, tables);
        return result;
    }
    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('c', 'm', 'a', 'p'));
    result = fr_ttf_assign_table(font, table, &font->cmap, &font->cmap_length);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, tables);
        return result;
    }
    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('l', 'o', 'c', 'a'));
    result = fr_ttf_assign_table(font, table, &font->loca, &font->loca_length);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, tables);
        return result;
    }
    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('g', 'l', 'y', 'f'));
    result = fr_ttf_assign_table(font, table, &font->glyf, &font->glyf_length);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, tables);
        return result;
    }
    table = fr_ttf_find_table(tables, num_tables, FR_TTF_TAG('n', 'a', 'm', 'e'));
    if (table != NULL) {
        result = fr_ttf_assign_table(font, table, &font->name, &font->name_length);
        if (result != FR_TTF_OK) {
            fr_platform_free_with(&font->platform, tables);
            return result;
        }
    }

    fr_platform_free_with(&font->platform, tables);

    result = fr_ttf_parse_head(font);
    if (result != FR_TTF_OK) {
        return result;
    }
    result = fr_ttf_parse_maxp(font);
    if (result != FR_TTF_OK) {
        return result;
    }
    result = fr_ttf_parse_hhea(font);
    if (result != FR_TTF_OK) {
        return result;
    }
    result = fr_ttf_validate_hmtx(font);
    if (result != FR_TTF_OK) {
        return result;
    }
    result = fr_ttf_parse_cmap(font);
    if (result != FR_TTF_OK) {
        return result;
    }
    result = fr_ttf_validate_loca_glyf(font);
    if (result != FR_TTF_OK) {
        return result;
    }
    return FR_TTF_OK;
}

static FrTtfResult fr_ttf_lookup_cmap12(const FrTtfFont *font, uint32_t codepoint, uint16_t *out_glyph_index) {
    uint32_t low = 0u;
    uint32_t high = font->cmap12.n_groups;

    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        const uint8_t *group = font->cmap12.data + 16u + (size_t)mid * 12u;
        uint32_t start_char = fr_ttf_read_u32(group);
        uint32_t end_char = fr_ttf_read_u32(group + 4u);

        if (codepoint < start_char) {
            high = mid;
        } else if (codepoint > end_char) {
            low = mid + 1u;
        } else {
            uint32_t glyph_index = fr_ttf_read_u32(group + 8u) + (codepoint - start_char);
            if (glyph_index == 0u) {
                return FR_TTF_ERR_NOT_FOUND;
            }
            if (glyph_index >= font->info.num_glyphs) {
                return FR_TTF_ERR_MALFORMED;
            }
            *out_glyph_index = (uint16_t)glyph_index;
            return FR_TTF_OK;
        }
    }

    return FR_TTF_ERR_NOT_FOUND;
}

static FrTtfResult fr_ttf_lookup_cmap4(const FrTtfFont *font, uint32_t codepoint, uint16_t *out_glyph_index) {
    size_t end_codes_off = 14u;
    size_t start_codes_off = 16u + (size_t)font->cmap4.seg_count * 2u;
    size_t id_delta_off = start_codes_off + (size_t)font->cmap4.seg_count * 2u;
    size_t id_range_off = id_delta_off + (size_t)font->cmap4.seg_count * 2u;
    uint16_t low;
    uint16_t high;
    uint16_t segment;
    uint16_t start_code;
    uint16_t end_code;
    int16_t id_delta;
    uint16_t id_range_offset;
    uint32_t glyph_index;

    if (codepoint > 0xFFFFu) {
        return FR_TTF_ERR_NOT_FOUND;
    }

    low = 0u;
    high = font->cmap4.seg_count;
    while (low < high) {
        uint16_t mid = (uint16_t)(low + (uint16_t)((high - low) / 2u));
        uint16_t mid_end = fr_ttf_read_u16(font->cmap4.data + end_codes_off + (size_t)mid * 2u);
        if ((uint32_t)mid_end < codepoint) {
            low = (uint16_t)(mid + 1u);
        } else {
            high = mid;
        }
    }
    if (low >= font->cmap4.seg_count) {
        return FR_TTF_ERR_NOT_FOUND;
    }

    segment = low;
    start_code = fr_ttf_read_u16(font->cmap4.data + start_codes_off + (size_t)segment * 2u);
    end_code = fr_ttf_read_u16(font->cmap4.data + end_codes_off + (size_t)segment * 2u);
    if (codepoint < start_code || codepoint > end_code) {
        return FR_TTF_ERR_NOT_FOUND;
    }

    id_delta = fr_ttf_read_s16(font->cmap4.data + id_delta_off + (size_t)segment * 2u);
    id_range_offset = fr_ttf_read_u16(font->cmap4.data + id_range_off + (size_t)segment * 2u);
    if (id_range_offset == 0u) {
        glyph_index = (uint32_t)(((uint32_t)codepoint + (uint32_t)(int32_t)id_delta) & 0xFFFFu);
    } else {
        size_t range_word_off = id_range_off + (size_t)segment * 2u;
        size_t glyph_word_off;

        if (fr_ttf_add_overflow(range_word_off, id_range_offset, &glyph_word_off) ||
            fr_ttf_add_overflow(glyph_word_off, (size_t)(codepoint - start_code) * 2u, &glyph_word_off) ||
            glyph_word_off + 2u > font->cmap4.length) {
            return FR_TTF_ERR_MALFORMED;
        }

        glyph_index = fr_ttf_read_u16(font->cmap4.data + glyph_word_off);
        if (glyph_index != 0u) {
            glyph_index = (uint32_t)((glyph_index + (uint32_t)(int32_t)id_delta) & 0xFFFFu);
        }
    }

    if (glyph_index == 0u) {
        return FR_TTF_ERR_NOT_FOUND;
    }
    if (glyph_index >= font->info.num_glyphs) {
        return FR_TTF_ERR_MALFORMED;
    }

    *out_glyph_index = (uint16_t)glyph_index;
    return FR_TTF_OK;
}

FrTtfResult fr_ttf_open(FrTtfFont **out_font, const char *path) {
    FrTtfFont *font;
    FrPlatform platform;
    FrTtfResult result;

    if (out_font == NULL || path == NULL) {
        return FR_TTF_ERR_INVALID_ARGUMENT;
    }
    *out_font = NULL;

    fr_platform_resolve(&platform);
    font = (FrTtfFont *)fr_platform_calloc_with(&platform, 1u, sizeof(*font));
    if (font == NULL) {
        return FR_TTF_ERR_NO_MEMORY;
    }
    font->platform = platform;

    result = fr_ttf_load_file(font, path);
    if (result != FR_TTF_OK) {
        fr_platform_free_with(&font->platform, font);
        return result;
    }

    result = fr_ttf_parse(font);
    if (result != FR_TTF_OK) {
        fr_ttf_close(font);
        return result;
    }

    *out_font = font;
    return FR_TTF_OK;
}

FrTtfResult fr_ttf_open_memory(FrTtfFont **out_font, const void *data, size_t size) {
    FrTtfFont *font;
    FrPlatform platform;
    FrTtfResult result;

    if (out_font == NULL || data == NULL) {
        return FR_TTF_ERR_INVALID_ARGUMENT;
    }
    *out_font = NULL;

    fr_platform_resolve(&platform);
    font = (FrTtfFont *)fr_platform_calloc_with(&platform, 1u, sizeof(*font));
    if (font == NULL) {
        return FR_TTF_ERR_NO_MEMORY;
    }
    font->platform = platform;
    font->data = (const uint8_t *)data;
    font->data_size = size;

    result = fr_ttf_parse(font);
    if (result != FR_TTF_OK) {
        fr_ttf_close(font);
        return result;
    }

    *out_font = font;
    return FR_TTF_OK;
}

void fr_ttf_close(FrTtfFont *font) {
    if (font == NULL) {
        return;
    }
    if (font->loaded_file.data != NULL) {
        fr_platform_unload_file_with(&font->platform, &font->loaded_file);
    }
    fr_platform_free_with(&font->platform, font);
}

const char *fr_ttf_result_string(FrTtfResult result) {
    switch (result) {
        case FR_TTF_OK:
            return "ok";
        case FR_TTF_ERR_INVALID_ARGUMENT:
            return "invalid argument";
        case FR_TTF_ERR_IO:
            return "i/o error";
        case FR_TTF_ERR_NO_MEMORY:
            return "out of memory";
        case FR_TTF_ERR_UNSUPPORTED_SFNT:
            return "unsupported sfnt";
        case FR_TTF_ERR_MALFORMED:
            return "malformed font";
        case FR_TTF_ERR_MISSING_TABLE:
            return "missing required table";
        case FR_TTF_ERR_UNSUPPORTED_CMAP:
            return "unsupported cmap";
        case FR_TTF_ERR_INVALID_GLYPH:
            return "invalid glyph";
        case FR_TTF_ERR_NOT_FOUND:
            return "not found";
        default:
            return "unknown";
    }
}

FrTtfResult fr_ttf_get_info(const FrTtfFont *font, FrTtfInfo *out_info) {
    if (font == NULL || out_info == NULL) {
        return FR_TTF_ERR_INVALID_ARGUMENT;
    }
    *out_info = font->info;
    return FR_TTF_OK;
}

FrTtfResult fr_ttf_lookup_glyph(const FrTtfFont *font, uint32_t codepoint, uint16_t *out_glyph_index) {
    FrTtfResult result;

    if (font == NULL || out_glyph_index == NULL || codepoint > 0x10FFFFu) {
        return FR_TTF_ERR_INVALID_ARGUMENT;
    }

    if (font->cmap12.data != NULL) {
        result = fr_ttf_lookup_cmap12(font, codepoint, out_glyph_index);
        if (result == FR_TTF_OK || result == FR_TTF_ERR_MALFORMED) {
            return result;
        }
    }
    if (font->cmap4.data != NULL) {
        result = fr_ttf_lookup_cmap4(font, codepoint, out_glyph_index);
        if (result == FR_TTF_OK || result == FR_TTF_ERR_MALFORMED) {
            return result;
        }
    }

    return FR_TTF_ERR_NOT_FOUND;
}

FrTtfResult fr_ttf_get_hmetric(const FrTtfFont *font, uint16_t glyph_index, FrTtfHMetric *out_metric) {
    size_t long_metric_off;
    size_t lsb_off;
    uint16_t metric_index;

    if (font == NULL || out_metric == NULL) {
        return FR_TTF_ERR_INVALID_ARGUMENT;
    }
    if (glyph_index >= font->info.num_glyphs) {
        return FR_TTF_ERR_INVALID_GLYPH;
    }

    metric_index = glyph_index < font->info.number_of_h_metrics ? glyph_index : (uint16_t)(font->info.number_of_h_metrics - 1u);
    long_metric_off = (size_t)metric_index * 4u;
    out_metric->advance_width = fr_ttf_read_u16(font->hmtx + long_metric_off);

    if (glyph_index < font->info.number_of_h_metrics) {
        out_metric->left_side_bearing = fr_ttf_read_s16(font->hmtx + long_metric_off + 2u);
    } else {
        lsb_off = (size_t)font->info.number_of_h_metrics * 4u +
                  (size_t)(glyph_index - font->info.number_of_h_metrics) * 2u;
        out_metric->left_side_bearing = fr_ttf_read_s16(font->hmtx + lsb_off);
    }
    return FR_TTF_OK;
}

FrTtfResult fr_ttf_get_glyph(const FrTtfFont *font, uint16_t glyph_index, FrTtfGlyph *out_glyph) {
    uint32_t glyph_offset;
    uint32_t next_offset;
    size_t glyph_length;
    FrTtfResult result;

    if (font == NULL || out_glyph == NULL) {
        return FR_TTF_ERR_INVALID_ARGUMENT;
    }
    if (glyph_index >= font->info.num_glyphs) {
        return FR_TTF_ERR_INVALID_GLYPH;
    }

    result = fr_ttf_get_loca_offset(font, glyph_index, &glyph_offset);
    if (result != FR_TTF_OK) {
        return result;
    }
    result = fr_ttf_get_loca_offset(font, (uint32_t)glyph_index + 1u, &next_offset);
    if (result != FR_TTF_OK) {
        return result;
    }
    if (next_offset < glyph_offset) {
        return FR_TTF_ERR_MALFORMED;
    }

    glyph_length = (size_t)(next_offset - glyph_offset);
    fr_platform_memset_with(&font->platform, out_glyph, 0, sizeof(*out_glyph));
    out_glyph->glyph_index = glyph_index;
    out_glyph->data = glyph_length == 0u ? NULL : font->glyf + glyph_offset;
    out_glyph->data_length = glyph_length;

    if (glyph_length == 0u) {
        return FR_TTF_OK;
    }
    if (glyph_length < 10u) {
        return FR_TTF_ERR_MALFORMED;
    }

    out_glyph->number_of_contours = fr_ttf_read_s16(out_glyph->data);
    out_glyph->x_min = fr_ttf_read_s16(out_glyph->data + 2u);
    out_glyph->y_min = fr_ttf_read_s16(out_glyph->data + 4u);
    out_glyph->x_max = fr_ttf_read_s16(out_glyph->data + 6u);
    out_glyph->y_max = fr_ttf_read_s16(out_glyph->data + 8u);

    if (out_glyph->number_of_contours < -1 || out_glyph->x_min > out_glyph->x_max || out_glyph->y_min > out_glyph->y_max) {
        return FR_TTF_ERR_MALFORMED;
    }

    return FR_TTF_OK;
}
