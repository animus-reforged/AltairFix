#!/usr/bin/env python3
"""Set up AltairFix for CLion with 32-bit MinGW and launch it.

Run from anywhere with:

    python tools/setup_clion.py

The script finds the installed CLion, CMake, Ninja, and 32-bit MinGW-w64
toolchain, configures and builds the ``mingw32`` CMake preset, enables that
preset in the project's CLion workspace, and optionally opens the project.

The broken "MinGW (32 Bit)" CLion toolchain is intentionally not referenced.
The CMake preset's toolchain file selects the i686 compiler directly.

The AltairFix run configuration is given the GAME_EXE from .env as its
executable, so the Debug button launches the game with the ASI loaded.

If CLion is already open, the script asks you to close it before updating the
CMake profile. Use --close-clion to attempt an automatic close instead.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# CLion keeps per-project run configurations and CMake profile state here.
WORKSPACE = PROJECT_ROOT / ".idea" / "workspace.xml"

# Preset names must match CMakePresets.json.
PRESET = "mingw32"
RELEASE_PRESET = "mingw32-release"
MIN_CMAKE = (4, 4)

# Used when the project has never been opened in CLion.
MINIMAL_WORKSPACE = """<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="CMakePresetLoader"><![CDATA[{
  "useNewFormat": true
}]]></component>
  <component name="CMakeProjectFlavorService">
    <option name="flavorId" value="CMakePlainProjectFlavor" />
  </component>
  <component name="CMakeSettings">
    <configurations>
    </configurations>
  </component>
</project>
"""

MINIMAL_CMAKE_SETTINGS = """  <component name="CMakeSettings">
    <configurations>
    </configurations>
  </component>
