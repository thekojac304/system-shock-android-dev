#ifndef SHOCK_AUDIO_MASTER_GAIN_H
#define SHOCK_AUDIO_MASTER_GAIN_H
#include <stddef.h>
#include <stdint.h>
#include "AudioVolumeCurve.h"
/* Final S16 postmix bus: scale every source once after category/local mixing. */
static inline void AudioMasterGainS16(void *buffer,size_t bytes,int percent) {
    if (!buffer || percent>=100) return;
    double gain=AudioVolumeGain(percent);
    int16_t *samples=(int16_t *)buffer;
    for (size_t i=0;i<bytes/sizeof(int16_t);++i)
        samples[i]=(int16_t)((double)samples[i]*gain);
}
#endif
