/* platform_darwin.m - macOS platform layer with Metal-backed window */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include "platform.h"
#include <string.h>
#include <mach/mach_time.h>
#include <unistd.h>

/* --- Key character storage --- */
/* We store the NSEvent characters string alongside each key event so
 * platform_get_key_char can retrieve the typed character later. */
static char g_key_chars[PLATFORM_EVENT_QUEUE_SIZE][8];
static int  g_key_chars_len[PLATFORM_EVENT_QUEUE_SIZE];

/* Track last popped event's key chars for platform_get_key_char */
static char g_last_key_chars[8];
static int  g_last_key_chars_len = 0;

/* --- Event ring buffer --- */
static PlatformEvent g_event_queue[PLATFORM_EVENT_QUEUE_SIZE];
static int g_queue_head = 0;  /* read position */
static int g_queue_tail = 0;  /* write position */
static int g_queue_count = 0;

static void event_push(PlatformEvent *ev) {
    if (g_queue_count >= PLATFORM_EVENT_QUEUE_SIZE) {
        /* Queue full — drop oldest event */
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
    /* Save key chars for platform_get_key_char */
    memcpy(g_last_key_chars, g_key_chars[g_queue_head], 8);
    g_last_key_chars_len = g_key_chars_len[g_queue_head];
    g_queue_head = (g_queue_head + 1) % PLATFORM_EVENT_QUEUE_SIZE;
    g_queue_count--;
    return true;
}

/* --- Globals --- */
static NSWindow *g_window = nil;
static CAMetalLayer *g_metal_layer = nil;
static NSApplication *g_app = nil;

/* --- Helper: modifier flags to booleans --- */
static void fill_modifiers(PlatformEvent *ev, NSEventModifierFlags flags) {
    ev->ctrl  = (flags & NSEventModifierFlagControl) != 0;
    ev->shift = (flags & NSEventModifierFlagShift) != 0;
    ev->alt   = (flags & NSEventModifierFlagOption) != 0;
    ev->meta  = (flags & NSEventModifierFlagCommand) != 0;
}

/* --- MetalView: NSView subclass that vends a CAMetalLayer --- */
@interface MetalView : NSView
@end

@implementation MetalView

- (BOOL)wantsLayer { return YES; }

- (CALayer *)makeBackingLayer {
    CAMetalLayer *layer = [CAMetalLayer layer];
    g_metal_layer = layer;
    return layer;
}

- (BOOL)wantsUpdateLayer { return YES; }

- (BOOL)acceptsFirstResponder { return YES; }

- (void)mouseDown:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_MOUSE_DOWN;
    ev.mouse_x = (float)loc.x;
    ev.mouse_y = (float)(self.bounds.size.height - loc.y);
    ev.mouse_button = 0;
    fill_modifiers(&ev, [event modifierFlags]);
    event_push(&ev);
}

- (void)mouseUp:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_MOUSE_UP;
    ev.mouse_x = (float)loc.x;
    ev.mouse_y = (float)(self.bounds.size.height - loc.y);
    ev.mouse_button = 0;
    fill_modifiers(&ev, [event modifierFlags]);
    event_push(&ev);
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_MOUSE_MOVE;
    ev.mouse_x = (float)loc.x;
    ev.mouse_y = (float)(self.bounds.size.height - loc.y);
    fill_modifiers(&ev, [event modifierFlags]);
    event_push(&ev);
}

- (void)mouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}

- (void)rightMouseDown:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_MOUSE_DOWN;
    ev.mouse_x = (float)loc.x;
    ev.mouse_y = (float)(self.bounds.size.height - loc.y);
    ev.mouse_button = 1;
    fill_modifiers(&ev, [event modifierFlags]);
    event_push(&ev);
}

- (void)rightMouseUp:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_MOUSE_UP;
    ev.mouse_x = (float)loc.x;
    ev.mouse_y = (float)(self.bounds.size.height - loc.y);
    ev.mouse_button = 1;
    fill_modifiers(&ev, [event modifierFlags]);
    event_push(&ev);
}

- (void)keyDown:(NSEvent *)event {
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_KEY_DOWN;
    ev.keycode = (uint32_t)[event keyCode];
    fill_modifiers(&ev, [event modifierFlags]);

    /* Store typed characters for platform_get_key_char */
    NSString *chars = [event characters];
    int slot = g_queue_tail;  /* will be written to this slot by event_push */
    memset(g_key_chars[slot], 0, 8);
    g_key_chars_len[slot] = 0;
    if (chars && [chars length] > 0) {
        const char *utf8 = [chars UTF8String];
        if (utf8) {
            int len = (int)strlen(utf8);
            if (len > 7) len = 7;
            memcpy(g_key_chars[slot], utf8, (size_t)len);
            g_key_chars_len[slot] = len;
        }
    }

    event_push(&ev);
}

