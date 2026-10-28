# Changelog

## 1.5.3

- **Fixed: extracting from a RAR (or rewritten ZIP) after renaming inside it.** The scan's archive listing was not refreshed after an in-archive rename. Extraction then asked UnRAR for the old entry names and every renamed RAR project failed with "Entry was not decoded"; a rewritten ZIP failed with "Missing local file header". Extraction now re-reads the archive headers itself and matches each project by entry position and size, and a rename refreshes the stored listing.
- **More robust RAR entry matching:** RAR5 file versions (WinRAR `-ver`, which UnRAR reports as `name;1`) and legacy RAR4 names in a different OEM code page no longer cause a mismatch. A remaining mismatch now gives a specific message instead of "Entry was not decoded".
- Verified: under Wine, scan → rename inside archives → extract on RAR5, solid RAR5, RAR5 with recovery record and ZIP, 108 rows, all byte-identical (1.5.2: 83 of 108 failed). Regression unit tests added.

## 1.5.2

- **Move, copy and extract selected rows.** The list now supports multiple selection: Ctrl/Shift-click, or Ctrl+A to select all. Use **Move / extract selected...** or the right-click menu.
  - **Move / extract selected to folder...** moves plain files (across drives too) and extracts projects out of ZIP/RAR archives.
  - **Copy / extract selected to folder...** copies plain files instead and also extracts archive projects.
  - **Use the planned new names** (remembered) applies the proposed name while moving or extracting. It only applies to rows that are planned for renaming, never to review-only proposals.
  - **Never overwrites:** a name already used in the destination, or by another file in the same batch, gets " 2", " 3", and so on.
  - **Extraction:** each project is decoded straight from the archive into `<name>.olsrenamer-part`, then checked (ZIP: size and CRC-32; RAR: UnRAR's checksum). It is renamed into place only if the check passes, so corrupt data or a cancel leaves no file behind. The archive is never modified. Solid RAR archives are decoded in one pass.
  - With **Save rename logs (CSV)** on, an `OLS Renamer transfer log <date>.csv` (operation, source, target, result) is written in the destination folder.
  - Moved rows show their new location; rescan to plan renames in the new folder. Cancel stops between files.
- `archive_cli extract ARCHIVE DESTDIR` extracts every project (used by the tests).
- **Verified:**
  - Every archive type in the integration test extracts byte-identical projects, the archives stay unchanged, and re-extracting adds numbered copies instead of overwriting.
  - Four projects stored before and after a 205 GiB entry extracted in 0.11 s, byte-identical.
  - Unit tests for no-overwrite and CRC rejection pass, also under ASan/UBSan.
  - Under Wine, a move, a copy and an extraction (44 rows each from a ZIP, a solid RAR and plain files) gave byte-identical results, with plain sources moved and archives untouched.


## 1.5.1

First published release since 1.3.18. It includes everything in 1.4.0 and 1.5.0 below.

New in 1.5.1:
- **Sort by any column.** Click a column header (File, New name, Maps, Status) to sort by it; click again to reverse.
  - The header shows the direction, and the choice is remembered between launches.
  - File, New name and Status use natural (Explorer-style) ordering; Maps sorts numerically.
  - Rows without a value (unreadable projects, unchanged names) stay at the bottom in both directions, and ties are ordered by path.
  - The selected row stays selected and in view when the list is re-sorted or filtered.

Highlights of 1.5.0:
- Reads, counts maps in, and renames WinOLS projects inside ZIP and RAR archives without extracting them, including 200 GB+ archives.
- ZIP names that fit are renamed in place in under a second, protected by an undo journal. Otherwise the archive is rewritten as a verified copy.
- RAR renames use WinRAR when it is installed, otherwise a built-in header writer.
- New options: **Look inside ZIP/RAR archives** and **Allow full archive rewrite**.

Highlights of 1.4.0:
- Counts 82,955 previously missed map names stored behind ID-only names (Cummins and GM projects no longer show `(Nil)`), and excludes WinOLS statistics captions and other generated junk.
- Fast map finding scans only the map/ID area of each project; analysis is 4.4× faster.
- Analysis uses every logical processor (16 workers on a Ryzen 7 7800X3D).
- New **Save rename logs (CSV)** toggle.


## 1.5.0

Archives (ZIP and RAR), read and renamed without extraction:
- **Scanning.** Every `.zip`/`.rar` found is listed from its central directory or block headers. Its `.ols` projects become normal rows, shown as `archive.zip › folder/name.ols`.
- **Analysis.** Decoding goes into memory only and keeps just the map/ID area, using the same load plan as plain files.
  - ZIP projects are analysed in parallel.
  - Each RAR archive is decoded in one sequential pass, which solid archives require, using the bundled UnRAR 7.0.7 library.
  - On the test archives, every in-archive map count equals the plain-file count.
- **ZIP renaming.**
  - In place when the new name fits the old local header: a 0xD935 padding extra field absorbs the difference. Only the headers and central directory are rewritten, behind a CRC-protected undo journal that is rolled back automatically after a crash.
  - Otherwise by a streamed copy with re-read CRC verification of every copied span, then an atomic replace.
  - Zip64, data descriptors, SFX stubs, comments and extra fields are preserved. Info-ZIP Unicode Path fields are dropped from renamed entries. Non-ASCII names set the UTF-8 flag. OEM-encoded folders keep their bytes.
- **RAR renaming.** Uses WinRAR's `Rar.exe rn` when installed, otherwise a built-in writer for RAR4 and RAR5 headers. The built-in writer:
  - recomputes header CRCs and copies packed data verbatim;
  - removes quick-open and recovery-record blocks and the main-header locator;
  - retargets RAR5 hard links and file copies to renamed entries;
  - refuses encrypted headers, multi-volume sets, and locked or signed archives.
  - The RAR writer is written from RARLAB's published format notes; UnRAR is used only for reading.
- **Free space and rewrites.** Free space is checked before a full rewrite. The new **Allow full archive rewrite** option (and low disk space) falls back to renaming only the names that fit in place.
- **GUI.**
  - New **Look inside ZIP/RAR archives** and **Allow full archive rewrite** options, remembered between launches.
  - Archive progress is shown during renames, and Cancel stops an archive rewrite (the archive stays untouched).
  - The details dialog shows the archive and inner path.
- **Verification.**
  - Archives made with `zip`, 7-Zip, Python (Zip64, data descriptors) and RAR 7 were scanned, renamed in place and by copy, then validated with `unzip -t`, `7z t`, Python `testzip`, `unrar t` and `rar t`, with byte-identical extracted contents.
  - 90 rename runs across libarchive's RAR4/RAR5 sample archives passed (SFX, solid, filters, PPMd, Unicode, hard links, recovery records).
  - A 205 GiB Zip64 archive scanned in 0.43 s and renamed in place in 0.05 s.
  - 26,000 mutated archives were fuzzed under ASan/UBSan.
- **Limits.**
  - ZIP methods other than stored/deflate are listed but not analysed.
  - `.7z` is not supported.
  - Solid RAR analysis must decode everything before each project.

Fixes:
- `finalProposedName` (now in the core) no longer nests `[old name]` tags when re-scanning its own output.
- Review-only files keep their current names reserved during collision planning. Previously another file could be planned onto the name, which made the batch preflight fail.
- No empty CSV log is written when nothing was renamed.


## 1.4.0

Naming (verified against all 305 AI-Dump `definitions/ols` projects plus the 27 repository fixtures; 3.42M map records):
- Count a map when its name is only a record ID (decimal/hex ID, address pair, one-letter index or symbol such as `00004b0e`, `0x1b2f / 0x1b2f`, `A0`, `N0`, `n`) or a bare record word (`Map`, `Kennfeld`) and the description is a real label. 82,955 previously skipped records are now counted; 11 projects (Cummins CM2150E/CM2220C/CM2350A, GM E69) no longer show `(Nil)`. Generated supplier names (`Denso Map`) still never borrow a description.
- Exclude WinOLS potential-map statistics captions (`1700 =8192`, `7500x120c =8192/0`, `G 2000x1317 =<666`, `95% =0`, `10%`), quoted type labels without a supplier family (`Map "Float"`), bare supplier/type captions (`Bosch 16`, `Denso Float`, `"Bosch 8"  ????`), question-mark-only labels (`Map ???`, `???`), `DEFAULT` and single-character labels. 290 previously counted junk records are now excluded.
- No other verdict changed across the corpus: the faster classifier reproduces every previous decision outside these rules.
- Reject hexadecimal addresses/sizes (`0x200000`) as software IDs from source filenames.
- ECU family and source-filename fallbacks scan the map/ID area and the first ROM image only. In the ROM image a family must use an upper-case prefix and a filename keeps only its ASCII tail, removing junk such as `dCM2`, `edc5H`, `Me9.0001` and `c€ÔX¾.Hex` while keeping real ROM IDs (`EDC16C31`, `ME7.1`, `CAFT5F04.DAT`).
- Re-scanning files this tool already renamed no longer nests the old name, e.g. `Chrysler Voyager 0281010814 [Chrysler Voyager 0281010814 (17)] (17).ols`.

Performance:
- Fast map finding: the loader reads 1 MiB, proves the map/ID-area boundary (M1 `version_data_start`) and reads only the map/ID area plus the first ROM image. It falls back to a full read when the boundary is unproven; results are identical to full reads on the whole corpus.
- Map-label classification gates every regex behind cheap necessary-condition tests and memoizes verdicts per worker thread (59.1 s → 3.7 s on the corpus).
- ECU-family detection uses a byte prefilter and runs the unchanged regex only at candidate positions (224 s → 16 s whole-file; identical results on all 332 projects).
- Source-filename scanning skips strings that cannot contain an extension.
- Worker pool uses all logical processors (16 on a Ryzen 7 7800X3D/7700X) capped at half of free RAM / 256 MiB per worker, below-normal priority; buffers are reused per worker and only the 40 map records shown in the details dialog are kept per project.

GUI:
- New **Save rename logs (CSV)** checkbox (remembered between launches). When off, neither the recovery plan nor the rename log is written.
- Details dialog shows the map/ID area, map-directory offset, first ROM image range and bytes read.
- Fixed the multi-character `L'\\0'` literal used for registry/combo-box string termination.


## 1.3.18

- Keep diagnostic trouble codes out of proposed filenames. Standard OBD codes such as `P0420`, `C0035` and `U0100` are rejected as software-version candidates and omitted from every filename slot.
- Prefer a source-filename token whose digits appear in the production or hardware number when choosing a software fallback, so a related software ID (for example `SW37805` next to production `37805-RRF-Q120`) wins over an unrelated later token.
- Add regression coverage for DTC rejection and production-linked software selection.

## 1.3.17

- Stop treating manufacturer names (Denso, Keihin, Temic, Kefico, Transtron) as ECU-family prefixes and require a digit immediately after the family prefix. This prevents garbage ECU slots such as `DENSO2005`, `Denso89663-26432` and `Me-b1G`.
- When no ECU family is present, use a non-empty hardware number as the ECU identity token so projects still receive a useful part identifier (for example Bosch `038906012L` or Denso `89663-26432`).
- Add regression coverage for the tighter family inference.

## 1.3.16

- Normalize single-quoted WinOLS generated supplier labels (for example, `Map 'Bosch II 16'`) before placeholder detection, fixing inflated defined-map counts in Audi Q3 EDC17C46 CM/CN projects.
- Add regression tests for single-quoted Bosch/Denso family labels and update fixture expectations so both Q3 projects count zero human-named maps.
- Preserve descriptive labels and double-quoted placeholder handling.

## 1.3.15

- Exclude generated layout/type labels such as `Kennfeld "Denso Float"` and `Kennfeld "Bosch III 16/8"` from defined-map counts.
- Add regression tests for these WinOLS-generated labels while preserving descriptive map names.
- Extend CI map-count audits to all root-level OLS projects and assert expected counts for all ten uploaded files.

## 1.3.14

- Exclude bare supplier/family labels such as `Bosch II` from real-map counts while preserving descriptive names such as `Bosch II torque limiter`.
- Add regression coverage for supplier/family placeholders in both name and description fields.

## 1.3.13

- Reject autogenerated statistical map labels, including WinOLS variants such as `Statistich`, from the real-map count.
- Avoid unnecessary sequential recovery scans when a complete map directory was parsed, improving batch naming performance.
- Add regression tests for statistical placeholder labels.

## 1.3.12

- Count meaningful map descriptions when the explicit map-name field is empty, fixing projects where valid map labels such as DTC-off, torque, boost and injection maps were all counted as skipped.
- Continue excluding autogenerated labels, including supplier/family placeholders such as `Map "Bosch II 16"`, `Kennfeld "Denso Vertikal 16"`, and `Map "Statistical 16"`.
- Add regression tests for description-backed labels and a 17-project OLS corpus audit with expected defined-map counts.
- Publish full parser map audits as CI artifacts for review.

## 1.3.11

- Search up to 64 structurally plausible map-directory markers instead of stopping after eight, preventing early false markers from hiding the real map directory.
- Keep projects with partially parsed map directories review-only so incomplete records cannot silently produce undercounted filenames.
- Add regression coverage for marker candidates beyond the previous limit and for incomplete map-directory status.

## 1.3.10

- Fix additional false-positive map counts from autogenerated hexadecimal address pairs and one-letter indexes such as `A0`, `A1` and `A2`; human-readable identifiers remain countable.
- Keep heuristic fallback map parsing bounded to the serialized header when the M1 version directory cannot be validated, so marker-like payload bytes cannot extend the scan into ROM data.
- Add a low-allocation fast path for validating legacy (pre-330) map-record prefixes during fallback discovery.
- Add regression coverage for generated labels, pre-330 schema gates, M1 dispatch collisions, malformed inputs and sanitizer checks.

## 1.3.9

- Exclude WinOLS-generated supplier placeholders even when they include map indices, hexadecimal IDs, dimensions, bit width or endian labels (e.g. “Bosch map 2: 16 bit / 16x16”), while retaining genuinely human-authored labels such as “Bosch map torque limiter”.
- Apply the same placeholder rule to localized supplier labels such as “Bosch Kennfeld” and “Siemens Kennlinie”.
- Keep the map count based on human-named map names only; a populated description never substitutes for a missing or generated name.
- Accelerate M1 version-directory dispatch with optimized candidate-byte searching while retaining structural validation and preference for corroborated M3 directories over marker-shaped payload collisions.
- Reuse the already validated M1 version directory during map extraction instead of parsing it a second time.
- Cache the Bosch software-part-number regular expression rather than recompiling it for every OLS project.
- Preserve the four-worker cap; further concurrency changes should be guided by benchmarks on representative 4,000+ file corpora and peak-memory measurements.
- Add regression cases for generated-label variants, human-name preservation, M1 collisions, malformed input and schema-boundary dispatch.


## 1.3.8

- Remove repeated identity phrases embedded in longer metadata slots, such as a make repeated inside the model name.
- Keep software fallback limited to a plausible identifier extracted from the embedded source filename; descriptive names and ECU identifiers are not copied into the software slot.
- Add regression coverage for partial phrase overlap and duplicate software/ECU slots.


## 1.3.7

- Treat embedded original HEX/BIN names as evidence only, not as ready-made software-version strings.
- Promote only a version-like identifier from the source basename; discard descriptive/vendor-only names such as `tunecenter` and exclude identifiers already represented by the ECU/identity slots.
- Deduplicate repeated filename segments and add regression tests for source-name junk, software-code extraction and repeated slots.


## 1.3.6

- Count only human-named map labels; generated supplier placeholders such as “Bosch map”, “Denso map”, and “Siemens map 2” do not inflate the map count, even when a description is populated.
- Bound M1 version-directory discovery to the serialized header region to avoid dispatching marker-like bytes found deep in ROM payloads.
- Analyze files with a bounded pool of up to four workers for better throughput on large databases while limiting concurrent file-memory use; progress callbacks remain serialized and monotonic.
- Added live GUI filtering across filenames, vehicle/ECU identity and status, plus an “Only rename candidates” view for large scan results.
- Kept the persistent recent-directory dropdown and last-used scan preferences.
- Added regression coverage for supplier-generated labels and deep false M1 markers.


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
