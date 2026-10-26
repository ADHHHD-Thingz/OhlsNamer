# Changelog

## 1.3.0

- Expanded vehicle-brand and ECU-manufacturer catalogues with common aliases.
- Added ECU-family manufacturer inference when the producer field is blank.
- Added embedded source HEX/BIN filename fallback for generic or missing software metadata.
- Exposed original file, hardware, production, engine, year, ECU producer and software-source evidence in parser/CLI output.
- Added header completeness and declared-size consistency states.
- Restricted automatic renaming to complete, high-confidence metadata and structured map-directory results.
- Capped filenames and checked log write/flush/close results.

The catalogue is broad and extensible, but cannot be literally exhaustive across every historical, regional, aftermarket and future manufacturer. Unknown names are preserved.
