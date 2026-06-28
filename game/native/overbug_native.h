#ifndef OVERBUG_NATIVE_H
#define OVERBUG_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#define OVERBUG_COMPONENT_COUNT 3

int overbug_match_order(
    const int board_counts[OVERBUG_COMPONENT_COUNT],
    const int orders[][OVERBUG_COMPONENT_COUNT],
    int order_count
);

int overbug_score_order(const int order_counts[OVERBUG_COMPONENT_COUNT]);

int overbug_resolve_collision(
    double ax,
    double ay,
    double ar,
    double bx,
    double by,
    double br,
    double *out_push_x,
    double *out_push_y
);

#ifdef __cplusplus
}
#endif

#endif
