from __future__ import annotations

from dataclasses import dataclass
from math import hypot
from pathlib import Path
from random import Random
from typing import Optional

from .input import InputState
from .level import load_level, parse_player, parse_rect, parse_station
from .models import (
    BoardState,
    Component,
    Item,
    ItemKind,
    Order,
    Player,
    Rect,
    Station,
    StationKind,
)
from .native import native_rules
from .rules import (
    INSPECTION_SECONDS,
    ORDER_QUEUE_SIZE,
    ROUND_SECONDS,
    SOLDER_SECONDS,
    generate_order_requirements,
)


@dataclass
class GameStats:
    score: int = 0
    completed: int = 0
    recycled: int = 0


class GameState:
    def __init__(
        self,
        map_rect: Rect,
        stations: list[Station],
        players: list[Player],
        round_seconds: float = ROUND_SECONDS,
        seed: int = 7,
    ) -> None:
        self.map_rect = map_rect
        self.stations = stations
        self.players = players
        self.round_seconds = round_seconds
        self.remaining_seconds = round_seconds
        self.stats = GameStats()
        self.rng = Random(seed)
        self.next_order_id = 1
        self.orders: list[Order] = []
        self.game_over = False
        for _ in range(ORDER_QUEUE_SIZE):
            self.orders.append(self.create_order())

    @classmethod
    def from_level_file(cls, path: Path, seed: int = 7) -> "GameState":
        raw = load_level(path)
        return cls(
            map_rect=parse_rect(raw["map_rect"]),
            stations=[parse_station(station) for station in raw["stations"]],
            players=[parse_player(player) for player in raw["players"]],
            round_seconds=float(raw.get("round_seconds", ROUND_SECONDS)),
            seed=seed,
        )

    def create_order(self) -> Order:
        order = Order(
            id=self.next_order_id,
            requirements=generate_order_requirements(self.rng),
        )
        self.next_order_id += 1
        return order

    def create_board_item(self) -> Item:
        return Item.board_item()

    def create_material_item(self, component: Component) -> Item:
        return Item.material(component)

    def station_by_id(self, station_id: str) -> Station:
        for station in self.stations:
            if station.id == station_id:
                return station
        raise KeyError(station_id)

    def tick(self, dt: float, input_state: InputState) -> None:
        if self.game_over:
            return
        self.remaining_seconds = max(0.0, self.remaining_seconds - dt)
        if self.remaining_seconds <= 0.0:
            self.game_over = True
            return

        self._move_players(dt, input_state)
        self._handle_player_actions(input_state)
        self._advance_soldering(dt, input_state)
        self._advance_inspections(dt)

    def _move_players(self, dt: float, input_state: InputState) -> None:
        old_positions = [(player.x, player.y) for player in self.players]
        for index, player in enumerate(self.players):
            dx = 0.0
            dy = 0.0
            if input_state.is_held(player.controls.left):
                dx -= 1.0
            if input_state.is_held(player.controls.right):
                dx += 1.0
            if input_state.is_held(player.controls.up):
                dy -= 1.0
            if input_state.is_held(player.controls.down):
                dy += 1.0
            length = hypot(dx, dy)
            if length > 0.0:
                dx /= length
                dy /= length
                player.facing_x = dx
                player.facing_y = dy
            player.x += dx * player.speed * dt
            player.y += dy * player.speed * dt
            self._clamp_player_to_map(player)
            if self._player_hits_station(player) or self._player_hits_other(player, index):
                player.x, player.y = old_positions[index]

    def _clamp_player_to_map(self, player: Player) -> None:
        player.x = min(
            max(player.x, self.map_rect.x + player.radius),
            self.map_rect.x + self.map_rect.w - player.radius,
        )
        player.y = min(
            max(player.y, self.map_rect.y + player.radius),
            self.map_rect.y + self.map_rect.h - player.radius,
        )

    def _player_hits_station(self, player: Player) -> bool:
        return any(
            station.blocks_movement
            and station.rect.intersects_circle(player.x, player.y, player.radius)
            for station in self.stations
        )

    def _player_hits_other(self, player: Player, player_index: int) -> bool:
        for other_index, other in enumerate(self.players):
            if player_index == other_index:
                continue
            if hypot(player.x - other.x, player.y - other.y) < player.radius + other.radius:
                return True
        return False

    def _handle_player_actions(self, input_state: InputState) -> None:
        for player in self.players:
            if input_state.was_pressed(player.controls.action):
                station = self.nearest_station(player)
                if station is not None:
                    self.interact(player, station)

    def nearest_station(self, player: Player, max_distance: float = 54.0) -> Optional[Station]:
        best_station: Optional[Station] = None
        best_distance = max_distance
        for station in self.stations:
            distance = station.rect.distance_to_point(player.x, player.y)
            if distance < best_distance:
                best_distance = distance
                best_station = station
        return best_station

    def interact(self, player: Player, station: Station) -> None:
        if player.held_item is None:
            player.held_item = self.pick_from_station(station)
        else:
            if self.drop_to_station(station, player.held_item):
                player.held_item = None

    def pick_from_station(self, station: Station) -> Optional[Item]:
        if station.kind is StationKind.MATERIAL_BIN and station.component is not None:
            return self.create_material_item(station.component)
        if station.kind is StationKind.INTAKE:
            return self.create_board_item()
        if station.kind is StationKind.COUNTER:
            item = station.item
            station.item = None
            return item
        if station.kind is StationKind.SOLDER:
            if station.pending_material is None and station.board_item is not None:
                item = station.board_item
                station.board_item = None
                station.solder_progress_seconds = 0.0
                return item
            return None
        if station.kind is StationKind.INSPECTION:
            if (
                station.item is not None
                and station.item.board is not None
                and station.item.board.state in {BoardState.PASSED, BoardState.FAILED}
            ):
                item = station.item
                station.item = None
                station.inspection_progress_seconds = 0.0
                return item
        return None

    def drop_to_station(self, station: Station, item: Item) -> bool:
        if station.kind is StationKind.COUNTER:
            if station.item is None:
                station.item = item
                return True
            return False
        if station.kind is StationKind.SOLDER:
            return self._drop_to_solder(station, item)
        if station.kind is StationKind.INSPECTION:
            return self._drop_to_inspection(station, item)
        if station.kind is StationKind.SHIPPING:
            return self._ship_item(item)
        if station.kind is StationKind.TRASH:
            self.stats.recycled += 1
            return True
        return False

    def _drop_to_solder(self, station: Station, item: Item) -> bool:
        if item.kind is ItemKind.BOARD and item.board is not None:
            if (
                station.board_item is None
                and item.board.state
                in {BoardState.RAW, BoardState.SOLDERING, BoardState.READY_TO_TEST}
            ):
                item.board.state = BoardState.SOLDERING
                station.board_item = item
                return True
            return False
        if item.kind is ItemKind.MATERIAL:
            if station.board_item is not None and station.pending_material is None:
                station.pending_material = item
                return True
        return False

    def _drop_to_inspection(self, station: Station, item: Item) -> bool:
        if (
            station.item is None
            and item.kind is ItemKind.BOARD
            and item.board is not None
            and item.board.state
            in {BoardState.RAW, BoardState.SOLDERING, BoardState.READY_TO_TEST}
        ):
            item.board.state = BoardState.TESTING
            station.item = item
            station.inspection_progress_seconds = 0.0
            return True
        return False

    def _ship_item(self, item: Item) -> bool:
        if item.kind is not ItemKind.BOARD or item.board is None:
            return False
        board = item.board
        if board.state is not BoardState.PASSED or board.locked_order_id is None:
            return False
        order = self._order_by_id(board.locked_order_id)
        if order is None:
            return False
        self.stats.score += native_rules.score(order.requirements)
        self.stats.completed += 1
        self.orders.remove(order)
        self.orders.append(self.create_order())
        return True

    def _order_by_id(self, order_id: int) -> Optional[Order]:
        for order in self.orders:
            if order.id == order_id:
                return order
        return None

    def _advance_soldering(self, dt: float, input_state: InputState) -> None:
        active_station_ids: set[str] = set()
        for player in self.players:
            if not input_state.is_held(player.controls.solder):
                continue
            station = self.nearest_station(player)
            if station is not None and station.kind is StationKind.SOLDER:
                active_station_ids.add(station.id)

        for station in self.stations:
            if station.kind is not StationKind.SOLDER:
                continue
            if (
                station.id in active_station_ids
                and station.board_item is not None
                and station.board_item.board is not None
                and station.pending_material is not None
                and station.pending_material.component is not None
            ):
                station.solder_progress_seconds += dt
                if station.solder_progress_seconds >= SOLDER_SECONDS:
                    station.board_item.board.add_component(station.pending_material.component)
                    station.pending_material = None
                    station.solder_progress_seconds = 0.0

    def _advance_inspections(self, dt: float) -> None:
        for station in self.stations:
            if (
                station.kind is StationKind.INSPECTION
                and station.item is not None
                and station.item.board is not None
                and station.item.board.state is BoardState.TESTING
            ):
                station.inspection_progress_seconds += dt
                if station.inspection_progress_seconds >= INSPECTION_SECONDS:
                    self._finish_inspection(station)

    def _finish_inspection(self, station: Station) -> None:
        if station.item is None or station.item.board is None:
            return
        board = station.item.board
        match_index = native_rules.match_order(board.counts, self.orders)
        if match_index is None:
            board.state = BoardState.FAILED
            board.locked_order_id = None
            return
        order = self.orders[match_index]
        board.state = BoardState.PASSED
        board.locked_order_id = order.id
        order.locked_board_id = board.id
