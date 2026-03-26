/*
 * input_text.c - LightShell Text Input Handler
 *
 * Handles keyboard input for <input> and <textarea> elements:
 * - Character insertion at cursor
 * - Backspace / Delete
 * - Arrow key navigation
 * - Cmd+A/C/V/X clipboard operations
 * - Home/End cursor movement
 * - Enter handling (newline for textarea, submit for input)
 */

#include "input_text.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* macOS virtual keycodes */
#define KEY_RETURN    0x24
#define KEY_DELETE    0x33  /* backspace */
#define KEY_FWD_DEL   0x75  /* forward delete */
#define KEY_LEFT      0x7B
#define KEY_RIGHT     0x7C
#define KEY_HOME      0x73
#define KEY_END       0x77
#define KEY_A         0x00
#define KEY_C         0x08
#define KEY_V         0x09
#define KEY_X         0x07

/* --- Lifecycle --- */

LSTextInput *ls_text_input_create(LSInputType type) {
    LSTextInput *input = calloc(1, sizeof(LSTextInput));
    if (!input) return NULL;

    input->capacity = LS_TEXT_INPUT_INITIAL_CAPACITY;
    input->buffer = calloc(1, input->capacity);
    if (!input->buffer) {
        free(input);
        return NULL;
    }

    input->length = 0;
    input->cursor_pos = 0;
    input->selection_start = LS_TEXT_INPUT_NO_SELECTION;
    input->selection_end = LS_TEXT_INPUT_NO_SELECTION;
    input->focused = false;
    input->type = type;

    return input;
}

void ls_text_input_destroy(LSTextInput *input) {
    if (!input) return;
    free(input->buffer);
    free(input);
}

/* --- Internal helpers --- */

static void ensure_capacity(LSTextInput *input, uint32_t needed) {
    if (input->length + needed < input->capacity) return;

    uint32_t new_cap = input->capacity * 2;
    while (new_cap < input->length + needed + 1) {
        new_cap *= 2;
    }

    char *new_buf = realloc(input->buffer, new_cap);
    if (!new_buf) return;  /* OOM: silently fail */

    input->buffer = new_buf;
    input->capacity = new_cap;
}

static bool has_selection(LSTextInput *input) {
    return input->selection_start != LS_TEXT_INPUT_NO_SELECTION &&
           input->selection_end != LS_TEXT_INPUT_NO_SELECTION &&
           input->selection_start != input->selection_end;
}

/* --- Focus management --- */

void ls_text_input_focus(LSTextInput *input) {
    if (!input) return;
    input->focused = true;
}

void ls_text_input_blur(LSTextInput *input) {
    if (!input) return;
    input->focused = false;
    ls_text_input_clear_selection(input);
}

/* --- Text manipulation --- */

void ls_text_input_insert(LSTextInput *input, const char *text, int len) {
    if (!input || !text || len <= 0) return;

    /* If there's a selection, delete it first */
    if (has_selection(input)) {
        ls_text_input_delete_selection(input);
    }

    ensure_capacity(input, (uint32_t)len);

    /* Shift text after cursor to make room */
    if (input->cursor_pos < input->length) {
        memmove(input->buffer + input->cursor_pos + len,
                input->buffer + input->cursor_pos,
                input->length - input->cursor_pos);
    }

    /* Insert new text */
    memcpy(input->buffer + input->cursor_pos, text, (size_t)len);
    input->length += (uint32_t)len;
    input->cursor_pos += (uint32_t)len;
    input->buffer[input->length] = '\0';
}

void ls_text_input_delete_before(LSTextInput *input) {
    if (!input) return;

    if (has_selection(input)) {
        ls_text_input_delete_selection(input);
        return;
    }

    if (input->cursor_pos == 0) return;

    /* Simple single-byte delete for ASCII; for full UTF-8 you'd walk back */
    uint32_t del_pos = input->cursor_pos - 1;

    /* Walk back over UTF-8 continuation bytes */
    while (del_pos > 0 && (input->buffer[del_pos] & 0xC0) == 0x80) {
        del_pos--;
    }

    uint32_t del_len = input->cursor_pos - del_pos;

    memmove(input->buffer + del_pos,
            input->buffer + input->cursor_pos,
            input->length - input->cursor_pos);

    input->length -= del_len;
    input->cursor_pos = del_pos;
    input->buffer[input->length] = '\0';
}

void ls_text_input_delete_after(LSTextInput *input) {
    if (!input) return;

    if (has_selection(input)) {
        ls_text_input_delete_selection(input);
        return;
    }

    if (input->cursor_pos >= input->length) return;

    /* Determine character length (UTF-8) */
    uint32_t del_len = 1;
    unsigned char ch = (unsigned char)input->buffer[input->cursor_pos];
    if (ch >= 0xC0) {
        if (ch < 0xE0) del_len = 2;
        else if (ch < 0xF0) del_len = 3;
        else del_len = 4;
    }

    if (input->cursor_pos + del_len > input->length) {
        del_len = input->length - input->cursor_pos;
    }

    memmove(input->buffer + input->cursor_pos,
            input->buffer + input->cursor_pos + del_len,
            input->length - input->cursor_pos - del_len);

    input->length -= del_len;
    input->buffer[input->length] = '\0';
}

/* --- Cursor movement --- */

