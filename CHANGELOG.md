# Changelog

## 1.3.5

- Added persistent recent-directory picker and restored last-used scan settings.
- Enabled conservative auto-renaming when some identity fields are missing but structural evidence is valid.
- Omitted unknown placeholders and retained the existing filename stem when identity is incomplete.
- Added regression tests for partial identity filename construction.


## 1.3.4

- Hardened M1 version-directory selection: when multiple structurally plausible candidates exist, prefer the one whose data start is corroborated by the M3 marker, rather than a false M1-like sequence inside a ROM payload that happens to calculate to EOF.
- Retained compatibility fallback for schema variants without M3 at the version-data start; M3 is corroborating evidence, not treated as universally mandatory.
- Excluded supplier-only generic map labels such as “Bosch map”, “Denso map”, and “Siemens map” from the defined/human-named map count.
- Added regression tests for false M1 collisions and generic supplier map labels.

## 1.3.3

- Added bounds-checked M1 version-directory parsing for version count, data start and per-version record size.
- Bounded map-marker search and record parsing to the pre-ROM region whenever a valid version directory is found, preventing ROM bytes from being mistaken for map-directory dispatch markers.
- Exposed version-array diagnostics in the CLI and added regression tests for invalid dimensions and false markers inside ROM payloads.
- This is a safety correction to dispatch boundaries, not a claim that all version-record or map-record semantics are fully decoded.

## Unreleased — OLS format research tooling

- Added an independently developed OLS parser as a development-time reference for metadata, version/ROM segments, and calibration-map records.
- Added version-aware research notes separating observed layout from inferred semantics and tracking unknown flag bits and reserved fields.
- Added CI compilation for the independent parser. Full-corpus differential validation remains an evidence-gathering step, not a claim of complete bit-level understanding.

## 1.3.2

- Fixed ECU-family detection accepting alphabetic prose such as "Meine" as a valid "ME" family and consequently inferring Bosch incorrectly.
- ECU-family candidates must contain digits; dotted/structured family identifiers receive higher confidence, with source position used as a deterministic tie-breaker.
- Added a regression test using "Meine Kennfelder ME7910 ME7.9.10".

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
