/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include <math.h>
#include <string.h>

static void emit(TouchControls *t, TouchOutput output, float x, float y) {
    if (t->emit) t->emit(t->user, output, x, y);
}

static TouchMode effective(const TouchControls *t) {
    return t->panel==TOUCH_UI_CLOSED && t->gameplay_available && !t->controller ? t->mode : TOUCH_CURSOR;
}

void touch_init(TouchControls *t, TouchEmit callback, void *user) {
    memset(t, 0, sizeof(*t));
    t->mode = TOUCH_GAMEPLAY;
    t->effective_mode = TOUCH_CURSOR;
    t->move_owner = t->look_owner = t->cursor_owner = -1;
    t->emit = callback;
    t->user = user;
    touch_settings_defaults(&t->settings);
    t->saved_settings=t->settings;
}

void touch_cancel(TouchControls *t) {
    if (t->panel!=TOUCH_UI_CLOSED) {
        t->settings=t->saved_settings;
        t->panel=TOUCH_UI_CLOSED;
        touch_layout_refresh(t);
    }
    t->ui_owned=t->ui_drag=false;
    t->effective_mode=effective(t);
    memset(t->fingers, 0, sizeof(t->fingers));
    t->move_owner = t->look_owner = t->cursor_owner = -1;
    t->movement_x = t->movement_y = t->look_x = t->look_y = 0;
    t->fire_count = t->use_count = 0;
    memset(t->action_count, 0, sizeof(t->action_count));
    t->sprint_latched = false;
    emit(t, TOUCH_CANCEL, 0, 0);
}

void touch_configure(TouchControls *t, float w, float h, bool enabled,
                     bool gameplay_available, bool controller) {
    enabled = enabled && w > 0 && h > 0;
    if (w == t->layout.width && h == t->layout.height && enabled == t->enabled &&
        gameplay_available == t->gameplay_available && controller == t->controller) return;
    touch_cancel(t);
    t->enabled = enabled;
    t->gameplay_available = gameplay_available;
    t->pause_available = gameplay_available;
    t->controller = controller;
    t->effective_mode = effective(t);
    t->layout.width=w; t->layout.height=h;
    touch_layout_refresh(t);
    t->cursor_slop = h * (8.0f / 768.0f);
    t->look_scale = 1.0f;
}

void touch_set_mode(TouchControls *t, TouchMode mode) {
    if (t->mode == mode) return;
    touch_cancel(t);
    t->mode = mode;
    t->effective_mode = effective(t);
    emit(t, TOUCH_MODE_CHANGED, 0, 0);
}

static bool in_circle(float x, float y, TouchCircle c) {
    x -= c.x; y -= c.y;
    return x*x + y*y <= c.radius*c.radius;
}

static bool in_rect(float x, float y, TouchRect r) {
    return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
}

static void move_finger(TouchControls *t, TouchFinger *f, float x, float y) {
    if (f->owns_look) {
        t->look_x += (x - f->x) * t->look_scale * t->settings.sensitivity_x;
        t->look_y += (y - f->y) * t->look_scale * t->settings.sensitivity_y *
                     (t->settings.invert_y ? -1.0f : 1.0f);
    }
    f->x = x; f->y = y;
    if (f->role == TOUCH_MOVE) {
        float dx = (x - t->anchor_x) / t->layout.move_radius;
        float dy = (y - t->anchor_y) / t->layout.move_radius;
        float magnitude = sqrtf(dx*dx + dy*dy);
        if (magnitude > 1) { dx /= magnitude; dy /= magnitude; }
        t->movement_x = dx; t->movement_y = dy;
    }
}