void ls_text_input_move_left(LSTextInput *input) {
    if (!input || input->cursor_pos == 0) return;
    ls_text_input_clear_selection(input);

    /* Walk back over UTF-8 continuation bytes */
    input->cursor_pos--;
    while (input->cursor_pos > 0 &&
           (input->buffer[input->cursor_pos] & 0xC0) == 0x80) {
        input->cursor_pos--;
    }
}

void ls_text_input_move_right(LSTextInput *input) {
    if (!input || input->cursor_pos >= input->length) return;
    ls_text_input_clear_selection(input);

    /* Skip over UTF-8 character */
    unsigned char ch = (unsigned char)input->buffer[input->cursor_pos];
    uint32_t skip = 1;
    if (ch >= 0xC0 && ch < 0xE0) skip = 2;
    else if (ch >= 0xE0 && ch < 0xF0) skip = 3;
    else if (ch >= 0xF0) skip = 4;

    input->cursor_pos += skip;
    if (input->cursor_pos > input->length) {
        input->cursor_pos = input->length;
    }
}

void ls_text_input_move_home(LSTextInput *input) {
    if (!input) return;
    ls_text_input_clear_selection(input);
    input->cursor_pos = 0;
}

void ls_text_input_move_end(LSTextInput *input) {
    if (!input) return;
    ls_text_input_clear_selection(input);
    input->cursor_pos = input->length;
}

/* --- Selection --- */

void ls_text_input_select_all(LSTextInput *input) {
    if (!input) return;
    input->selection_start = 0;
    input->selection_end = input->length;
    input->cursor_pos = input->length;
}

void ls_text_input_clear_selection(LSTextInput *input) {
    if (!input) return;
    input->selection_start = LS_TEXT_INPUT_NO_SELECTION;
    input->selection_end = LS_TEXT_INPUT_NO_SELECTION;
}

char *ls_text_input_get_selected_text(LSTextInput *input) {
    if (!input || !has_selection(input)) return NULL;

    uint32_t start = input->selection_start;
    uint32_t end = input->selection_end;
    if (start > end) { uint32_t t = start; start = end; end = t; }
    if (end > input->length) end = input->length;

    uint32_t sel_len = end - start;
    char *text = malloc(sel_len + 1);
    if (!text) return NULL;

    memcpy(text, input->buffer + start, sel_len);
    text[sel_len] = '\0';
    return text;
}

void ls_text_input_delete_selection(LSTextInput *input) {
    if (!input || !has_selection(input)) return;

    uint32_t start = input->selection_start;
    uint32_t end = input->selection_end;
    if (start > end) { uint32_t t = start; start = end; end = t; }
    if (end > input->length) end = input->length;

    uint32_t sel_len = end - start;

    memmove(input->buffer + start,
            input->buffer + end,
            input->length - end);

    input->length -= sel_len;
    input->cursor_pos = start;
    input->buffer[input->length] = '\0';

    ls_text_input_clear_selection(input);
}

/* --- Set/get value --- */

void ls_text_input_set_value(LSTextInput *input, const char *text) {
    if (!input) return;

    uint32_t len = text ? (uint32_t)strlen(text) : 0;

    if (len >= input->capacity) {
        uint32_t new_cap = len + 1;
        char *new_buf = realloc(input->buffer, new_cap);
        if (!new_buf) return;
        input->buffer = new_buf;
        input->capacity = new_cap;
    }

    if (text && len > 0) {
        memcpy(input->buffer, text, len);
    }
    input->buffer[len] = '\0';
    input->length = len;
    input->cursor_pos = len;
    ls_text_input_clear_selection(input);
}

const char *ls_text_input_get_value(LSTextInput *input) {
    if (!input) return "";
    return input->buffer;
}

/* --- Key event handler --- */

bool ls_text_input_handle_key(LSTextInput *input, PlatformEvent *event) {
    if (!input || !input->focused) return false;
    if (event->type != PLATFORM_EVENT_KEY_DOWN) return false;

    uint32_t key = event->keycode;

    /* Cmd+key shortcuts */
    if (event->meta) {
        switch (key) {
            case KEY_A:
                ls_text_input_select_all(input);
                return true;

            case KEY_C: {
                /* Copy — caller should read selected text and put on clipboard */
                /* We just return true to indicate the event was handled */
                return true;
            }

            case KEY_X: {
                /* Cut — caller should read selected text, put on clipboard, then delete */
                return true;
            }

            case KEY_V: {
                /* Paste — caller should get clipboard text and call insert */
                return true;
            }

            default:
                break;
        }
    }

    switch (key) {
        case KEY_DELETE:
            ls_text_input_delete_before(input);
            return true;

        case KEY_FWD_DEL:
            ls_text_input_delete_after(input);
            return true;

        case KEY_LEFT:
            ls_text_input_move_left(input);
            return true;

        case KEY_RIGHT:
            ls_text_input_move_right(input);
            return true;

        case KEY_HOME:
            ls_text_input_move_home(input);
            return true;

        case KEY_END:
            ls_text_input_move_end(input);
            return true;

        case KEY_RETURN:
            if (input->type == LS_INPUT_TYPE_TEXTAREA) {
                ls_text_input_insert(input, "\n", 1);
            }
            /* For LS_INPUT_TYPE_INPUT, Enter triggers submit (caller handles) */
            return true;

        default:
            break;
    }

    /* Printable character insertion is handled via platform_get_key_char()
     * by the caller, who then calls ls_text_input_insert(). */

    return false;
}