- (void)keyUp:(NSEvent *)event {
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_KEY_UP;
    ev.keycode = (uint32_t)[event keyCode];
    fill_modifiers(&ev, [event modifierFlags]);
    event_push(&ev);
}

@end

/* --- Resize render callback ---
 * During window resize, macOS enters a modal event tracking loop.
 * Our main loop stops running, so no frames render.
 * We use a callback to keep rendering during resize. */
typedef void (*PlatformResizeRenderCallback)(void);
static PlatformResizeRenderCallback g_resize_render_cb = NULL;

void platform_set_resize_render_callback(PlatformResizeRenderCallback cb) {
    g_resize_render_cb = cb;
}

/* --- Window delegate --- */
@interface LightShellWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation LightShellWindowDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_CLOSE;
    event_push(&ev);
    return NO;  /* We handle shutdown ourselves */
}

- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    NSView *contentView = [g_window contentView];
    NSSize size = contentView.bounds.size;
    CGFloat scale = g_window.backingScaleFactor;

    /* Update Metal layer drawable size immediately */
    if (g_metal_layer) {
        g_metal_layer.drawableSize = CGSizeMake(size.width * scale, size.height * scale);
    }

    PlatformEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = PLATFORM_EVENT_RESIZE;
    ev.width = (int)(size.width * scale);
    ev.height = (int)(size.height * scale);
    event_push(&ev);

    /* Note: smooth resize is handled via presentsWithTransaction on the Metal layer,
     * set during platform_init. This avoids reentrancy issues from calling render
     * during the resize delegate callback. */
}

- (void)windowWillStartLiveResize:(NSNotification *)notification {
    (void)notification;
    /* Set up a display link timer during resize for smooth rendering */
}

@end

static LightShellWindowDelegate *g_window_delegate = nil;

/* --- App delegate to stop the run loop after launch --- */
@interface LightShellAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation LightShellAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    [g_app stop:nil];
    /* Post a dummy event to ensure [NSApp run] returns after stop */
    NSEvent *dummy = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSMakePoint(0, 0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [g_app postEvent:dummy atStart:YES];
}

@end

static LightShellAppDelegate *g_app_delegate = nil;

/* --- Platform API implementation --- */

int platform_init(PlatformWindowConfig *config) {
    g_app = [NSApplication sharedApplication];
    [g_app setActivationPolicy:NSApplicationActivationPolicyRegular];

    /* Create menu bar (required for proper cmd+Q, focus, etc.) */
    NSMenu *menubar = [[NSMenu alloc] init];
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    [menubar addItem:appMenuItem];
    [g_app setMainMenu:menubar];

    NSMenu *appMenu = [[NSMenu alloc] init];
    NSString *quitTitle = [NSString stringWithFormat:@"Quit %s", config->title];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                     action:@selector(terminate:)
                                              keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [appMenuItem setSubmenu:appMenu];

    /* Window style */
    NSWindowStyleMask styleMask = NSWindowStyleMaskTitled
                                | NSWindowStyleMaskClosable
                                | NSWindowStyleMaskMiniaturizable;
    if (config->resizable) {
        styleMask |= NSWindowStyleMaskResizable;
    }

    NSRect frame = NSMakeRect(0, 0, config->width, config->height);
    g_window = [[NSWindow alloc] initWithContentRect:frame
                                           styleMask:styleMask
                                             backing:NSBackingStoreBuffered
                                               defer:NO];

    NSString *title = [NSString stringWithUTF8String:config->title];
    [g_window setTitle:title];
    [g_window center];

    if (config->min_width > 0 && config->min_height > 0) {
        [g_window setMinSize:NSMakeSize(config->min_width, config->min_height)];
    }

    /* Window delegate */
    g_window_delegate = [[LightShellWindowDelegate alloc] init];
    [g_window setDelegate:g_window_delegate];

    /* Metal view */
    MetalView *metalView = [[MetalView alloc] initWithFrame:frame];
    [g_window setContentView:metalView];

    /* Force layer creation by accessing .layer — makeBackingLayer is called lazily */
    [metalView setWantsLayer:YES];
    CALayer *backing = [metalView layer];  /* triggers makeBackingLayer */
    (void)backing;

    /* Configure Metal layer after view is attached */
    if (g_metal_layer) {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        g_metal_layer.device = device;
        g_metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_metal_layer.framebufferOnly = YES;
        g_metal_layer.presentsWithTransaction = YES;  /* smooth resize: syncs Metal with Core Animation */
        g_metal_layer.contentsScale = g_window.backingScaleFactor;
        CGFloat scale = g_window.backingScaleFactor;
        g_metal_layer.drawableSize = CGSizeMake(config->width * scale, config->height * scale);
        fprintf(stderr, "[platform] Metal layer created: %p device=%s\n",
                (__bridge void *)g_metal_layer, [[device name] UTF8String]);
    } else {
        fprintf(stderr, "[platform] ERROR: Metal layer is nil after view creation!\n");
    }

    /* Accept mouse moved events */
    [g_window setAcceptsMouseMovedEvents:YES];

    /* Show and activate */
    [g_window makeKeyAndOrderFront:nil];

    /* Use app delegate to properly initialize and then return control */
    g_app_delegate = [[LightShellAppDelegate alloc] init];
    [g_app setDelegate:g_app_delegate];
    [g_app run];  /* This returns after applicationDidFinishLaunching calls stop */

    [g_app activateIgnoringOtherApps:YES];

    return 0;
}

