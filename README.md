# AltairFix

**Enhancements & Fixes for _Assassin's Creed: Director's Cut Edition_**
_(Compatible with Steam, GOG & Ubisoft/Epic releases)_

## Overview

**AltairFix** is an ASI mod for the PC release of **Assassin's Creed: Director's Cut Edition**. It applies targeted compatibility fixes and provides an in-game debug overlay.

## Supported Executables

GOG/Steam and Ubisoft/Epic releases ship different executables. AltairFix detects the renderer from the import table and matches these builds:

| Edition      | Executable                | MD5                                | SHA-1                                      |
| ------------ | ------------------------- | ---------------------------------- | ------------------------------------------ |
| GOG/Steam    | `AssassinsCreed_Dx9.exe`  | `8E72C3333743780E43BC2C34BBF625F9` | `A9EA476E70F076709CCA59A802E26EC894432120` |
| GOG/Steam    | `AssassinsCreed_Dx10.exe` | `CA87753255E2D14B1F18BB737C643792` | `601475FD5EFA02BBAE80D1C06DFA25A0A1E7FD2F` |
| Ubisoft/Epic | `AssassinsCreed_Dx9.exe`  | `C85473CC4F2A5D1391A6D77BCEAF276E` | `B3FFF1737CF77BC3EF5BB0EDE6905B40C8F4E476` |
| Ubisoft/Epic | `AssassinsCreed_Dx10.exe` | `86924B021DDEFDECD96EBEA48AA79701` | `D56A31195D34D9BAAA79B1A3EB445422A7031760` |

Ubisoft/Epic launches through `AssassinsCreed_Game.exe`, a selector that picks DX10 whenever a D3D10 adapter is available. AltairFix builds a tiny `d3d10.dll` shim that makes that probe fail, so the selector starts `AssassinsCreed_Dx9.exe` instead. No game executable is patched. Install it with `python tools/setup_dx9_shim.py` (use `--remove` to uninstall).

## Features

- **Renderer Detection** — Identifies DX9 or DX10 from the executable's import table.
- **Renderer Reset Crash Guard** — Hooks two renderer cleanup checks and skips invalid resource pointers.
- **High Core Count Fix** - Spoofs `GetSystemInfo` and `GetProcessAffinityMask` to report at most 31 cores.
- **DX9 Launch Fix** - Installs a D3D10 detection shim so the Ubisoft selector starts DX9.

## Configuration

Settings live in `AltairFix.toml` next to the ASI:

```text
<game folder>\scripts\AltairFix.toml
```

The file is created with defaults when missing. Missing keys are added back on load, preserving comments and unknown settings.

```toml
[metadata]
version = "1.0.0"

[debug]
enable_logging = true
log_level = 2

[fixes]
renderer_crash_guard = true
cpu_core_limit = true
```

`log_level` uses 0 = Trace through 6 = Off. The overlay's **Fixes** tab toggles the fix settings and writes them back to this file.

## Development

### Requirements

- Windows
- _Assassin's Creed: Director's Cut Edition_ (Steam, GOG, Ubisoft, or Epic)
- CMake 4.4+ and Ninja
- 32-bit MinGW-w64 with C++23 support, such as `i686-w64-mingw32-g++` from llvm-mingw
- Git, for the submodules in `external/`
- Python 3.10+ for the scripts in `tools/`

### Building

1. Fetch the third-party submodules (spdlog, toml++, safetyhook, Zydis, and Dear ImGui):

    ```powershell
    git submodule update --init --recursive
    ```

2. Copy `.env.example` to `.env` and set the game executable:

    ```ini
    GAME_EXE=D:\Games\Assassin's Creed\GOG\AssassinsCreed_Dx9.exe
    ```

3. Set up the CLion/MinGW environment and install Ultimate ASI Loader:

    ```powershell
    python tools/setup_clion.py
    python tools/setup_asi_loader.py
    ```

4. Build the mod:

    ```powershell
    cmake --preset mingw32
    cmake --build --preset mingw32
    ```

In CLion, enable the **MinGW 32-bit Debug** CMake profile. The ASI is deployed to `<game folder>\scripts\AltairFix.asi`; Ultimate ASI Loader installs as `dinput8.dll` in the game folder. `git clone --recurse-submodules <url>` performs step 1 automatically.

### Tooling

```powershell
python tools/format.py          # Format source/
python tools/format.py --check  # Verify formatting without changing files
python tools/setup_dx9_shim.py  # Install the DX9 launch shim (--remove to uninstall)
python tools/generate_changelog.py <since-sha> --output release_notes.md
```

CMake targets `format`, `format-check`, and `lint` are also available. The changelog generator groups `feat:` and `fix:` commits and skips docs/CI/build-only changes.

### Releasing

Pushing to a branch builds and uploads a nightly artifact. To publish, run the **Build release** workflow and choose `Nightly`, `Stable`, or `Both`. Nightly releases can only be created from `main` or `dev`; stable releases can only be created from `main`. Stable releases require a tag such as `v0.1.0` and are created as drafts.

The build produces `AltairFix-artifact.zip` with the `scripts/` layout for testing, plus a flat `AltairFix.zip` release package with a SHA-256 checksum. Release notes are generated during release creation.

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) - mod loading backbone
- [spdlog](https://github.com/gabime/spdlog) - fast C++ logging library
- [toml++](https://github.com/marzer/tomlplusplus) - TOML parser
- [safetyhook](https://github.com/cursey/safetyhook) - inline and vtable hooking
- [Zydis](https://github.com/zyantific/zydis) - x86 disassembler used by safetyhook
- [ImGui](https://github.com/ocornut/imgui) - in-game debug/settings UI

## License

AltairFix is licensed under the **GNU General Public License v3.0 or later**. See [LICENSE](LICENSE).
