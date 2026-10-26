# OhlsNamer (OLS Renamer) — v1.3

A small Win32 GUI tool that renames WinOLS `.ols` project files based on the metadata stored in their header.

New name format:  
`Brand Model ECU SoftwareVersion (definedMaps).ols`

For example: `BMW 320d EDC17C50 03G906016 (42).ols`

## Features

- Scans a folder (optionally recursively) for `.ols` files
- Extracts vehicle identity, ECU producer/model, hardware/software/production numbers, engine code, year, original source filename and WinOLS format version\n- Normalizes common vehicle-brand and ECU-manufacturer aliases while preserving unknown values\n- Uses an embedded source HEX/BIN filename when software metadata is missing or generic
- Counts defined (named, non-placeholder) maps
- Proposes clean, filesystem-safe names
- Disambiguates collisions inside each folder
- Keeps uncertain identity/map results review-only rather than silently batch-renaming them\n- Renames in-place with a multi-pass algorithm that handles swaps and cycles via temporary names
- Writes a CSV log of the renames
- Drag-and-drop a folder or file onto the window
- Cancelable scan

## Version 1.3\n\n- Added a broad vehicle-brand and ECU-manufacturer catalogue, alias normalization, and ECU-family manufacturer inference.\n- Added embedded HEX/BIN filename fallback for unidentified software metadata.\n- Exposed additional identification factors through the parser and CLI.\n- Added header/size validity states, filename length limits, safer automatic rename eligibility and checked log writes.\n\nThe catalogue is extensible, not literally exhaustive; unrecognized names are preserved rather than discarded.\n\n## Building

### On Linux / WSL (cross-compile to Windows)

```bash
sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 libz-mingw-w64-dev
./build.sh
```

This produces `OLS Renamer.exe`.

### Test the core parser (no GUI)

```bash
g++ -std=c++17 -o core_cli core_cli.cpp -lz
./core_cli some-project.ols
```

## Source layout

- `ols_core.hpp` — platform-independent OLS header + map-directory parser (port of TypeScript logic)
- `ols_fs.hpp` — Windows filesystem scan, analysis, rename planning and execution, CSV log
- `olsrenamer.cpp` — Win32 GUI
- `core_cli.cpp` — small command-line tester
- `build.sh`, `app.rc`, `app.manifest` — build and resources

## Notes

The parser supports a wide range of WinOLS format versions and both uncompressed and zlib-compressed map directories. Unreadable or non-OLS files are left alone.

Automatic renaming requires a complete header, consistent declared size for modern files, explicit vehicle/ECU identity, and a structured map directory. Fallback-derived identities and map counts are review-only. CSV logging checks write, flush and close results.

## License

MIT (see LICENSE if present; otherwise treat as MIT for this tool).