bool platform_poll_event(PlatformEvent *event) {
    /* Drain Cocoa event queue first */
    @autoreleasepool {
        NSEvent *ns_event;
        while ((ns_event = [g_app nextEventMatchingMask:NSEventMaskAny
                                             untilDate:nil
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES])) {
            [g_app sendEvent:ns_event];
            [g_app updateWindows];
        }
    }

    return event_pop(event);
}

void platform_shutdown(void) {
    if (g_window) {
        [g_window close];
        g_window = nil;
    }
    g_metal_layer = nil;
    g_window_delegate = nil;
    g_app_delegate = nil;
}

void platform_get_size(int *width, int *height) {
    if (g_window) {
        NSSize size = [[g_window contentView] bounds].size;
        CGFloat scale = g_window.backingScaleFactor;
        *width = (int)(size.width * scale);
        *height = (int)(size.height * scale);
    } else {
        *width = 0;
        *height = 0;
    }
}

float platform_get_scale_factor(void) {
    if (g_window) {
        return (float)g_window.backingScaleFactor;
    }
    return 1.0f;
}

void *platform_get_metal_layer(void) {
    return (__bridge void *)g_metal_layer;
}

/* --- Window management --- */

void platform_set_title(const char *title) {
    if (g_window && title) {
        NSString *nsTitle = [NSString stringWithUTF8String:title];
        [g_window setTitle:nsTitle];
    }
}

void platform_set_size(int width, int height) {
    if (g_window) {
        NSRect frame = [g_window frame];
        NSRect content = [g_window contentRectForFrameRect:frame];
        /* Keep the top-left corner fixed */
        CGFloat titleBarHeight = frame.size.height - content.size.height;
        frame.origin.y += (frame.size.height - (height + titleBarHeight));
        frame.size.width = width;
        frame.size.height = height + titleBarHeight;
        [g_window setFrame:frame display:YES animate:NO];
    }
}

void platform_minimize(void) {
    if (g_window) {
        [g_window miniaturize:nil];
    }
}

void platform_maximize(void) {
    if (g_window) {
        [g_window zoom:nil];
    }
}

void platform_close(void) {
    if (g_window) {
        /* Push a close event so the main loop can handle shutdown */
        PlatformEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = PLATFORM_EVENT_CLOSE;
        event_push(&ev);
    }
}

/* --- Key character extraction --- */
void platform_get_key_char(PlatformEvent *event, char *out, int *out_len) {
    (void)event;
    /* Return the characters from the last popped key event */
    if (g_last_key_chars_len > 0) {
        memcpy(out, g_last_key_chars, (size_t)g_last_key_chars_len);
        out[g_last_key_chars_len] = '\0';
        *out_len = g_last_key_chars_len;
    } else {
        out[0] = '\0';
        *out_len = 0;
    }
}

static uint64_t g_frame_start = 0;
static double g_tick_to_ns = 0;

void platform_frame_begin(void) {
    if (g_tick_to_ns == 0) {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        g_tick_to_ns = (double)info.numer / (double)info.denom;
    }
    g_frame_start = mach_absolute_time();
}

void platform_frame_end(void) {
    uint64_t elapsed = mach_absolute_time() - g_frame_start;
    double elapsed_ms = (double)elapsed * g_tick_to_ns / 1000000.0;
    double target_ms = 16.667;  /* 60fps */
    double remaining = target_ms - elapsed_ms;
    if (remaining > 0.5) {
        usleep((useconds_t)(remaining * 1000));
    }
}