"""


def info(message: str) -> None:
    """Print a setup status message."""
    print(f"[setup] {message}", flush=True)


def warn(message: str) -> None:
    """Print a setup warning."""
    print(f"[setup] warning: {message}", flush=True)


def fail(message: str) -> None:
    """Print a setup error and exit with a failure code."""
    print(f"[setup] error: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def capture(command: list[object]) -> str | None:
    """Run a command quietly and return its combined output."""
    try:
        result = subprocess.run(
            [str(part) for part in command],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0:
        return None
    return (result.stdout or result.stderr).strip()


def run_visible(command: list[object], *, cwd: Path, env: dict[str, str]) -> None:
    """Run a command with visible output and stop on a non-zero exit code."""
    info("$ " + " ".join(str(part) for part in command))
    result = subprocess.run(
        [str(part) for part in command],
        cwd=str(cwd),
        env=env,
    )
    if result.returncode != 0:
        fail(f"Command failed with exit code {result.returncode}.")


def _clion_candidates() -> list[Path]:
    """Return likely CLion launchers, starting with entries found on PATH."""
    candidates: list[Path] = []

    # Prefer a launcher the user has already put on PATH.
    for name in ("clion64.exe", "clion.exe", "clion.bat"):
        found = shutil.which(name)
        if found:
            candidates.append(Path(found))

    # Then check the usual standalone and Toolbox install locations.
    local = Path(os.environ.get("LOCALAPPDATA", ""))
    program_files = Path(os.environ.get("ProgramFiles", ""))
    program_files_x86 = Path(os.environ.get("ProgramFiles(x86)", ""))

    candidates.extend(
        [
            local / "Programs" / "CLion" / "bin" / "clion64.exe",
            local / "Programs" / "CLion" / "bin" / "clion.exe",
            program_files / "JetBrains" / "CLion" / "bin" / "clion64.exe",
            program_files_x86 / "JetBrains" / "CLion" / "bin" / "clion64.exe",
            Path("D:/Programs/CLion/bin/clion64.exe"),
            Path("C:/Programs/CLion/bin/clion64.exe"),
        ]
    )

    search_roots = [
        local / "JetBrains" / "Toolbox" / "apps" / "CLion",
        local / "JetBrains" / "Toolbox" / "apps",
        program_files / "JetBrains",
        program_files_x86 / "JetBrains",
        Path("D:/Programs"),
        Path("C:/Program Files/JetBrains"),
    ]
    for root in search_roots:
        if not root.is_dir():
            continue
        for pattern in (
            "CLion*/bin/clion64.exe",
            "CLion*/bin/clion.exe",
            "*/bin/clion64.exe",
            "*/bin/clion.exe",
            "CLion/*/*/bin/clion64.exe",
            "CLion/*/*/bin/clion.exe",
        ):
            candidates.extend(root.glob(pattern))

    return candidates


def find_clion(explicit: str | None) -> Path:
    """Locate CLion from an explicit path or the common install locations."""
    if explicit:
        # Accept the install root, its bin directory, or the launcher itself.
        candidate = Path(explicit).expanduser()
        if candidate.is_dir():
            for name in ("clion64.exe", "clion.exe", "clion.bat"):
                for exe in (candidate / name, candidate / "bin" / name):
                    if exe.is_file():
                        return exe
        elif candidate.is_file():
            info(f"Using CLion: {candidate}")
            return candidate
        fail(f"CLion not found at '{explicit}'.")

    seen: set[Path] = set()
    for candidate in _clion_candidates():
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved in seen or not resolved.is_file():
            continue
        seen.add(resolved)
        info(f"Using CLion: {resolved}")
        return resolved

    fail("CLion was not found. Pass --clion <path-to-clion64.exe>.")


def _cmake_version(cmake: Path) -> tuple[int, int, int] | None:
    """Return the parsed CMake version, or None if it cannot be determined."""
    output = capture([cmake, "--version"])
    if not output:
        return None
    match = re.search(r"cmake version (\d+)\.(\d+)(?:\.(\d+))?", output)
    if not match:
        return None
    return tuple(int(part or 0) for part in match.groups())  # type: ignore[return-value]


def find_cmake(clion_exe: Path) -> Path:
    """Find a CMake new enough for CMakePresets.json."""
    candidates: list[Path] = []

    # Prefer the system CMake, then fall back to the copy bundled with CLion.
    system_cmake = shutil.which("cmake")
    if system_cmake:
        candidates.append(Path(system_cmake))

    candidates.append(
        clion_exe.parent / "cmake" / "win" / "x64" / "bin" / "cmake.exe"
    )

    for candidate in candidates:
        if not candidate.is_file():
            continue
        version = _cmake_version(candidate)
        if version and version[:2] >= MIN_CMAKE:
            info(f"Using CMake {'.'.join(str(part) for part in version)}: {candidate}")
            return candidate

    fail("CMake 4.4 or newer was not found. Install it or use CLion's bundled CMake.")


def find_ninja(clion_exe: Path) -> Path:
    """Find Ninja on PATH or use the copy bundled with CLion."""
    system_ninja = shutil.which("ninja")
    if system_ninja:
        info(f"Using Ninja: {system_ninja}")
        return Path(system_ninja)

    bundled = clion_exe.parent / "ninja" / "win" / "x64" / "ninja.exe"
    if bundled.is_file():
        info(f"Using Ninja: {bundled}")
        return bundled

    fail("Ninja was not found. Install it or use CLion's bundled Ninja.")


def _compiler_target(compiler: Path) -> str:
    """Return the compiler's target triple from -dumpmachine."""
    output = capture([compiler, "-dumpmachine"])
    return output.splitlines()[0].strip() if output else ""


def _is_32bit_target(target: str) -> bool:
    """Return True for 32-bit x86 MinGW target triples."""
    # Matches i386/i486/i586/i686 targets, but not x86_64.
    return bool(re.match(r"^(i[3-6]86)(-|$)", target))


