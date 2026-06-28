from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class InputState:
    held: set[str] = field(default_factory=set)
    pressed: set[str] = field(default_factory=set)

    def is_held(self, key: str) -> bool:
        return key in self.held

    def was_pressed(self, key: str) -> bool:
        return key in self.pressed
