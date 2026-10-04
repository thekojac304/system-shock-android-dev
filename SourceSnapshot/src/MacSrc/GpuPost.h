/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_POST_H
#define SHOCK_GPU_POST_H
#include <math.h>

/* Centralized, deliberately small art-direction table. Index zero chooses the
 * level default; indices 1..3 are Medical and 4..6 are Executive. */
typedef struct {
    const char *name;
    float exposure, contrast, toe, shoulder, saturation;
    float shadow[3], highlight[3];
    float shadow_strength, highlight_strength;
    float vignette, radius, softness, grain;
    float haze_color[3], haze_start, haze_end, haze_strength;
    float far_desaturation, far_contrast;
    float shadow_bias[3];
    float drift_period, drift_strength;
} GpuPostPreset;

/* Distance is packed into the world target's otherwise unused alpha channel.
 * A zero-alpha clear pixel has no atmosphere, even at the edge of the view. */
#define GPU_POST_DISTANCE_RANGE 24.f
static inline float gpu_post_clamp(float x,float low,float high) {
    return fmaxf(low,fminf(high,x));
}
static inline float gpu_post_distance_factor(float distance,float start,float end) {
    if(end<=start||distance<=start)return 0.f;
    float t=gpu_post_clamp((distance-start)/(end-start),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
static inline float gpu_post_shadow_weight(float lum) {
    return 1.f-gpu_post_distance_factor(gpu_post_clamp(lum,0.f,1.f),.12f,.62f);
}
static inline float gpu_post_drift(float game_seconds,float period,float strength) {
    if(period<=0.f||strength<=0.f)return 0.f;
    return sinf(6.28318530718f*fmodf(game_seconds,period)/period)*
        gpu_post_clamp(strength,0.f,.02f);
}

static inline int gpu_post_resolve(int level,int choice) {
    if(choice<0||choice>6)choice=0;
    if(level==1)return choice>=1&&choice<=3?choice:1;
    if(level==6)return choice>=4&&choice<=6?choice:4;
    return 0;
}

const GpuPostPreset *gpu_post_preset(int level, int choice);
const char *gpu_post_choice_name(int choice);

#endif
