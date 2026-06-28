from __future__ import annotations

import unittest
import sys
from pathlib import Path
from random import Random

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from overbug.game_state import GameState
from overbug.input import InputState
from overbug.models import BoardState, Component, Item
from overbug.native import native_rules
from overbug.rules import (
    INSPECTION_SECONDS,
    SOLDER_SECONDS,
    counts_match,
    find_matching_order_index,
    generate_order_requirements,
    score_counts,
)


class RuleTests(unittest.TestCase):
    def test_order_generation_limits(self) -> None:
        rng = Random(123)
        for _ in range(1000):
            counts = generate_order_requirements(rng)
            self.assertTrue(all(0 <= value <= 2 for value in counts))
            self.assertGreaterEqual(sum(counts), 1)
            self.assertLessEqual(sum(counts), 4)

    def test_exact_match_only(self) -> None:
        state = make_state()
        state.orders[0].requirements = [1, 1, 0]
        state.orders[1].requirements = [0, 2, 1]
        state.orders[2].requirements = [2, 0, 0]
        self.assertTrue(counts_match([1, 1, 0], [1, 1, 0]))
        self.assertFalse(counts_match([1, 1, 0], [1, 0, 0]))
        self.assertFalse(counts_match([1, 1, 0], [2, 1, 0]))
        self.assertEqual(find_matching_order_index([0, 2, 1], state.orders), 1)
        self.assertIsNone(find_matching_order_index([0, 2, 2], state.orders))

    def test_score_formula(self) -> None:
        self.assertEqual(score_counts([1, 0, 0]), 15)
        self.assertEqual(score_counts([2, 1, 1]), 30)

    def test_solder_progress_pauses_without_input(self) -> None:
        state = make_state()
        station = state.station_by_id("solder_a")
        station.board_item = state.create_board_item()
        station.pending_material = state.create_material_item(Component.RESISTOR)
        player = state.players[0]
        player.x = station.rect.center[0]
        player.y = station.rect.bottom + player.radius + 1

        state.tick(0.75, InputState(held={player.controls.solder}))
        self.assertAlmostEqual(station.solder_progress_seconds, 0.75)
        state.tick(0.75, InputState())
        self.assertAlmostEqual(station.solder_progress_seconds, 0.75)
        state.tick(SOLDER_SECONDS, InputState(held={player.controls.solder}))
        self.assertIsNone(station.pending_material)
        self.assertEqual(station.board_item.board.counts[Component.RESISTOR.value], 1)

    def test_inspection_success_locks_order_and_shipping_replaces_order(self) -> None:
        state = make_state()
        state.orders[0].requirements = [1, 0, 0]
        before_ids = [order.id for order in state.orders]
        inspect = state.station_by_id("inspect_a")
        board_item = Item.board_item()
        board_item.board.counts = [1, 0, 0]
        board_item.board.state = BoardState.TESTING
        inspect.item = board_item

        state.tick(INSPECTION_SECONDS + 0.01, InputState())
        self.assertEqual(board_item.board.state, BoardState.PASSED)
        self.assertEqual(board_item.board.locked_order_id, before_ids[0])
        self.assertEqual(state.orders[0].locked_board_id, board_item.board.id)

        shipped = state.drop_to_station(state.station_by_id("shipping"), board_item)
        self.assertTrue(shipped)
        self.assertEqual(state.stats.score, 15)
        self.assertEqual(state.stats.completed, 1)
        self.assertEqual(len(state.orders), 3)
        self.assertNotIn(before_ids[0], [order.id for order in state.orders])

    def test_inspection_failure_and_recycle_no_penalty(self) -> None:
        state = make_state()
        for order in state.orders:
            order.requirements = [1, 0, 0]
        inspect = state.station_by_id("inspect_a")
        board_item = Item.board_item()
        board_item.board.counts = [2, 0, 0]
        board_item.board.state = BoardState.TESTING
        inspect.item = board_item

        state.tick(INSPECTION_SECONDS + 0.01, InputState())
        self.assertEqual(board_item.board.state, BoardState.FAILED)
        self.assertTrue(state.drop_to_station(state.station_by_id("trash"), board_item))
        self.assertEqual(state.stats.score, 0)
        self.assertEqual(state.stats.recycled, 1)

    def test_counter_slot_holds_one_item(self) -> None:
        state = make_state()
        counter = state.station_by_id("counter_bottom_a")
        resistor = state.create_material_item(Component.RESISTOR)
        capacitor = state.create_material_item(Component.CAPACITOR)
        self.assertTrue(state.drop_to_station(counter, resistor))
        self.assertFalse(state.drop_to_station(counter, capacitor))
        self.assertIs(state.pick_from_station(counter), resistor)
        self.assertIsNone(counter.item)

    def test_native_parity_when_available(self) -> None:
        if not native_rules.available:
            self.skipTest("optional native library is not built")
        state = make_state()
        rng = Random(999)
        for _ in range(200):
            for order in state.orders:
                order.requirements = generate_order_requirements(rng)
                order.locked_board_id = None
            board_counts = generate_order_requirements(rng)
            native_index = native_rules.match_order(board_counts, state.orders)
            python_index = find_matching_order_index(board_counts, state.orders)
            self.assertEqual(native_index, python_index)
            self.assertEqual(native_rules.score(board_counts), score_counts(board_counts))


def make_state() -> GameState:
    path = Path(__file__).resolve().parents[1] / "data" / "lab_mvp.json"
    return GameState.from_level_file(path, seed=1)


if __name__ == "__main__":
    unittest.main()
