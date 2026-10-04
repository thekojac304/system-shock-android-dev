/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_POST_H
#define SHOCK_GPU_POST_H

/* Centralized, deliberately small art-direction table. Index zero chooses the
 * level default; indices 1..3 are Medical and 4..6 are Executive. */
typedef struct {
    const char *name;
    float exposure, contrast, toe, shoulder, saturation;
    float shadow[3], highlight[3];
    float shadow_strength, highlight_strength;
    float vignette, radius, softness, grain;
} GpuPostPreset;

static inline int gpu_post_resolve(int level,int choice) {
    if(choice<0||choice>6)choice=0;
    if(level==1)return choice>=1&&choice<=3?choice:1;
    if(level==6)return choice>=4&&choice<=6?choice:4;
    return 0;
}

const GpuPostPreset *gpu_post_preset(int level, int choice);
const char *gpu_post_choice_name(int choice);

#endif
