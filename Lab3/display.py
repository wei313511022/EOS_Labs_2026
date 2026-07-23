from __future__ import annotations

import argparse
import json
import socket
from pathlib import Path
from typing import Any


WIDTH = 1280
HEIGHT = 720
MAP_RECT = (70, 96, 1140, 540)

# Fixed facility geometry. UDP snapshots carry only changing station state.
FACILITIES: tuple[dict[str, Any], ...] = (
    {"id": "bin_resistor", "type": "material_bin", "x": 92, "y": 160, "w": 92, "h": 62, "component": "resistor"},
    {"id": "bin_capacitor", "type": "material_bin", "x": 92, "y": 246, "w": 92, "h": 62, "component": "capacitor"},
    {"id": "bin_inductor", "type": "material_bin", "x": 92, "y": 332, "w": 92, "h": 62, "component": "inductor"},
    {"id": "trash", "type": "trash", "x": 92, "y": 542, "w": 112, "h": 68},
    {"id": "intake", "type": "intake", "x": 1058, "y": 542, "w": 112, "h": 68},
    {"id": "shipping", "type": "shipping", "x": 1092, "y": 286, "w": 92, "h": 84},
    {"id": "welder_1", "type": "welder", "x": 304, "y": 542, "w": 132, "h": 68},
    {"id": "welder_2", "type": "welder", "x": 474, "y": 542, "w": 132, "h": 68},
    {"id": "welder_3", "type": "welder", "x": 574, "y": 118, "w": 132, "h": 68},
    {"id": "verifier_1", "type": "verification_computer", "x": 786, "y": 118, "w": 136, "h": 68},
    {"id": "verifier_2", "type": "verification_computer", "x": 954, "y": 118, "w": 136, "h": 68},
    {"id": "table_1", "type": "table", "x": 230, "y": 118, "w": 100, "h": 60},
    {"id": "table_2", "type": "table", "x": 354, "y": 118, "w": 100, "h": 60},
    {"id": "table_3", "type": "table", "x": 646, "y": 542, "w": 100, "h": 68},
    {"id": "table_4", "type": "table", "x": 776, "y": 542, "w": 100, "h": 68},
    {"id": "table_5", "type": "table", "x": 906, "y": 542, "w": 100, "h": 68},
)