def _mingw_candidates() -> list[Path]:
    """Return likely 32-bit MinGW compiler paths."""
    candidates: list[Path] = []

    # Prefer the target-prefixed compiler; plain g++ may be a 64-bit toolchain.
    for name in ("i686-w64-mingw32-g++", "g++"):
        found = shutil.which(name)
        if found:
            candidates.append(Path(found))

    # Common WinGet, MSYS2, and legacy CLion locations.
    local = Path(os.environ.get("LOCALAPPDATA", ""))
    winget_packages = local / "Microsoft" / "WinGet" / "Packages"
    if winget_packages.is_dir():
        for package in winget_packages.glob("MartinStorsjo.LLVM-MinGW*"):
            if package.is_dir():
                candidates.extend(
                    package.glob("llvm-mingw-*/bin/i686-w64-mingw32-g++.exe")
                )
                candidates.extend(package.glob("llvm-mingw-*/bin/g++.exe"))

    candidate_dirs = [
        Path("C:/msys64/mingw32/bin"),
        Path("D:/msys64/mingw32/bin"),
        Path("C:/mingw32/bin"),
        Path("D:/Programs/mingw32/bin"),
        local / "Programs" / "CLion" / "bin" / "mingw32" / "bin",
        Path("D:/Programs/CLion/bin/mingw32/bin"),
    ]
    for directory in candidate_dirs:
        if directory.is_dir():
            candidates.extend(directory.glob("i686-w64-mingw32-g++.exe"))
            candidates.extend(directory.glob("g++.exe"))

    for root in (
        Path("C:/Program Files/mingw-w64"),
        Path("C:/Program Files (x86)/mingw-w64"),
    ):
        if root.is_dir():
            candidates.extend(root.glob("i686-*/mingw32/bin/g++.exe"))
            candidates.extend(root.glob("i686-*/mingw64/bin/g++.exe"))

    return candidates


def find_mingw(explicit: str | None) -> tuple[Path, Path, str]:
    """Find a 32-bit MinGW compiler and return (bin dir, compiler, target)."""
    if explicit:
        path = Path(explicit).expanduser()
        if path.is_file():
            compilers = [path]
        elif path.is_dir():
            compilers = [
                path / "i686-w64-mingw32-g++.exe",
                path / "g++.exe",
                path / "bin" / "i686-w64-mingw32-g++.exe",
                path / "bin" / "g++.exe",
            ]
        else:
            fail(f"MinGW path not found: '{explicit}'.")
        for compiler in compilers:
            if compiler.is_file():
                # Validate the target triple; a compiler named g++ may still
                # produce 64-bit binaries.
                target = _compiler_target(compiler)
                if _is_32bit_target(target):
                    info(f"Using 32-bit MinGW ({target}): {compiler}")
                    return compiler.parent, compiler, target
        fail(f"No 32-bit MinGW compiler found under '{explicit}'.")

    seen: set[Path] = set()
    for candidate in _mingw_candidates():
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved in seen or not resolved.is_file():
            continue
        seen.add(resolved)
        target = _compiler_target(resolved)
        if _is_32bit_target(target):
            info(f"Using 32-bit MinGW ({target}): {resolved}")
            return resolved.parent, resolved, target

    fail(
        "No 32-bit MinGW-w64 compiler was found. Install one (for example "
        "`winget install MartinStorsjo.LLVM-MinGW.UCRT`) or pass --mingw-bin."
    )


def find_optional_tool(name: str, mingw_bin: Path) -> Path | None:
    """Find clang-format/clang-tidy beside the compiler or on PATH."""
    local_tool = mingw_bin / f"{name}.exe"
    if local_tool.is_file():
        return local_tool
    found = shutil.which(name)
    return Path(found) if found else None


def build_environment(*, mingw_bin: Path, cmake_exe: Path, ninja_exe: Path) -> dict[str, str]:
    """Return an environment whose PATH prioritizes the selected toolchain."""
    env = os.environ.copy()
    path_entries = [mingw_bin, cmake_exe.parent, ninja_exe.parent]
    prefix = os.pathsep.join(str(entry) for entry in path_entries)
    current = env.get("PATH", "")
    env["PATH"] = prefix + (os.pathsep + current if current else "")
    return env