void touch_finger(TouchControls *t, TouchPhase phase, int64_t device, int64_t id,
                  float x, float y, uint32_t timestamp_ms) {
    if (phase == TOUCH_ABORT) { touch_cancel(t); return; }
    /* Raw surface pixels, including bars. Ownership precedes any UI mapping. */
    if (!t->enabled || !isfinite(x) || !isfinite(y)) return;
    if (t->panel!=TOUCH_UI_CLOSED) {
        touch_ui_finger(t,phase,device,id,x,y);
        return;
    }
    if (phase==TOUCH_DOWN && !t->controller && (t->gameplay_available || t->pause_available) &&
        touch_control_hit(t,TC_SETTINGS,x,y)) {
        touch_ui_open(t);
        return;
    }
    int index = -1, free_slot = -1;
    for (int i = 0; i < TOUCH_MAX_FINGERS; ++i) {
        TouchFinger *f = &t->fingers[i];
        if (f->active && f->touch == device && f->finger == id) index = i;
        if (!f->active && free_slot < 0) free_slot = i;
    }
    if (phase == TOUCH_DOWN) {
        if (index >= 0 || free_slot < 0) return;
        index = free_slot;
        TouchRole role = TOUCH_NONE;
        if (!t->controller && t->pause_available &&
            in_circle(x, y, t->layout.action[TOUCH_PAUSE])) {
            role = TOUCH_ACTION;
        } else if (!t->controller && t->gameplay_available && in_rect(x, y, t->layout.toggle)) {
            touch_set_mode(t, t->mode == TOUCH_GAMEPLAY ? TOUCH_CURSOR : TOUCH_GAMEPLAY);
            role = TOUCH_MODE_TOGGLE;
        } else if (t->effective_mode == TOUCH_CURSOR) {
            if (t->cursor_owner < 0) role = TOUCH_CURSOR_OWNER;
        } else {
            for (int action = 0; action < TOUCH_PAUSE; ++action)
                if (in_circle(x, y, t->layout.action[action])) {
                    role = TOUCH_ACTION; break;
                }
        }
        if (role == TOUCH_NONE && t->effective_mode == TOUCH_GAMEPLAY &&
            in_circle(x, y, t->layout.fire)) role = TOUCH_FIRE;
        else if (role == TOUCH_NONE && t->effective_mode == TOUCH_GAMEPLAY &&
                 in_circle(x, y, t->layout.use)) role = TOUCH_USE;
        else if (role == TOUCH_NONE && t->effective_mode == TOUCH_GAMEPLAY &&
                 x < t->layout.move_split && t->move_owner < 0) role = TOUCH_MOVE;
        else if (role == TOUCH_NONE && t->effective_mode == TOUCH_GAMEPLAY &&
                 x >= t->layout.move_split && t->look_owner < 0) role = TOUCH_LOOK;

        TouchFinger *f = &t->fingers[index];
        *f = (TouchFinger){.touch=device, .finger=id, .active=true, .role=role,
            .x=x, .y=y, .start_x=x, .start_y=y, .down_ms=timestamp_ms};
        if (role == TOUCH_ACTION) {
            for (int action = 0; action < TOUCH_ACTION_COUNT; ++action)
                if (in_circle(x, y, t->layout.action[action])) {
                    f->action = (TouchAction)action; break;
                }
            if (t->action_count[f->action]++ == 0 && f->action != TOUCH_SPRINT)
                emit(t, TOUCH_ACTION_DOWN, (float)f->action, 0);
        }
        if (role == TOUCH_MOVE) {
            t->move_owner = index; t->anchor_x = x; t->anchor_y = y;
        }
        if ((role == TOUCH_LOOK || role == TOUCH_FIRE) && t->look_owner < 0) {
            t->look_owner = index; f->owns_look = true;
        }
        if (role == TOUCH_FIRE && t->fire_count++ == 0) emit(t, TOUCH_FIRE_DOWN, x, y);
        if (role == TOUCH_USE && t->use_count++ == 0) emit(t, TOUCH_USE_PRESS, x, y);
        if (role == TOUCH_CURSOR_OWNER) {
            t->cursor_owner = index;
            emit(t, TOUCH_POINTER_MOVE, x, y); /* position, never press yet */
        }
        return;
    }
    /* Unknown motion/up, including cancelled fingers, can never claim a role. */
    if (index < 0) return;
    TouchFinger *f = &t->fingers[index];
    move_finger(t, f, x, y);
    if (f->role == TOUCH_CURSOR_OWNER) {
        float dx = x - f->start_x, dy = y - f->start_y;
        if (!f->dragging && dx*dx + dy*dy > t->cursor_slop*t->cursor_slop) {
            f->dragging = true;
            /* Start the original UI drag at the touched object, not at the
               first point outside slop (which could already be another object). */
            emit(t, TOUCH_POINTER_DOWN, f->start_x, f->start_y);
        }
        if (f->dragging) {
            emit(t, TOUCH_POINTER_MOVE, x, y);
            if (phase == TOUCH_UP) emit(t, TOUCH_POINTER_UP, x, y);
        } else if (phase == TOUCH_UP && timestamp_ms - f->down_ms <= TOUCH_TAP_MS) {
            /* Anchor taps to their initial position so small jitter cannot
               move a click onto the neighbouring small original UI target. */
            emit(t, TOUCH_POINTER_TAP, f->start_x, f->start_y);
        }
    }
    if (phase != TOUCH_UP) return;
    if (f->role == TOUCH_MOVE) {
        t->move_owner = -1; t->movement_x = t->movement_y = 0;
    }
    if (f->owns_look) t->look_owner = -1;
    if (f->role == TOUCH_FIRE && --t->fire_count == 0) emit(t, TOUCH_FIRE_UP, x, y);
    if (f->role == TOUCH_USE) --t->use_count;
    if (f->role == TOUCH_ACTION && --t->action_count[f->action] == 0) {
        if (f->action == TOUCH_SPRINT) {
            float dx = x - f->start_x, dy = y - f->start_y;
            if (timestamp_ms - f->down_ms <= TOUCH_TAP_MS &&
                dx*dx + dy*dy <= t->cursor_slop*t->cursor_slop &&
                in_circle(x, y, t->layout.action[TOUCH_SPRINT])) {
                t->sprint_latched = !t->sprint_latched;
                emit(t, t->sprint_latched ? TOUCH_ACTION_DOWN : TOUCH_ACTION_UP,
                     (float)TOUCH_SPRINT, 0);
            }
        } else emit(t, TOUCH_ACTION_UP, (float)f->action, 0);
    }
    if (f->role == TOUCH_CURSOR_OWNER) t->cursor_owner = -1;
    f->active = false;
}

