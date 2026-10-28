# OhlsNamer (OLS Renamer) — v1.5.3

A small Win32 GUI tool that renames WinOLS `.ols` project files based on the metadata stored in their header.

New name format:  
`Brand Model ECU SoftwareVersion (definedMaps).ols`

For example: `BMW 320d EDC17C50 03G906016 (42).ols`

## Features

- Scans a folder (optionally recursively) for `.ols` files, including projects inside ZIP and RAR archives (never extracted)
- Extracts vehicle identity, ECU producer/model, hardware/software/production numbers, engine code, year, original source filename and WinOLS format version
- Normalizes common vehicle-brand and ECU-manufacturer aliases while preserving unknown values
- Uses an embedded source HEX/BIN filename when software metadata is missing or generic
- Counts defined (named, non-placeholder) maps
- Proposes clean, filesystem-safe names
- Disambiguates collisions inside each folder
- Keeps malformed headers and unvalidated map structures review-only; partial-identity proposals omit unknown placeholders and retain the original filename stem
- Renames in-place with a multi-pass algorithm that handles swaps and cycles via temporary names
- Writes a CSV log and recovery plan of the renames (can be switched off with **Save rename logs (CSV)**)
- Drag-and-drop a folder or file onto the window
- Cancelable scan
- Recent-directory dropdown remembers the last 10 successful scan folders
- Live result filtering by filename, vehicle/ECU identity and status
- Click any column header to sort by it (click again to reverse; remembered between launches)
- Select rows (Ctrl/Shift-click, Ctrl+A) and move, copy or extract them to another folder, optionally with their planned new names; nothing is ever overwritten
- “Only rename candidates” view for quickly reviewing large databases
- Parallel analysis on every logical processor (16 workers on a Ryzen 7 7800X3D/7700X), capped by free memory
- Fast map finding: reads and scans only the map/ID area of each project, plus the first ROM image for identity strings
- Restores the last-used directory and recursive-scan preference between launches
- Double-click a project to inspect parsed header fields, identity sources, map evidence and warnings

## Projects inside ZIP and RAR archives

With **Look inside ZIP/RAR archives** on, every `.zip` and `.rar` in the scanned folders is opened in place. The projects inside are analysed and renamed like plain files, without unpacking anything to disk. Archives of any size are supported; a 205 GiB archive scans in under a second. See [docs/ARCHIVES.md](docs/ARCHIVES.md) for the design and test evidence.

- **Reading.**
  - ZIP entries (stored or deflate, including Zip64 and data descriptors) are decoded straight from the archive.
  - RAR4/RAR5 entries, including solid archives, are decoded by the bundled UnRAR library.
  - Only each project's map/ID area is kept in memory.
- **Renaming.** A name lives only in an archive's headers, so renames rewrite headers and copy all compressed data byte for byte. Nothing is recompressed.
  - **ZIP in place:** when a new name fits its old local header, only the headers and central directory are rewritten (kilobytes, under a second even for 200 GB+). An undo journal (`archive.zip.olsrenamer-undo`) protects against power loss; an interrupted rename is rolled back automatically on the next scan.
  - **Full rewrite:** otherwise the archive is streamed to `archive.olsrenamer-tmp` beside it. Every copied byte is re-read and checksummed, then the original is replaced. This needs free space equal to the archive size.
  - With **Allow full archive rewrite** off, or without enough free space, ZIP names that fit are still renamed in place and the rest are reported.
  - **RAR:** WinRAR's `Rar.exe rn` is used when WinRAR is installed (it keeps recovery records). Otherwise the built-in header writer is used; it removes recovery-record and quick-open blocks (re-add a recovery record in WinRAR if you use one).
  - Hard links and file copies inside RAR5 follow the renamed entry.
- **Not supported (left untouched and reported):**
  - encrypted RAR headers;
  - multi-volume RAR sets;
  - split ZIP sets;
  - locked or signed RAR archives;
  - ZIP methods other than stored/deflate (analysis only);
  - `.7z` files.

## Large database performance

**Fast map finding.** Every project has a map/ID area (metadata, map directory, map names) followed by the embedded ECU ROM images. The validated M1 version directory gives the exact boundary (`version_data_start`). The loader reads the first 1 MiB, proves the boundary, then reads only the map/ID area plus the first ROM image; later ROM versions (tuned copies) are never read. Map-directory search and map parsing never leave the map/ID area. If the boundary cannot be proven from the prefix, the whole file is read, so results never depend on the prefix size.

**Workers.** Analysis is CPU-bound once files come from an SSD, so the pool uses every logical processor (16 on an 8-core/16-thread Ryzen 7 7800X3D or 7700X). Each worker is budgeted 256 MiB (largest corpus project peaked at 195 MiB) and at most half of the free RAM is used, so low-memory machines get fewer workers. Workers run below normal priority to keep the window responsive.

