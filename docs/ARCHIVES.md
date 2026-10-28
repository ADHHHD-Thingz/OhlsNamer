# Projects inside ZIP and RAR archives

OhlsNamer reads, counts maps in, and renames WinOLS projects stored inside `.zip` and `.rar` archives without unpacking them. This page records how it works, what was tested, and the limits.

## Reading

| | ZIP | RAR |
|---|---|---|
| Listing | Central directory (EOCD, Zip64 EOCD/locator) | Block headers (RAR4 and RAR5), header CRCs checked |
| Decoding | stored and deflate via zlib, streamed from the entry offset | bundled UnRAR 7.0.7 (all RAR methods), streamed to memory |
| Parallelism | one worker per project (random access) | one sequential pass per archive (solid archives require it); UnRAR's own RAR5 decoder is multi-threaded |
| Memory | map/ID area + first ROM image of the project being decoded | same |

Each project goes through `olsarc::PrefixCollector`, which follows the same load plan as plain files (`ols::requiredLoadLength`). The plan keeps 1 MiB, proves the map/ID-area boundary, then keeps the map area plus the first ROM image, or the whole project when the boundary cannot be proven. ZIP decoding stops as soon as the plan is satisfied. RAR entries are decoded completely, which also checks their CRCs, but bytes past the plan are discarded.

On the integration archives (27 fixture projects in each archive type), every in-archive map count equals the plain-file count.

## Renaming

A file name is stored only in archive headers. The only checksum that covers it is a RAR header CRC, which is recomputed. Renames therefore rebuild headers and copy compressed data unchanged.

### ZIP

1. **In place.** Each renamed local header must keep its exact length.
   - A shorter name is padded with a `0xD935` extra field, an ID that readers ignore.
   - Old Info-ZIP Unicode Path fields (`0x7075`) are removed because they describe the old name.
   - The central directory and end records are rewritten at their original offset, and the file is truncated or extended.
   - Before the first write, the original header bytes and the old tail are saved to `archive.zip.olsrenamer-undo` (CRC-protected) and flushed. A failed write rolls back immediately. A crash or power loss is rolled back the next time the folder is scanned.
   - A slack of 1–3 bytes cannot hold a padding field, so such a name needs a rewrite.
2. **Full rewrite.**
   - The archive is streamed to `archive.zip.olsrenamer-tmp` in the same folder, with new local headers and offsets.
   - Data, data descriptors and any bytes between entries are copied verbatim.
   - Zip64 extra fields are rebuilt (offsets past 4 GiB) and SFX stubs and comments are kept.
   - Every copied span is CRC-checked while writing and re-read from the new file. The new archive is re-parsed and compared field by field, and renamed entries are fully decoded and CRC-checked.
   - Only then is the original replaced (`MoveFileEx` replace, write-through).
3. **Neither possible.** When **Allow full archive rewrite** is off, or free space is short, the names that fit are renamed in place. The rest are listed as not renamed.

### RAR

- If WinRAR is installed (found through the registry or Program Files), `Rar.exe rn` renames all entries in one call. The result is then re-parsed and verified. WinRAR keeps recovery records and quick-open data.
- Otherwise the built-in writer rebuilds the renamed headers:
  - **RAR5:** vint name length, header size, and CRC-32 are recomputed.
  - **RAR4:** name field (with RAR's Unicode name encoding for non-ASCII names), HEAD_SIZE, NAME_SIZE and the 16-bit header CRC are rewritten.
  - Packed data is copied verbatim.
  - Quick-open (`QO`) and recovery-record blocks are removed, because they describe the old bytes, and the RAR5 locator record and recovery flag are cleared.
  - RAR5 hard links and file copies, which name their source entry, are retargeted.
- The same copy verification as ZIP is applied.
- The built-in writer is implemented from RARLAB's published format documentation. It does not use UnRAR source code, and it never compresses data.
- **Refused:**
  - encrypted headers (`-hp`);
  - multi-volume sets;
  - files split across volumes;
  - locked archives;
  - archives with an authenticity signature;
  - headers that do not parse cleanly (for example libarchive's `rar5_extra_field_underflow` fuzz sample).
- Data-encrypted entries (`-p`) can be renamed but not analysed, so they stay review-only.

## Scale (200 GB+ archives)

| Operation | Cost | Measured here |
|---|---|---|
| Scan a ZIP | central directory + each project's map area | 205 GiB Zip64, 9 entries: 0.43 s |
| Scan a non-solid RAR | headers; non-project data is skipped by seeking | 6 GiB: 0.17 s |
| Scan a solid RAR | decodes everything up to the last project | UnRAR speed (archive dependent) |
| ZIP rename in place | kilobytes of header writes | 205 GiB: 0.05 s |
| ZIP/RAR full rewrite | write the whole archive once and re-read it once; needs free space = archive size + 64 MiB | 6 GiB RAR: 17.3 s on this VM's disk |

For a 200 GB full rewrite, expect roughly 200 GB ÷ (write speed) + 200 GB ÷ (read speed). On an NVMe SSD that is a few minutes; on a SATA SSD about 10–15 minutes. Cancel during a rewrite deletes the temporary file and leaves the archive untouched.

## Test evidence (this release)

- `archive_tests`: unit tests covering:
  - vint and RAR4 Unicode-name round trips;
  - in-place padding, slack limits and undo-journal round trips;
  - torn-journal rejection;
  - write-failure rollback and simulated power-loss recovery;
  - copy rewrite with Unicode Path fields and UTF-8 names;
  - duplicate-name detection;
  - refusal when the archive changed after the scan.
  - The tests also pass under ASan/UBSan.
- `tests/archive_integration.sh`:
  - Archive types: `zip` (deflate and stored), 7-Zip ZIP, Python Zip64, Python data-descriptor ZIP, RAR5, solid RAR5, and RAR5 with recovery record and quick-open data.
  - In-archive counts equal plain-file counts.
  - Each archive is renamed by copy and in place (shorter names), then validated with `unzip -t`, `7z t`, Python `testzip`, `unrar t` and `rar t`, and extracted bytes are compared.
  - A second scan proposes no further renames.
  - Encrypted archives are reported.
- `tests/rar_format_corpus.sh` on libarchive's sample archives: 90 renames passed (longer and Unicode names) with `unrar t`/`rar t` and byte-identical extraction. The 72 refusals were the multi-volume sets plus the malformed underflow sample.
- 205 GiB sparse Zip64 archive (projects before and after a 205 GiB stored entry, so offsets are above 4 GiB): renamed in place, verified with `unzip -t`, Python `zipfile` (full CRC check of every project) and 7-Zip.
- 26,000 mutated ZIP/RAR archives fuzzed through the parsers, planners and writers under ASan/UBSan with no findings.
- Windows build under Wine: scan → analysis → rename on ZIP, RAR, solid RAR with a recovery record, and plain files, through the GUI and the file layer. Results were validated afterwards with the Linux tools.

## Not supported

- `.7z` archives (compressed, usually solid headers).
- ZIP compression methods other than stored and deflate. These entries are listed but not analysed.
- Split ZIP and multi-volume RAR sets.
- Prepended data with uncorrected ZIP offsets.
- Duplicate entry names (case-insensitive).
