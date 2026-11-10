#ifndef SHOCK_AUDIO_VOLUME_CURVE_H
#define SHOCK_AUDIO_VOLUME_CURVE_H
/* Fixed microgain table for -30 dB at 1% through 0 dB at 100%.
   Generated from round(1e6 * 10^(-1.5 * (100-p) / 100)). Fixed values avoid
   libm in the callback and make MIDI CC7/SDL_mixer conversion deterministic. */
static inline double AudioVolumeGain(int percent) {
    static const int microgain[101] = {
        0, 32734, 33884, 35075, 36308, 37584, 38905, 40272,
        41687, 43152, 44668, 46238, 47863, 49545, 51286, 53088,
        54954, 56885, 58884, 60954, 63096, 65313, 67608, 69984,
        72444, 74989, 77625, 80353, 83176, 86099, 89125, 92257,
        95499, 98855, 102329, 105925, 109648, 113501, 117490, 121619,
        125893, 130317, 134896, 139637, 144544, 149624, 154882, 160325,
        165959, 171791, 177828, 184077, 190546, 197242, 204174, 211349,
        218776, 226464, 234423, 242661, 251189, 260016, 269153, 278612,
        288403, 298538, 309030, 319890, 331131, 342768, 354813, 367282,
        380189, 393550, 407380, 421697, 436516, 451856, 467735, 484172,
        501187, 518800, 537032, 555904, 575440, 595662, 616595, 638263,
        660693, 683912, 707946, 732825, 758578, 785236, 812831, 841395,
        870964, 901571, 933254, 966051, 1000000
    };
    if (percent <= 0) return 0.0;
    if (percent >= 100) return 1.0;
    return microgain[percent] / 1000000.0;
}

static inline int AudioVolumeScale(int percent, int full_scale) {
    if (full_scale <= 0) return 0;
    return (int)(AudioVolumeGain(percent) * full_scale + 0.5);
}
#endif
