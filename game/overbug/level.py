from __future__ import annotations

import json
from pathlib import Path

from .models import Component, Controls, Player, Rect, Station, StationKind


def load_level(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


def parse_rect(values: list[float]) -> Rect:
    return Rect(float(values[0]), float(values[1]), float(values[2]), float(values[3]))


def parse_component(value: str | None) -> Component | None:
    if value is None:
        return None
    normalized = value.upper()
    return {
        "RESISTOR": Component.RESISTOR,
        "CAPACITOR": Component.CAPACITOR,
        "INDUCTOR": Component.INDUCTOR,
    }[normalized]


def parse_station(raw: dict) -> Station:
    return Station(
        id=raw["id"],
        kind=StationKind(raw["kind"]),
        rect=parse_rect(raw["rect"]),
        label=raw.get("label", raw["id"]),
        component=parse_component(raw.get("component")),
    )


def parse_player(raw: dict) -> Player:
    controls = raw["controls"]
    return Player(
        id=int(raw["id"]),
        x=float(raw["spawn"][0]),
        y=float(raw["spawn"][1]),
        color=tuple(raw["color"]),
        controls=Controls(
            up=controls["up"],
            down=controls["down"],
            left=controls["left"],
            right=controls["right"],
            action=controls["action"],
            solder=controls["solder"],
        ),
    )
