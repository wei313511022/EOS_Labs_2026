from __future__ import annotations

from ctypes import CDLL, POINTER, c_double, c_int, byref
from pathlib import Path
from typing import Iterable, Optional

from .models import Order
from .rules import find_matching_order_index, score_counts


class NativeRules:
    def __init__(self) -> None:
        self._lib = self._load_library()
        if self._lib is not None:
            self._lib.overbug_match_order.argtypes = [
                POINTER(c_int),
                POINTER(c_int),
                c_int,
            ]
            self._lib.overbug_match_order.restype = c_int
            self._lib.overbug_score_order.argtypes = [POINTER(c_int)]
            self._lib.overbug_score_order.restype = c_int
            if hasattr(self._lib, "overbug_resolve_collision"):
                self._lib.overbug_resolve_collision.argtypes = [
                    c_double,
                    c_double,
                    c_double,
                    c_double,
                    c_double,
                    c_double,
                    POINTER(c_double),
                    POINTER(c_double),
                ]
                self._lib.overbug_resolve_collision.restype = c_int

    @property
    def available(self) -> bool:
        return self._lib is not None

    def match_order(self, board_counts: Iterable[int], orders: list[Order]) -> Optional[int]:
        if self._lib is None:
            return find_matching_order_index(board_counts, orders)

        unlocked = [order for order in orders if order.locked_board_id is None]
        board_array = (c_int * 3)(*list(board_counts))
        flat_values: list[int] = []
        for order in unlocked:
            flat_values.extend(order.requirements)
        order_array = (c_int * max(1, len(flat_values)))(*flat_values) if flat_values else (c_int * 1)(0)
        result = self._lib.overbug_match_order(board_array, order_array, len(unlocked))
        if result < 0:
            return None

        native_match = unlocked[result]
        for index, order in enumerate(orders):
            if order.id == native_match.id:
                return index
        return None

    def score(self, counts: Iterable[int]) -> int:
        if self._lib is None:
            return score_counts(counts)
        count_array = (c_int * 3)(*list(counts))
        return int(self._lib.overbug_score_order(count_array))

    def resolve_collision(
        self,
        ax: float,
        ay: float,
        ar: float,
        bx: float,
        by: float,
        br: float,
    ) -> tuple[bool, float, float]:
        if self._lib is None or not hasattr(self._lib, "overbug_resolve_collision"):
            return _python_resolve_collision(ax, ay, ar, bx, by, br)

        out_x = c_double(0.0)
        out_y = c_double(0.0)
        hit = self._lib.overbug_resolve_collision(
            ax, ay, ar, bx, by, br, byref(out_x), byref(out_y)
        )
        return bool(hit), float(out_x.value), float(out_y.value)

    def _load_library(self) -> Optional[CDLL]:
        native_dir = Path(__file__).resolve().parents[1].joinpath("native")
        candidates = [
            native_dir / "build" / "overbug_native.dll",
            native_dir / "build" / "liboverbug_native.so",
            native_dir / "build" / "liboverbug_native.dylib",
        ]
        for candidate in candidates:
            if candidate.exists():
                try:
                    return CDLL(str(candidate))
                except OSError:
                    continue
        return None


def _python_resolve_collision(
    ax: float, ay: float, ar: float, bx: float, by: float, br: float
) -> tuple[bool, float, float]:
    dx = ax - bx
    dy = ay - by
    min_dist = ar + br
    dist_sq = dx * dx + dy * dy
    if dist_sq >= min_dist * min_dist:
        return False, 0.0, 0.0
    if dist_sq == 0.0:
        return True, min_dist, 0.0
    dist = dist_sq**0.5
    push = min_dist - dist
    return True, dx / dist * push, dy / dist * push


native_rules = NativeRules()
