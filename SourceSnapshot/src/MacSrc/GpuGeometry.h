/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_GEOMETRY_H
#define SHOCK_GPU_GEOMETRY_H

#include <stdbool.h>
#include <stdint.h>

/* Clipping may emit the same view-space vertex twice: an ordinary point and
 * an intersection. The ordinary projector uses one multiply/divide; the clip
 * projector truncates x/z or y/z before multiplying by half the viewport.
 * Their screen coordinates can therefore differ despite EXACT equal xyz.
 *
 * Normalize only clipped points that have an unambiguous ordinary counterpart.
 * Keep all vertices/attributes, and change only the caller's GPU screen copy.
 * The allowed delta is derived from those two projection formulas: no more
 * than ceil(viewport_dimension/2)+1 raw 16.16 units per coordinate (under .008
 * pixel at 1024x768). Larger differences and clipped-only groups are untouched.
 * The convexity guard below remains unchanged and validates the final outline.
 * Returns changed-point count, or -1 for invalid arguments.
 */
static inline int gw_polygon_canonicalize_clipped_positions(
    int n, const int32_t (*xyz)[3], const unsigned char *clipped,
    int width, int height, int32_t (*xy)[2]) {
    if (!xyz || !clipped || !xy || n < 3 || n > 32 || width <= 0 || height <= 0)
        return -1;
    int64_t limit_x = width / 2 + width % 2 + 1;
    int64_t limit_y = height / 2 + height % 2 + 1;
    int changed = 0;
    for (int i = 0; i < n; ++i) {
        if (!clipped[i]) continue;
        int representative = -1;
        bool ambiguous = false;
        for (int j = 0; j < n; ++j) {
            if (clipped[j] || xyz[i][0] != xyz[j][0] ||
                xyz[i][1] != xyz[j][1] || xyz[i][2] != xyz[j][2]) continue;
            if (representative < 0) representative = j;
            else if (xy[j][0] != xy[representative][0] ||
                     xy[j][1] != xy[representative][1]) ambiguous = true;
        }
        if (representative < 0 || ambiguous) continue;
        int64_t dx = (int64_t)xy[i][0] - xy[representative][0];
        int64_t dy = (int64_t)xy[i][1] - xy[representative][1];
        if (dx < -limit_x || dx > limit_x || dy < -limit_y || dy > limit_y)
            continue;
        if (dx || dy) {
            xy[i][0] = xy[representative][0];
            xy[i][1] = xy[representative][1];
            ++changed;
        }
    }
    return changed;
}

/* Input coordinates are the engine's projected signed 16.16 values. Triangle
 * fans are safe only when every vertex lies on the same side of every directed
 * boundary edge. Testing just consecutive turns would admit some star polygons.
 *
 * A two-raw-unit edge-distance tolerance admits projection/clipping roundoff:
 * |cross| <= 2 * (|dx| + |dy|). Two raw units are 1/32768 of a screen pixel;
 * they are NOT two pixels. This bound applies to each edge/vertex comparison,
 * not to arbitrary independent perturbations of a complete collinear chain.
 * Entirely collinear/repeated-point polygons are harmless zero-area fans.
 *
 * Cast before subtracting. An int32 difference can exceed INT32_MAX, and a
 * determinant can exceed INT64_MAX; double covers the complete input range.
 */
static inline bool gw_polygon_is_convex(int n, const int32_t (*xy)[2]) {
    if (!xy || n < 3 || n > 32) return false;
    int winding = 0;
    for (int i = 0; i < n; ++i) {
        int next = (i + 1) % n;
        double dx = (double)xy[next][0] - (double)xy[i][0];
        double dy = (double)xy[next][1] - (double)xy[i][1];
        double tolerance = 2.0 * ((dx < 0.0 ? -dx : dx) +
                                  (dy < 0.0 ? -dy : dy));
        for (int j = 0; j < n; ++j) {
            double px = (double)xy[j][0] - (double)xy[i][0];
            double py = (double)xy[j][1] - (double)xy[i][1];
            double cross = dx * py - dy * px;
            int sign = (cross > tolerance) - (cross < -tolerance);
            if (sign && winding && sign != winding) return false;
            if (sign) winding = sign;
        }
    }
    return true;
}

#endif
