from __future__ import annotations

from random import Random
from typing import Iterable, Optional

from .models import Component, Order

MIN_ORDER_COMPONENTS = 1
MAX_ORDER_COMPONENTS = 4
MAX_COMPONENT_PER_KIND = 2
ORDER_QUEUE_SIZE = 3
ROUND_SECONDS = 180.0
SOLDER_SECONDS = 2.0
INSPECTION_SECONDS = 4.0


def score_counts(counts: Iterable[int]) -> int:
    return 10 + sum(counts) * 5


def counts_match(left: Iterable[int], right: Iterable[int]) -> bool:
    return tuple(left) == tuple(right)


def generate_order_requirements(rng: Random) -> list[int]:
    while True:
        counts = [rng.randint(0, MAX_COMPONENT_PER_KIND) for _ in Component]
        total = sum(counts)
        if MIN_ORDER_COMPONENTS <= total <= MAX_ORDER_COMPONENTS:
            return counts


def find_matching_order_index(
    board_counts: Iterable[int], orders: Iterable[Order]
) -> Optional[int]:
    board_tuple = tuple(board_counts)
    for index, order in enumerate(orders):
        if order.locked_board_id is None and tuple(order.requirements) == board_tuple:
            return index
    return None
