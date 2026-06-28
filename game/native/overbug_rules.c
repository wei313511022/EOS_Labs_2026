#include "overbug_native.h"

int overbug_match_order(
    const int board_counts[OVERBUG_COMPONENT_COUNT],
    const int orders[][OVERBUG_COMPONENT_COUNT],
    int order_count
) {
    int order_index;
    int component_index;

    if (board_counts == 0 || orders == 0 || order_count <= 0) {
        return -1;
    }

    for (order_index = 0; order_index < order_count; ++order_index) {
        int matches = 1;
        for (component_index = 0; component_index < OVERBUG_COMPONENT_COUNT; ++component_index) {
            if (board_counts[component_index] != orders[order_index][component_index]) {
                matches = 0;
                break;
            }
        }
        if (matches) {
            return order_index;
        }
    }

    return -1;
}

int overbug_score_order(const int order_counts[OVERBUG_COMPONENT_COUNT]) {
    int component_index;
    int total = 0;

    if (order_counts == 0) {
        return 0;
    }

    for (component_index = 0; component_index < OVERBUG_COMPONENT_COUNT; ++component_index) {
        total += order_counts[component_index];
    }

    return 10 + total * 5;
}
