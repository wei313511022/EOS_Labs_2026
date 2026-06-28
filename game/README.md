# Overbug Game MVP

Overbug is a cooperative classroom repair game inspired by Overcooked. Players
repair broken Raspberry-Pi-like boards by collecting components, soldering each
component, inspecting the board at a computer station, and shipping successful
repairs before the 3 minute timer ends.

## Run

Install Python 3.12 or newer, then install the game dependency:

```powershell
python -m pip install -r game/requirements.txt
```

Start the game:

```powershell
python game/main.py
```

The game uses `pygame-ce`, which is imported as `pygame`.

## Controls

The MVP supports 1 to 4 players on the same keyboard. All four players are
spawned by default so the controls can be tested immediately.

| Player | Move | Pick / Drop | Solder |
| --- | --- | --- | --- |
| P1 | W A S D | E | Q |
| P2 | Arrow keys | Right Ctrl | Right Shift |
| P3 | I J K L | U | O |
| P4 | T F G H | R | Y |

## Rules

- Orders show 3 cards at the top of the screen.
- Each order has 0 to 2 resistors, capacitors, and inductors, with a total of
  1 to 4 components.
- A player can hold only one item.
- Put a broken board on a soldering station before adding materials.
- A soldering station can hold one board and one pending material.
- Hold the solder key near the station for 2 seconds to attach the material.
- Solder progress pauses if the player leaves or releases the key.
- Inspection stations are computer test terminals. They inspect one board for
  4 seconds without needing a nearby player.
- A matching board locks an order and must be shipped to score.
- Failed boards and abandoned boards can be recycled without penalty.
- Score is `10 + component_count * 5`.

## Tests

The core rules do not require pygame:

```powershell
python -m unittest discover -s game/tests
```

## Optional Native Module

The Python game is complete without native code. The optional native library in
`game/native` exposes C ABI functions for order matching, scoring, and collision
helpers. Build it only when a C/C++ compiler is available:

```powershell
python game/native/build_native.py
```

If the library is missing, Overbug automatically uses the Python fallback.
