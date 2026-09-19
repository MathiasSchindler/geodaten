#include "tui.h"

#include "runtime.h"

static int tui_write_uint(TuiTerminal *terminal, unsigned int value) {
    char buffer[32];
    rt_unsigned_to_string((unsigned long long)value, buffer, sizeof(buffer));
    return tui_write_cstr(terminal, buffer);
}

int tui_write(TuiTerminal *terminal, const char *text, size_t length) {
    if (terminal == 0 || text == 0) return -1;
    return rt_write_all(terminal->output_fd, text, length);
}

int tui_write_cstr(TuiTerminal *terminal, const char *text) {
    if (text == 0) return -1;
    return tui_write(terminal, text, rt_strlen(text));
}

int tui_terminal_refresh_size(TuiTerminal *terminal) {
    unsigned int rows = 0U;
    unsigned int columns = 0U;

    if (terminal == 0) return -1;
    if (platform_get_terminal_size(terminal->output_fd, &rows, &columns) != 0) {
        rows = 24U;
        columns = 80U;
    }
    terminal->rows = rows == 0U ? 24U : rows;
    terminal->columns = columns == 0U ? 80U : columns;
    return 0;
}

int tui_enter_alternate_screen(TuiTerminal *terminal) {
    if (tui_write_cstr(terminal, "\033[?1049h") != 0) return -1;
    terminal->alternate_screen = 1;
    return 0;
}

int tui_leave_alternate_screen(TuiTerminal *terminal) {
    if (terminal == 0 || !terminal->alternate_screen) return 0;
    terminal->alternate_screen = 0;
    return tui_write_cstr(terminal, "\033[?1049l");
}

int tui_hide_cursor(TuiTerminal *terminal) {
    if (tui_write_cstr(terminal, "\033[?25l") != 0) return -1;
    terminal->cursor_hidden = 1;
    return 0;
}

int tui_show_cursor(TuiTerminal *terminal) {
    if (terminal == 0 || !terminal->cursor_hidden) return 0;
    terminal->cursor_hidden = 0;
    return tui_write_cstr(terminal, "\033[?25h");
}

int tui_terminal_open(TuiTerminal *terminal, int input_fd, int output_fd, int use_alternate_screen) {
    if (terminal == 0) return -1;

    rt_memset(terminal, 0, sizeof(*terminal));
    terminal->input_fd = input_fd;
    terminal->output_fd = output_fd;

    if (!platform_isatty(input_fd) || !platform_isatty(output_fd)) return -1;
    if (platform_terminal_enable_raw_mode(input_fd, &terminal->saved_state) != 0) return -1;
    terminal->raw_enabled = 1;
    (void)tui_terminal_refresh_size(terminal);
    if (use_alternate_screen && tui_enter_alternate_screen(terminal) != 0) {
        tui_terminal_close(terminal);
        return -1;
    }
    (void)tui_hide_cursor(terminal);
    return 0;
}

void tui_terminal_close(TuiTerminal *terminal) {
    if (terminal == 0) return;
    (void)tui_show_cursor(terminal);
    (void)tui_set_inverse(terminal, 0);
    (void)tui_leave_alternate_screen(terminal);
    if (terminal->raw_enabled) {
        (void)platform_terminal_restore_mode(terminal->input_fd, &terminal->saved_state);
        terminal->raw_enabled = 0;
    }
}

int tui_clear_screen(TuiTerminal *terminal) {
    return tui_write_cstr(terminal, "\033[2J");
}

int tui_clear_line(TuiTerminal *terminal) {
    return tui_write_cstr(terminal, "\033[K");
}

int tui_move_cursor(TuiTerminal *terminal, unsigned int row, unsigned int column) {
    if (row == 0U) row = 1U;
    if (column == 0U) column = 1U;
    if (tui_write_cstr(terminal, "\033[") != 0) return -1;
    if (tui_write_uint(terminal, row) != 0) return -1;
    if (tui_write_cstr(terminal, ";") != 0) return -1;
    if (tui_write_uint(terminal, column) != 0) return -1;
    return tui_write_cstr(terminal, "H");
}

int tui_set_inverse(TuiTerminal *terminal, int enabled) {
    return tui_write_cstr(terminal, enabled ? "\033[7m" : "\033[0m");
}

