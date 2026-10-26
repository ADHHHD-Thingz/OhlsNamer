# OhlsNamer (OLS Renamer) — v1.3.4

A small Win32 GUI tool that renames WinOLS `.ols` project files based on the metadata stored in their header.

New name format:  
`Brand Model ECU SoftwareVersion (definedMaps).ols`

For example: `BMW 320d EDC17C50 03G906016 (42).ols`

## Features

- Scans a folder (optionally recursively) for `.ols` files
- Extracts vehicle identity, ECU producer/model, hardware/software/production numbers, engine code, year, original source filename and WinOLS format version
- Normalizes common vehicle-brand and ECU-manufacturer aliases while preserving unknown values
- Uses an embedded source HEX/BIN filename when software metadata is missing or generic
- Counts defined (named, non-placeholder) maps
- Proposes clean, filesystem-safe names
- Disambiguates collisions inside each folder
- Keeps uncertain identity/map results review-only rather than silently batch-renaming them
- Renames in-place with a multi-pass algorithm that handles swaps and cycles via temporary names
- Writes a CSV log of the renames
- Drag-and-drop a folder or file onto the window
- Cancelable scan
- Double-click a project to inspect parsed header fields, identity sources, map evidence and warnings

## Version 1.3.4

- Strengthens M1 version-directory dispatch when a false marker-like sequence inside ROM data has plausible lengths and ends at EOF.
- Excludes supplier-only generic labels such as “Bosch map”, “Denso map” and “Siemens map” from the human-named map count.

## Version 1.3.3

- Validates the M1 embedded-ROM version directory and uses its data-start offset to bound map-marker scanning, preventing marker-like bytes in ROM/version payloads from being dispatched as map records.
- Exposes validated version count, record size, and data start in CLI diagnostics.
- Adds regression tests for invalid version-array dimensions and fake map markers inside ROM bytes.

## Version 1.3.2

- Added a broad vehicle-brand and ECU-manufacturer catalogue, alias normalization, and ECU-family manufacturer inference.
- Added embedded HEX/BIN filename fallback for unidentified software metadata, including ANSI and UTF-16LE filename strings.
- Used the selected source filename for vehicle-brand and ECU-manufacturer inference when the dedicated original-file field is missing or invalid.
- Expanded vehicle and ECU-supplier catalogues and added common filename aliases such as VW and Mercedes-Benz.
- Filtered alphabetic prose false positives from ECU-family detection and ranked numeric family candidates by specificity and source order.
- Exposed additional identification factors through the parser and CLI.
- Added header/size validity states, filename length limits, safer automatic rename eligibility and checked log writes.

The catalogue is extensible, not literally exhaustive; unrecognized names are preserved rather than discarded.

## OLS format investigation

The naming parser is intentionally conservative and is **not** a complete OLS decoder. An independently developed, zero-clause-licensed C parser is vendored under `third_party/ols-parser-public/` for deeper inspection of metadata, version/ROM segments, maps, axes and cell layout.

- [OLS format research notes](docs/OLS-FORMAT-RESEARCH.md) distinguish observed structure from inferred or unknown semantics.
- CI compiles both parsers.
- The private AI-Dump corpus workflow is used for real-project validation.

This is a reverse-engineered, version-dependent format, not an official EVC specification. We do not claim every reserved byte or flag bit is understood.

## Building

### On Linux / WSL (cross-compile to Windows)

```bash
sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 libz-mingw-w64-dev
sh ./build.sh
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

Automatic renaming requires a complete header, consistent declared size for modern files, explicit vehicle/ECU identity, and a structured map directory. Fallback-derived identities and map counts are review-only. A flushed recovery plan is written before the first move, with operation results appended as the batch runs. CSV logging checks write, flush and close results.

## License

MIT (see LICENSE if present; otherwise treat as MIT for this tool).
