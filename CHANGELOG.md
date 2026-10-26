# Changelog

## 1.3.1

- Fixed source-filename inference ordering: when the dedicated original-file field is absent or unusable, the scanned embedded HEX/BIN filename now informs vehicle-brand and ECU-manufacturer inference as well as software fallback.
- Added UTF-16LE printable-run support to embedded HEX/BIN filename scanning while retaining Windows-1252/ANSI scanning.
- Expanded vehicle makes across passenger, commercial, motorcycle and off-highway categories, and expanded ECU/module supplier aliases.
- Added regression coverage for VW/Mercedes-Benz filename aliases, source-file fallback selection, UTF-16LE scanning and additional ECU supplier names.
- Inferred or incomplete identity remains review-only; no claim of a literally exhaustive universal catalogue is made.

## 1.3.0

- Expanded vehicle-brand and ECU-manufacturer catalogues with common aliases.
- Added ECU-family manufacturer inference when the producer field is blank.
- Added embedded source HEX/BIN filename fallback for generic or missing software metadata.
- Exposed original file, hardware, production, engine, year, ECU producer and software-source evidence in parser/CLI output.
- Added header completeness and declared-size consistency states.
- Restricted automatic renaming to complete, high-confidence metadata and structured map-directory results.
- Capped filenames and checked log write/flush/close results.

The catalogue is broad and extensible, but cannot be literally exhaustive across every historical, regional, aftermarket and future manufacturer. Unknown names are preserved.