int tui_set_style(TuiTerminal *terminal, int style) {
    switch (style) {
        case TUI_STYLE_INVERSE: return tui_write_cstr(terminal, "\033[7m");
        case TUI_STYLE_COMMENT: return tui_write_cstr(terminal, "\033[32m");
        case TUI_STYLE_STRING: return tui_write_cstr(terminal, "\033[33m");
        case TUI_STYLE_KEYWORD: return tui_write_cstr(terminal, "\033[36m");
        case TUI_STYLE_NUMBER: return tui_write_cstr(terminal, "\033[35m");
        case TUI_STYLE_MATCH: return tui_write_cstr(terminal, "\033[30;47m");
        default: return tui_write_cstr(terminal, "\033[0m");
    }
}

static int tui_read_byte(TuiTerminal *terminal, unsigned char *byte_out) {
    long bytes;

    if (terminal == 0 || byte_out == 0) return -1;
    bytes = platform_read(terminal->input_fd, byte_out, 1U);
    return bytes == 1 ? 0 : -1;
}

static unsigned int tui_csi_modifiers(unsigned int value) {
    unsigned int modifiers = 0U;

    if (value < 2U) {
        return 0U;
    }
    value -= 1U;
    if ((value & 1U) != 0U) modifiers |= TUI_MOD_SHIFT;
    if ((value & 2U) != 0U) modifiers |= TUI_MOD_ALT;
    if ((value & 4U) != 0U) modifiers |= TUI_MOD_CTRL;
    return modifiers;
}

static void tui_event_special_mod(TuiKeyEvent *event, unsigned int codepoint, unsigned int modifiers) {
    rt_memset(event, 0, sizeof(*event));
    event->type = TUI_KEY_SPECIAL;
    event->codepoint = codepoint;
    event->modifiers = modifiers;
}

static void tui_event_special(TuiKeyEvent *event, unsigned int codepoint) {
    tui_event_special_mod(event, codepoint, 0U);
}

static void tui_event_ctrl(TuiKeyEvent *event, unsigned int codepoint) {
    rt_memset(event, 0, sizeof(*event));
    event->type = TUI_KEY_CTRL;
    event->codepoint = codepoint;
}

static void tui_event_character(TuiKeyEvent *event, const char *text, size_t length, unsigned int codepoint) {
    rt_memset(event, 0, sizeof(*event));
    event->type = TUI_KEY_CHARACTER;
    event->codepoint = codepoint;
    if (length > sizeof(event->text)) length = sizeof(event->text);
    memcpy(event->text, text, length);
    event->text_length = length;
}

static int tui_read_utf8_tail(TuiTerminal *terminal, unsigned char lead, TuiKeyEvent *event_out) {
    char text[4];
    size_t length;
    size_t index = 1U;
    size_t decode_index = 0U;
    unsigned int codepoint = 0U;

    text[0] = (char)lead;
    if ((lead & 0xe0U) == 0xc0U) length = 2U;
    else if ((lead & 0xf0U) == 0xe0U) length = 3U;
    else if ((lead & 0xf8U) == 0xf0U) length = 4U;
    else {
        tui_event_character(event_out, "?", 1U, '?');
        return 0;
    }

    while (index < length) {
        unsigned char next = 0U;
        if (tui_read_byte(terminal, &next) != 0) {
            tui_event_character(event_out, "?", 1U, '?');
            return 0;
        }
        text[index] = (char)next;
        index += 1U;
    }

    if (rt_utf8_decode(text, length, &decode_index, &codepoint) != 0 || decode_index != length) {
        tui_event_character(event_out, "?", 1U, '?');
        return 0;
    }
    tui_event_character(event_out, text, length, codepoint);
    return 0;
}