COLORS = {
    "bg": (238, 243, 249),
    "floor": (225, 235, 244),
    "tile": (210, 224, 237),
    "counter": (248, 250, 252),
    "line": (131, 150, 169),
    "ink": (34, 49, 64),
    "muted": (96, 112, 128),
    "red": (224, 86, 86),
    "green": (75, 176, 112),
    "blue": (72, 134, 220),
    "purple": (139, 103, 206),
    "gold": (246, 193, 84),
    "accent": (38, 166, 154),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Overbug Lab 2 display client")
    parser.add_argument("--port", type=int, default=17667)
    parser.add_argument("--host", default="0.0.0.0")
    return parser.parse_args()


def bind_socket(host: str, port: int) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.setblocking(False)
    return sock


def receive_latest(sock: socket.socket, current: dict[str, Any] | None) -> dict[str, Any] | None:
    latest = current
    while True:
        try:
            data, _addr = sock.recvfrom(65535)
        except BlockingIOError:
            return latest
        try:
            snapshot = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        if snapshot.get("packet_id") == "OBST":
            latest = snapshot


class Renderer:
    def __init__(self, pygame: Any, screen: Any) -> None:
        self.pygame = pygame
        self.screen = screen
        self.font = pygame.font.SysFont("Segoe UI", 20)
        self.small = pygame.font.SysFont("Segoe UI", 15)
        self.big = pygame.font.SysFont("Segoe UI", 42, bold=True)
        self.mid = pygame.font.SysFont("Segoe UI", 26, bold=True)
        self.solder_icon = self._load_solder_icon()

    def _load_solder_icon(self) -> Any | None:
        path = Path(__file__).resolve().parents[1] / "game" / "assets" / "soldering_iron_reference.png"
        if not path.exists():
            return None
        try:
            image = self.pygame.image.load(str(path)).convert_alpha()
        except self.pygame.error:
            return None
        return self.pygame.transform.smoothscale(image, (46, 46))

    def draw(self, snapshot: dict[str, Any] | None) -> None:
        self.screen.fill(COLORS["bg"])
        self._draw_map()
        if snapshot is None:
            self._draw_waiting()
            return
        station_states = {
            station["id"]: station
            for station in snapshot.get("stations", [])
            if isinstance(station, dict) and isinstance(station.get("id"), str)
        }
        for facility in FACILITIES:
            self._draw_station({**station_states.get(facility["id"], {}), **facility})
        for player in snapshot.get("players", []):
            self._draw_player(player)
        self._draw_orders(snapshot)
        self._draw_hud(snapshot)
        if snapshot.get("game_over"):
            self._draw_game_over(snapshot)

    def _draw_map(self) -> None:
        pygame = self.pygame
        rect = pygame.Rect(*MAP_RECT)
        pygame.draw.rect(self.screen, COLORS["floor"], rect, border_radius=18)
        pygame.draw.rect(self.screen, (182, 198, 214), rect, 4, border_radius=18)
        for x in range(rect.x, rect.right, 40):
            pygame.draw.line(self.screen, COLORS["tile"], (x, rect.y), (x, rect.bottom), 1)
        for y in range(rect.y, rect.bottom, 40):
            pygame.draw.line(self.screen, COLORS["tile"], (rect.x, y), (rect.right, y), 1)

    def _draw_station(self, station: dict[str, Any]) -> None:
        pygame = self.pygame
        rect = pygame.Rect(int(station["x"]), int(station["y"]), int(station["w"]), int(station["h"]))
        station_type = station.get("type", "")
        fill = {
            "material_bin": (233, 247, 243),
            "welder": (255, 247, 224),
            "verification_computer": (232, 242, 255),
            "shipping": (230, 248, 234),
            "trash": (252, 233, 233),
            "intake": (238, 236, 252),
            "table": COLORS["counter"],
        }.get(station_type, COLORS["counter"])

        pygame.draw.rect(self.screen, (181, 195, 208), rect.move(0, 5), border_radius=10)
        pygame.draw.rect(self.screen, fill, rect, border_radius=10)
        pygame.draw.rect(self.screen, COLORS["line"], rect, 2, border_radius=10)

        if station_type == "material_bin":
            self._draw_component(station.get("component", "resistor"), rect.centerx, rect.centery + 4)
            self._badge(station.get("component", "?")[:1].upper(), rect.centerx, rect.bottom + 12)
        elif station_type == "welder":
            if self.solder_icon is not None:
                self.screen.blit(self.solder_icon, self.solder_icon.get_rect(center=(rect.centerx, rect.centery - 8)))
            else:
                self._label("WELD", rect.centerx, rect.centery - 8)
            self._progress(rect.x + 10, rect.bottom - 18, rect.w - 20, float(station.get("progress", 0.0)))
            self._draw_item(station.get("board"), rect.x + 16, rect.y + 4, 0.7)
            self._draw_item(station.get("pending_material"), rect.right - 42, rect.y + 16, 0.62)
            self._badge("2s", rect.centerx, rect.bottom + 12)
        elif station_type == "verification_computer":
            self._draw_computer(rect.centerx, rect.centery - 8)
            self._progress(rect.x + 10, rect.bottom - 18, rect.w - 20, float(station.get("progress", 0.0)))
            self._draw_item(station.get("item"), rect.x + 12, rect.y + 12, 0.68)
            self._badge("4s", rect.centerx, rect.bottom + 12)
        elif station_type == "intake":
            self._draw_board(rect.centerx, rect.centery - 4, [0, 0, 0], broken=True, scale=0.85)
            self._badge("IN", rect.centerx, rect.bottom + 12)
        elif station_type == "shipping":
            self._draw_shipping(rect.centerx, rect.centery - 6)
            self._badge("OUT", rect.centerx, rect.bottom + 12)
        elif station_type == "trash":
            self._draw_trash(rect.centerx, rect.centery - 4)
            self._badge("RECYCLE", rect.centerx, rect.bottom + 12)
        elif station_type == "table":
            self._draw_item(station.get("item"), rect.centerx - 18, rect.centery - 18, 0.75)

    def _draw_orders(self, snapshot: dict[str, Any]) -> None:
        pygame = self.pygame
        title = self.big.render("OVERBUG", True, COLORS["ink"])
        self.screen.blit(title, (28, 22))
        for index, order in enumerate(snapshot.get("orders", [])):
            x = 292 + index * 206
            rect = pygame.Rect(x, 18, 184, 64)
            fill = (255, 255, 255) if not order.get("locked_board_id") else (232, 248, 234)
            pygame.draw.rect(self.screen, fill, rect, border_radius=10)
            pygame.draw.rect(self.screen, COLORS["line"], rect, 2, border_radius=10)
            self._label(f"Order {order['id']}", x + 48, 34, small=True)
            cx = x + 28
            req = order.get("req", [0, 0, 0])
            for component, count in zip(("resistor", "capacitor", "inductor"), req):
                self._draw_component(component, cx, 58, 0.58)
                text = self.small.render(f"x{count}", True, COLORS["ink"])
                self.screen.blit(text, (cx + 16, 48))
                cx += 52

    def _draw_hud(self, snapshot: dict[str, Any]) -> None:
        pygame = self.pygame
        score_rect = pygame.Rect(24, 660, 250, 46)
        timer_rect = pygame.Rect(1060, 660, 190, 46)
        for rect in (score_rect, timer_rect):
            pygame.draw.rect(self.screen, (255, 255, 255), rect, border_radius=12)
            pygame.draw.rect(self.screen, COLORS["line"], rect, 2, border_radius=12)
        score = self.mid.render(f"Score {snapshot.get('score', 0)}", True, COLORS["ink"])
        self.screen.blit(score, (score_rect.x + 18, score_rect.y + 8))
        remaining = int(snapshot.get("remaining_ms", 0)) // 1000
        timer = self.mid.render(f"{remaining // 60:02d}:{remaining % 60:02d}", True, COLORS["ink"])
        self.screen.blit(timer, (timer_rect.x + 45, timer_rect.y + 8))

    def _draw_player(self, player: dict[str, Any]) -> None:
        pygame = self.pygame
        x = int(player.get("x", 0))
        y = int(player.get("y", 0))
        color = [(230, 89, 82), (74, 144, 226), (72, 174, 112), (143, 105, 210)][
            (int(player.get("id", 1)) - 1) % 4
        ]
        pygame.draw.circle(self.screen, (120, 132, 145), (x, y + 5), 18)
        pygame.draw.circle(self.screen, color, (x, y), 18)
        pygame.draw.circle(self.screen, (255, 238, 207), (x, y - 8), 9)
        pygame.draw.circle(self.screen, COLORS["ink"], (x - 3, y - 10), 2)
        pygame.draw.circle(self.screen, COLORS["ink"], (x + 4, y - 10), 2)
        label = self.small.render(f"P{player.get('id', 1)}", True, (255, 255, 255))
        self.screen.blit(label, (x - 9, y + 1))
        self._draw_item(player.get("held"), x - 14, y - 46, 0.75)

    def _draw_item(self, item: dict[str, Any] | None, x: float, y: float, scale: float = 1.0) -> None:
        if not item:
            return
        if item.get("kind") == "material":
            self._draw_component(item.get("component", "resistor"), int(x + 22 * scale), int(y + 22 * scale), scale)
        elif item.get("kind") == "board":
            counts = item.get("counts", [0, 0, 0])
            broken = item.get("state") == "failed"
            self._draw_board(int(x + 24 * scale), int(y + 20 * scale), counts, broken, scale)

    def _draw_component(self, component: str, x: float, y: float, scale: float = 1.0) -> None:
        pygame = self.pygame
        color = {
            "resistor": COLORS["red"],
            "capacitor": COLORS["blue"],
            "inductor": COLORS["purple"],
        }.get(component, COLORS["muted"])
        x = int(x)
        y = int(y)
        s = scale
        if component == "resistor":
            points = [
                (x - int(18 * s), y),
                (x - int(10 * s), y - int(8 * s)),
                (x - int(2 * s), y + int(8 * s)),
                (x + int(6 * s), y - int(8 * s)),
                (x + int(14 * s), y + int(8 * s)),
                (x + int(20 * s), y),
            ]
            pygame.draw.lines(self.screen, color, False, points, max(2, int(4 * s)))
        elif component == "capacitor":
            pygame.draw.line(self.screen, color, (x - int(17 * s), y), (x - int(5 * s), y), max(2, int(3 * s)))
            pygame.draw.line(self.screen, color, (x + int(5 * s), y), (x + int(17 * s), y), max(2, int(3 * s)))
            pygame.draw.line(self.screen, color, (x - int(5 * s), y - int(14 * s)), (x - int(5 * s), y + int(14 * s)), max(2, int(4 * s)))
            pygame.draw.line(self.screen, color, (x + int(5 * s), y - int(14 * s)), (x + int(5 * s), y + int(14 * s)), max(2, int(4 * s)))
        else:
            pygame.draw.line(self.screen, color, (x - int(22 * s), y), (x - int(13 * s), y), max(2, int(3 * s)))
            for offset in (-9, 0, 9):
                rect = pygame.Rect(x + int((offset - 5) * s), y - int(10 * s), int(11 * s), int(20 * s))
                pygame.draw.arc(self.screen, color, rect, 3.14, 0, max(2, int(3 * s)))
            pygame.draw.line(self.screen, color, (x + int(18 * s), y), (x + int(24 * s), y), max(2, int(3 * s)))

    def _draw_board(self, x: int, y: int, counts: list[int], broken: bool, scale: float = 1.0) -> None:
        pygame = self.pygame
        w = int(48 * scale)
        h = int(34 * scale)
        rect = pygame.Rect(x - w // 2, y - h // 2, w, h)
        pygame.draw.rect(self.screen, (77, 176, 105), rect, border_radius=max(4, int(6 * scale)))
        pygame.draw.rect(self.screen, (39, 112, 70), rect, 2, border_radius=max(4, int(6 * scale)))
        pygame.draw.rect(self.screen, (42, 56, 66), (rect.x + int(16 * scale), rect.y + int(9 * scale), int(17 * scale), int(14 * scale)), border_radius=2)
        if broken:
            pygame.draw.line(self.screen, COLORS["red"], (rect.x + 7, rect.y + 7), (rect.right - 7, rect.bottom - 7), 4)
            pygame.draw.line(self.screen, COLORS["red"], (rect.right - 7, rect.y + 7), (rect.x + 7, rect.bottom - 7), 4)
        if sum(counts) > 0:
            text = " ".join(f"{name}{count}" for name, count in zip(("R", "C", "L"), counts) if count)
            self._badge(text, x, y + int(24 * scale))

    def _draw_computer(self, x: int, y: int) -> None:
        pygame = self.pygame
        screen = pygame.Rect(x - 27, y - 20, 54, 34)
        pygame.draw.rect(self.screen, (50, 72, 96), screen, border_radius=5)
        pygame.draw.rect(self.screen, (149, 217, 232), screen.inflate(-8, -8), border_radius=4)
        pygame.draw.line(self.screen, COLORS["green"], (x - 10, y - 3), (x - 2, y + 6), 3)
        pygame.draw.line(self.screen, COLORS["green"], (x - 2, y + 6), (x + 14, y - 9), 3)
        pygame.draw.rect(self.screen, (50, 72, 96), (x - 18, y + 18, 36, 6), border_radius=3)

    def _draw_shipping(self, x: int, y: int) -> None:
        pygame = self.pygame
        pygame.draw.rect(self.screen, COLORS["green"], (x - 28, y - 16, 38, 28), border_radius=6)
        pygame.draw.polygon(self.screen, COLORS["green"], [(x + 6, y - 24), (x + 34, y), (x + 6, y + 24)])

    def _draw_trash(self, x: int, y: int) -> None:
        pygame = self.pygame
        pygame.draw.rect(self.screen, COLORS["red"], (x - 18, y - 14, 36, 34), border_radius=5)
        pygame.draw.rect(self.screen, (165, 62, 62), (x - 24, y - 22, 48, 8), border_radius=4)
        pygame.draw.line(self.screen, (255, 245, 245), (x - 8, y - 5), (x + 8, y + 11), 3)
        pygame.draw.line(self.screen, (255, 245, 245), (x + 8, y - 5), (x - 8, y + 11), 3)

    def _progress(self, x: float, y: float, w: float, progress: float) -> None:
        pygame = self.pygame
        progress = max(0.0, min(1.0, progress))
        bg = pygame.Rect(int(x), int(y), int(w), 8)
        pygame.draw.rect(self.screen, (221, 228, 236), bg, border_radius=4)
        if progress > 0:
            fg = pygame.Rect(bg.x, bg.y, int(bg.w * progress), bg.h)
            pygame.draw.rect(self.screen, COLORS["accent"], fg, border_radius=4)

    def _label(self, text: str, x: float, y: float, small: bool = False) -> None:
        font = self.small if small else self.font
        rendered = font.render(text, True, COLORS["ink"])
        self.screen.blit(rendered, rendered.get_rect(center=(int(x), int(y))))

    def _badge(self, text: str, x: float, y: float) -> None:
        pygame = self.pygame
        rendered = self.small.render(text, True, COLORS["ink"])
        rect = rendered.get_rect(center=(int(x), int(y)))
        rect.inflate_ip(12, 6)
        pygame.draw.rect(self.screen, (255, 255, 255), rect, border_radius=6)
        pygame.draw.rect(self.screen, COLORS["line"], rect, 1, border_radius=6)
        self.screen.blit(rendered, rendered.get_rect(center=rect.center))

    def _draw_waiting(self) -> None:
        text = self.mid.render("Waiting for server.c snapshots on UDP...", True, COLORS["ink"])
        self.screen.blit(text, text.get_rect(center=(WIDTH // 2, HEIGHT // 2)))

    def _draw_game_over(self, snapshot: dict[str, Any]) -> None:
        pygame = self.pygame
        overlay = pygame.Surface(self.screen.get_size(), pygame.SRCALPHA)
        overlay.fill((24, 34, 44, 150))
        self.screen.blit(overlay, (0, 0))
        panel = pygame.Rect(390, 210, 500, 270)
        pygame.draw.rect(self.screen, (255, 255, 255), panel, border_radius=18)
        pygame.draw.rect(self.screen, COLORS["line"], panel, 3, border_radius=18)
        title = self.big.render("Time!", True, COLORS["ink"])
        self.screen.blit(title, title.get_rect(center=(panel.centerx, panel.y + 58)))
        lines = [
            f"Final score: {snapshot.get('score', 0)}",
            f"Boards shipped: {snapshot.get('completed', 0)}",
            f"Recycled items: {snapshot.get('recycled', 0)}",
            "Press Esc to quit",
        ]
        for index, line in enumerate(lines):
            color = COLORS["ink"] if index < 3 else COLORS["muted"]
            text = self.mid.render(line, True, color)
            self.screen.blit(text, text.get_rect(center=(panel.centerx, panel.y + 104 + index * 38)))


def main() -> int:
    args = parse_args()
    sock = bind_socket(args.host, args.port)

    import pygame

    pygame.init()
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Overbug Lab 2 Display")
    clock = pygame.time.Clock()
    renderer = Renderer(pygame, screen)
    snapshot: dict[str, Any] | None = None
    running = True

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False

        snapshot = receive_latest(sock, snapshot)
        renderer.draw(snapshot)
        pygame.display.flip()
        clock.tick(60)

    pygame.quit()
    sock.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
