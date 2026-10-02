/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_CONTROLS_H
#define TOUCH_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

#define TOUCH_MAX_FINGERS 16
typedef enum { TOUCH_GAMEPLAY, TOUCH_CURSOR } TouchMode;
typedef enum {
    TOUCH_NONE, TOUCH_MOVE, TOUCH_LOOK, TOUCH_FIRE, TOUCH_USE,
    TOUCH_CURSOR_OWNER, TOUCH_MODE_TOGGLE
} TouchRole;
typedef enum { TOUCH_DOWN, TOUCH_MOTION, TOUCH_UP, TOUCH_ABORT } TouchPhase;
#define TOUCH_TAP_MS 350u
typedef enum {
    TOUCH_FIRE_DOWN, TOUCH_FIRE_UP, TOUCH_USE_PRESS,
    TOUCH_POINTER_DOWN, TOUCH_POINTER_MOVE, TOUCH_POINTER_UP,
    TOUCH_MODE_CHANGED, TOUCH_CANCEL
} TouchOutput;
/* Outputs are ordered edges, not polled booleans: even a down/up between
   game polls reaches the original input queue. Cancellation invalidates them. */
typedef void (*TouchEmit)(void *user, TouchOutput output, float x, float y);
typedef struct { float x, y, radius; } TouchCircle;
typedef struct { float x, y, w, h; } TouchRect;
typedef struct {
    float width, height, move_radius, move_split;
    TouchCircle fire, use;
    TouchRect toggle;
} TouchLayout;
typedef struct {
    int64_t touch, finger; /* exact SDL_TouchID / SDL_FingerID values */
    bool active, owns_look;
    TouchRole role;
    float x, y, start_x, start_y;
    uint32_t down_ms;
    bool dragging;
} TouchFinger;
typedef struct {
    TouchMode mode, effective_mode;
    bool enabled, gameplay_available, controller;
    TouchLayout layout;
    TouchFinger fingers[TOUCH_MAX_FINGERS];
    int move_owner, look_owner, cursor_owner;
    unsigned fire_count, use_count;
    float anchor_x, anchor_y, movement_x, movement_y, look_x, look_y;
    float cursor_slop, look_scale;
    TouchEmit emit;
    void *user;
} TouchControls;

void touch_init(TouchControls *t, TouchEmit emit, void *user);
void touch_cancel(TouchControls *t);
void touch_configure(TouchControls *t, float width, float height,
                     bool enabled, bool gameplay_available, bool controller);
void touch_set_mode(TouchControls *t, TouchMode mode);
void touch_finger(TouchControls *t, TouchPhase phase, int64_t device, int64_t finger,
                  float surface_x, float surface_y, uint32_t timestamp_ms);
/* Sole surface -> game transform; clamp only after gameplay ownership. */
void touch_map_cursor(float x, float y, TouchRect viewport, float scale_x,
                      float scale_y, int width, int height, float *gx, float *gy);
void touch_consume_look(TouchControls *t, int *dx, int *dy);
/* Per-axis keyboard/controller precedence, at normal walking strength. */
void touch_merge_axes(const TouchControls *t, int walk_strength, int *x, int *y);

/* Independent source masks: release edges occur only when the aggregate goes
   up. Bit numbering is the original Shock left=0, right=1 convention. */
typedef enum { TOUCH_MOUSE_PHYSICAL, TOUCH_MOUSE_CONTROLLER, TOUCH_MOUSE_TOUCH } TouchMouseSource;
typedef struct { uint8_t source[3]; } TouchMouseButtons;
uint8_t touch_mouse_buttons(const TouchMouseButtons *b);
bool touch_mouse_button(TouchMouseButtons *b, TouchMouseSource source,
                        uint8_t bit, bool down);

struct SDL_Renderer;
void touch_draw(const TouchControls *t, struct SDL_Renderer *renderer);
#endif
