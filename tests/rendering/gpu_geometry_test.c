/* SPDX-License-Identifier: GPL-3.0-or-later
 * Build: cc -std=c11 -Wall -Wextra -Werror -O2 gpu_geometry_test.c -o geometry-test
 * Pure production helper: no game assets, renderer, GL context or device API.
 */
#include "../../SourceSnapshot/src/MacSrc/GpuGeometry.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Every boundary start and both windings must give the same decision. */
static void check_rotations(int n, const int32_t (*xy)[2], bool expected) {
    int32_t reordered[32][2];
    int32_t xyz[32][3];
    unsigned char clipped[32];
    for (int reverse = 0; reverse < 2; ++reverse)
        for (int start = 0; start < n; ++start) {
            for (int i = 0; i < n; ++i) {
                int index = (start + (reverse ? n - i : i)) % n;
                reordered[i][0] = xy[index][0];
                reordered[i][1] = xy[index][1];
                xyz[i][0] = xy[index][0];
                xyz[i][1] = xy[index][1];
                xyz[i][2] = 65536;
                clipped[i] = (unsigned char)(i % 2);
            }
            assert(gw_polygon_canonicalize_clipped_positions(
                n, (const int32_t (*)[3])xyz, clipped, 1024, 768, reordered) == 0);
            assert(gw_polygon_is_convex(n, (const int32_t (*)[2])reordered) == expected);
        }
}