static int tui_decode_csi(TuiTerminal *terminal, TuiKeyEvent *event_out) {
    unsigned char ch = 0U;
    unsigned int params[3];
    unsigned int param_count = 0U;
    unsigned int number = 0U;
    int has_number = 0;

    rt_memset(params, 0, sizeof(params));

    for (;;) {
        if (tui_read_byte(terminal, &ch) != 0) {
            tui_event_special(event_out, TUI_KEY_ESCAPE);
            return 0;
        }
        if (ch >= '0' && ch <= '9') {
            has_number = 1;
            number = number * 10U + (unsigned int)(ch - '0');
            continue;
        }
        if (ch == ';') {
            if (param_count < sizeof(params) / sizeof(params[0])) {
                params[param_count++] = has_number ? number : 0U;
            }
            number = 0U;
            has_number = 0;
            continue;
        }
        if (has_number && param_count < sizeof(params) / sizeof(params[0])) {
            params[param_count++] = number;
        }
        if (ch == '~') {
            unsigned int key = param_count > 0U ? params[0] : 0U;
            unsigned int modifiers = param_count > 1U ? tui_csi_modifiers(params[1]) : 0U;
            switch (key) {
                case 1U: case 7U: tui_event_special_mod(event_out, TUI_KEY_HOME, modifiers); return 0;
                case 3U: tui_event_special_mod(event_out, TUI_KEY_DELETE, modifiers); return 0;
                case 4U: case 8U: tui_event_special_mod(event_out, TUI_KEY_END, modifiers); return 0;
                case 5U: tui_event_special_mod(event_out, TUI_KEY_PAGE_UP, modifiers); return 0;
                case 6U: tui_event_special_mod(event_out, TUI_KEY_PAGE_DOWN, modifiers); return 0;
                default: tui_event_special(event_out, TUI_KEY_ESCAPE); return 0;
            }
        }
        {
            unsigned int modifiers = param_count > 1U ? tui_csi_modifiers(params[1]) : 0U;
            switch (ch) {
                case 'A': tui_event_special_mod(event_out, TUI_KEY_ARROW_UP, modifiers); return 0;
                case 'B': tui_event_special_mod(event_out, TUI_KEY_ARROW_DOWN, modifiers); return 0;
                case 'C': tui_event_special_mod(event_out, TUI_KEY_ARROW_RIGHT, modifiers); return 0;
                case 'D': tui_event_special_mod(event_out, TUI_KEY_ARROW_LEFT, modifiers); return 0;
                case 'H': tui_event_special_mod(event_out, TUI_KEY_HOME, modifiers); return 0;
                case 'F': tui_event_special_mod(event_out, TUI_KEY_END, modifiers); return 0;
                default: break;
            }
        }
        tui_event_special(event_out, TUI_KEY_ESCAPE);
        return 0;
    }
}

int tui_read_key(TuiTerminal *terminal, TuiKeyEvent *event_out) {
    unsigned char ch = 0U;

    if (event_out == 0) return -1;
    if (tui_read_byte(terminal, &ch) != 0) return -1;

    if (ch == 27U) {
        unsigned char next = 0U;
        if (tui_read_byte(terminal, &next) != 0) {
            tui_event_special(event_out, TUI_KEY_ESCAPE);
            return 0;
        }
        if (next == '[') return tui_decode_csi(terminal, event_out);
        if (next == 'O' && tui_read_byte(terminal, &next) == 0) {
            if (next == 'H') {
                tui_event_special(event_out, TUI_KEY_HOME);
                return 0;
            }
            if (next == 'F') {
                tui_event_special(event_out, TUI_KEY_END);
                return 0;
            }
        }
        tui_event_special(event_out, TUI_KEY_ESCAPE);
        return 0;
    }

    if (ch == '\r' || ch == '\n') {
        tui_event_special(event_out, TUI_KEY_ENTER);
        return 0;
    }
    if (ch == 127U || ch == 8U) {
        tui_event_special(event_out, TUI_KEY_BACKSPACE);
        return 0;
    }
    if (ch > 0U && ch < 27U) {
        tui_event_ctrl(event_out, (unsigned int)('A' + ch - 1U));
        return 0;
    }
    if (ch == '\t') {
        tui_event_character(event_out, "\t", 1U, '\t');
        return 0;
    }
    if (ch >= 32U && ch < 127U) {
        char text = (char)ch;
        tui_event_character(event_out, &text, 1U, (unsigned int)ch);
        return 0;
    }
    if (ch >= 128U) return tui_read_utf8_tail(terminal, ch, event_out);

    rt_memset(event_out, 0, sizeof(*event_out));
    event_out->type = TUI_KEY_NONE;
    return 0;
}