Measured on the 332-project corpus (AI-Dump `definitions/ols` + repository fixtures), single thread, analysis time only:

| | v1.3.18 | v1.4.0 |
|---|---:|---:|
| Analysis CPU time | 108.1 s | 24.6 s |
| Map-label classification | 59.1 s | 3.7 s |
| ECU-family text scan (all files) | 224.3 s | 16.3 s |
| Bytes read | 3.16 GB | 2.93 GB |

Results with 1, 2 and 16 workers, and with prefix loading versus full reads, are byte-for-byte identical. The 16-worker figure on a 7800X3D was not benchmarked here (the test machine has 2 cores); expect scaling close to the physical core count.

## Version 1.5.3

- Move, copy or extract selected rows to a folder (right-click or **Move / extract selected...**). Projects inside ZIP/RAR archives are extracted and CRC-checked without changing the archive.

## Version 1.5.1

- Sort the result list by clicking any column header; click again to reverse. The choice is remembered.

## Version 1.5.0

- Reads, counts and renames projects inside ZIP and RAR archives without extracting them, with in-place ZIP header renames and verified full rewrites for 200 GB+ archives.
- Re-scans of partial-identity names no longer nest bracketed old names, and review-only files keep their names reserved during collision planning.

## Version 1.4.0

- Counts valuable map names that importers store behind an ID-only name (`00004b0e`, `0x2`, `A0`, `n`, bare `Map`): 82,955 records in the corpus, e.g. Cummins CM2150E/CM2350A projects went from `(Nil)` to 5,631–14,867 maps.
- Excludes more WinOLS-generated junk: statistics captions (`1700 =8192`, `G 2000x1317 =<666`), `Map "Float"`, bare supplier/type labels (`Bosch 16`, `"Bosch 8" ????`), question-mark placeholders, `DEFAULT` and single characters.
- Fast map finding (map/ID area only), 4.4× faster analysis, workers sized to the CPU, and a **Save rename logs (CSV)** toggle.

## Version 1.3.18

- Keeps diagnostic trouble codes such as `P0420` out of proposed filenames.
- Prefers a source-filename software token that matches the production or hardware number.

## Version 1.3.17

- Stops treating manufacturer names as ECU-family prefixes, eliminating garbage slots such as `DENSO2005` and `Me-b1G`.
- Falls back to the hardware number when no ECU family is present so the proposed name still carries a part identifier.

## Version 1.3.8

- Removes repeated identity phrases even when one slot contains a longer phrase (for example, a make repeated at the start of the model).

## Version 1.3.7

- Uses embedded source filenames only to recover a plausible software/calibration identifier, rather than copying the whole descriptive filename into the software slot.
- Rejects generic source names such as `tunecenter` when no software identifier can be recovered, and prevents duplicate identity/software segments.

## Version 1.3.6

- Counts only human-named maps; generated supplier labels are excluded even if a description is present.
- Bounds version-directory discovery to avoid false dispatch from marker-like bytes deep in ROM payloads.
- Uses up to four bounded analysis workers and monotonic progress reporting for large databases.
- Adds live filtering and a rename-candidate-only view in the GUI.

## Version 1.3.5

- Adds a persistent recent-directory picker and restores the last-used directory and recursive-scan preference.
- Allows auto-renaming with partial identity when header, version-directory, size and map-directory checks pass.
- Omits unknown identity placeholders and retains the original filename stem in brackets when brand, model or ECU identity is missing.
- Adds regression tests for partial identity filename generation.

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
- `ols_archive.hpp` — ZIP/RAR parsing, streamed project capture, in-place and copy rename writers, undo journal
- `ols_archive_scan.hpp`, `ols_unrar.hpp/.cpp` — archive analysis and the UnRAR library adapter
- `archive_cli.cpp`, `archive_tests.cpp`, `tests/` — archive CLI and tests (`sh build_archive_cli.sh`)
- `third_party/unrar/` — UnRAR 7.0.7 source (extraction only, see THIRD_PARTY_NOTICES.md)
- `build.sh`, `app.rc`, `app.manifest` — build and resources

## Notes

The parser supports a wide range of WinOLS format versions and both uncompressed and zlib-compressed map directories. Unreadable or non-OLS files are left alone.

Automatic renaming requires a complete header, consistent declared size for modern files, explicit vehicle/ECU identity, and a structured map directory. Fallback-derived identities and map counts are review-only. A flushed recovery plan is written before the first move, with operation results appended as the batch runs. CSV logging checks write, flush and close results.

## License

MIT (see LICENSE if present; otherwise treat as MIT for this tool).
