/*
 * api_timers.c - setTimeout / setInterval / clearTimeout / clearInterval
 *
 * Registered as globals. Uses a simple timer queue checked each frame.
 * Timer callbacks are GC-retained until fired (setTimeout) or cancelled.
 */

#include "api.h"
#include <string.h>
#include <mach/mach_time.h>

typedef struct {
    R8EValue callback;
    double fire_at_ms;      /* absolute time to fire */
    double interval_ms;     /* 0 for setTimeout, >0 for setInterval */
    uint32_t id;
    bool cancelled;
    bool active;
} TimerEntry;

#define MAX_TIMERS 256
static TimerEntry g_timers[MAX_TIMERS];
static uint32_t g_timer_count = 0;
static uint32_t g_timer_next_id = 1;

/* Get current time in milliseconds */
static double now_ms(void) {
    static double tick_to_ms = 0;
    if (tick_to_ms == 0) {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        tick_to_ms = ((double)info.numer / (double)info.denom) / 1000000.0;
    }
    return (double)mach_absolute_time() * tick_to_ms;
}

/* Common implementation for setTimeout/setInterval */
static R8EValue api_set_timer(R8EContext *ctx, int argc, const R8EValue *argv,
                               bool repeating) {
    if (argc < 1 || !r8e_is_function(argv[0])) {
        r8e_throw_type_error(ctx, "setTimeout/setInterval: first argument must be a function");
        return R8E_UNDEFINED;
    }

    double delay_ms = 0;
    if (argc >= 2 && r8e_is_number(argv[1])) {
        delay_ms = r8e_to_double(argv[1]);
        if (delay_ms < 0) delay_ms = 0;
    }

    /* Find a free slot */
    uint32_t slot = MAX_TIMERS;
    for (uint32_t i = 0; i < MAX_TIMERS; i++) {
        if (!g_timers[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == MAX_TIMERS) {
        r8e_throw_error(ctx, "setTimeout/setInterval: too many timers (max %d)", MAX_TIMERS);
        return R8E_UNDEFINED;
    }

    uint32_t id = g_timer_next_id++;
    g_timers[slot].callback = argv[0];
    g_timers[slot].fire_at_ms = now_ms() + delay_ms;
    g_timers[slot].interval_ms = repeating ? delay_ms : 0;
    g_timers[slot].id = id;
    g_timers[slot].cancelled = false;
    g_timers[slot].active = true;

    /* Retain the callback so GC doesn't collect it */
    r8e_value_retain(argv[0]);

    if (slot >= g_timer_count) {
        g_timer_count = slot + 1;
    }

    return r8e_from_int32((int32_t)id);
}

/* setTimeout(callback, delay) */
static R8EValue api_set_timeout(R8EContext *ctx, R8EValue this_val,
                                 int argc, const R8EValue *argv) {
    (void)this_val;
    return api_set_timer(ctx, argc, argv, false);
}

/* setInterval(callback, delay) */
static R8EValue api_set_interval(R8EContext *ctx, R8EValue this_val,
                                  int argc, const R8EValue *argv) {
    (void)this_val;
    return api_set_timer(ctx, argc, argv, true);
}

/* Common clear implementation */
static R8EValue api_clear_timer(R8EContext *ctx, R8EValue this_val,
                                 int argc, const R8EValue *argv) {
    (void)this_val;
    if (argc < 1 || !r8e_is_number(argv[0])) {
        return R8E_UNDEFINED;  /* silent no-op per spec */
    }
    uint32_t id = (uint32_t)r8e_to_int32(argv[0]);
    for (uint32_t i = 0; i < g_timer_count; i++) {
        if (g_timers[i].active && g_timers[i].id == id) {
            g_timers[i].cancelled = true;
            g_timers[i].active = false;
            r8e_value_release(ctx, g_timers[i].callback);
            break;
        }
    }
    return R8E_UNDEFINED;
}

/* clearTimeout(id) */
static R8EValue api_clear_timeout(R8EContext *ctx, R8EValue this_val,
                                   int argc, const R8EValue *argv) {
    return api_clear_timer(ctx, this_val, argc, argv);
}

/* clearInterval(id) */
static R8EValue api_clear_interval(R8EContext *ctx, R8EValue this_val,
                                    int argc, const R8EValue *argv) {
    return api_clear_timer(ctx, this_val, argc, argv);
}

void ls_timers_tick(R8EContext *ctx) {
    double current = now_ms();

    for (uint32_t i = 0; i < g_timer_count; i++) {
        if (!g_timers[i].active || g_timers[i].cancelled) continue;
        if (current < g_timers[i].fire_at_ms) continue;

        /* Fire the callback */
        R8EValue cb = g_timers[i].callback;
        r8e_call(ctx, cb, R8E_UNDEFINED, 0, NULL);

        if (g_timers[i].interval_ms > 0) {
            /* Repeating timer: reschedule */
            g_timers[i].fire_at_ms = current + g_timers[i].interval_ms;
        } else {
            /* One-shot timer: deactivate */
            g_timers[i].active = false;
            r8e_value_release(ctx, cb);
        }
    }
}

void ls_api_timers_init(R8EContext *ctx) {
    memset(g_timers, 0, sizeof(g_timers));
    g_timer_count = 0;
    g_timer_next_id = 1;

    r8e_set_global_func(ctx, "setTimeout", api_set_timeout, 2);
    r8e_set_global_func(ctx, "setInterval", api_set_interval, 2);
    r8e_set_global_func(ctx, "clearTimeout", api_clear_timeout, 1);
    r8e_set_global_func(ctx, "clearInterval", api_clear_interval, 1);
}
