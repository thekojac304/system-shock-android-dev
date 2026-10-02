/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_shock_bridge.h"
#include "touch_surface.h"
#include "player.h"
#include "physics.h"
#include "mainloop.h"
#include "mouselook.h"
#include <stddef.h>

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern int mlook_enabled;
extern bool TriggerRelMouseMode;
extern uchar game_paused, wrapper_panel_on, olh_overlay_on;
extern void middleize_mouse(void);

static TouchControls touch;
static bool initialized, syncing, controller_connected, background, unfocused;
static bool requested_look;
static int output_width, output_height, window_width, window_height;
static byte applied[2];
static bool applied_axis[2];
static TouchRect game_viewport;
static float game_scale_x, game_scale_y;
static int game_width, game_height;

#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
/* Temporary Touch Alpha visibility diagnostics: log transitions, not frames. */
static bool diagnostic_state_valid;
static unsigned diagnostic_state;
static short diagnostic_loop;
static unsigned long diagnostic_draw_calls;

static void log_touch_state(void) {
    unsigned state = (touch.enabled ? 1u : 0u) |
        (touch.gameplay_available ? 2u : 0u) | (touch.controller ? 4u : 0u) |
        (touch.mode == TOUCH_GAMEPLAY ? 8u : 0u) |
        (touch.effective_mode == TOUCH_GAMEPLAY ? 16u : 0u) |
        (global_fullmap ? 32u : 0u) | (global_fullmap && global_fullmap->cyber ? 64u : 0u) |
        ((_change_flag & GL_CHG_LOOP) ? 128u : 0u) | (game_paused ? 256u : 0u) |
        (wrapper_panel_on ? 512u : 0u) | (olh_overlay_on ? 1024u : 0u) |
        (physics_running ? 2048u : 0u) | (time_passes ? 4096u : 0u);
    if (diagnostic_state_valid && state == diagnostic_state && _current_loop == diagnostic_loop) return;
    diagnostic_state_valid = true;
    diagnostic_state = state;
    diagnostic_loop = _current_loop;
    SDL_Log("RP5NP_TOUCH_STATE mode=%s effective=%s gameplay=%s controller_suppression=%s overlay=%s enabled=%d draw_calls=%lu",
            touch.mode == TOUCH_GAMEPLAY ? "GAMEPLAY" : "CURSOR",
            touch.effective_mode == TOUCH_GAMEPLAY ? "GAMEPLAY" : "CURSOR",
            touch.gameplay_available ? "available" : "unavailable",
            touch.controller ? "active" : "inactive",
            touch.enabled && touch.gameplay_available && !touch.controller ? "visible" : "hidden",
            touch.enabled, diagnostic_draw_calls);
    SDL_Log("RP5NP_TOUCH_CONTEXT loop=%d map=%d cyber=%d changing_loop=%d paused=%d wrapper=%d help=%d physics=%d time=%d background=%d unfocused=%d window_flags=0x%x",
            _current_loop, global_fullmap != NULL, global_fullmap ? global_fullmap->cyber : -1,
            !!(_change_flag & GL_CHG_LOOP), game_paused, wrapper_panel_on, olh_overlay_on,
            physics_running, time_passes, background, unfocused, SDL_GetWindowFlags(window));
}
#endif

static void clear_movement(void) {
    for (int i = 0; i < 2; ++i) {
        byte current;
        if (applied_axis[i]) {
            physics_get_one_control(KEYBD_CONTROL_BANK, i, &current);
            if (current == applied[i]) physics_set_one_control(KEYBD_CONTROL_BANK, i, 0);
            applied_axis[i] = false;
        }
    }
}

static bool gameplay_available(void) {
    return global_fullmap && !global_fullmap->cyber &&
        (_current_loop == GAME_LOOP || _current_loop == FULLSCREEN_LOOP || _current_loop == SVGA_LOOP) &&
        !(_change_flag & GL_CHG_LOOP) && !game_paused && !wrapper_panel_on && !olh_overlay_on &&
        physics_running && time_passes;
}

