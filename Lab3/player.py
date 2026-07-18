#!/usr/bin/env python3
"""Multi-player keyboard controller that sends six-byte Overbug OBIN packets."""

import argparse
import socket
import struct
import sys


PACKET = struct.Struct("!4sBB")
assert PACKET.size == 6

BUTTON_UP = 0x01
BUTTON_DOWN = 0x02
BUTTON_LEFT = 0x04
BUTTON_RIGHT = 0x08
BUTTON_ACTION = 0x10
BUTTON_SOLDER = 0x20

CONTROLS = {
    1: {
        "up": "W", "down": "S", "left": "A", "right": "D",
        "action": "Space", "solder": "Left Shift",
        "keys": ("K_w", "K_s", "K_a", "K_d", "K_SPACE", "K_LSHIFT"),
    },
    2: {
        "up": "Up", "down": "Down", "left": "Left", "right": "Right",
        "action": "Right Ctrl", "solder": "Right Shift",
        "keys": ("K_UP", "K_DOWN", "K_LEFT", "K_RIGHT", "K_RCTRL", "K_RSHIFT"),
    },
    3: {
        "up": "I", "down": "K", "left": "J", "right": "L",
        "action": "O", "solder": "P",
        "keys": ("K_i", "K_k", "K_j", "K_l", "K_o", "K_p"),
    },
    4: {
        "up": "8", "down": "5", "left": "4", "right": "6",
        "action": "0", "solder": "7",
        "keys": ("K_8", "K_5", "K_4", "K_6", "K_0", "K_7"),
    },
}
PLAYER_IDS = (1, 2, 3, 4)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Send simultaneous Overbug OBIN input for four keyboard players."
    )
    parser.add_argument("--server", required=True, help="server IP address or hostname")
    parser.add_argument("--port", type=int, default=17666,
                        help="server UDP port (default: 17666)")
    parser.add_argument("--hz", type=int, default=60,
                        help="packets sent per second (default: 60)")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if not 1 <= args.hz <= 1000:
        parser.error("--hz must be between 1 and 1000")
    return args


def button_mask(keys, player_id, pygame):
    """Translate one player's current keys to the Lab 2 button bit mask."""
    key_names = CONTROLS[player_id]["keys"]
    up, down, left, right, action, solder = (getattr(pygame, name) for name in key_names)
    mask = 0
    if keys[up]:
        mask |= BUTTON_UP
    if keys[down]:
        mask |= BUTTON_DOWN
    if keys[left]:
        mask |= BUTTON_LEFT
    if keys[right]:
        mask |= BUTTON_RIGHT
    if keys[action]:
        mask |= BUTTON_ACTION
    if keys[solder]:
        mask |= BUTTON_SOLDER
    return mask


def send_packet(sock, server, player_id, mask):
    sock.sendto(PACKET.pack(b"OBIN", player_id, mask), server)


def draw_window(screen, font, masks, pygame):
    screen.fill((27, 37, 48))
    title = font.render("Overbug multi-player keyboard controller", True, (235, 241, 245))
    screen.blit(title, (24, 20))
    for player_id in PLAYER_IDS:
        control = CONTROLS[player_id]
        line = (f"P{player_id}: {control['up']}/{control['left']}/{control['down']}/{control['right']} move"
                f" | {control['action']} action | {control['solder']} solder"
                f" | mask 0x{masks[player_id]:02X}")
        color = (122, 220, 164) if masks[player_id] else (220, 228, 233)
        text = font.render(line, True, color)
        screen.blit(text, (24, 66 + (player_id - 1) * 48))
    footer = font.render("Esc or close this window to stop all simulated players.", True,
                         (235, 241, 245))
    screen.blit(footer, (24, 66 + len(PLAYER_IDS) * 48))
    pygame.display.flip()


def main():
    args = parse_args()
    try:
        import pygame
    except ImportError:
        print("pygame is required. Install it with: python3 -m pip install pygame-ce",
              file=sys.stderr)
        return 1

    server = (args.server, args.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pygame.init()
    pygame.display.set_caption("Overbug multi-player keyboard controller")
    screen = pygame.display.set_mode((780, 120 + len(PLAYER_IDS) * 48))
    font = pygame.font.Font(None, 25)
    clock = pygame.time.Clock()
    running = True

    try:
        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                    running = False

            keys = pygame.key.get_pressed()
            masks = {player_id: button_mask(keys, player_id, pygame)
                     for player_id in PLAYER_IDS}
            for player_id, mask in masks.items():
                send_packet(sock, server, player_id, mask)
            draw_window(screen, font, masks, pygame)
            clock.tick(args.hz)
    finally:
        # Stop movement/action promptly when this controller closes.
        try:
            for player_id in PLAYER_IDS:
                send_packet(sock, server, player_id, 0)
        finally:
            sock.close()
            pygame.quit()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
