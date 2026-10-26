# WinOLS OLS format research notes

## Scope and confidence

These notes describe a reverse-engineered working model of the WinOLS `.ols` project format. They combine OhlsNamer, an independently developed parser vendored under `third_party/ols-parser-public/`, and public WinOLS help. EVC's public manual does not provide a complete byte-level specification. The external parser is an independent implementation, not an authoritative EVC specification.

Confidence labels: **Observed** = layout checked in code/files; **Cross-implementation** = independently implemented; **Inferred** = plausible but not proven by controlled WinOLS experiments; **Unknown** = no verified semantic interpretation.

Successful parsing does not prove every field is understood, and 305 corpus files cannot represent every format generation.

## 1. Outer header (24 bytes)

| Offset | Width | Interpretation | Caveat |
|---|---:|---|---|
| `0x00` | 4 | Little-endian `0x0000000B` (11), signature-length prefix | Observed by both parsers |
| `0x04` | 11 | ASCII `WinOLS File` | Observed |
| `0x0F` | 1 | NUL terminator | Observed |
| `0x10` | 4 | Little-endian format/schema version | Observed; gates payload layouts |
| `0x14` | 4 | Little-endian declared file size | Field is observed; trustworthiness may vary |
| `0x18` | — | Serialized payload starts | Observed |

The schema number is a layout discriminator, not a guarantee that every optional field is present. A declared-size mismatch is a validation warning, not a safe memory bound.

## 2. Metadata serialization and strings

The reference parser models the payload as MFC-style `CArchive` serialization. The common initial sequence has 16 strings: make, model, type, year, output, cylinders, country, drivetrain, memory, manufacturer, ECU name, hardware number, software number, production number, engine code, transmission. A 64-bit timestamp follows, then seven strings for original filename, WinOLS version, and reserved/revision fields.

| Schema threshold | Optional field interpreted by reference parser |
|---:|---|
| `>= 0x6E` (110) | Base-address string |
| `>= 0x30` (48) | Build number (`u32`) |
| `>= 0x34` (52) | Checksum (`u64`) |
| `>= 0x38` (56) | Flags (`u32`) |
| `>= 0x3E` (62) | Import comment string |
| `>= 0x3F` (63) | Post-comment field (`u32`) |
| `>= 0x40` (64) | Tag string |
| `>= 0x4F` (79) | Region count (`u32`) |
| `>= 0x55` (85) | Notes string |

For schema versions below 439, strings are read as a 32-bit byte length, payload bytes, and a trailing NUL for non-empty strings. At schema 439 and above, the reference parser observes length-prefixed strings without the trailing NUL and treats lengths -1, -2, and -3 as special values rendered as `-`, `?`, and `%`. These conventions need fixtures from each schema family.

The timestamp is currently exposed as a raw 64-bit value; epoch and timezone are unconfirmed. Meanings of `flags`, `post_comment_flag`, `region_count`, reserved strings, and all individual bits within metadata values are **not fully established**.

## 3. Version and ROM structures

The independent parser searches for these little-endian anchors:

| Anchor | Value | Working interpretation |
|---|---:|---|
| M1 | `0x42007899` | Version-record fence |
| M2 | `0x11883377` | Version-array dispatch |
| M3 | `0x98728833` | Per-version directory |
| M4 | `0x08260064` | Legacy-format path |
| M5 | `0xCD23018A` | Project-properties extension |
| M6 | `0x88271283` | Optional comment/build fingerprint |
| M7 | `0x84C0AD36` | Optional XOR-scrambled trailer |

These names/purposes are the reference parser's hypotheses, not official EVC definitions. Anchor occurrence alone is insufficient; surrounding structure and bounds must validate.