static void request_mode(bool look) {
    if (look && !mlook_enabled) mouse_look_toggle();
    else if (!look && mlook_enabled) {
        /* The normal toggle explicitly warps to the window center after SDL
           disables relative mode. That queued motion can overwrite a touch
           position. Direct touch needs only the mode change, never that warp.
           Physical mouse/controller transitions keep the original toggle. */
        mlook_enabled = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
    if (!look) TriggerRelMouseMode = false;
    requested_look = look;
}

static void pointer(float x, float y) {
    float gx, gy;
    touch_map_cursor(x, y, game_viewport, game_scale_x, game_scale_y,
                     game_width, game_height, &gx, &gy);
    touch_input_pointer(gx, gy);
}

static void output(void *user, TouchOutput event, float x, float y) {
    (void)user;
    switch (event) {
    case TOUCH_CANCEL:
        clear_movement();
        touch_input_cancel();
        break;
    case TOUCH_MODE_CHANGED:
        request_mode(touch.effective_mode == TOUCH_GAMEPLAY);
        break;
    case TOUCH_FIRE_DOWN:
        /* Establish the same aim point used by mouse_look_physics, even on
           the very first action before a physics tick has centered it. */
        middleize_mouse();
        touch_input_button(false, true);
        break;
    case TOUCH_FIRE_UP:
        touch_input_button(false, false);
        break;
    case TOUCH_USE_PRESS:
        middleize_mouse();
        touch_input_button(true, true);
        touch_input_button(true, false);
        touch_input_button(true, true);
        touch_input_button(true, false);
        break;
    case TOUCH_POINTER_DOWN:
        request_mode(false);
        pointer(x, y);
        touch_input_button(true, true);
        break;
    case TOUCH_POINTER_MOVE:
        request_mode(false);
        pointer(x, y);
        break;
    case TOUCH_POINTER_UP:
        pointer(x, y);
        touch_input_button(true, false);
        break;
    }
}

void touch_bridge_init(void) {
    if (initialized) touch_cancel(&touch);
    requested_look = controller_connected = background = unfocused = false;
    output_width = output_height = window_width = window_height = 0;
    /* Must precede SDL_Init: gameplay touches must never enter SDL's mouse
       state/relative accumulator. Physical mice still follow the old path. */
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_TOUCH_EVENTS, "0", SDL_HINT_OVERRIDE);
    touch_init(&touch, output, NULL);
    initialized = true;
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    diagnostic_state_valid = false;
    diagnostic_draw_calls = 0;
    SDL_Log("RP5NP_TOUCH_INIT initialized=1 mode=GAMEPLAY effective=CURSOR");
#endif
}

void touch_bridge_cancel(void) {
    if (initialized) touch_cancel(&touch);
}

void touch_bridge_sync(void) {
    if (!initialized || syncing || !renderer || !window) return;
    syncing = true;
    touch_surface_init();
    int w = 0, h = 0, ow = 0, oh = 0, ww = 0, wh = 0;
    SDL_RenderGetLogicalSize(renderer, &w, &h);
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    SDL_GetWindowSize(window, &ww, &wh);
    /* SDL exposes the exact (possibly rounded) viewport origin through this
       query. Convert it back to output pixels, then use one explicit transform
       for every pointer event. Do not approximate a centered 4:3 rectangle. */
    float origin_x, origin_y, sx, sy;
    SDL_RenderGetScale(renderer, &sx, &sy);
    SDL_RenderWindowToLogical(renderer, 0, 0, &origin_x, &origin_y);
    TouchRect viewport = {-origin_x * sx, -origin_y * sy, w * sx, h * sy};
    if (w != game_width || h != game_height || sx != game_scale_x || sy != game_scale_y ||
        viewport.x != game_viewport.x || viewport.y != game_viewport.y) touch_cancel(&touch);
    game_width = w; game_height = h; game_scale_x = sx; game_scale_y = sy;
    game_viewport = viewport;
    if (ow != output_width || oh != output_height || ww != window_width || wh != window_height) {
        touch_cancel(&touch);
        output_width = ow; output_height = oh; window_width = ww; window_height = wh;
    }
    bool available = gameplay_available();
    Uint32 flags = SDL_GetWindowFlags(window);
    bool enabled = !background && !unfocused && (flags & SDL_WINDOW_INPUT_FOCUS) &&
                   !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED));
    /* Original game interactions may deliberately leave freelook. Respect
       that transition and require a fresh toggle before gameplay gestures. */
    if (requested_look && !mlook_enabled && available && !controller_connected) {
        requested_look = false;
        touch_set_mode(&touch, TOUCH_CURSOR);
    }
    enabled = enabled && w > 0 && h > 0 && sx > 0 && sy > 0;
    touch_configure(&touch, (float)ow, (float)oh, enabled, available, controller_connected);
    /* Preserve Alpha 1 look sensitivity: surface deltas become the same
       logical pixel deltas as before, without clamping motion in the bars. */
    touch.look_scale = sx > 0 ? 1.0f / sx : 1.0f;
    touch.layout.move_radius = h * sy * .11f;
    if (!available || controller_connected) requested_look = false;
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    log_touch_state();
#endif
    syncing = false;
}

void touch_bridge_controller(bool connected) {
    touch_bridge_cancel(); /* includes replacement controllers with no gap */
    controller_connected = connected;
    touch_bridge_sync();
}

