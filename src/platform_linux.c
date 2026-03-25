/* platform_linux.c - Linux platform layer with X11 window + Vulkan surface
 *
 * Mirrors the macOS platform_darwin.m implementation:
 * - X11 window creation and event handling
 * - Vulkan instance and surface creation
 * - Same event queue ring buffer pattern
 * - Frame timing for 60fps target
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* --- Event ring buffer (same as darwin) --- */
static PlatformEvent g_event_queue[PLATFORM_EVENT_QUEUE_SIZE];
static int g_queue_head = 0;
static int g_queue_tail = 0;
static int g_queue_count = 0;

static void event_push(PlatformEvent *ev) {
    if (g_queue_count >= PLATFORM_EVENT_QUEUE_SIZE) {
        g_queue_head = (g_queue_head + 1) % PLATFORM_EVENT_QUEUE_SIZE;
        g_queue_count--;
    }
    g_event_queue[g_queue_tail] = *ev;
    g_queue_tail = (g_queue_tail + 1) % PLATFORM_EVENT_QUEUE_SIZE;
    g_queue_count++;
}

static bool event_pop(PlatformEvent *ev) {
    if (g_queue_count == 0) return false;
    *ev = g_event_queue[g_queue_head];
    g_queue_head = (g_queue_head + 1) % PLATFORM_EVENT_QUEUE_SIZE;
    g_queue_count--;
    return true;
}

/* --- Globals --- */
static Display   *g_display;
static Window     g_window;
static Atom       g_wm_delete;
static int        g_win_width;
static int        g_win_height;

static VkInstance  g_vk_instance;
static VkSurfaceKHR g_vk_surface;

/* --- Helper: X11 modifier state to booleans --- */
static void fill_modifiers(PlatformEvent *ev, unsigned int state) {
    ev->ctrl  = (state & ControlMask) != 0;
    ev->shift = (state & ShiftMask) != 0;
    ev->alt   = (state & Mod1Mask) != 0;
    ev->meta  = (state & Mod4Mask) != 0;
}

/* --- Helper: X11 button to our button index --- */
static int x11_button_to_index(unsigned int button) {
    switch (button) {
        case Button1: return 0;  /* left */
        case Button2: return 2;  /* middle */
        case Button3: return 1;  /* right */
        default:      return (int)button;
    }
}

/* --- Vulkan instance creation --- */
static int create_vulkan_instance(void) {
    const char *extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    };

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "LightShell",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "LightShell",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount = 0,
    };

#ifndef NDEBUG
    /* Enable validation layers in debug builds */
    const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    if (layer_count > 0) {
        VkLayerProperties *props = calloc(layer_count, sizeof(VkLayerProperties));
        vkEnumerateInstanceLayerProperties(&layer_count, props);
        for (uint32_t i = 0; i < layer_count; i++) {
            if (strcmp(props[i].layerName, layers[0]) == 0) {
                create_info.enabledLayerCount = 1;
                create_info.ppEnabledLayerNames = layers;
                fprintf(stderr, "[platform] Vulkan validation layers enabled\n");
                break;
            }
        }
        free(props);
    }
#endif

    VkResult res = vkCreateInstance(&create_info, NULL, &g_vk_instance);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[platform] vkCreateInstance failed: %d\n", res);
        return -1;
    }
    return 0;
}

/* --- Vulkan surface creation --- */
static int create_vulkan_surface(void) {
    VkXlibSurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = g_display,
        .window = g_window,
    };

    VkResult res = vkCreateXlibSurfaceKHR(g_vk_instance, &surface_info, NULL,
                                           &g_vk_surface);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[platform] vkCreateXlibSurfaceKHR failed: %d\n", res);
        return -1;
    }
    return 0;
}

/* --- Platform API implementation --- */

int platform_init(PlatformWindowConfig *config) {
    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        fprintf(stderr, "[platform] Cannot open X11 display\n");
        return -1;
    }

    int screen = DefaultScreen(g_display);
    Window root = RootWindow(g_display, screen);

    /* Window attributes */
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
                     | ButtonPressMask | ButtonReleaseMask
                     | PointerMotionMask | StructureNotifyMask;
    attrs.background_pixel = BlackPixel(g_display, screen);

    g_win_width = config->width;
    g_win_height = config->height;

    g_window = XCreateWindow(
        g_display, root,
        0, 0, (unsigned)config->width, (unsigned)config->height,
        0,                          /* border width */
        CopyFromParent,             /* depth */
        InputOutput,                /* class */
        CopyFromParent,             /* visual */
        CWEventMask | CWBackPixel, /* value mask */
        &attrs
    );

    if (!g_window) {
        fprintf(stderr, "[platform] Cannot create X11 window\n");
        XCloseDisplay(g_display);
        return -1;
    }

    /* Set window title */
    XStoreName(g_display, g_window, config->title);

    /* Set minimum size hints */
    if (config->min_width > 0 && config->min_height > 0) {
        XSizeHints *hints = XAllocSizeHints();
        hints->flags = PMinSize;
        hints->min_width = config->min_width;
        hints->min_height = config->min_height;
        if (!config->resizable) {
            hints->flags |= PMaxSize;
            hints->max_width = config->width;
            hints->max_height = config->height;
        }
        XSetWMNormalHints(g_display, g_window, hints);
        XFree(hints);
    }

    /* Handle window close via WM_DELETE_WINDOW protocol */
    g_wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, g_window, &g_wm_delete, 1);

    /* Map (show) window */
    XMapWindow(g_display, g_window);
    XFlush(g_display);

    /* Wait for MapNotify to ensure window is visible before creating Vulkan surface */
    XEvent ev;
    while (1) {
        XNextEvent(g_display, &ev);
        if (ev.type == MapNotify) break;
    }

    /* Create Vulkan instance and surface */
    if (create_vulkan_instance() != 0) {
        XDestroyWindow(g_display, g_window);
        XCloseDisplay(g_display);
        return -1;
    }

    if (create_vulkan_surface() != 0) {
        vkDestroyInstance(g_vk_instance, NULL);
        XDestroyWindow(g_display, g_window);
        XCloseDisplay(g_display);
        return -1;
    }

    fprintf(stderr, "[platform] X11 + Vulkan initialized (%dx%d)\n",
            config->width, config->height);
    return 0;
}

