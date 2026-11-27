#!/usr/bin/env python3
"""Install Ultimate ASI Loader next to the game executable.

Run with:

    python tools/setup_asi_loader.py

The game path is read from GAME_EXE in .env (or passed with --game-dir).
The 32-bit dinput8.dll proxy is installed next to the game executable; the
mod itself (AltairFix.asi) is deployed by CMake to <game folder>/scripts.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# Pinned loader release. The proxy DLL is installed next to the game exe.
DEFAULT_LOADER_URL = (
    "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/"
    "v10.0.5/Ultimate-ASI-Loader.zip"
)
DEFAULT_DLL_NAME = "dinput8.dll"

# PE machine values from the COFF header.
PE_I386 = 0x014C
PE_AMD64 = 0x8664


def info(message: str) -> None:
    """Print a loader setup message."""
    print(f"[asi-loader] {message}", flush=True)


def warn(message: str) -> None:
    """Print a loader setup warning."""
    print(f"[asi-loader] warning: {message}", flush=True)


def fail(message: str) -> None:
    """Print a loader setup error and exit with a failure code."""
    print(f"[asi-loader] error: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def find_env_file(explicit: str | None) -> Path | None:
    """Return the requested .env file, or the first project-level one found."""
    if explicit:
        path = Path(explicit).expanduser()
        if not path.is_file():
            fail(f"Env file not found: {path}")
        return path

    for name in (".env", ".ENV", ".env.local"):
        candidate = PROJECT_ROOT / name
        if candidate.is_file():
            return candidate
    return None


def read_env(path: Path) -> dict[str, str]:
    """Read simple KEY=VALUE lines from an env file.

    Blank lines and comments are ignored. Matching single or double quotes
    around a value are stripped, and keys are normalized to uppercase.
    """
    values: dict[str, str] = {}
    try:
        # utf-8-sig tolerates a BOM added by Windows editors.
        text = path.read_text(encoding="utf-8-sig")
    except OSError as exc:
        fail(f"Could not read {path}: {exc}")

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip().upper()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values


def pe_machine_bytes(data: bytes) -> int | None:
    """Return the PE machine type from a byte buffer, or None if it is not PE."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        return None

    # e_lfanew at 0x3C points to the PE signature.
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 6 > len(data) or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        return None
    return struct.unpack_from("<H", data, pe_offset + 4)[0]


def pe_machine_file(path: Path) -> int | None:
    """Read enough of a file to determine its PE machine type."""
    try:
        # The PE header is normally within the first few hundred bytes.
        with path.open("rb") as handle:
            header = handle.read(4096)
    except OSError:
        return None
    return pe_machine_bytes(header)


def resolve_game_dir(
    game_dir_arg: str | None, env_file_arg: str | None
) -> tuple[Path, Path]:
    """Resolve the game folder and executable from --game-dir or .env."""
    if game_dir_arg:
        candidate = Path(game_dir_arg).expanduser()
        if candidate.is_file():
            candidate = candidate.parent
        if not candidate.is_dir():
            fail(f"Game directory not found: {candidate}")
        game_dir = candidate
        game_exe = game_dir / "AssassinsCreed_Dx9.exe"
    else:
        env_file = find_env_file(env_file_arg)
        if env_file is None:
            fail(
                "No .env file found. Copy .env.example to .env and set GAME_EXE, "
                "or pass --game-dir."
            )
        info(f"Reading game path from {env_file}")
        values = read_env(env_file)
        game_exe_value = values.get("GAME_EXE") or values.get("GAME_PATH")
        if not game_exe_value:
            fail(f"GAME_EXE is not set in {env_file}.")
        game_exe = Path(game_exe_value).expanduser()
        # Relative paths in .env are resolved from the project root so the
        # file can be shared between machines.
        if not game_exe.is_absolute():
            game_exe = (PROJECT_ROOT / game_exe).resolve()
        if not game_exe.is_file():
            fail(f"Game executable not found: {game_exe}")
        game_dir = game_exe.parent

    # The ASI loader loads AltairFix.asi from this folder.
    scripts_dir = game_dir / "scripts"
    scripts_dir.mkdir(parents=True, exist_ok=True)
    return game_dir, game_exe


