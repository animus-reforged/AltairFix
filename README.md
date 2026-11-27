# AltairFix

**Enhancements & Fixes for _Assassin's Creed: Director's Cut Edition_**

_(Compatible with Steam, GOG & Ubisoft/Epic releases)_

## Overview

**AltairFix** is an ASI mod for the PC release of **Assassin's Creed: Director's Cut Edition**.

## Requirements

- Windows
- _Assassin's Creed: Director's Cut Edition_ (Steam, GOG, Ubisoft, or Epic)
- CMake 4.4+ and Ninja
- 32-bit MinGW-w64, such as the `i686-w64-mingw32-g++` compiler from llvm-mingw
- Python 3.10+ for the scripts in `tools/`

## Setup

1. Copy `.env.example` to `.env` and set the game executable:

   ```ini
   GAME_EXE=D:\Games\Assassin's Creed\GOG\AssassinsCreed_Dx9.exe
   ```

2. Set up the CLion/MinGW environment:

   ```powershell
   python tools/setup_clion.py
   ```

3. Install Ultimate ASI Loader next to the game executable:

   ```powershell
   python tools/setup_asi_loader.py
   ```

4. Build the mod:

   ```powershell
   cmake --preset mingw32
   cmake --build --preset mingw32
   ```

   In CLion, enable the **MinGW 32-bit Debug** CMake profile and press Build or Debug.

The ASI is deployed to `<game folder>\scripts\AltairFix.asi`. Ultimate ASI Loader is installed as `dinput8.dll` in `<game folder>`.

## Tooling

```powershell
python tools/format.py          # Format source/
python tools/format.py --check  # Verify formatting without changing files

python tools/setup_clion.py     # Configure CLion, build, set the debug executable
python tools/setup_asi_loader.py

python tools/generate_changelog.py <since-sha> --output release_notes.md
```

CMake targets `format`, `format-check`, and `lint` are also available. The changelog generator groups `feat:` and `fix:` commits and skips docs/CI/build-only changes.

## Releasing

Pushing to a branch builds and uploads a nightly artifact. To publish, run the **Build release** workflow and choose `Nightly`, `Stable`, or `Both`. Nightly releases can only be created from `main` or `dev`; stable releases can only be created from `main`. Stable/Both require a tag such as `v0.1.0`, and stable releases are created as drafts.

The build produces `AltairFix-artifact.zip` with the `scripts/` layout for testing, plus a flat `AltairFix.zip` release package with a SHA-256 checksum. Release notes are generated from `tools/generate_changelog.py` during release creation.

## Debugging

The CLion **AltairFix** run configuration uses the game executable from `.env`, so **Debug** builds the ASI and launches the game. Breakpoints in `source/dllmain.cpp` bind when Ultimate ASI Loader loads the ASI.

## Credits

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) - mod loading backbone

## License

AltairFix is licensed under the **GNU General Public License v3.0 or later**. See [LICENSE](LICENSE).