bool platform_poll_event(PlatformEvent *event) {
    /* Drain X11 event queue */
    while (XPending(g_display) > 0) {
        XEvent xev;
        XNextEvent(g_display, &xev);

        PlatformEvent ev;
        memset(&ev, 0, sizeof(ev));

        switch (xev.type) {
            case ButtonPress: {
                ev.type = PLATFORM_EVENT_MOUSE_DOWN;
                ev.mouse_x = (float)xev.xbutton.x;
                ev.mouse_y = (float)xev.xbutton.y;
                ev.mouse_button = x11_button_to_index(xev.xbutton.button);
                fill_modifiers(&ev, xev.xbutton.state);
                event_push(&ev);
                break;
            }

            case ButtonRelease: {
                ev.type = PLATFORM_EVENT_MOUSE_UP;
                ev.mouse_x = (float)xev.xbutton.x;
                ev.mouse_y = (float)xev.xbutton.y;
                ev.mouse_button = x11_button_to_index(xev.xbutton.button);
                fill_modifiers(&ev, xev.xbutton.state);
                event_push(&ev);
                break;
            }

            case MotionNotify: {
                ev.type = PLATFORM_EVENT_MOUSE_MOVE;
                ev.mouse_x = (float)xev.xmotion.x;
                ev.mouse_y = (float)xev.xmotion.y;
                fill_modifiers(&ev, xev.xmotion.state);
                event_push(&ev);
                break;
            }

            case KeyPress: {
                ev.type = PLATFORM_EVENT_KEY_DOWN;
                ev.keycode = (uint32_t)XkbKeycodeToKeysym(g_display,
                    (KeyCode)xev.xkey.keycode, 0, 0);
                fill_modifiers(&ev, xev.xkey.state);
                event_push(&ev);
                break;
            }

            case KeyRelease: {
                ev.type = PLATFORM_EVENT_KEY_UP;
                ev.keycode = (uint32_t)XkbKeycodeToKeysym(g_display,
                    (KeyCode)xev.xkey.keycode, 0, 0);
                fill_modifiers(&ev, xev.xkey.state);
                event_push(&ev);
                break;
            }

            case ConfigureNotify: {
                int new_w = xev.xconfigure.width;
                int new_h = xev.xconfigure.height;
                if (new_w != g_win_width || new_h != g_win_height) {
                    g_win_width = new_w;
                    g_win_height = new_h;
                    ev.type = PLATFORM_EVENT_RESIZE;
                    ev.width = new_w;
                    ev.height = new_h;
                    event_push(&ev);
                }
                break;
            }

            case ClientMessage: {
                if ((Atom)xev.xclient.data.l[0] == g_wm_delete) {
                    ev.type = PLATFORM_EVENT_CLOSE;
                    event_push(&ev);
                }
                break;
            }

            default:
                break;
        }
    }

    return event_pop(event);
}

void platform_shutdown(void) {
    if (g_vk_surface) {
        vkDestroySurfaceKHR(g_vk_instance, g_vk_surface, NULL);
        g_vk_surface = VK_NULL_HANDLE;
    }
    if (g_vk_instance) {
        vkDestroyInstance(g_vk_instance, NULL);
        g_vk_instance = VK_NULL_HANDLE;
    }
    if (g_display && g_window) {
        XDestroyWindow(g_display, g_window);
        g_window = 0;
    }
    if (g_display) {
        XCloseDisplay(g_display);
        g_display = NULL;
    }
    fprintf(stderr, "[platform] Shutdown complete\n");
}

void platform_get_size(int *width, int *height) {
    *width = g_win_width;
    *height = g_win_height;
}

float platform_get_scale_factor(void) {
    /* X11 does not have a native HiDPI scale factor like macOS.
     * Check Xft.dpi resource, fall back to 1.0. */
    if (!g_display) return 1.0f;

    char *dpi_str = XGetDefault(g_display, "Xft", "dpi");
    if (dpi_str) {
        float dpi = (float)atof(dpi_str);
        if (dpi > 0.0f) return dpi / 96.0f;
    }
    return 1.0f;
}

/* --- Vulkan surface accessors for the GPU backend --- */

void *platform_get_vulkan_instance(void) {
    return (void *)g_vk_instance;
}

void *platform_get_vulkan_surface(void) {
    return (void *)g_vk_surface;
}

/* --- Frame timing (same logic as darwin, using clock_gettime) --- */

static struct timespec g_frame_start;

void platform_frame_begin(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_frame_start);
}

void platform_frame_end(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double elapsed_ms = (double)(now.tv_sec - g_frame_start.tv_sec) * 1000.0
                      + (double)(now.tv_nsec - g_frame_start.tv_nsec) / 1000000.0;
    double target_ms = 16.667;  /* 60fps */
    double remaining = target_ms - elapsed_ms;
    if (remaining > 0.5) {
        usleep((useconds_t)(remaining * 1000));
    }
}