def sha256_file(path: Path) -> str:
    """Return the lowercase SHA-256 digest of a file."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_loader(url: str, cache_file: Path, force: bool) -> Path:
    """Download the loader archive unless a valid cached copy is available."""
    cache_file.parent.mkdir(parents=True, exist_ok=True)

    if cache_file.is_file() and not force:
        info(f"Using cached loader archive: {cache_file}")
        return cache_file

    info(f"Downloading Ultimate ASI Loader: {url}")
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "AltairFix-ASI-Loader-Setup/1.0"},
    )

    # Download to a .part file first so an interrupted transfer never looks
    # like a complete cached archive.
    temp_file = cache_file.with_suffix(cache_file.suffix + ".part")

    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            with temp_file.open("wb") as output:
                shutil.copyfileobj(response, output)
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, OSError) as exc:
        temp_file.unlink(missing_ok=True)
        fail(f"Download failed: {exc}")

    # Atomic replace keeps the cache consistent if the script is interrupted.
    os.replace(temp_file, cache_file)
    info(f"Downloaded {cache_file.stat().st_size} bytes")
    return cache_file


def choose_loader(
    archive_path: Path, preferred_name: str
) -> tuple[str, bytes, int]:
    """Select a 32-bit proxy DLL from the loader archive.

    The release normally contains a single dinput8.dll, but newer archives may
    include several proxy names or architectures. Prefer the requested name
    and always validate the PE machine before installing it.
    """
    try:
        archive = zipfile.ZipFile(archive_path)
    except (OSError, zipfile.BadZipFile) as exc:
        fail(f"Could not open loader archive {archive_path}: {exc}")

    with archive:
        dll_entries = [
            entry
            for entry in archive.infolist()
            if not entry.is_dir() and entry.filename.lower().endswith(".dll")
        ]
        if not dll_entries:
            fail(f"No DLLs found in {archive_path}.")

        # First try the requested proxy name.
        preferred = [
            entry
            for entry in dll_entries
            if Path(entry.filename).name.lower() == preferred_name.lower()
        ]

        candidates: list[tuple[str, bytes, int]] = []
        for entry in preferred:
            data = archive.read(entry)
            machine = pe_machine_bytes(data)
            if machine is not None:
                candidates.append((entry.filename, data, machine))

        if not candidates:
            # Fall back to any 32-bit DLL when the archive names it differently.
            for entry in dll_entries:
                data = archive.read(entry)
                machine = pe_machine_bytes(data)
                if machine == PE_I386:
                    candidates.append((entry.filename, data, machine))

        if not candidates:
            # Include architectures in the error to make a bad archive obvious.
            available = []
            for entry in dll_entries:
                data = archive.read(entry)
                machine = pe_machine_bytes(data)
                arch = (
                    "x86"
                    if machine == PE_I386
                    else "x64"
                    if machine == PE_AMD64
                    else "unknown"
                )
                available.append(f"{Path(entry.filename).name} ({arch})")
            fail(
                f"No 32-bit {preferred_name} found in the loader archive. "
                f"Available: {', '.join(sorted(available))}"
            )

        for name, data, machine in candidates:
            if machine == PE_I386:
                info(f"Selected loader: {name} (PE i386)")
                return name, data, machine

        fail(f"{preferred_name} in the archive is not a 32-bit DLL.")


def install_loader(
    game_dir: Path,
    dll_name: str,
    data: bytes,
    force: bool,
) -> Path:
    """Install the proxy DLL, preserving an existing unrelated loader."""
    destination = game_dir / dll_name
    new_hash = hashlib.sha256(data).hexdigest()

    if destination.is_file():
        old_hash = sha256_file(destination)
        if old_hash == new_hash:
            info(f"Loader already installed: {destination}")
            return destination
        if not force:
            fail(
                f"{destination} already exists and differs from the downloaded loader. "
                "Pass --force to back it up and replace it."
            )

        # Never silently overwrite a different proxy in the game folder.
        stamp = time.strftime("%Y%m%d-%H%M%S")
        backup = destination.with_name(f"{destination.name}.bak-{stamp}")
        try:
            shutil.copy2(destination, backup)
        except OSError as exc:
            fail(f"Could not back up {destination}: {exc}")
        info(f"Backed up existing loader to {backup}")

    # Write through a temporary file so the game never sees a partial DLL.
    temp = destination.with_name(destination.name + ".tmp")
    try:
        temp.write_bytes(data)
        os.replace(temp, destination)
    except OSError as exc:
        temp.unlink(missing_ok=True)
        fail(
            f"Could not install {destination}: {exc}. "
            "Close the game if it is running and check folder permissions."
        )

    info(f"Installed Ultimate ASI Loader: {destination}")
    return destination


def main(argv: list[str] | None = None) -> int:
    """Resolve the game, download the loader, and install the 32-bit proxy."""
    parser = argparse.ArgumentParser(
        description=(
            "Download Ultimate ASI Loader and install its 32-bit proxy DLL "
            "next to the game executable from .env."
        )
    )
    parser.add_argument("--game-dir", help="Game folder (overrides .env)")
    parser.add_argument("--env-file", help="Path to .env (defaults to project .env)")
    parser.add_argument(
        "--dll-name",
        default=DEFAULT_DLL_NAME,
        help=f"Proxy DLL name to install (default: {DEFAULT_DLL_NAME})",
    )
    parser.add_argument(
        "--loader-url",
        default=DEFAULT_LOADER_URL,
        help="Ultimate ASI Loader archive URL",
    )
    parser.add_argument(
        "--cache-dir",
        default=str(PROJECT_ROOT / "build" / "asi-loader"),
        help="Directory for the downloaded archive",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Back up and replace an existing different loader DLL",
    )
    parser.add_argument(
        "--force-download",
        action="store_true",
        help="Download the loader archive again even if it is cached",
    )
    args = parser.parse_args(argv)

    if os.name != "nt":
        fail("This setup script targets Windows.")

    game_dir, game_exe = resolve_game_dir(args.game_dir, args.env_file)
    info(f"Game directory: {game_dir}")
    info(f"Game executable: {game_exe}")

    # A 32-bit game cannot load a 64-bit proxy, and vice versa.
    game_machine = pe_machine_file(game_exe)
    if game_machine != PE_I386:
        fail(
            f"{game_exe.name} is not a 32-bit executable "
            f"(PE machine: {game_machine!r}); the 32-bit ASI loader will not work."
        )

    # Derive the cache filename from the URL so custom loader URLs work too.
    cache_name = Path(urllib.parse.urlparse(args.loader_url).path).name
    cache_file = Path(args.cache_dir) / (cache_name or "Ultimate-ASI-Loader.zip")
    archive_path = download_loader(args.loader_url, cache_file, args.force_download)
    info(f"Loader archive SHA-256: {sha256_file(archive_path)}")

    # Validate the DLL architecture before touching the game folder.
    dll_name, dll_data, _ = choose_loader(archive_path, args.dll_name)
    install_loader(game_dir, dll_name, dll_data, args.force)

    # The build is independent of the loader; warn instead of failing if the
    # ASI has not been built yet.
    mod_path = game_dir / "scripts" / "AltairFix.asi"
    if mod_path.is_file():
        info(f"Mod found: {mod_path}")
    else:
        warn(
            f"Mod not built yet: {mod_path}. "
            "Build with `cmake --build --preset mingw32`."
        )

    info("Ultimate ASI Loader setup complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
