from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from itertools import count
from math import hypot
from typing import Optional


class Component(Enum):
    RESISTOR = 0
    CAPACITOR = 1
    INDUCTOR = 2

    @property
    def short(self) -> str:
        return ("R", "C", "L")[self.value]

    @property
    def label(self) -> str:
        return ("Resistor", "Capacitor", "Inductor")[self.value]


COMPONENTS: tuple[Component, Component, Component] = (
    Component.RESISTOR,
    Component.CAPACITOR,
    Component.INDUCTOR,
)


class ItemKind(Enum):
    MATERIAL = "material"
    BOARD = "board"


class BoardState(Enum):
    RAW = "raw"
    SOLDERING = "soldering"
    READY_TO_TEST = "ready_to_test"
    TESTING = "testing"
    PASSED = "passed"
    FAILED = "failed"


class StationKind(Enum):
    MATERIAL_BIN = "material_bin"
    INTAKE = "intake"
    SOLDER = "solder"
    INSPECTION = "inspection"
    SHIPPING = "shipping"
    TRASH = "trash"
    COUNTER = "counter"


_board_ids = count(1)


@dataclass
class Rect:
    x: float
    y: float
    w: float
    h: float

    @property
    def center(self) -> tuple[float, float]:
        return (self.x + self.w / 2.0, self.y + self.h / 2.0)

    @property
    def right(self) -> float:
        return self.x + self.w

    @property
    def bottom(self) -> float:
        return self.y + self.h

    def contains_point(self, x: float, y: float) -> bool:
        return self.x <= x <= self.x + self.w and self.y <= y <= self.y + self.h

    def distance_to_point(self, x: float, y: float) -> float:
        closest_x = min(max(x, self.x), self.x + self.w)
        closest_y = min(max(y, self.y), self.y + self.h)
        return hypot(x - closest_x, y - closest_y)

    def intersects_circle(self, x: float, y: float, radius: float) -> bool:
        return self.distance_to_point(x, y) < radius


@dataclass
class Board:
    counts: list[int] = field(default_factory=lambda: [0, 0, 0])
    state: BoardState = BoardState.RAW
    id: int = field(default_factory=lambda: next(_board_ids))
    locked_order_id: Optional[int] = None

    @property
    def total_components(self) -> int:
        return sum(self.counts)

    def add_component(self, component: Component) -> None:
        self.counts[component.value] += 1
        self.state = BoardState.READY_TO_TEST


@dataclass
class Item:
    kind: ItemKind
    component: Optional[Component] = None
    board: Optional[Board] = None

    @classmethod
    def material(cls, component: Component) -> "Item":
        return cls(kind=ItemKind.MATERIAL, component=component)

    @classmethod
    def board_item(cls, board: Optional[Board] = None) -> "Item":
        return cls(kind=ItemKind.BOARD, board=board or Board())

    @property
    def name(self) -> str:
        if self.kind is ItemKind.MATERIAL and self.component is not None:
            return self.component.label
        if self.kind is ItemKind.BOARD and self.board is not None:
            return f"Board #{self.board.id}"
        return "Item"


@dataclass
class Order:
    id: int
    requirements: list[int]
    locked_board_id: Optional[int] = None

    @property
    def total_components(self) -> int:
        return sum(self.requirements)


@dataclass
class Controls:
    up: str
    down: str
    left: str
    right: str
    action: str
    solder: str


@dataclass
class Player:
    id: int
    x: float
    y: float
    color: tuple[int, int, int]
    controls: Controls
    radius: float = 18.0
    speed: float = 185.0
    facing_x: float = 0.0
    facing_y: float = 1.0
    held_item: Optional[Item] = None


@dataclass
class Station:
    id: str
    kind: StationKind
    rect: Rect
    label: str
    component: Optional[Component] = None
    item: Optional[Item] = None
    board_item: Optional[Item] = None
    pending_material: Optional[Item] = None
    solder_progress_seconds: float = 0.0
    inspection_progress_seconds: float = 0.0

    @property
    def blocks_movement(self) -> bool:
        return self.kind in {
            StationKind.MATERIAL_BIN,
            StationKind.INTAKE,
            StationKind.SOLDER,
            StationKind.INSPECTION,
            StationKind.SHIPPING,
            StationKind.TRASH,
            StationKind.COUNTER,
        }
