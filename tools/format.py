#!/usr/bin/env python3
"""Format AltairFix C/C++ sources with clang-format.

Run from anywhere with:

    python tools/format.py

Use --check to verify formatting without changing files:

    python tools/format.py --check

Format specific files:

    python tools/format.py --files source/dllmain.cpp
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# Directories scanned when --files is not provided.
SOURCE_DIRS = ("source", "include")

# File extensions handled by clang-format.
SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
}


def info(message: str) -> None:
    """Print a formatter status message."""
    print(f"[format] {message}", flush=True)


def fail(message: str) -> None:
    """Print a formatter error and exit with a failure code."""
    print(f"[format] error: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def find_clang_format(explicit: str | None) -> Path:
    """Find clang-format from an explicit path, PATH, or common install locations."""
    if explicit:
        candidate = Path(explicit).expanduser()
        if candidate.is_file():
            return candidate
        fail(f"clang-format not found at: {candidate}")

    # Prefer the executable the user has already put on PATH.
    found = shutil.which("clang-format")
    if found:
        return Path(found)

    # llvm-mingw ships clang-format next to the compiler; use it when it is not
    # on PATH (for example when the script is run from a bare shell).
    local = Path(os.environ.get("LOCALAPPDATA", ""))
    winget_packages = local / "Microsoft" / "WinGet" / "Packages"
    if winget_packages.is_dir():
        for package in winget_packages.glob("MartinStorsjo.LLVM-MinGW*"):
            if package.is_dir():
                for candidate in package.glob("llvm-mingw-*/bin/clang-format.exe"):
                    if candidate.is_file():
                        return candidate

    # Standard LLVM install locations.
    program_files = Path(os.environ.get("ProgramFiles", ""))
    for candidate in (
        program_files / "LLVM" / "bin" / "clang-format.exe",
        Path("C:/Program Files/LLVM/bin/clang-format.exe"),
        Path("D:/Programs/LLVM/bin/clang-format.exe"),
    ):
        if candidate.is_file():
            return candidate

    fail(
        "clang-format was not found. Install LLVM/llvm-mingw or pass "
        "--clang-format <path>."
    )


def collect_sources(explicit_files: list[str] | None) -> list[Path]:
    """Return the files to format, either explicit paths or the project source tree."""
    if explicit_files:
        files = []
        for value in explicit_files:
            path = Path(value).expanduser().resolve()
            if not path.is_file():
                fail(f"File not found: {path}")
            files.append(path)
        # De-duplicate while keeping the output deterministic.
        return sorted(set(files))

    # Walk the normal source roots and ignore everything else (build trees,
    # IDE files, third-party directories).
    files: set[Path] = set()
    for directory_name in SOURCE_DIRS:
        directory = PROJECT_ROOT / directory_name
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_EXTENSIONS:
                files.add(path.resolve())
    return sorted(files)


def clang_format_version(clang_format: Path) -> str:
    """Return the first line of clang-format --version, or 'unknown'."""
    try:
        result = subprocess.run(
            [str(clang_format), "--version"],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"
    # Some clang-format builds print the version to stderr.
    output = (result.stdout or result.stderr).strip()
    return output.splitlines()[0] if output else "unknown"


def main(argv: list[str] | None = None) -> int:
    """Parse arguments, locate clang-format, and format the selected files."""
    parser = argparse.ArgumentParser(
        description="Format AltairFix C/C++ sources with clang-format."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Check formatting without modifying files",
    )
    parser.add_argument(
        "--files",
        nargs="+",
        metavar="FILE",
        help="Format only the specified files instead of source/ and include/",
    )
    parser.add_argument(
        "--clang-format",
        metavar="PATH",
        help="Path to clang-format.exe",
    )
    args = parser.parse_args(argv)

    clang_format = find_clang_format(args.clang_format)
    files = collect_sources(args.files)
    if not files:
        info("No C/C++ source files found.")
        return 0

    info(f"Using {clang_format_version(clang_format)}: {clang_format}")
    for path in files:
        # Display paths relative to the project when possible; --files can
        # also point outside the project root.
        try:
            display_path = path.relative_to(PROJECT_ROOT)
        except ValueError:
            display_path = path
        info(f"  {display_path}")

    # --style=file makes clang-format discover the nearest .clang-format.
    command = [str(clang_format), "--style=file"]
    if args.check:
        # Report violations and exit non-zero without modifying files.
        command.extend(["--dry-run", "--Werror"])
    else:
        command.append("-i")
    command.extend(str(path) for path in files)

    result = subprocess.run(command, cwd=str(PROJECT_ROOT))
    if result.returncode != 0:
        if args.check:
            fail("Formatting check failed. Run `python tools/format.py` to fix it.")
        fail(f"clang-format exited with code {result.returncode}.")

    if args.check:
        info(f"Formatting check passed for {len(files)} file(s).")
    else:
        info(f"Formatted {len(files)} file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
