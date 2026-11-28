#!/usr/bin/env python3
"""Install or remove the D3D10 detection shim next to the game executable.

Run with:

    python tools/setup_dx9_shim.py
    python tools/setup_dx9_shim.py --remove

The game path is read from GAME_EXE in .env (or passed with --game-dir).
Build the shim first with `cmake --build --preset mingw32`; it is produced at
build/mingw32/shim/d3d10.dll.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SHIM_NAME = "d3d10.dll"
DEFAULT_SHIM = PROJECT_ROOT / "build" / "mingw32" / "shim" / SHIM_NAME


def info(message: str) -> None:
    """Print a shim setup message."""
    print(f"[dx9-shim] {message}", flush=True)


def fail(message: str) -> None:
    """Print a shim setup error and exit."""
    print(f"[dx9-shim] error: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def game_dir_from_env() -> Path | None:
    """Return the game directory from the first project-level .env file."""
    for path in sorted(PROJECT_ROOT.glob(".env*")):
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            match = re.match(r"\s*GAME_EXE\s*=\s*(.+?)\s*$", line)
            if match:
                value = match.group(1).strip().strip("\"'")
                return Path(value).parent
    return None


def sha256(path: Path) -> str:
    """Return the SHA-256 of a file."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    """Install or remove the shim."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, help="Game directory instead of GAME_EXE from .env")
    parser.add_argument("--shim", type=Path, default=DEFAULT_SHIM, help="Built shim DLL")
    parser.add_argument("--remove", action="store_true", help="Remove an installed shim")
    parser.add_argument("--force", action="store_true", help="Replace an existing d3d10.dll")
    args = parser.parse_args()

    game_dir = args.game_dir or game_dir_from_env()
    if game_dir is None:
        fail("Could not find GAME_EXE in .env; pass --game-dir")

    target = game_dir / SHIM_NAME
    if args.remove:
        if target.is_file():
            target.unlink()
            info(f"removed {target}")
        else:
            info(f"not installed: {target}")
        return

    if not args.shim.is_file():
        fail(f"Shim not built: {args.shim} (run cmake --build --preset mingw32 first)")

    if target.is_file() and not args.force:
        if sha256(target) != sha256(args.shim):
            fail(f"{target} already exists and is not the AltairFix shim; pass --force to replace it")

    shutil.copy2(args.shim, target)
    info(f"installed {target}")


if __name__ == "__main__":
    main()
