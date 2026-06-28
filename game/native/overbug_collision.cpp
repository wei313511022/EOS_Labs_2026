#include "overbug_native.h"

#include <cmath>

extern "C" int overbug_resolve_collision(
    double ax,
    double ay,
    double ar,
    double bx,
    double by,
    double br,
    double *out_push_x,
    double *out_push_y
) {
    if (out_push_x == nullptr || out_push_y == nullptr) {
        return 0;
    }

    const double dx = ax - bx;
    const double dy = ay - by;
    const double min_distance = ar + br;
    const double distance_sq = dx * dx + dy * dy;

    *out_push_x = 0.0;
    *out_push_y = 0.0;

    if (distance_sq >= min_distance * min_distance) {
        return 0;
    }

    if (distance_sq == 0.0) {
        *out_push_x = min_distance;
        return 1;
    }

    const double distance = std::sqrt(distance_sq);
    const double push = min_distance - distance;
    *out_push_x = dx / distance * push;
    *out_push_y = dy / distance * push;
    return 1;
}