void touch_map_cursor(float x, float y, TouchRect viewport, float sx, float sy,
                      int width, int height, float *gx, float *gy) {
    *gx = fmaxf(0, fminf((x - viewport.x) / sx, (float)(width - 1)));
    *gy = fmaxf(0, fminf((y - viewport.y) / sy, (float)(height - 1)));
}

void touch_consume_look(TouchControls *t, int *dx, int *dy) {
    *dx = (int)t->look_x; *dy = (int)t->look_y;
    t->look_x -= *dx; t->look_y -= *dy;
}

void touch_merge_axes(const TouchControls *t, int walk_strength, int *x, int *y) {
    if (!t->enabled || t->effective_mode != TOUCH_GAMEPLAY) return;
    if (!*x) *x = (int)lroundf(t->movement_x * walk_strength);
    if (!*y) *y = (int)lroundf(-t->movement_y * walk_strength);
}

void touch_movie_tap_reset(TouchMovieTap *tap) {
    memset(tap, 0, sizeof(*tap));
}

bool touch_movie_tap_event(TouchMovieTap *tap, TouchPhase phase,
                           int64_t device, int64_t finger, float x, float y,
                           uint32_t timestamp_ms, float slop) {
    if (phase == TOUCH_ABORT) { touch_movie_tap_reset(tap); return false; }
    if (!isfinite(x) || !isfinite(y) || !isfinite(slop) || slop < 0) return false;
    int index = -1, free_slot = -1;
    for (int i = 0; i < TOUCH_MAX_FINGERS; ++i) {
        TouchFinger *f = &tap->fingers[i];
        if (f->active && f->touch == device && f->finger == finger) index = i;
        if (!f->active && free_slot < 0) free_slot = i;
    }
    if (phase == TOUCH_DOWN) {
        if (index >= 0) return false;
        if (free_slot < 0) { tap->blocked = tap->overflow = true; return false; }
        if (tap->active_count == 0) tap->blocked = tap->overflow;
        TouchFinger *f = &tap->fingers[free_slot];
        *f = (TouchFinger){.touch=device, .finger=finger, .active=true,
                           .start_x=x, .start_y=y, .down_ms=timestamp_ms};
        if (++tap->active_count > 1) tap->blocked = true;
        return false;
    }
    if (index < 0) return false;
    TouchFinger *f = &tap->fingers[index];
    float dx = x - f->start_x, dy = y - f->start_y;
    if (dx*dx + dy*dy > slop*slop) tap->blocked = true;
    if (phase != TOUCH_UP) return false;
    bool skip = tap->active_count == 1 && !tap->blocked &&
                timestamp_ms - f->down_ms <= TOUCH_TAP_MS;
    f->active = false;
    --tap->active_count;
    if (tap->active_count == 0) tap->blocked = tap->overflow;
    return skip;
}

uint8_t touch_mouse_buttons(const TouchMouseButtons *b) {
    return b->source[0] | b->source[1] | b->source[2];
}

bool touch_mouse_button(TouchMouseButtons *b, TouchMouseSource source, uint8_t bit, bool down) {
    uint8_t before = touch_mouse_buttons(b);
    if (down) b->source[source] |= bit;
    else b->source[source] &= (uint8_t)~bit;
    return ((before ^ touch_mouse_buttons(b)) & bit) != 0;
}

bool touch_key_held(const TouchKeySources *k) {
    return k->source[0] || k->source[1] || k->source[2];
}

bool touch_key_source(TouchKeySources *k, TouchKeySource source, bool down) {
    bool before = touch_key_held(k);
    k->source[source] = down;
    return before != touch_key_held(k);
}
