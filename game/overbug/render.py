from __future__ import annotations

from pathlib import Path

from .models import BoardState, Component, Item, ItemKind, Player, Rect, Station, StationKind
from .rules import INSPECTION_SECONDS, SOLDER_SECONDS


PALETTE = {
    "bg": (238, 243, 249),
    "floor": (225, 235, 244),
    "tile": (210, 224, 237),
    "counter": (248, 250, 252),
    "counter_side": (201, 214, 227),
    "line": (131, 150, 169),
    "ink": (34, 49, 64),
    "muted": (96, 112, 128),
    "accent": (38, 166, 154),
    "gold": (246, 193, 84),
    "red": (224, 86, 86),
    "green": (75, 176, 112),
    "blue": (72, 134, 220),
    "purple": (139, 103, 206),
}


class Renderer:
    def __init__(self, pygame, screen) -> None:
        self.pygame = pygame
        self.screen = screen
        self.font = pygame.font.SysFont("Segoe UI", 20)
        self.small = pygame.font.SysFont("Segoe UI", 15)
        self.big = pygame.font.SysFont("Segoe UI", 42, bold=True)
        self.mid = pygame.font.SysFont("Segoe UI", 26, bold=True)
        self.solder_icon_image = self._load_solder_icon_image()

    def draw(self, state) -> None:
        self.screen.fill(PALETTE["bg"])
        self._draw_map(state)
        for station in state.stations:
            self._draw_station(station)
        for player in state.players:
            self._draw_player(player)
        self._draw_orders(state)
        self._draw_hud(state)
        if state.game_over:
            self._draw_game_over(state)

    def _draw_map(self, state) -> None:
        pygame = self.pygame
        rect = _pg_rect(pygame, state.map_rect)
        pygame.draw.rect(self.screen, PALETTE["floor"], rect, border_radius=18)
        pygame.draw.rect(self.screen, (182, 198, 214), rect, 4, border_radius=18)

        tile = 40
        x0 = int(state.map_rect.x)
        y0 = int(state.map_rect.y)
        x1 = int(state.map_rect.x + state.map_rect.w)
        y1 = int(state.map_rect.y + state.map_rect.h)
        for x in range(x0, x1, tile):
            pygame.draw.line(self.screen, PALETTE["tile"], (x, y0), (x, y1), 1)
        for y in range(y0, y1, tile):
            pygame.draw.line(self.screen, PALETTE["tile"], (x0, y), (x1, y), 1)

    def _draw_station(self, station: Station) -> None:
        pygame = self.pygame
        rect = _pg_rect(pygame, station.rect)
        shadow = rect.move(0, 5)
        pygame.draw.rect(self.screen, (181, 195, 208), shadow, border_radius=10)
        color = PALETTE["counter"]

        if station.kind is StationKind.MATERIAL_BIN:
            color = (233, 247, 243)
        elif station.kind is StationKind.SOLDER:
            color = (255, 247, 224)
        elif station.kind is StationKind.INSPECTION:
            color = (232, 242, 255)
        elif station.kind is StationKind.SHIPPING:
            color = (230, 248, 234)
        elif station.kind is StationKind.TRASH:
            color = (252, 233, 233)
        elif station.kind is StationKind.INTAKE:
            color = (238, 236, 252)

        pygame.draw.rect(self.screen, color, rect, border_radius=10)
        pygame.draw.rect(self.screen, PALETTE["line"], rect, 2, border_radius=10)

        if station.kind is StationKind.MATERIAL_BIN and station.component is not None:
            self._draw_component_icon(station.component, rect.centerx, rect.centery + 4)
            self._badge(station.component.short, rect.centerx, rect.bottom + 12, small=True)
        elif station.kind is StationKind.SOLDER:
            self._draw_solder_icon(rect.centerx, rect.centery - 8)
            self._progress_bar(
                rect.x + 10,
                rect.bottom - 18,
                rect.w - 20,
                station.solder_progress_seconds / SOLDER_SECONDS,
            )
            if station.board_item:
                self._draw_item(station.board_item, rect.x + 16, rect.y + 4, 0.7)
            if station.pending_material:
                self._draw_item(station.pending_material, rect.right - 42, rect.y + 16, 0.62)
            self._badge("2s", rect.centerx, rect.bottom + 12, small=True)
        elif station.kind is StationKind.INSPECTION:
            self._draw_computer_icon(rect.centerx, rect.centery - 8)
            self._progress_bar(
                rect.x + 10,
                rect.bottom - 18,
                rect.w - 20,
                station.inspection_progress_seconds / INSPECTION_SECONDS,
            )
            if station.item:
                self._draw_item(station.item, rect.x + 12, rect.y + 12, 0.68)
            self._badge("4s", rect.centerx, rect.bottom + 12, small=True)
        elif station.kind is StationKind.INTAKE:
            self._draw_board_icon(rect.centerx, rect.centery - 4, sparks=True, scale=0.85)
            self._badge("IN", rect.centerx, rect.bottom + 12, small=True)
        elif station.kind is StationKind.SHIPPING:
            self._draw_shipping_icon(rect.centerx, rect.centery - 6)
            self._badge("OUT", rect.centerx, rect.bottom + 12, small=True)
        elif station.kind is StationKind.TRASH:
            self._draw_trash_icon(rect.centerx, rect.centery - 4)
            self._badge("RECYCLE", rect.centerx, rect.bottom + 12, small=True)
        elif station.kind is StationKind.COUNTER:
            if station.item:
                self._draw_item(station.item, rect.centerx - 18, rect.centery - 18, 0.75)

    def _draw_orders(self, state) -> None:
        pygame = self.pygame
        title = self.big.render("OVERBUG", True, PALETTE["ink"])
        self.screen.blit(title, (28, 22))
        start_x = 292
        for index, order in enumerate(state.orders):
            x = start_x + index * 206
            rect = pygame.Rect(x, 18, 184, 64)
            fill = (255, 255, 255) if order.locked_board_id is None else (232, 248, 234)
            pygame.draw.rect(self.screen, fill, rect, border_radius=10)
            pygame.draw.rect(self.screen, PALETTE["line"], rect, 2, border_radius=10)
            self._label(f"Order {order.id}", x + 48, 34, small=True)
            cx = x + 28
            for component in Component:
                count = order.requirements[component.value]
                self._draw_component_icon(component, cx, 58, scale=0.58)
                text = self.small.render(f"x{count}", True, PALETTE["ink"])
                self.screen.blit(text, (cx + 16, 48))
                cx += 52

    def _draw_hud(self, state) -> None:
        pygame = self.pygame
        score_rect = pygame.Rect(24, 660, 250, 46)
        timer_rect = pygame.Rect(1060, 660, 190, 46)
        for rect in (score_rect, timer_rect):
            pygame.draw.rect(self.screen, (255, 255, 255), rect, border_radius=12)
            pygame.draw.rect(self.screen, PALETTE["line"], rect, 2, border_radius=12)
        score = self.mid.render(f"Score {state.stats.score}", True, PALETTE["ink"])
        self.screen.blit(score, (score_rect.x + 18, score_rect.y + 8))
        minutes = int(state.remaining_seconds) // 60
        seconds = int(state.remaining_seconds) % 60
        timer = self.mid.render(f"{minutes:02d}:{seconds:02d}", True, PALETTE["ink"])
        self.screen.blit(timer, (timer_rect.x + 45, timer_rect.y + 8))

    def _draw_player(self, player: Player) -> None:
        pygame = self.pygame
        x = int(player.x)
        y = int(player.y)
        pygame.draw.circle(self.screen, (120, 132, 145), (x, y + 5), int(player.radius))
        pygame.draw.circle(self.screen, player.color, (x, y), int(player.radius))
        pygame.draw.circle(self.screen, (255, 238, 207), (x, y - 8), 9)
        pygame.draw.circle(self.screen, PALETTE["ink"], (x - 3, y - 10), 2)
        pygame.draw.circle(self.screen, PALETTE["ink"], (x + 4, y - 10), 2)
        label = self.small.render(f"P{player.id}", True, (255, 255, 255))
        self.screen.blit(label, (x - 9, y + 1))
        if player.held_item:
            self._draw_item(player.held_item, x - 14, y - 46, 0.75)

    def _draw_item(self, item: Item, x: float, y: float, scale: float = 1.0) -> None:
        if item.kind is ItemKind.MATERIAL and item.component is not None:
            self._draw_component_icon(item.component, int(x + 22 * scale), int(y + 22 * scale), scale)
        elif item.kind is ItemKind.BOARD and item.board is not None:
            sparks = item.board.state is BoardState.FAILED
            board_x = int(x + 24 * scale)
            board_y = int(y + 20 * scale)
            self._draw_board_icon(board_x, board_y, sparks=sparks, scale=scale)
            self._draw_board_counts(item.board, board_x, board_y + int(24 * scale), scale)

    def _draw_component_icon(self, component: Component, x: float, y: float, scale: float = 1.0) -> None:
        pygame = self.pygame
        color = {
            Component.RESISTOR: PALETTE["red"],
            Component.CAPACITOR: PALETTE["blue"],
            Component.INDUCTOR: PALETTE["purple"],
        }[component]
        x = int(x)
        y = int(y)
        s = scale

        if component is Component.RESISTOR:
            points = [
                (x - int(18 * s), y),
                (x - int(10 * s), y - int(8 * s)),
                (x - int(2 * s), y + int(8 * s)),
                (x + int(6 * s), y - int(8 * s)),
                (x + int(14 * s), y + int(8 * s)),
                (x + int(20 * s), y),
            ]
            pygame.draw.lines(self.screen, color, False, points, max(2, int(4 * s)))
        elif component is Component.CAPACITOR:
            pygame.draw.line(self.screen, color, (x - int(17 * s), y), (x - int(5 * s), y), max(2, int(3 * s)))
            pygame.draw.line(self.screen, color, (x + int(5 * s), y), (x + int(17 * s), y), max(2, int(3 * s)))
            pygame.draw.line(
                self.screen,
                color,
                (x - int(5 * s), y - int(14 * s)),
                (x - int(5 * s), y + int(14 * s)),
                max(2, int(4 * s)),
            )
            pygame.draw.line(
                self.screen,
                color,
                (x + int(5 * s), y - int(14 * s)),
                (x + int(5 * s), y + int(14 * s)),
                max(2, int(4 * s)),
            )
        else:
            pygame.draw.line(self.screen, color, (x - int(22 * s), y), (x - int(13 * s), y), max(2, int(3 * s)))
            for offset in (-9, 0, 9):
                rect = pygame.Rect(
                    x + int((offset - 5) * s),
                    y - int(10 * s),
                    int(11 * s),
                    int(20 * s),
                )
                pygame.draw.arc(self.screen, color, rect, 3.14, 0, max(2, int(3 * s)))
            pygame.draw.line(self.screen, color, (x + int(18 * s), y), (x + int(24 * s), y), max(2, int(3 * s)))

    def _draw_board_icon(self, x: int, y: int, sparks: bool, scale: float = 1.0) -> None:
        pygame = self.pygame

        w = int(48 * scale)
        h = int(34 * scale)
        rect = pygame.Rect(x - w // 2, y - h // 2, w, h)
        pygame.draw.rect(self.screen, (77, 176, 105), rect, border_radius=max(4, int(6 * scale)))
        pygame.draw.rect(self.screen, (39, 112, 70), rect, 2, border_radius=max(4, int(6 * scale)))
        chip = pygame.Rect(
            rect.x + int(16 * scale),
            rect.y + int(9 * scale),
            int(17 * scale),
            int(14 * scale),
        )
        pygame.draw.rect(self.screen, (42, 56, 66), chip, border_radius=2)
        for px in (rect.x + 8, rect.right - 8):
            for py in (rect.y + 7, rect.bottom - 7):
                pygame.draw.circle(self.screen, (232, 238, 219), (px, py), max(2, int(3 * scale)))
        if sparks:
            inset = max(6, int(9 * scale))
            width = max(3, int(4 * scale))
            pygame.draw.line(
                self.screen,
                PALETTE["red"],
                (rect.x + inset, rect.y + inset),
                (rect.right - inset, rect.bottom - inset),
                width,
            )
            pygame.draw.line(
                self.screen,
                PALETTE["red"],
                (rect.right - inset, rect.y + inset),
                (rect.x + inset, rect.bottom - inset),
                width,
            )

    def _draw_solder_icon(self, x: float, y: float) -> None:
        pygame = self.pygame
        x = int(x)
        y = int(y)
        if self.solder_icon_image is not None:
            image_rect = self.solder_icon_image.get_rect(center=(x, y - 2))
            self.screen.blit(self.solder_icon_image, image_rect)
            return

        black = (12, 16, 20)
        light = (250, 252, 254)

        cord_points = [
            (x - 29, y + 10),
            (x - 37, y + 18),
            (x - 30, y + 26),
            (x - 15, y + 23),
            (x - 2, y + 19),
            (x + 13, y + 24),
            (x + 31, y + 9),
        ]
        pygame.draw.lines(self.screen, black, False, cord_points, 8)
        pygame.draw.lines(self.screen, light, False, cord_points[:-1], 4)

        handle_points = [
            (x - 29, y + 10),
            (x - 40, y + 4),
            (x - 35, y - 9),
            (x - 28, y - 13),
            (x - 26, y - 20),
            (x - 15, y - 10),
            (x - 21, y - 1),
            (x - 13, y + 6),
        ]
        pygame.draw.lines(self.screen, black, False, handle_points, 8)
        pygame.draw.lines(self.screen, light, False, handle_points[1:-1], 4)

        collar = [
            (x - 16, y - 20),
            (x + 3, y - 2),
            (x - 2, y + 4),
            (x - 22, y - 14),
        ]
        pygame.draw.polygon(self.screen, black, collar)
        inner_collar = [
            (x - 15, y - 16),
            (x - 4, y - 5),
            (x - 5, y - 2),
            (x - 17, y - 13),
        ]
        pygame.draw.polygon(self.screen, light, inner_collar)

        body_start = (x - 9, y - 20)
        body_end = (x + 22, y - 49)
        pygame.draw.line(self.screen, black, body_start, body_end, 12)
        pygame.draw.line(self.screen, light, (x - 6, y - 17), (x + 18, y - 41), 6)
        pygame.draw.line(self.screen, black, (x + 20, y - 47), (x + 29, y - 56), 7)

    def _draw_computer_icon(self, x: float, y: float) -> None:
        pygame = self.pygame
        x = int(x)
        y = int(y)
        screen = pygame.Rect(x - 27, y - 20, 54, 34)
        pygame.draw.rect(self.screen, (50, 72, 96), screen, border_radius=5)
        pygame.draw.rect(self.screen, (149, 217, 232), screen.inflate(-8, -8), border_radius=4)
        pygame.draw.line(self.screen, PALETTE["green"], (x - 10, y - 3), (x - 2, y + 6), 3)
        pygame.draw.line(self.screen, PALETTE["green"], (x - 2, y + 6), (x + 14, y - 9), 3)
        pygame.draw.rect(self.screen, (50, 72, 96), (x - 18, y + 18, 36, 6), border_radius=3)

    def _draw_shipping_icon(self, x: float, y: float) -> None:
        pygame = self.pygame
        x = int(x)
        y = int(y)
        pygame.draw.rect(self.screen, PALETTE["green"], (x - 28, y - 16, 38, 28), border_radius=6)
        pygame.draw.polygon(self.screen, PALETTE["green"], [(x + 6, y - 24), (x + 34, y), (x + 6, y + 24)])

    def _draw_trash_icon(self, x: float, y: float) -> None:
        pygame = self.pygame
        x = int(x)
        y = int(y)
        pygame.draw.rect(self.screen, PALETTE["red"], (x - 18, y - 14, 36, 34), border_radius=5)
        pygame.draw.rect(self.screen, (165, 62, 62), (x - 24, y - 22, 48, 8), border_radius=4)
        pygame.draw.line(self.screen, (255, 245, 245), (x - 8, y - 5), (x + 8, y + 11), 3)
        pygame.draw.line(self.screen, (255, 245, 245), (x + 8, y - 5), (x - 8, y + 11), 3)

    def _progress_bar(self, x: float, y: float, w: float, progress: float) -> None:
        pygame = self.pygame
        progress = max(0.0, min(1.0, progress))
        bg = pygame.Rect(int(x), int(y), int(w), 8)
        pygame.draw.rect(self.screen, (221, 228, 236), bg, border_radius=4)
        if progress > 0.0:
            fg = pygame.Rect(int(x), int(y), int(w * progress), 8)
            pygame.draw.rect(self.screen, PALETTE["accent"], fg, border_radius=4)

    def _label(self, text: str, x: float, y: float, small: bool = False) -> None:
        font = self.small if small else self.font
        rendered = font.render(text, True, PALETTE["ink"])
        self.screen.blit(rendered, (int(x - rendered.get_width() / 2), int(y - rendered.get_height() / 2)))

    def _badge(self, text: str, x: float, y: float, small: bool = False) -> None:
        pygame = self.pygame
        font = self.small if small else self.font
        rendered = font.render(text, True, PALETTE["ink"])
        rect = rendered.get_rect(center=(int(x), int(y)))
        rect.inflate_ip(12, 6)
        pygame.draw.rect(self.screen, (255, 255, 255), rect, border_radius=6)
        pygame.draw.rect(self.screen, PALETTE["line"], rect, 1, border_radius=6)
        self.screen.blit(rendered, rendered.get_rect(center=rect.center))

    def _draw_board_counts(self, board, x: int, y: int, scale: float) -> None:
        if board.total_components <= 0:
            return
        parts = [
            f"{component.short}{board.counts[component.value]}"
            for component in Component
            if board.counts[component.value] > 0
        ]
        self._badge(" ".join(parts), x, y, small=True)

    def _load_solder_icon_image(self):
        path = Path(__file__).resolve().parents[1] / "assets" / "soldering_iron_reference.png"
        if not path.exists():
            return None
        try:
            image = self.pygame.image.load(str(path)).convert_alpha()
        except self.pygame.error:
            return None
        return self.pygame.transform.smoothscale(image, (48, 48))

    def _draw_game_over(self, state) -> None:
        pygame = self.pygame
        overlay = pygame.Surface(self.screen.get_size(), pygame.SRCALPHA)
        overlay.fill((24, 34, 44, 150))
        self.screen.blit(overlay, (0, 0))
        panel = pygame.Rect(390, 210, 500, 270)
        pygame.draw.rect(self.screen, (255, 255, 255), panel, border_radius=18)
        pygame.draw.rect(self.screen, PALETTE["line"], panel, 3, border_radius=18)
        title = self.big.render("Time!", True, PALETTE["ink"])
        self.screen.blit(title, (panel.centerx - title.get_width() / 2, panel.y + 34))
        lines = [
            f"Final score: {state.stats.score}",
            f"Boards shipped: {state.stats.completed}",
            f"Recycled items: {state.stats.recycled}",
            "Press Esc to quit",
        ]
        for index, line in enumerate(lines):
            text = self.mid.render(line, True, PALETTE["ink"] if index < 3 else PALETTE["muted"])
            self.screen.blit(text, (panel.centerx - text.get_width() / 2, panel.y + 104 + index * 38))


def _pg_rect(pygame, rect: Rect):
    return pygame.Rect(int(rect.x), int(rect.y), int(rect.w), int(rect.h))