def tasklist_pids(image: str) -> list[int]:
    """Return PIDs for a Windows image name using tasklist CSV output."""
    try:
        result = subprocess.run(
            [
                "tasklist",
                "/FI",
                f"IMAGENAME eq {image}",
                "/FO",
                "CSV",
                "/NH",
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return []

    pids: list[int] = []
    for row in csv.reader(result.stdout.splitlines()):
        # tasklist emits "INFO: No tasks..." when nothing matches; those rows
        # do not have a PID column and are skipped here.
        if len(row) >= 2 and row[0].strip().lower() == image.lower():
            try:
                pids.append(int(row[1]))
            except ValueError:
                continue
    return pids


def running_clion_pids() -> list[int]:
    """Return all running CLion launcher PIDs."""
    return sorted(set(tasklist_pids("clion64.exe") + tasklist_pids("clion.exe")))


def close_clion(timeout: float = 30.0) -> bool:
    """Ask CLion to close and wait briefly for the process to exit."""
    pids = running_clion_pids()
    if not pids:
        return True

    info("Closing CLion (PIDs: " + ", ".join(str(pid) for pid in pids) + ")")
    # taskkill without /F sends WM_CLOSE, allowing CLion to save its state.
    for image in ("clion64.exe", "clion.exe"):
        subprocess.run(
            ["taskkill", "/IM", image],
            capture_output=True,
            text=True,
            timeout=30,
        )

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not running_clion_pids():
            return True
        time.sleep(0.5)
    return False


def ensure_clion_closed(close_without_prompt: bool) -> None:
    """Make sure CLion is not running before editing its workspace file."""
    if not running_clion_pids():
        return

    if close_without_prompt:
        if not close_clion():
            fail("CLion did not close. Close it manually, then rerun the script.")
        return

    if not sys.stdin.isatty():
        fail(
            "CLion is running. Close it before updating the CMake profile, "
            "or pass --close-clion."
        )

    info("CLion is running. Close it manually (File > Exit).")
    input("Press Enter after CLion has closed to continue...")

    if running_clion_pids():
        fail("CLion is still running. Close it completely, then rerun the script.")


def _get_attribute(tag: str, name: str) -> str | None:
    """Read an XML attribute from a single tag string."""
    match = re.search(rf'\b{re.escape(name)}="([^"]*)"', tag)
    return match.group(1) if match else None


def _set_attribute(tag: str, name: str, value: str) -> str:
    """Set an XML attribute, adding it to self-closing or opening tags."""
    pattern = re.compile(rf'(\b{re.escape(name)}=")[^"]*(")')
    if pattern.search(tag):
        return pattern.sub(
            lambda match: f"{match.group(1)}{value}{match.group(2)}",
            tag,
            count=1,
        )

    # Support both <tag ... /> and <tag ...> forms.
    if tag.endswith("/>"):
        return f'{tag[:-2].rstrip()} {name}="{value}" />'
    if tag.endswith(">"):
        return f'{tag[:-1].rstrip()} {name}="{value}">'
    raise ValueError(f"Cannot add attribute '{name}' to tag: {tag}")


def _remove_attribute(tag: str, name: str) -> str:
    """Remove an XML attribute if it exists."""
    return re.sub(rf'\s+{re.escape(name)}="[^"]*"', "", tag)


def find_game_exe() -> Path | None:
    """Return the existing GAME_EXE path from .env, if one is configured."""
    # Keep this parser intentionally small; the scripts only need one key.
    env_file = None
    for name in (".env", ".ENV", ".env.local"):
        candidate = PROJECT_ROOT / name
        if candidate.is_file():
            env_file = candidate
            break
    if env_file is None:
        return None

    try:
        text = env_file.read_text(encoding="utf-8-sig")
    except OSError:
        return None

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.strip().upper() not in {"GAME_EXE", "GAME_PATH"}:
            continue
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        game_exe = Path(value).expanduser()
        if not game_exe.is_absolute():
            game_exe = (PROJECT_ROOT / game_exe).resolve()
        return game_exe if game_exe.is_file() else None
    return None


def _new_run_configuration(game_exe: Path | None) -> str:
    """Build the CMake run configuration used to debug the ASI in the game."""
    working_dir = ""
    run_path = ""
    if game_exe is not None:
        # The target is a DLL, so CLion needs the host executable in RUN_PATH.
        # WORKING_DIR is a file:// URL, matching CLion's workspace format.
        working_dir = f' WORKING_DIR="file://{game_exe.parent.as_posix()}"'
        run_path = f' RUN_PATH="{game_exe.as_posix()}"'
    return (
        '    <configuration name="AltairFix" type="CMakeRunConfiguration" '
        'factoryName="Application" REDIRECT_INPUT="false" ELEVATE="false" '
        'USE_EXTERNAL_CONSOLE="false" EMULATE_TERMINAL="false"'
        f'{working_dir} PASS_PARENT_ENVS_2="true" PROJECT_NAME="AltairFix" '
        f'TARGET_NAME="AltairFix" CONFIG_NAME="{PRESET}"{run_path}>\n'
        "      <method v=\"2\">\n"
        '        <option name="com.jetbrains.cidr.execution.CidrBuildBeforeRunTaskProvider$BuildBeforeRunTask" enabled="true" />\n'
        "      </method>\n"
        "    </configuration>"
    )


def _ensure_run_configuration(text: str, game_exe: Path | None) -> str:
    """Add the AltairFix run configuration if the workspace has none yet."""
    if re.search(
        r'<configuration\b[^>]*type="CMakeRunConfiguration"[^>]*TARGET_NAME="AltairFix"',
        text,
    ):
        return text

    config = _new_run_configuration(game_exe)
    # Preserve the rest of the RunManager block and insert our config before
    # its closing tag.
    run_manager = re.search(
        r'(<component name="RunManager"[^>]*>)(.*?)(</component>)',
        text,
        re.DOTALL,
    )
    if run_manager:
        opening = run_manager.group(1)
        # Select the AltairFix config so the Debug button uses it by default.
        if 'selected="' in opening:
            opening = re.sub(
                r'selected="[^"]*"',
                'selected="CMake Application.AltairFix"',
                opening,
                count=1,
            )
        else:
            opening = opening[:-1] + ' selected="CMake Application.AltairFix">'
        body = run_manager.group(2).rstrip()
        new_body = f"{body}\n{config}\n  "
        return (
            text[: run_manager.start()]
            + opening
            + new_body
            + run_manager.group(3)
            + text[run_manager.end() :]
        )

    component = (
        '  <component name="RunManager" selected="CMake Application.AltairFix">\n'
        f"{config}\n"
        "  </component>\n"
    )
    return text.replace("</project>", component + "</project>")


def _new_profile(
    name: str, display_name: str, enabled: bool, generation_dir: str
) -> str:
    """Build a CLion CMake preset profile entry."""
    return (
        f'      <configuration PROFILE_NAME="{name}" '
        f'PROFILE_DISPLAY_NAME="{display_name}" '
        f'ENABLED="{"true" if enabled else "false"}" '
        f'FROM_PRESET="true" '
        f'GENERATION_DIR="$PROJECT_DIR$/{generation_dir}" />'
    )


def update_workspace() -> None:
    """Enable the MinGW 32-bit profile and point Debug at the game executable."""
    game_exe = find_game_exe()
    existed = WORKSPACE.is_file()
    if existed:
        # Keep a backup in case CLion needs its previous workspace state.
        original = WORKSPACE.read_text(encoding="utf-8")
        backup = WORKSPACE.with_name("workspace.xml.bak")
        backup.write_text(original, encoding="utf-8")
        text = original
    else:
        backup = None
        text = MINIMAL_WORKSPACE

    # Locate the CMakeSettings block; create one for a fresh workspace.
    settings_pattern = re.compile(
        r'(<component name="CMakeSettings">\s*<configurations>)'
        r"(.*?)"
        r"(</configurations>\s*</component>)",
        re.DOTALL,
    )
    match = settings_pattern.search(text)
    if not match:
        if '<component name="CMakeSettings">' in text:
            fail(
                "The CMakeSettings section in .idea/workspace.xml has an "
                "unrecognized format; refusing to modify it."
            )
        text = text.replace("</project>", MINIMAL_CMAKE_SETTINGS + "</project>")
        match = settings_pattern.search(text)
        if not match:
            fail("Could not add a CMakeSettings section to .idea/workspace.xml.")

    body = match.group(2)
    existing_profiles: set[str] = set()
    replacements: dict[str, str] = {}

    # Enable the mingw32 profile and disable every other profile so CLion does
    # not fall back to the stale MSVC configuration.
    for tag_match in re.finditer(r"<configuration\b[^>]*?/>", body):
        tag = tag_match.group(0)
        profile_name = _get_attribute(tag, "PROFILE_NAME")
        if not profile_name:
            continue

        existing_profiles.add(profile_name)
        if profile_name == PRESET:
            tag = _set_attribute(tag, "ENABLED", "true")
            tag = _set_attribute(tag, "PROFILE_DISPLAY_NAME", "MinGW 32-bit Debug")
            tag = _set_attribute(tag, "FROM_PRESET", "true")
            tag = _set_attribute(tag, "GENERATION_DIR", "$PROJECT_DIR$/build/mingw32")
            # Do not use the known-broken "MinGW (32 Bit)" CLion toolchain.
            tag = _remove_attribute(tag, "TOOLCHAIN_NAME")
        else:
            tag = _set_attribute(tag, "ENABLED", "false")

        replacements[tag_match.group(0)] = tag

    updated_body = body
    for old_tag, new_tag in replacements.items():
        updated_body = updated_body.replace(old_tag, new_tag, 1)

    # Add missing preset profiles so the workspace is usable on a fresh clone.
    additions: list[str] = []
    if PRESET not in existing_profiles:
        additions.append(
            _new_profile(
                PRESET,
                "MinGW 32-bit Debug",
                True,
                "build/mingw32",
            )
        )
    if RELEASE_PRESET not in existing_profiles:
        additions.append(
            _new_profile(
                RELEASE_PRESET,
                "MinGW 32-bit Release",
                False,
                "build/mingw32-release",
            )
        )
    if additions:
        updated_body = updated_body.rstrip() + "\n" + "\n".join(additions) + "\n    "

    text = text[: match.start(2)] + updated_body + text[match.end(2) :]

    # Point CMake run configurations at the enabled profile and give the
    # AltairFix config the game executable it needs for Debug.
    def update_run_configuration(tag_match: re.Match[str]) -> str:
        """Update one CMake run configuration tag in place."""
        tag = tag_match.group(0)
        if 'type="CMakeRunConfiguration"' not in tag:
            return tag
        config_name = _get_attribute(tag, "CONFIG_NAME")
        if config_name == "Debug":
            tag = _set_attribute(tag, "CONFIG_NAME", PRESET)
        elif config_name == "Release":
            tag = _set_attribute(tag, "CONFIG_NAME", RELEASE_PRESET)

        if game_exe is not None and _get_attribute(tag, "TARGET_NAME") == "AltairFix":
            tag = _set_attribute(tag, "RUN_PATH", game_exe.as_posix())
            tag = _set_attribute(
                tag, "WORKING_DIR", f"file://{game_exe.parent.as_posix()}"
            )
        return tag

    text = re.sub(r"<configuration\b[^>]*?>", update_run_configuration, text)
    # CLion normally generates this config itself; add it if it is missing.
    text = _ensure_run_configuration(text, game_exe)

    # Validate before writing; a corrupt workspace file can break CLion.
    try:
        ET.fromstring(text)
    except ET.ParseError as exc:
        fail(f"Refusing to write invalid XML to .idea/workspace.xml: {exc}")

    WORKSPACE.parent.mkdir(parents=True, exist_ok=True)
    WORKSPACE.write_text(text, encoding="utf-8")
    info(f"Enabled CLion CMake profile '{PRESET}' in {WORKSPACE}")
    if game_exe is not None:
        info(f"CLion debug executable: {game_exe}")
    else:
        warn(
            "GAME_EXE was not found in .env; the CLion run configuration has no "
            "executable path."
        )
    if backup is not None:
        info(f"Previous workspace backed up to {backup}")


def launch_clion(clion_exe: Path, env: dict[str, str]) -> None:
    """Launch CLion with the selected toolchain on PATH."""
    info(f"Launching CLion: {clion_exe}")
    if clion_exe.suffix.lower() == ".bat":
        command = ["cmd", "/c", str(clion_exe), str(PROJECT_ROOT)]
    else:
        command = [str(clion_exe), str(PROJECT_ROOT)]
    subprocess.Popen(command, cwd=str(PROJECT_ROOT), env=env)


def main(argv: list[str] | None = None) -> int:
    """Resolve the toolchain, configure/build the preset, and update CLion."""
    parser = argparse.ArgumentParser(
        description=(
            "Set up the 32-bit MinGW build for CLion, select the mingw32 "
            "CMake profile, and optionally launch CLion."
        )
    )
    parser.add_argument(
        "--clion",
        help="Path to CLion, CLion's bin directory, or clion64.exe",
    )
    parser.add_argument(
        "--mingw-bin",
        help="Directory containing i686-w64-mingw32-g++ (or a 32-bit g++)",
    )
    parser.add_argument(
        "--close-clion",
        action="store_true",
        help="Try to close a running CLion automatically (otherwise close it manually)",
    )
    parser.add_argument(
        "--skip-ide-settings",
        action="store_true",
        help="Configure/build but do not modify .idea/workspace.xml",
    )
    parser.add_argument(
        "--configure-only",
        action="store_true",
        help="Configure the preset but do not build",
    )
    parser.add_argument(
        "--no-launch",
        action="store_true",
        help="Do not launch CLion",
    )
    args = parser.parse_args(argv)

    if os.name != "nt":
        fail("This setup script targets Windows.")

    # Fail early with a clear message if the script is run outside the repo.
    if not (PROJECT_ROOT / "CMakeLists.txt").is_file():
        fail(f"CMakeLists.txt not found under {PROJECT_ROOT}.")
    if not (PROJECT_ROOT / "CMakePresets.json").is_file():
        fail(f"CMakePresets.json not found under {PROJECT_ROOT}.")

    # Resolve every external tool before touching the build or IDE state.
    clion_exe = find_clion(args.clion)
    cmake_exe = find_cmake(clion_exe)
    ninja_exe = find_ninja(clion_exe)
    mingw_bin, mingw_cxx, mingw_target = find_mingw(args.mingw_bin)
    env = build_environment(
        mingw_bin=mingw_bin,
        cmake_exe=cmake_exe,
        ninja_exe=ninja_exe,
    )

    # These tools are optional; CMake simply omits their targets when missing.
    clang_format = find_optional_tool("clang-format", mingw_bin)
    clang_tidy = find_optional_tool("clang-tidy", mingw_bin)
    if clang_format:
        info(f"Using clang-format: {clang_format}")
    else:
        warn("clang-format not found; the 'format' CMake targets will be disabled.")
    if clang_tidy:
        info(f"Using clang-tidy: {clang_tidy}")
    else:
        warn("clang-tidy not found; the 'lint' CMake target will be disabled.")

    info(f"Project: {PROJECT_ROOT}")
    info(f"Target: {mingw_target} via {mingw_cxx}")

    # CLion caches workspace.xml in memory, so close it before editing that
    # file or the changes will be overwritten on exit.
    if not args.skip_ide_settings:
        ensure_clion_closed(args.close_clion)
    elif running_clion_pids():
        warn(
            "CLion is running; close it if CMake reports that build files are in use."
        )

    # Use the project preset; the modified PATH lets the toolchain file find
    # the 32-bit compiler and the bundled format/lint tools.
    run_visible(
        [cmake_exe, "--preset", PRESET],
        cwd=PROJECT_ROOT,
        env=env,
    )

    if not args.configure_only:
        run_visible(
            [cmake_exe, "--build", "--preset", PRESET],
            cwd=PROJECT_ROOT,
            env=env,
        )

    # Only edit the IDE file after the configure/build steps have succeeded.
    if not args.skip_ide_settings:
        update_workspace()

    if not args.no_launch:
        launch_clion(clion_exe, env)
    else:
        info("--no-launch specified; CLion was not started.")

    info("Setup complete.")
    info(f"Build:      cmake --build --preset {PRESET}")
    info(f"Format:     cmake --build --preset {PRESET} --target format")
    info(f"Format check: cmake --build --preset {PRESET} --target format-check")
    info(f"Lint:       cmake --build --preset {PRESET} --target lint")
    info("Formatter:  python tools/format.py [--check]")
    info("ASI loader: python tools/setup_asi_loader.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
