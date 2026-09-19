#include "fontrender/font_backend.h"
#include "fontrender_runtime.h"
#include "runtime.h"

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FONT.ttf [CODEPOINT] [PIXEL_SIZE]\n");
}

static int parse_codepoint(const char *text, unsigned int *codepoint_out) {
    unsigned long long value;

    if (text == 0 || text[0] == '\0') return -1;
    if (rt_parse_uint(text, &value) == 0) {
        if (value > 0x10ffffULL) return -1;
        *codepoint_out = (unsigned int)value;
        return 0;
    }
    *codepoint_out = (unsigned char)text[0];
    return 0;
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "test-font";
    FrFont *font = 0;
    const FrGlyph *glyph;
    unsigned int codepoint = 'A';
    unsigned long long size_value = 32ULL;
    unsigned long long nonzero_pixels = 0ULL;
    int pixel_size;
    int index;

    if (argc < 2 || argc > 4) {
        write_usage(program);
        return 1;
    }
    if (argc >= 3 && parse_codepoint(argv[2], &codepoint) != 0) {
        write_usage(program);
        return 1;
    }
    if (argc >= 4 && (rt_parse_uint(argv[3], &size_value) != 0 || size_value == 0ULL || size_value > 512ULL)) {
        write_usage(program);
        return 1;
    }
    pixel_size = (int)size_value;
    if (fontrender_runtime_install() != 0 || fr_font_open(&font, argv[1]) != 0) {
        rt_write_cstr(2, "test-font: could not open font\n");
        return 1;
    }
    glyph = fr_font_get_glyph(font, codepoint, pixel_size, 0U);
    if (glyph == 0) {
        rt_write_cstr(2, "test-font: could not render glyph\n");
        fr_font_close(font);
        return 1;
    }
    for (index = 0; index < glyph->width * glyph->height; ++index) {
        if (glyph->bitmap[index] != 0U) nonzero_pixels += 1ULL;
    }
    rt_write_cstr(1, "codepoint: ");
    rt_write_uint(1, codepoint);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "pixel_size: ");
    rt_write_uint(1, (unsigned long long)pixel_size);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "line_height: ");
    rt_write_int(1, fr_font_line_height(font, pixel_size));
    rt_write_char(1, '\n');
    rt_write_cstr(1, "advance: ");
    rt_write_int(1, glyph->advance);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "left: ");
    rt_write_int(1, glyph->left);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "top: ");
    rt_write_int(1, glyph->top);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "width: ");
    rt_write_int(1, glyph->width);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "height: ");
    rt_write_int(1, glyph->height);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "nonzero_pixels: ");
    rt_write_uint(1, nonzero_pixels);
    rt_write_char(1, '\n');
    fr_font_close(font);
    return 0;
}