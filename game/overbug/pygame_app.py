from __future__ import annotations

from pathlib import Path

from .game_state import GameState
from .input import InputState
from .render import Renderer


def _key_name(pygame, key: int) -> str:
    return pygame.key.name(key).lower()


def run(level_path: Path) -> int:
    import pygame

    state = GameState.from_level_file(level_path)
    pygame.init()
    pygame.display.set_caption("Overbug")
    screen = pygame.display.set_mode((1280, 720))
    clock = pygame.time.Clock()
    renderer = Renderer(pygame, screen)
    held_keys: set[str] = set()

    running = True
    while running:
        pressed: set[str] = set()
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                key = _key_name(pygame, event.key)
                held_keys.add(key)
                pressed.add(key)
                if key == "escape":
                    running = False
            elif event.type == pygame.KEYUP:
                held_keys.discard(_key_name(pygame, event.key))

        dt = clock.tick(60) / 1000.0
        state.tick(dt, InputState(held=held_keys.copy(), pressed=pressed))
        renderer.draw(state)
        pygame.display.flip()

    pygame.quit()
    return 0
