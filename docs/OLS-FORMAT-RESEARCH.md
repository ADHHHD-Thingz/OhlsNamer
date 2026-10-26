# WinOLS OLS format research notes

## Scope and confidence

This is a reverse-engineered working model, not an official EVC specification. The most important evidence source for this repository is the corpus-backed investigation in [AI-Dump's OLS format notes](https://github.com/ADHHHD-Thingz/AI-Dump/blob/main/audit/OLS_FORMAT.md) and its [binary field ledger](https://github.com/ADHHHD-Thingz/AI-Dump/blob/main/audit/OLS_BINARY_FIELD_LEDGER.md). The independent C parser vendored here is useful for differential checks, but its field names and some interpretations conflict with the corpus-backed evidence.

- **Confirmed (corpus):** repeated observation across the currently audited project collection.
- **Confirmed (vendor docs):** explicitly described by EVC.
- **Corroborated implementation:** a parser uses the rule and it agrees with available structural samples.
- **Inferred:** plausible interpretation that still needs controlled WinOLS experiments.
- **Unknown:** meaning or complete behaviour has not been established.

A parser accepting a file does not prove every field is interpreted correctly. Nor does a 305-file corpus prove coverage of all WinOLS versions.

## 1. Header and schema-dependent offset

| Offset | Width | Interpretation | Status |
|---|---:|---|---|
| `0x00` | 4 | Little-endian `0x0000000B` (11) | Confirmed in corpus |
| `0x04` | 11 | ASCII `WinOLS File` | Confirmed in corpus |
| `0x0F` | 1 | NUL terminator | Confirmed in corpus |
| `0x10` | 4 | Little-endian format/schema version | Confirmed in corpus |
| `0x14` | 4 | Schema-dependent: for versions >=200, declared total file size; for versions <200, first metadata string length | Confirmed in corpus |
| `0x18` | — | Metadata start for versions >=200; older versions start metadata at `0x14` | Confirmed in corpus |

Thus, “24-byte header” is shorthand for the modern layout; for pre-200 files, the first string starts at `0x14`. For modern files, compare declared size with actual length, but never use the declared value as a memory-safety bound.

The corpus currently documents schema versions 97–132, 247–396, 479, 503, 597 and 804. EVC's WinOLS 5.94 release notes dated 2026-10-01 state that the OLS/OLSX format was updated; we have not yet established that format version 804 covers that change. A known 5.94 export is still needed. Encrypted `.olsx` is a separate, unvalidated test class.

## 2. Metadata strings and timestamps

The corpus-backed wire layout is **16 length-prefixed strings, two consecutive little-endian u32 timestamp values, then eight more strings** (24 string slots total). This differs from the vendored public parser's metadata interpretation.

The observed string encoding is:

| Format version | String encoding |
|---|---|
| <200 | u32 byte length, CP1252 payload, trailing NUL for non-empty strings; metadata starts at `0x14` |
| 200–438 | same string encoding; metadata starts at `0x18` |
| >=439 | u32 byte length, CP1252 payload without trailing NUL; -1/-2/-3 represent `-`, `?`, `%` |

These boundaries are confirmed by the corpus audit and synthetic tests. No UTF-16 encoding was observed in the structured metadata strings.

The corpus-backed interpretation of the 24 slots is:

| Slot | Working name | Confidence |
|---:|---|---|
| 0 | Vehicle producer / make | Inferred mapping |
| 1 | Vehicle series / chassis | Inferred mapping |
| 2 | Vehicle model | Inferred mapping |
| 3 | Vehicle year | Inferred mapping |
| 4 | Engine type | Inferred mapping |
| 5 | Engine displacement | Inferred mapping |
| 6 | Engine output | Inferred mapping |
| 7 | Transmission | Inferred mapping |
| 8 | ECU usage / element | Inferred mapping |
| 9 | ECU producer | Inferred mapping |
| 10 | ECU build / family | Inferred mapping |
| 11 | ECU production number | Inferred mapping |
| 12 | ECU STG / hardware number | Inferred mapping |
| 13 | ECU software | Inferred mapping |
| 14–15 | Unknown fields | Unknown |
| — | Two u32 timestamps: created and modified Unix seconds | Confirmed by corpus pattern; semantic labels inferred |
| 16 | Import/original filename | Inferred mapping |
| 17 | Program string | Observed values; meaning beyond display string not established |
| 18 | Memory chip | Inferred mapping |
| 19 | Version list | Inferred mapping; count-minus-one relationship confirmed for checkable projects |
| 20 | VIN or reference | Inferred mapping; values are not uniformly VINs |
| 21 | Engine code | Inferred mapping |
| 22 | Checksum module | Inferred mapping |
| 23 | Software size in hexadecimal | Strong structural evidence: matches embedded ROM record size in 148/148 checked projects |

The two timestamps are **not** interpreted as a single Windows FILETIME in the corpus-backed implementation. The public parser's FILETIME interpretation yields implausible dates in checked samples. Likewise, the public parser's names for many early string slots conflict with actual values (for example, a value like “Turbodiesel” appears in its “Output” slot). Therefore, use the public parser for structural cross-checking, not as ground truth for metadata semantics.

## 3. Version directory and ROM payload

The corpus-backed parser looks for little-endian anchor `0x42007899` (M1). The three u32 words immediately before it are interpreted as:

`[version_count_minus_one, version_data_start, version_record_size]`

For version index `i`, the observed record starts at `version_data_start + 4 + version_record_size * i`. In the previously audited 148 projects, the record size agreed with metadata slot 23 in 148/148; version count agreed with the metadata version list in 140/140 checkable projects. This is strong structural evidence, not a complete interpretation of every byte inside the record.

The independent public parser also documents anchors M2–M7 (`0x11883377`, `0x98728833`, `0x08260064`, `0xCD23018A`, `0x88271283`, `0x84C0AD36`) and a `0xFADECAFE / 0xCAFEAFFE` segment descriptor scheme. These are useful candidate structures, but they are not universal: an audited format-479 sample had a valid M1 directory and no such segment pair, and the earlier corpus audit observed the pair in only a small minority of containers. The segment descriptor's individual words and high-bit interpretation remain implementation hypotheses until validated per schema family.

The optional `0x84C0AD36` trailer is reported as a marker; scrambled/encrypted content is not decoded.

## 3.1 Concrete sample cross-check

A supplied 1,771,332-byte format-479 project was inspected read-only (SHA-256 `e5aaf7da564d81967e41cff71bc4c05d43107a0a838fb06f6be7788a2fd51669`).

- The signature is valid; the format version is 479; the declared size equals the actual file size.
- The metadata is fully consumed using the >=439 no-NUL string rule.
- The two timestamp words are `1539785900` and `1539788858`, corresponding to plausible UTC times in October 2018 rather than a plausible FILETIME.
- Metadata slot 23 is `D0000` (hex = 851,968 bytes).
- M1 is at `0x5FE`; its three preceding words are `[1, 0x10740, 0xD0000]`. This describes two versions, with the first record at `0x10744`.
- `0x10740 + 4 + 2 * 0xD0000 = 0x1B0744`, exactly the file length. This cross-checks the version directory, record size and software-size metadata.
- No `FADECAFE/CAFEAFFE` pair occurs in this file, so that descriptor scheme cannot be assumed for every modern OLS project.

This sample supports the header, metadata and M1 interpretations; it does not decode every byte inside the version records.

## 3.2 Version-array dispatch and map scan boundary (v1.3.3)

OhlsNamer now validates the M1 version directory before using it as a parsing boundary. The three little-endian words before M1 are checked as `[version_count - 1, version_data_start, version_record_size]`; count, record size, arithmetic and in-file end are bounded. If validation succeeds, map-marker discovery and map-record parsing are restricted to bytes before `version_data_start`, so marker-like bytes inside embedded ROM versions cannot be dispatched as map records. The CLI exposes the validated version count, data start and record size.

This is a dispatch-boundary fix, not a complete semantic decoding of M1/M2. The independent parser calls `0x11883377` M2 a version-array dispatch marker, while OhlsNamer's existing map candidate logic treats the bytes after it as a possible compressed/count-prefixed map block. In the supplied format-479 sample, M2 is at `0x84E`, M1 at `0x5FE`, and the version array begins at `0x10740`; therefore M2 remains within the pre-ROM region and is still only a candidate marker. Its universal meaning and the corpus map-count disagreements remain unresolved.

## 4. Map records

The current OhlsNamer parser extracts candidate map names/descriptions and counts non-placeholder labels. It is not a full calibration-map decoder. The independent parser attempts to decode map records, axes, cell types, scaling and ROM addresses.

The latest completed tri-parser audit against 305 tracked OLS projects found:
- Python corpus extractor: metadata complete for 305/305.
- Independent C parser: parsed 305/305.
- OhlsNamer: accepted 305/305.
- Format-version disagreements: 0.
- Declared-size mismatches: 0.
- Timeouts or launch errors: 0.
- **Map-count disagreements: 281/305.**

This disagreement is a major unresolved finding. It does not by itself establish which parser is correct, and the differential JSONL artifact could not be uploaded because GitHub Actions storage quota was exhausted. The next step is to make disagreement details visible in job logs and investigate representative files before changing the renamer's map-count behaviour. Do not treat current map counts as a fully validated semantic interpretation.

The reference parser's cell-data enum interpretation (including integer/float widths, endian, scaling and axis layout) is provisional until values are checked against known maps and controlled WinOLS exports. Anonymous geometry integers and boolean flags must remain unknown until their behaviour is established.

## 5. Unresolved bit/field semantics

The repository does not have a complete per-bit specification. In particular, the meanings of every bit in metadata flags, reserved fields, map/axis booleans, version descriptors and optional trailer structures are unknown or only inferred. The exact timestamp semantics for every schema family, every map geometry field, and the completeness of enum values across recent WinOLS releases also need further verification.

## 6. Required experiments

1. Inventory every tracked OLS by schema, file/declared size, anchors, metadata, version count, ROM regions, map count and warnings.
2. Differentially parse identical files with the corpus-backed Python extractor, vendored C parser, and OhlsNamer. Print concrete disagreement values in CI logs because artifacts may be unavailable.
3. Create controlled WinOLS copies and change one property per save: metadata field, map name, dimensions, signedness, endian, scaling, version name and notes. Diff changed bytes and repeat across multiple projects to separate the property from timestamps, checksums and incidental serialization.
4. Cover pre-200, 200–438, 439+, every observed modern schema, and a known WinOLS 5.94 export.
5. Assign a bit meaning only after a controlled change consistently toggles it across multiple fixtures and checksum/timestamp/index/compression alternatives are excluded.
6. Fuzz/truncate copies and malformed lengths/counts/marker collisions. Require bounded reads, deterministic errors, no crashes and no writes to source OLS files.
7. Keep unknown fields labelled unknown; publish coverage and unsupported cases. Do not claim full understanding while any field or bit remains unresolved.

## Sources

- EVC, [WinOLS 5.91 help manual](https://www.evc.de/ftp/Winols/WinOLS%20HelpEn.pdf): confirms the native project format has changed across versions; it is not a complete byte-level specification.
- EVC, [WinOLS 5.94 release notes](https://www.evc.de/en/download/down_winols_whatsnew_raw.asp): states that the OLS/OLSX format was updated in 2026.
- [AI-Dump corpus-backed format notes](https://github.com/ADHHHD-Thingz/AI-Dump/blob/main/audit/OLS_FORMAT.md) and [binary field ledger](https://github.com/ADHHHD-Thingz/AI-Dump/blob/main/audit/OLS_BINARY_FIELD_LEDGER.md).
- [JSD-SpeedUP/ols-parser-public](https://github.com/JSD-SpeedUP/ols-parser-public): independently developed 0BSD parser; useful for structural comparison, but its metadata interpretation conflicts with corpus evidence.

## Status

**Partially reverse-engineered, not bit-complete.** The outer signature, schema-dependent metadata offset/string encoding, 24-slot metadata layout, timestamp pattern and M1 version directory have substantial corpus evidence. Map decoding has a high unresolved disagreement rate; segment descriptors and several flag/field meanings remain unverified.