void touch_bridge_event(const SDL_Event *event) {
    TouchSurfaceEvent surface;
    if (touch_surface_failed()) {
        /* Lost releases must never leave a button or movement held. Drain the
           remaining raw batch as well; only new input can acquire ownership. */
        touch_bridge_cancel();
        if (touch_surface_read(event, &surface)) return;
    }
    if (touch_surface_read(event, &surface)) {
        touch_bridge_sync();
        if (surface.phase == TOUCH_ABORT || surface.width != output_width || surface.height != output_height) {
            touch_bridge_cancel();
            return;
        }
        if (surface.phase == TOUCH_DOWN && touch.enabled && touch.effective_mode == TOUCH_GAMEPLAY)
            request_mode(true);
        touch_finger(&touch, surface.phase, surface.device, surface.finger,
                     surface.x, surface.y, surface.sample_ms);
        if (touch.move_owner < 0) clear_movement();
        return;
    }
    switch (event->type) {
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
    case SDL_APP_TERMINATING:
        background = true; touch_bridge_cancel(); break;
    case SDL_APP_WILLENTERFOREGROUND:
    case SDL_APP_DIDENTERFOREGROUND:
        background = event->type != SDL_APP_DIDENTERFOREGROUND;
        touch_bridge_cancel(); break;
    case SDL_DISPLAYEVENT:
        if (event->display.event == SDL_DISPLAYEVENT_ORIENTATION) touch_bridge_cancel();
        break;
    case SDL_WINDOWEVENT:
        switch (event->window.event) {
        case SDL_WINDOWEVENT_FOCUS_LOST:
            unfocused = true; touch_bridge_cancel(); break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            unfocused = false; touch_bridge_cancel(); break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
        case SDL_WINDOWEVENT_MINIMIZED:
        case SDL_WINDOWEVENT_HIDDEN:
            touch_bridge_cancel(); break;
        }
        break;
    /* SDL_FINGER events are intentionally ignored: their x/y can already be
       clamped by the renderer. Android supplies the raw surface events above. */
    }
    touch_bridge_sync();
}

void touch_bridge_merge_movement(void) {
    /* process_motion_keys has just replaced this bank. Forget last frame's
       contributions before context cancellation can clear any physical keys. */
    applied_axis[0] = applied_axis[1] = false;
    touch_bridge_sync();
    if (!touch.enabled || touch.effective_mode != TOUCH_GAMEPLAY) return;
    byte bx, by;
    physics_get_one_control(KEYBD_CONTROL_BANK, CONTROL_XVEL, &bx);
    physics_get_one_control(KEYBD_CONTROL_BANK, CONTROL_YVEL, &by);
    int x = bx, y = by;
    touch_merge_axes(&touch, CONTROL_MAX_VAL / 2, &x, &y);
    if (!bx && x) {
        applied_axis[0] = true; applied[0] = (byte)x;
        physics_set_one_control(KEYBD_CONTROL_BANK, CONTROL_XVEL, (byte)x);
    }
    if (!by && y) {
        applied_axis[1] = true; applied[1] = (byte)y;
        physics_set_one_control(KEYBD_CONTROL_BANK, CONTROL_YVEL, (byte)y);
    }
}

void touch_bridge_consume_look(int *dx, int *dy) {
    touch_bridge_sync();
    *dx = *dy = 0;
    if (!touch.enabled || touch.effective_mode != TOUCH_GAMEPLAY || !mlook_enabled) return;
    touch_consume_look(&touch, dx, dy);
}

void touch_bridge_draw(void) {
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    if (++diagnostic_draw_calls == 1)
        SDL_Log("RP5NP_TOUCH_DRAW_CALL count=1 initialized=%d renderer=%d", initialized, renderer != NULL);
#endif
    touch_bridge_sync();
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    if (renderer) {
        static bool valid;
        static SDL_Rect last_viewport;
        static int last_w, last_h, last_ow, last_oh;
        SDL_Rect viewport;
        int w, h;
        SDL_RenderGetLogicalSize(renderer, &w, &h);
        SDL_RenderGetViewport(renderer, &viewport);
        if (!valid || w != last_w || h != last_h || output_width != last_ow || output_height != last_oh ||
            viewport.x != last_viewport.x || viewport.y != last_viewport.y ||
            viewport.w != last_viewport.w || viewport.h != last_viewport.h) {
            SDL_Log("RP5NP_TOUCH_RENDERER logical=%dx%d viewport=%d,%d,%d,%d output=%dx%d window=%dx%d",
                    w, h, viewport.x, viewport.y, viewport.w, viewport.h,
                    output_width, output_height, window_width, window_height);
            valid = true; last_w = w; last_h = h;
            last_ow = output_width; last_oh = output_height; last_viewport = viewport;
        }
    }
#endif
    if (renderer) touch_draw(&touch, renderer);
}
