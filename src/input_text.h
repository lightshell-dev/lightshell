/*
 * input_text.h - LightShell Text Input Handler
 *
 * Handles keyboard input for <input> and <textarea> elements.
 */

#ifndef LIGHTSHELL_INPUT_TEXT_H
#define LIGHTSHELL_INPUT_TEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LS_TEXT_INPUT_INITIAL_CAPACITY 256
#define LS_TEXT_INPUT_NO_SELECTION ((uint32_t)-1)

typedef enum {
    LS_INPUT_TYPE_INPUT,    /* single-line <input> */
    LS_INPUT_TYPE_TEXTAREA, /* multi-line <textarea> */
} LSInputType;

typedef struct {
    char *buffer;               /* text content (UTF-8) */
    uint32_t length;            /* current text length in bytes */
    uint32_t capacity;          /* buffer capacity */
    uint32_t cursor_pos;        /* cursor position (byte index) */
    uint32_t selection_start;   /* selection start (-1 = no selection) */
    uint32_t selection_end;     /* selection end */
    bool focused;               /* whether this input has focus */
    LSInputType type;           /* input or textarea */
} LSTextInput;

/* Lifecycle */
LSTextInput *ls_text_input_create(LSInputType type);
void         ls_text_input_destroy(LSTextInput *input);

/* Focus management */
void ls_text_input_focus(LSTextInput *input);
void ls_text_input_blur(LSTextInput *input);

/* Text manipulation */
void ls_text_input_insert(LSTextInput *input, const char *text, int len);
void ls_text_input_delete_before(LSTextInput *input);  /* backspace */
void ls_text_input_delete_after(LSTextInput *input);   /* delete key */

/* Cursor movement */
void ls_text_input_move_left(LSTextInput *input);
void ls_text_input_move_right(LSTextInput *input);
void ls_text_input_move_home(LSTextInput *input);
void ls_text_input_move_end(LSTextInput *input);

/* Selection */
void ls_text_input_select_all(LSTextInput *input);
void ls_text_input_clear_selection(LSTextInput *input);

/* Clipboard operations (return allocated strings, caller must free) */
char *ls_text_input_get_selected_text(LSTextInput *input);
void  ls_text_input_delete_selection(LSTextInput *input);

/* Set/get value */
void        ls_text_input_set_value(LSTextInput *input, const char *text);
const char *ls_text_input_get_value(LSTextInput *input);

/* Key event handler — returns true if the event was consumed */
bool ls_text_input_handle_key(LSTextInput *input, PlatformEvent *event);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTSHELL_INPUT_TEXT_H */
