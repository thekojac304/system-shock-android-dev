/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include <math.h>
#include <string.h>

static void emit(TouchControls *t, TouchOutput output, float x, float y) {
    if (t->emit) t->emit(t->user, output, x, y);
}

static TouchMode effective(const TouchControls *t) {
    return t->gameplay_available && !t->controller ? t->mode : TOUCH_CURSOR;
}

void touch_init(TouchControls *t, TouchEmit callback, void *user) {
    memset(t, 0, sizeof(*t));
    t->mode = TOUCH_GAMEPLAY;
    t->effective_mode = TOUCH_CURSOR;
    t->move_owner = t->look_owner = t->cursor_owner = -1;
    t->emit = callback;
    t->user = user;
}

void touch_cancel(TouchControls *t) {
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
    t->layout = (TouchLayout){.width=w, .height=h, .move_radius=h*.11f,
        .move_split=w*.45f, .fire={w*.86f,h*.73f,h*.095f},
        .use={w*.69f,h*.85f,h*.065f},
        .toggle={w*.79f,h*.045f,w*.18f,h*.075f}};
    const TouchCircle actions[TOUCH_ACTION_COUNT] = {
        {w*.95f,h*.52f,h*.05f}, {w*.79f,h*.54f,h*.05f},
        {w*.55f,h*.78f,h*.045f}, {w*.55f,h*.90f,h*.045f},
        {w*.52f,h*.10f,h*.045f}, {w*.63f,h*.10f,h*.045f},
        {w*.22f,h*.54f,h*.05f}, {w*.73f,h*.10f,h*.045f},
        {w*.05f,h*.06f,h*.045f}
    };
    memcpy(t->layout.action, actions, sizeof(actions));
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
        t->look_x += (x - f->x) * t->look_scale;
        t->look_y += (y - f->y) * t->look_scale;
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

#ifndef TOUCH_HEADLESS
#include <SDL.h>
/* Small built-in stroke glyphs for temporary control labels.
   No texture assets, fonts, custom artwork, or allocation on the render path. */
static void label(SDL_Renderer *r, const char *s, float cx, float cy, float scale) {
    static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ<>";
    static const unsigned char glyphs[][5] = {
        {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},
        {7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},
        {7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
        {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},
        {2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
        {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
        {5,5,2,2,2},{7,1,2,4,7},{1,2,4,2,1},{4,2,1,2,4}
    };
    float x = cx - (float)strlen(s)*4*scale/2;
    for (; *s; ++s, x += 4*scale) {
        const char *p = strchr(letters, *s);
        if (!p) continue;
        for (int y = 0; y < 5; ++y) for (int col = 0; col < 3; ++col) {
            if (glyphs[p - letters][y] & (1u << (2-col))) {
                SDL_FRect rect = {x+col*scale, cy-2.5f*scale+y*scale, scale, scale};
                SDL_RenderFillRectF(r, &rect);
            }
        }
    }
}

static void circle(SDL_Renderer *r, float x, float y, float radius, bool held) {
    SDL_SetRenderDrawColor(r, 24, 36, 43, held ? 155 : 95);
    for (int row = -(int)radius; row <= (int)radius; ++row) {
        float span = sqrtf(fmaxf(0, radius*radius-row*row));
        SDL_RenderDrawLineF(r, x-span, y+row, x+span, y+row);
    }
    SDL_SetRenderDrawColor(r, 210, 238, 238, held ? 210 : 130);
    for (int i = 0; i < 64; ++i) {
        float a = i * 6.2831853f/64, b = (i+1)*6.2831853f/64;
        SDL_RenderDrawLineF(r, x+cosf(a)*radius, y+sinf(a)*radius,
                           x+cosf(b)*radius, y+sinf(b)*radius);
    }
}

void touch_draw(const TouchControls *t, SDL_Renderer *r) {
    if (!t->enabled || (!t->gameplay_available && !t->pause_available) || t->controller) return;
    Uint8 red, green, blue, alpha;
    SDL_BlendMode blend;
    SDL_Rect clip;
    SDL_Rect viewport;
    float scale_x, scale_y;
    SDL_bool clipped = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &clip);
    SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
    SDL_GetRenderDrawBlendMode(r, &blend);
    SDL_RenderGetScale(r, &scale_x, &scale_y);
    /* Query/save the viewport in pixels to avoid SDL's integer rounding of
       a letterbox offset divided by its logical scale. Logical size stays put. */
    SDL_RenderSetScale(r, 1, 1);
    SDL_RenderGetViewport(r, &viewport);
    SDL_RenderSetViewport(r, NULL);
    SDL_RenderSetClipRect(r, NULL);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    /* Full output surface: the exact raw touch hit-test space. */
    const TouchLayout *l = &t->layout;
    float font = l->height / 192;
    if (t->effective_mode == TOUCH_GAMEPLAY) {
        float x = t->move_owner < 0 ? l->width*.20f : t->anchor_x;
        float y = t->move_owner < 0 ? l->height*.77f : t->anchor_y;
        circle(r, x, y, l->move_radius, t->move_owner >= 0);
        circle(r, x+t->movement_x*l->move_radius, y+t->movement_y*l->move_radius,
               l->move_radius*.37f, t->move_owner >= 0);
        circle(r, l->fire.x, l->fire.y, l->fire.radius, t->fire_count != 0);
        label(r, "FIRE", l->fire.x, l->fire.y, font);
        circle(r, l->use.x, l->use.y, l->use.radius, t->use_count != 0);
        label(r, "USE", l->use.x, l->use.y, font);
        static const char *names[TOUCH_ACTION_COUNT] = {
            "JUMP", "CROUCH", "<", ">", "CONS", "BOOM", "RUN", "ID", "II"
        };
        for (int i = 0; i < TOUCH_PAUSE; ++i) {
            TouchCircle c = l->action[i];
            circle(r, c.x, c.y, c.radius,
                   i == TOUCH_SPRINT ? t->sprint_latched : t->action_count[i] != 0);
            label(r, names[i], c.x, c.y, font*.8f);
        }
    }
    if (t->pause_available) {
        TouchCircle c = l->action[TOUCH_PAUSE];
        circle(r, c.x, c.y, c.radius, t->action_count[TOUCH_PAUSE] != 0);
        label(r, "II", c.x, c.y, font);
    }
    if (!t->gameplay_available) goto restore;
    SDL_FRect toggle = {l->toggle.x, l->toggle.y, l->toggle.w, l->toggle.h};
    SDL_SetRenderDrawColor(r, 24, 36, 43, 125);
    SDL_RenderFillRectF(r, &toggle);
    SDL_SetRenderDrawColor(r, 210, 238, 238, 190);
    int draw_result = SDL_RenderDrawRectF(r, &toggle);
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    static int last_draw_result = 1;
    if (draw_result != last_draw_result) {
        SDL_Log("RP5NP_TOUCH_DRAW_RESULT result=%d blend=BLEND alpha=190 error=%s",
                draw_result, draw_result < 0 ? SDL_GetError() : "none");
        last_draw_result = draw_result;
    }
#else
    (void)draw_result;
#endif
    label(r, t->effective_mode == TOUCH_GAMEPLAY ? "CURSOR" : "LOOK",
          toggle.x+toggle.w/2, toggle.y+toggle.h/2, font);
restore:
    SDL_SetRenderDrawBlendMode(r, blend);
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    SDL_RenderSetViewport(r, &viewport);
    SDL_RenderSetScale(r, scale_x, scale_y);
    SDL_RenderSetClipRect(r, clipped ? &clip : NULL);
}
#endif