The reference implementation interprets the three 32-bit words immediately before M1 as version-count-minus-one, version-data start, and version-record size. For schema `>= 200`, ROM segments are located using adjacent `0xFADECAFE` / `0xCAFEAFFE` sentinels. The descriptor is interpreted as containing hashes, flash base/end values, and framing. The high address bit is masked, ranges are bounds-checked, and 76 bytes of framing are stripped. For schema below 200, the reference parser follows a legacy path without these segment sentinels.

This is a substantial structural hypothesis implemented independently, but it does not establish the meaning of every descriptor word or bit.

## 4. Map/axis records and cell data

Map records are walked sequentially with schema-gated fields. They include folder/comment references, names, map type and cell-data-type codes, geometry, scaling doubles, offset, unit, ROM address range, two axis records, and schema-dependent flags/arrays/folder lists. The reference parser looks for `0x98638811` as a post-map/header marker.

| Cell-data code | Interpreted storage | Endian interpretation |
|---:|---|---|
| 1 | 1 byte | No endian distinction |
| 2 / 3 | 2-byte integer | Big / little endian |
| 4 / 5 | 4-byte integer | Big / little endian |
| 6 / 7 | 4-byte float | Big / little endian |
| 10 / 11 | 8-byte integer | Big / little endian |
| 12 / 13 | 8-byte double | Big / little endian |

This is the reference implementation's mapping and must be tested against known map values. Its parser notes that `cell_bits` is display-oriented and should not be used to size ROM reads. Map type codes 1, 2, 3, and 5 are interpreted as scalar, value, curve, and map.

The meanings of anonymous geometry words (internally named `a276`, `a280`, `a396`, `a400`, `a408`) and several map/axis booleans are not independently confirmed. Treat these as implementation labels, not official field names.

## 5. Unresolved questions

- Semantic meaning of every bit in `flags`, post-comment values, and other flag-like fields.
- Meaning of every reserved field and every word in anchor/segment descriptors.
- Timestamp epoch and timezone.
- Every schema branch, optional field and future format version.
- Complete semantics of map geometry values and axis flags.
- Whether cell-data and map-type enums are complete across all versions.
- Meaning of checksum-like values across schema families.
- Exact relationship between legacy and current version-directory representations.

## 6. Verification plan

1. Inventory every tracked OLS: schema version, file/declared size, anchor positions, metadata completeness, versions, segments, maps and warnings.
2. Differentially parse identical bytes with OhlsNamer and the independent parser. Compare metadata, map counts/labels, versions and reconstructed ROM ranges; preserve disagreements for review.
3. Make controlled WinOLS project copies and change exactly one property per save (metadata, map name/dimensions, signedness, endian, scaling, version name, notes). Diff byte ranges and repeat across files to separate properties from timestamps, checksums and incidental serialization changes.
4. Cover representative schemas below 200, 200+, 439+, and every newer schema observed.
5. Assign meaning to a bit only after a controlled change consistently toggles it, the result repeats across multiple fixtures, and checksum/timestamp/index/compression explanations are excluded.
6. Fuzz/truncate copies, test malformed lengths/counts and sentinel collisions; require bounded reads, deterministic errors, no crashes, and no writes to original OLS files.
7. Report schema coverage and unsupported cases. Do not claim full understanding while any field or bit remains unresolved.

## Evidence sources

- EVC, [WinOLS 5.91 help manual](https://www.evc.de/ftp/Winols/WinOLS%20HelpEn.pdf): confirms OLS is the native project format and notes that its layout changed across versions; it is not a complete binary specification.
- EVC, [WinOLS product overview](https://www.evc.de/en/product/ols/software/Default.asp): describes project files as storing maps, versions and additional project metadata.
- [JSD-SpeedUP/ols-parser-public](https://github.com/JSD-SpeedUP/ols-parser-public): independent reverse-engineered C parser under 0BSD. Its interpretations are useful evidence, not official EVC guarantees.

## Status

**Partially reverse-engineered; not bit-complete.** The outer header and several payload structures have testable interpretations. Several fields and bit meanings remain unverified and require controlled WinOLS save/diff experiments and differential corpus testing.
