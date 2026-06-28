from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def build_with_msvc() -> None:
    BUILD.mkdir(exist_ok=True)
    run(
        [
            "cl",
            "/nologo",
            "/O2",
            "/LD",
            "/EHsc",
            "overbug_rules.c",
            "overbug_collision.cpp",
            f"/Fe:{BUILD / 'overbug_native.dll'}",
        ]
    )


def build_with_gnu() -> None:
    BUILD.mkdir(exist_ok=True)
    obj_c = BUILD / "overbug_rules.o"
    obj_cpp = BUILD / "overbug_collision.o"
    if sys.platform == "darwin":
        out = BUILD / "liboverbug_native.dylib"
    elif sys.platform == "win32":
        out = BUILD / "overbug_native.dll"
    else:
        out = BUILD / "liboverbug_native.so"

    cc = shutil.which("gcc") or shutil.which("clang")
    cxx = shutil.which("g++") or shutil.which("clang++")
    if cc is None or cxx is None:
        raise RuntimeError("gcc/clang and g++/clang++ are required for the native build")

    run([cc, "-O2", "-fPIC", "-c", "overbug_rules.c", "-o", str(obj_c)])
    run([cxx, "-std=c++17", "-O2", "-fPIC", "-c", "overbug_collision.cpp", "-o", str(obj_cpp)])
    run([cxx, "-shared", str(obj_c), str(obj_cpp), "-o", str(out)])


def main() -> int:
    if shutil.which("cl"):
        build_with_msvc()
    else:
        build_with_gnu()
    print(f"Native library written under {BUILD}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
