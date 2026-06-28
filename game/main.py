from __future__ import annotations

from pathlib import Path
import sys


def main() -> int:
    try:
        from overbug.pygame_app import run
        level_path = Path(__file__).with_name("data").joinpath("lab_mvp.json")
        return run(level_path)
    except ModuleNotFoundError as exc:
        if exc.name == "pygame":
            print(
                "pygame-ce is required to run the Overbug desktop game.\n"
                "Install it with:\n\n"
                "    python -m pip install -r game/requirements.txt\n\n"
                "The rules and unit tests can still run without pygame-ce."
            )
            return 1
        raise


if __name__ == "__main__":
    sys.exit(main())
