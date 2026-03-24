#ifndef LIGHTSHELL_PLATFORM_H
#define LIGHTSHELL_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *title;
    int width, height;
    int min_width, min_height;
    bool resizable;
} PlatformWindowConfig;

typedef enum {
    PLATFORM_EVENT_NONE,
    PLATFORM_EVENT_MOUSE_DOWN,
    PLATFORM_EVENT_MOUSE_UP,
    PLATFORM_EVENT_MOUSE_MOVE,
    PLATFORM_EVENT_KEY_DOWN,
    PLATFORM_EVENT_KEY_UP,
    PLATFORM_EVENT_RESIZE,
    PLATFORM_EVENT_CLOSE,
} PlatformEventType;

typedef struct {
    PlatformEventType type;
    float mouse_x, mouse_y;
    int mouse_button;
    uint32_t keycode;
    bool ctrl, shift, alt, meta;
    int width, height;  /* for resize */
} PlatformEvent;

/* Event queue */
#define PLATFORM_EVENT_QUEUE_SIZE 64

int  platform_init(PlatformWindowConfig *config);
bool platform_poll_event(PlatformEvent *event);  /* returns false if queue empty */
void platform_shutdown(void);
void platform_get_size(int *width, int *height);
float platform_get_scale_factor(void);
void *platform_get_metal_layer(void);  /* returns CAMetalLayer* */

void platform_frame_begin(void);
void platform_frame_end(void);   /* sleeps to hit 60fps target */

#endif
