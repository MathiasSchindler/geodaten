#ifndef NEWOS_TUI_H
#define NEWOS_TUI_H

#include "platform.h"

#include <stddef.h>

typedef struct {
    int input_fd;
    int output_fd;
    PlatformTerminalState saved_state;
    unsigned int rows;
    unsigned int columns;
    int raw_enabled;
    int alternate_screen;
    int cursor_hidden;
} TuiTerminal;

typedef enum {
    TUI_KEY_NONE = 0,
    TUI_KEY_CHARACTER,
    TUI_KEY_CTRL,
    TUI_KEY_SPECIAL
} TuiKeyType;

typedef enum {
    TUI_KEY_ESCAPE = 1,
    TUI_KEY_ENTER,
    TUI_KEY_BACKSPACE,
    TUI_KEY_DELETE,
    TUI_KEY_ARROW_UP,
    TUI_KEY_ARROW_DOWN,
    TUI_KEY_ARROW_LEFT,
    TUI_KEY_ARROW_RIGHT,
    TUI_KEY_HOME,
    TUI_KEY_END,
    TUI_KEY_PAGE_UP,
    TUI_KEY_PAGE_DOWN
} TuiSpecialKey;

typedef enum {
    TUI_MOD_SHIFT = 1U << 0,
    TUI_MOD_ALT = 1U << 1,
    TUI_MOD_CTRL = 1U << 2
} TuiKeyModifier;

typedef struct {
    TuiKeyType type;
    unsigned int codepoint;
    unsigned int modifiers;
    char text[8];
    size_t text_length;
} TuiKeyEvent;

typedef enum {
    TUI_STYLE_NORMAL = 0,
    TUI_STYLE_INVERSE,
    TUI_STYLE_COMMENT,
    TUI_STYLE_STRING,
    TUI_STYLE_KEYWORD,
    TUI_STYLE_NUMBER,
    TUI_STYLE_MATCH
} TuiTextStyle;

int tui_terminal_open(TuiTerminal *terminal, int input_fd, int output_fd, int use_alternate_screen);
void tui_terminal_close(TuiTerminal *terminal);
int tui_terminal_refresh_size(TuiTerminal *terminal);
int tui_read_key(TuiTerminal *terminal, TuiKeyEvent *event_out);

int tui_write(TuiTerminal *terminal, const char *text, size_t length);
int tui_write_cstr(TuiTerminal *terminal, const char *text);
int tui_clear_screen(TuiTerminal *terminal);
int tui_clear_line(TuiTerminal *terminal);
int tui_move_cursor(TuiTerminal *terminal, unsigned int row, unsigned int column);
int tui_hide_cursor(TuiTerminal *terminal);
int tui_show_cursor(TuiTerminal *terminal);
int tui_enter_alternate_screen(TuiTerminal *terminal);
int tui_leave_alternate_screen(TuiTerminal *terminal);
int tui_set_inverse(TuiTerminal *terminal, int enabled);
int tui_set_style(TuiTerminal *terminal, int style);

#endif