static void measured_duplicate_projection(void) {
    /* Unmodified numeric fixture from build6-device.log: frame100, level1,
     * terrain tile18,19, viewport1024x768, camera18.1114807,18.254715,1.72702026.
     * The first point is PF_CLIPPNT (flags2f); second is ordinary (flags0f).
     * Equal view-space xyz was projected by two different fixed-point formulas,
     * producing a 387-raw-unit reversed edge. Keep the strict guard unchanged. */
    const int32_t measured_xy[4][2] = {
        {17349632, 0}, {17349245, 0}, {0, 28653696}, {0, 0}
    };
    const int32_t measured_xyz[4][3] = {
        {-18839, 39008, 39008}, {-18839, 39008, 39008},
        {-30188, -4184, 30188}, {-43726, 43726, 43726}
    };
    const unsigned char measured_clipped[4] = {1, 0, 1, 1};
    for (int reverse = 0; reverse < 2; ++reverse)
        for (int start = 0; start < 4; ++start) {
            int32_t xy[4][2], xyz[4][3];
            unsigned char clipped[4];
            for (int i = 0; i < 4; ++i) {
                int index = (start + (reverse ? 4 - i : i)) % 4;
                memcpy(xy[i], measured_xy[index], sizeof(xy[i]));
                memcpy(xyz[i], measured_xyz[index], sizeof(xyz[i]));
                clipped[i] = measured_clipped[index];
            }
            assert(!gw_polygon_is_convex(4, (const int32_t (*)[2])xy));
            assert(gw_polygon_canonicalize_clipped_positions(
                4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 1);
            assert(gw_polygon_is_convex(4, (const int32_t (*)[2])xy));
            for (int i = 0; i < 4; ++i) {
                int index = (start + (reverse ? 4 - i : i)) % 4;
                const int32_t *expected = measured_xy[index == 0 ? 1 : index];
                assert(!memcmp(xy[i], expected, sizeof(xy[i])));
                assert(!memcmp(xyz[i], measured_xyz[index], sizeof(xyz[i])));
            }
            assert(gw_polygon_canonicalize_clipped_positions(
                4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
        }

    int32_t xy[4][2], xyz[4][3];
    unsigned char clipped[4];
    memcpy(xy, measured_xy, sizeof(xy));
    memcpy(xyz, measured_xyz, sizeof(xyz));
    memcpy(clipped, measured_clipped, sizeof(clipped));
    ++xyz[0][2]; /* Close in world space is insufficient: exact equality only. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    assert(!memcmp(xy, measured_xy, sizeof(xy)));
    memcpy(xyz, measured_xyz, sizeof(xyz));
    clipped[1] = 1; /* No ordinary counterpart: retain conservative fallback. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    clipped[1] = 0;
    xy[0][0] = xy[1][0] + 514; /* Greater than the viewport-derived X bound. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    assert(!gw_polygon_is_convex(4, (const int32_t (*)[2])xy));
    memcpy(xy, measured_xy, sizeof(xy));
    xy[0][1] = 386; /* Greater than the viewport-derived Y bound. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    memcpy(xy, measured_xy, sizeof(xy));
    memcpy(xyz[3], xyz[1], sizeof(xyz[3]));
    clipped[3] = 0; /* Conflicting ordinary projections are not normalized. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    memcpy(xyz, measured_xyz, sizeof(xyz));
    clipped[3] = 1;
    xy[0][0] = INT32_MIN;
    xy[1][0] = INT32_MAX; /* Difference must not overflow signed int32. */
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 1024, 768, xy) == 0);
    assert(gw_polygon_canonicalize_clipped_positions(
        4, (const int32_t (*)[3])xyz, clipped, 0, 768, xy) == -1);
    assert(gw_polygon_canonicalize_clipped_positions(
        4, NULL, clipped, 1024, 768, xy) == -1);
}

int main(void) {
    measured_duplicate_projection();
    const int32_t triangle[][2] = {{0, 0}, {8 * 65536, 0}, {3 * 65536, 6 * 65536}};
    const int32_t quad[][2] = {{-5 * 65536, -3 * 65536}, {7 * 65536, -2 * 65536},
                             {9 * 65536, 5 * 65536}, {-2 * 65536, 4 * 65536}};
    const int32_t clipped[][2] = {{0, 0}, {65536, 0}, {2 * 65536, 2},
                                 {2 * 65536, 65536}, {0, 65536}};
    const int32_t clipped_inward[][2] = {{0, 0}, {65536, 1}, {2 * 65536, 0},
                                        {2 * 65536, 65536}, {0, 65536}};
    const int32_t outside_edge_tolerance[][2] = {{0, 0}, {65536, 2}, {2 * 65536, 0},
                                               {2 * 65536, 65536}, {0, 65536}};
    const int32_t concave[][2] = {{0, 0}, {4 * 65536, 0}, {2 * 65536, 65536},
                                 {4 * 65536, 4 * 65536}, {0, 4 * 65536}};
    const int32_t bowtie[][2] = {{0, 0}, {4 * 65536, 4 * 65536},
                                {0, 4 * 65536}, {4 * 65536, 0}};
    /* A pentagram's consecutive turns agree, but its boundary is not convex. */
    const int32_t star[][2] = {{0, 100 * 65536}, {-59 * 65536, -81 * 65536},
                              {95 * 65536, 31 * 65536}, {-95 * 65536, 31 * 65536},
                              {59 * 65536, -81 * 65536}};
    const int32_t collinear[][2] = {{0, 0}, {65536, 65536}, {3 * 65536, 3 * 65536},
                                   {2 * 65536, 2 * 65536}, {-65536, -65536}};
    const int32_t same_point[][2] = {{17, -31}, {17, -31}, {17, -31}, {17, -31}};
    const int32_t repeated_vertex[][2] = {{0, 0}, {65536, 0}, {65536, 0},
                                         {65536, 65536}, {0, 65536}};
    const int32_t extremes[][2] = {{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN},
                                  {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MAX}};
    const int32_t extreme_concave[][2] = {{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN},
                                         {0, 0}, {INT32_MAX, INT32_MAX},
                                         {INT32_MIN, INT32_MAX}};
    const int32_t extreme_collinear[][2] = {{INT32_MIN, INT32_MIN}, {0, 0},
                                           {INT32_MAX, INT32_MAX}};
    check_rotations(3, triangle, true);
    check_rotations(4, quad, true);
    check_rotations(5, clipped, true);
    check_rotations(5, clipped_inward, true);
    check_rotations(5, outside_edge_tolerance, false);
    check_rotations(5, concave, false);
    check_rotations(4, bowtie, false);
    check_rotations(5, star, false);
    check_rotations(5, collinear, true);
    check_rotations(4, same_point, true);
    check_rotations(5, repeated_vertex, true);
    check_rotations(4, extremes, true);
    check_rotations(5, extreme_concave, false);
    check_rotations(3, extreme_collinear, true);
    assert(!gw_polygon_is_convex(3, NULL));
    assert(!gw_polygon_is_convex(-1, triangle));
    assert(!gw_polygon_is_convex(0, triangle));
    assert(!gw_polygon_is_convex(2, triangle));
    assert(!gw_polygon_is_convex(33, triangle));
    assert(!gw_polygon_is_convex(INT_MAX, triangle));
    int32_t maximum_vertices[32][2];
    for (int i = 0; i < 32; ++i) {
        int corner = i / 8;
        int t = (i % 8) * 65536;
        maximum_vertices[i][0] = corner == 0 ? t : corner == 1 ? 8 * 65536 :
                                corner == 2 ? 8 * 65536 - t : 0;
        maximum_vertices[i][1] = corner == 0 ? 0 : corner == 1 ? t :
                                corner == 2 ? 8 * 65536 : 8 * 65536 - t;
    }
    check_rotations(32, (const int32_t (*)[2])maximum_vertices, true);
    puts("PASS: production polygon guard: measured duplicate projection with bounded GPU-only canonicalization, both windings/all starts, 3..32 vertices, clipped tolerance, concavity, crossing/star topology, zero-area/repeated points, int32 extremes");
    return 0;
}
