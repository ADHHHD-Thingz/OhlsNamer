// Archive support: read and rename WinOLS projects inside ZIP and RAR archives
// without extracting them. Platform independent; the Windows file layer
// (ols_fs.hpp) and the tests provide ByteSource/ByteSink implementations.
//
// Reading: ZIP entries are decoded straight from their offsets (stored or
// deflate, streamed through zlib); RAR entries are decoded by the UnRAR library
// (ols_unrar.hpp). Only the map/ID area of each project is kept in memory
// (ols::requiredLoadLength), so 200 GB+ archives are never loaded.
//
// Renaming: a file name lives in the archive headers, never in the compressed
// data, and no checksum covers it except the RAR header CRC. Renames therefore
// rebuild headers and copy every data byte verbatim:
//   * ZIP in place: when each new local header fits the old one (padding a
//     0xD935 extra field), only those headers and the central directory are
//     rewritten, guarded by an undo journal. Cost: kilobytes, not gigabytes.
//   * ZIP/RAR copy: stream every block to a temporary file with rebuilt
//     headers, verify it, then replace the original.
// The RAR writer is written from RARLAB's published header documentation. It
// never compresses data and uses no UnRAR code.
#pragma once
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ols_core.hpp"

namespace olsarc {
using ols::ws;
using Bytes = std::vector<uint8_t>;

// ------------------------------------------------------------------ I/O
struct ByteSource {
  virtual ~ByteSource() = default;
  virtual uint64_t size() const = 0;
  virtual bool readAt(uint64_t offset, void* dst, size_t n) = 0;
};

struct ByteSink {
  virtual ~ByteSink() = default;
  virtual bool write(const void* src, size_t n) = 0;
  virtual uint64_t position() const = 0;
};

// Random-access read/write file for in-place header patching.
struct RandomFile : virtual ByteSource {
  virtual bool writeAt(uint64_t offset, const void* src, size_t n) = 0;
  virtual bool truncate(uint64_t newSize) = 0;
  virtual bool flush() = 0;
};

struct MemorySource : ByteSource {
  const uint8_t* data;
  size_t length;
  MemorySource(const uint8_t* d, size_t n) : data(d), length(n) {}
  uint64_t size() const override { return length; }
  bool readAt(uint64_t off, void* dst, size_t n) override {
    if (off > length || n > length - off) return false;
    std::memcpy(dst, data + off, n);
    return true;
  }
};

struct MemorySink : ByteSink {
  Bytes out;
  bool write(const void* src, size_t n) override {
    const uint8_t* p = static_cast<const uint8_t*>(src);
    out.insert(out.end(), p, p + n);
    return true;
  }
  uint64_t position() const override { return out.size(); }
};

using Progress = std::function<bool(uint64_t done, uint64_t total)>;  // false = cancel

inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) { return ols::rd32(p, 0); }
inline uint64_t le64(const uint8_t* p) { return ols::rd64(p, 0); }
inline void put16(Bytes& b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
inline void put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
inline void put64(Bytes& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
inline void set16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void set32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

inline uint32_t crc32Of(const uint8_t* p, size_t n, uint32_t crc = 0) {
  return (uint32_t)crc32_z(crc, p, n);
}

// ---------------------------------------------------------- text helpers
inline std::string toUtf8(const ws& s) { return ols::utf8(s); }

inline ws fromUtf8(const std::string& s, bool* ok = nullptr) {
  ws out;
  bool good = true;
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = (uint8_t)s[i];
    uint32_t cp = 0;
    size_t len = 0;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; len = 2; }
    else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; len = 3; }
    else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; len = 4; }
    else { good = false; out += (wchar_t)0xfffd; ++i; continue; }
    if (i + len > s.size()) { good = false; out += (wchar_t)0xfffd; break; }
    for (size_t k = 1; k < len; ++k) {
      const uint8_t cc = (uint8_t)s[i + k];
      if ((cc & 0xc0) != 0x80) { good = false; break; }
      cp = (cp << 6) | (cc & 0x3f);
    }
    i += len;
    if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
      cp -= 0x10000;
      out += (wchar_t)(0xd800 + (cp >> 10));
      out += (wchar_t)(0xdc00 + (cp & 0x3ff));
    } else {
      out += (wchar_t)cp;
    }
  }
  if (ok) *ok = good;
  return out;
}

// IBM PC code page 437: the ZIP specification's default name encoding.
inline ws fromCp437(const std::string& s) {
  static const uint16_t hi[128] = {
      0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
      0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
      0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
      0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
      0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
      0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
      0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
      0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0};
  ws out;
  for (unsigned char c : s) out += c < 0x80 ? (wchar_t)c : (wchar_t)hi[c - 0x80];
  return out;
}

inline bool isAscii(const std::string& s) {
  for (unsigned char c : s)
    if (c >= 0x80) return false;
  return true;
}

// Split "dir/sub/name.ols" (either separator) into the folder (with trailing
// separator) and the base name.
inline void splitInnerPath(const std::string& path, std::string& folder, std::string& base) {
  const size_t cut = path.find_last_of("/\\");
  folder = cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
  base = cut == std::string::npos ? path : path.substr(cut + 1);
}

inline bool endsWithOlsName(const std::string& utf8Name) {
  if (utf8Name.size() < 4) return false;
  std::string ext = utf8Name.substr(utf8Name.size() - 4);
  for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
  return ext == ".ols";
}

// ------------------------------------------------- streamed map-area capture
// Collects the leading bytes of one project while it is being decoded, using
// the same load plan as the plain-file loader. Bytes past the map/ID area and
// first ROM image are dropped, so memory stays bounded however large the
// entry or the archive.
struct PrefixCollector {
  uint64_t fileSize = 0;
  uint64_t target = 0;
  Bytes buf;
  bool done = false;

  explicit PrefixCollector(uint64_t size = 0) { reset(size); }
  void reset(uint64_t size) {
    fileSize = size;
    target = std::min<uint64_t>(size, ols::kInitialLoadBytes);
    buf.clear();
    done = size == 0;
  }
  void feed(const uint8_t* p, size_t n) {
    while (!done && n > 0) {
      const size_t want = (size_t)std::min<uint64_t>(n, target - buf.size());
      buf.insert(buf.end(), p, p + want);
      p += want;
      n -= want;
      if (buf.size() < target) return;
      if (target >= fileSize) { done = true; return; }
      const uint64_t need = ols::requiredLoadLength(buf.data(), buf.size(), fileSize);
      if (need <= buf.size()) { done = true; return; }
      target = need;
    }
  }
  ols::Analysis analyze() const {
    ols::Analysis a = ols::analyzeOls(buf.data(), buf.size(), fileSize);
    return a;
  }
};

// ===================================================================== ZIP
static constexpr uint32_t kZipLocal = 0x04034b50, kZipCentral = 0x02014b50, kZipEocd = 0x06054b50,
                          kZip64Eocd = 0x06064b50, kZip64Locator = 0x07064b50;
static constexpr uint16_t kZipPadding = 0xD935;  // ignored by readers (zipalign id)

struct ZipEntry {
  // Central directory fields (raw, as stored).
  uint16_t versionMade = 0, versionNeeded = 0, flags = 0, method = 0, time = 0, date = 0;
  uint32_t crc = 0;
  uint64_t compressedSize = 0, uncompressedSize = 0, localOffset = 0;
  uint32_t diskStart = 0;
  uint16_t internalAttr = 0;
  uint32_t externalAttr = 0;
  uint32_t rawCompressed32 = 0, rawUncompressed32 = 0, rawOffset32 = 0;
  uint16_t rawDisk16 = 0;
  std::string rawName, extra, comment;  // bytes as stored
  std::string nameUtf8;                 // decoded name
  bool nameFromUnicodeExtra = false;
  size_t cdIndex = 0;
  bool isDirectory() const { return !rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\'); }
  bool encrypted() const { return (flags & 0x0001) != 0; }
};

struct ZipArchive {
  uint64_t fileSize = 0;
  uint64_t cdOffset = 0, cdSize = 0, eocdOffset = 0, zip64EocdOffset = 0;
  bool zip64 = false;
  std::string comment;
  std::vector<ZipEntry> entries;  // central directory order
  uint64_t firstLocalOffset = 0;  // bytes before it (SFX stub) are preserved
  ws problem;                     // non-empty: archive cannot be renamed safely
};

struct ZipExtraField {
  uint16_t id;
  std::string data;
};

inline std::vector<ZipExtraField> splitExtra(const std::string& extra, bool* clean = nullptr) {
  std::vector<ZipExtraField> out;
  size_t p = 0;
  bool ok = true;
  while (p + 4 <= extra.size()) {
    const uint16_t id = le16((const uint8_t*)extra.data() + p);
    const uint16_t len = le16((const uint8_t*)extra.data() + p + 2);
    if (p + 4 + len > extra.size()) { ok = false; break; }
    out.push_back({id, extra.substr(p + 4, len)});
    p += 4 + len;
  }
  if (p != extra.size()) ok = false;
  if (clean) *clean = ok;
  return out;
}

inline std::string joinExtra(const std::vector<ZipExtraField>& fields) {
  std::string out;
  for (const auto& f : fields) {
    out += (char)(f.id & 0xff); out += (char)(f.id >> 8);
    out += (char)(f.data.size() & 0xff); out += (char)(f.data.size() >> 8);
    out += f.data;
  }
  return out;
}

inline bool readBytes(ByteSource& src, uint64_t off, size_t n, std::string& out) {
  out.assign(n, '\0');
  return n == 0 || src.readAt(off, &out[0], n);
}

// Parses the end records and the whole central directory. No entry data is read.
inline bool parseZip(ByteSource& src, ZipArchive& z, ws& error) {
  z = ZipArchive();
  const uint64_t n = src.size();
  z.fileSize = n;
  if (n < 22) { error = L"Too small to be a ZIP archive."; return false; }
  const size_t tail = (size_t)std::min<uint64_t>(n, 22u + 65535u + 20u);
  Bytes t(tail);
  if (!src.readAt(n - tail, t.data(), tail)) { error = L"Cannot read the end of the archive."; return false; }
  size_t at = std::string::npos;
  for (size_t i = tail - 22 + 1; i-- > 0;) {
    if (le32(&t[i]) != kZipEocd) continue;
    const uint16_t commentLen = le16(&t[i + 20]);
    if (i + 22 + commentLen == tail) { at = i; break; }
  }
  if (at == std::string::npos) { error = L"No ZIP end-of-central-directory record."; return false; }
  z.eocdOffset = n - tail + at;
  const uint8_t* e = &t[at];
  const uint16_t disk = le16(e + 4), cdDisk = le16(e + 6);
  uint64_t entriesTotal = le16(e + 10);
  z.cdSize = le32(e + 12);
  z.cdOffset = le32(e + 16);
  z.comment.assign((const char*)e + 22, le16(e + 20));
  if (disk != 0 || cdDisk != 0) { error = L"Split/spanned ZIP archives are not supported."; return false; }
  // Zip64 locator immediately precedes the EOCD.
  if (z.eocdOffset >= 20) {
    uint8_t loc[20];
    if (src.readAt(z.eocdOffset - 20, loc, 20) && le32(loc) == kZip64Locator) {
      z.zip64 = true;
      z.zip64EocdOffset = le64(loc + 8);
      uint8_t r[56];
      if (z.zip64EocdOffset + 56 > n || !src.readAt(z.zip64EocdOffset, r, 56) || le32(r) != kZip64Eocd) {
        error = L"Invalid Zip64 end-of-central-directory record.";
        return false;
      }
      if (le32(r + 16) != 0 || le32(r + 20) != 0) { error = L"Split/spanned ZIP archives are not supported."; return false; }
      entriesTotal = le64(r + 32);
      z.cdSize = le64(r + 40);
      z.cdOffset = le64(r + 48);
    }
  }
  const uint64_t cdEnd = z.zip64 ? z.zip64EocdOffset : z.eocdOffset;
  if (z.cdOffset + z.cdSize != cdEnd) {
    // Data prepended without fixing offsets (some SFX builders). Readable by
    // tolerant tools but offsets would be ambiguous after a rewrite.
    error = L"Central directory offsets do not match its position (prepended data with uncorrected offsets).";
    return false;
  }
  if (z.cdSize > 1ull << 31 || entriesTotal > 50000000ull) { error = L"Central directory is implausibly large."; return false; }
  Bytes cd((size_t)z.cdSize);
  if (!cd.empty() && !src.readAt(z.cdOffset, cd.data(), cd.size())) { error = L"Cannot read the central directory."; return false; }
  size_t p = 0;
  z.entries.reserve((size_t)std::min<uint64_t>(entriesTotal, 1u << 22));
  while (p + 46 <= cd.size() && le32(&cd[p]) == kZipCentral) {
    ZipEntry en;
    const uint8_t* c = &cd[p];
    en.versionMade = le16(c + 4); en.versionNeeded = le16(c + 6); en.flags = le16(c + 8);
    en.method = le16(c + 10); en.time = le16(c + 12); en.date = le16(c + 14); en.crc = le32(c + 16);
    en.rawCompressed32 = le32(c + 20); en.rawUncompressed32 = le32(c + 24);
    const uint16_t nl = le16(c + 28), xl = le16(c + 30), cl = le16(c + 32);
    en.rawDisk16 = le16(c + 34); en.internalAttr = le16(c + 36); en.externalAttr = le32(c + 38);
    en.rawOffset32 = le32(c + 42);
    if (p + 46 + nl + xl + cl > cd.size()) { error = L"Truncated central directory record."; return false; }
    en.rawName.assign((const char*)c + 46, nl);
    en.extra.assign((const char*)c + 46 + nl, xl);
    en.comment.assign((const char*)c + 46 + nl + xl, cl);
    en.compressedSize = en.rawCompressed32;
    en.uncompressedSize = en.rawUncompressed32;
    en.localOffset = en.rawOffset32;
    en.diskStart = en.rawDisk16;
    bool cleanExtra = true;
    for (const auto& f : splitExtra(en.extra, &cleanExtra)) {
      if (f.id == 0x0001) {  // Zip64: present only for fields saturated in the base record
        size_t q = 0;
        auto take = [&](uint64_t& v) {
          if (q + 8 <= f.data.size()) { v = le64((const uint8_t*)f.data.data() + q); q += 8; }
        };
        if (en.rawUncompressed32 == 0xFFFFFFFFu) take(en.uncompressedSize);
        if (en.rawCompressed32 == 0xFFFFFFFFu) take(en.compressedSize);
        if (en.rawOffset32 == 0xFFFFFFFFu) take(en.localOffset);
        if (en.rawDisk16 == 0xFFFF && q + 4 <= f.data.size()) en.diskStart = le32((const uint8_t*)f.data.data() + q);
      } else if (f.id == 0x7075 && f.data.size() >= 5 && f.data[0] == 1) {
        // Info-ZIP Unicode Path: valid only while its CRC matches the raw name.
        const uint32_t nameCrc = le32((const uint8_t*)f.data.data() + 1);
        if (nameCrc == crc32Of((const uint8_t*)en.rawName.data(), en.rawName.size())) {
          en.nameUtf8 = f.data.substr(5);
          en.nameFromUnicodeExtra = true;
        }
      }
    }
    if (!cleanExtra) z.problem = L"An entry has a malformed extra field.";
    if (en.nameUtf8.empty()) en.nameUtf8 = (en.flags & 0x0800) ? en.rawName : toUtf8(fromCp437(en.rawName));
    if (en.diskStart != 0) { error = L"Split/spanned ZIP archives are not supported."; return false; }
    if (en.flags & 0x2000) z.problem = L"Central-directory encryption is not supported.";
    en.cdIndex = z.entries.size();
    z.entries.push_back(std::move(en));
    p += 46 + nl + xl + cl;
  }
  if (z.entries.size() != entriesTotal || p != cd.size()) {
    error = L"Central directory entry count or size is inconsistent.";
    return false;
  }
  // Physical layout sanity: local headers must be distinct and lie before the CD.
  std::vector<uint64_t> offs;
  offs.reserve(z.entries.size());
  for (const auto& en : z.entries) offs.push_back(en.localOffset);
  std::sort(offs.begin(), offs.end());
  for (size_t i = 0; i < offs.size(); ++i) {
    if (offs[i] + 30 > z.cdOffset || (i && offs[i] == offs[i - 1])) {
      z.problem = L"Entries overlap or point outside the archive.";
      break;
    }
  }
  z.firstLocalOffset = offs.empty() ? z.cdOffset : offs.front();
  std::set<std::string> seen;
  for (const auto& en : z.entries) {
    std::string key = en.nameUtf8;
    for (auto& c : key) c = (char)std::tolower((unsigned char)c);
    if (!seen.insert(key).second) { z.problem = L"The archive contains duplicate entry names."; break; }
  }
  return true;
}

struct ZipLocalHeader {
  uint64_t offset = 0;
  uint16_t versionNeeded = 0, flags = 0, method = 0, time = 0, date = 0;
  uint32_t crc = 0, compressed32 = 0, uncompressed32 = 0;
  std::string rawName, extra;
  uint64_t dataOffset = 0;
  size_t headerSize() const { return 30 + rawName.size() + extra.size(); }
};

inline bool readLocalHeader(ByteSource& src, uint64_t offset, ZipLocalHeader& h, ws& error) {
  uint8_t b[30];
  if (!src.readAt(offset, b, 30) || le32(b) != kZipLocal) { error = L"Missing local file header."; return false; }
  h.offset = offset;
  h.versionNeeded = le16(b + 4); h.flags = le16(b + 6); h.method = le16(b + 8);
  h.time = le16(b + 10); h.date = le16(b + 12); h.crc = le32(b + 14);
  h.compressed32 = le32(b + 18); h.uncompressed32 = le32(b + 22);
  const uint16_t nl = le16(b + 26), xl = le16(b + 28);
  if (!readBytes(src, offset + 30, nl, h.rawName) || !readBytes(src, offset + 30 + nl, xl, h.extra)) {
    error = L"Truncated local file header.";
    return false;
  }
  h.dataOffset = offset + 30 + nl + xl;
  return true;
}

// Streams one entry's uncompressed bytes (stored or deflate) into `collector`
// and stops as soon as the map/ID area is captured. Nothing is written to disk.
inline bool readZipProject(ByteSource& src, const ZipEntry& en, PrefixCollector& collector, ws& error) {
  if (en.encrypted()) { error = L"Encrypted entry: contents cannot be analysed without the password."; return false; }
  ZipLocalHeader lh;
  if (!readLocalHeader(src, en.localOffset, lh, error)) return false;
  if (lh.dataOffset + en.compressedSize > src.size()) { error = L"Entry data runs past the end of the archive."; return false; }
  collector.reset(en.uncompressedSize);
  if (en.method == 0) {
    if (en.compressedSize != en.uncompressedSize) { error = L"Stored entry sizes disagree."; return false; }
    Bytes chunk;
    uint64_t pos = 0;
    while (!collector.done && pos < en.uncompressedSize) {
      const size_t want = (size_t)std::min<uint64_t>(en.uncompressedSize - pos,
                                                      std::max<uint64_t>(collector.target - collector.buf.size(), 1));
      chunk.resize(want);
      if (!src.readAt(lh.dataOffset + pos, chunk.data(), want)) { error = L"Read error inside the archive."; return false; }
      collector.feed(chunk.data(), want);
      pos += want;
    }
    return true;
  }
  if (en.method != 8) {
    error = L"Compression method " + std::to_wstring(en.method) + L" is not supported (stored and deflate are).";
    return false;
  }
  z_stream zs;
  std::memset(&zs, 0, sizeof zs);
  if (inflateInit2(&zs, -15) != Z_OK) { error = L"zlib initialisation failed."; return false; }
  Bytes in(1u << 20), out(1u << 20);
  uint64_t consumed = 0, produced = 0;
  int rc = Z_OK;
  bool ok = true;
  while (!collector.done && rc != Z_STREAM_END) {
    if (zs.avail_in == 0) {
      const size_t want = (size_t)std::min<uint64_t>(in.size(), en.compressedSize - consumed);
      if (want == 0) { ok = false; error = L"Deflate stream is truncated."; break; }
      if (!src.readAt(lh.dataOffset + consumed, in.data(), want)) { ok = false; error = L"Read error inside the archive."; break; }
      consumed += want;
      zs.next_in = in.data();
      zs.avail_in = (uInt)want;
    }
    zs.next_out = out.data();
    zs.avail_out = (uInt)out.size();
    rc = inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) { ok = false; error = L"Deflate data is corrupt."; break; }
    const size_t got = out.size() - zs.avail_out;
    produced += got;
    collector.feed(out.data(), got);
  }
  inflateEnd(&zs);
  if (ok && rc == Z_STREAM_END && produced != en.uncompressedSize) {
    ok = false;
    error = L"Decoded size does not match the directory.";
  }
  return ok;
}

// Fully decodes an entry and checks its CRC-32 (used to verify renamed entries).
inline bool verifyZipEntryCrc(ByteSource& src, const ZipEntry& en, ws& error) {
  if (en.encrypted()) return true;  // cannot decode; data bytes are compared instead
  ZipLocalHeader lh;
  if (!readLocalHeader(src, en.localOffset, lh, error)) return false;
  uint32_t crc = 0;
  uint64_t produced = 0;
  Bytes in(1u << 20), out(1u << 20);
  if (en.method == 0) {
    for (uint64_t pos = 0; pos < en.uncompressedSize;) {
      const size_t want = (size_t)std::min<uint64_t>(in.size(), en.uncompressedSize - pos);
      if (!src.readAt(lh.dataOffset + pos, in.data(), want)) { error = L"Read error."; return false; }
      crc = crc32Of(in.data(), want, crc);
      pos += want;
    }
    produced = en.uncompressedSize;
  } else if (en.method == 8) {
    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, -15) != Z_OK) return false;
    uint64_t consumed = 0;
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
      if (zs.avail_in == 0) {
        const size_t want = (size_t)std::min<uint64_t>(in.size(), en.compressedSize - consumed);
        if (want == 0 || !src.readAt(lh.dataOffset + consumed, in.data(), want)) { rc = Z_DATA_ERROR; break; }
        consumed += want;
        zs.next_in = in.data();
        zs.avail_in = (uInt)want;
      }
      zs.next_out = out.data();
      zs.avail_out = (uInt)out.size();
      rc = inflate(&zs, Z_NO_FLUSH);
      if (rc != Z_OK && rc != Z_STREAM_END) break;
      const size_t got = out.size() - zs.avail_out;
      crc = crc32Of(out.data(), got, crc);
      produced += got;
    }
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) { error = L"Deflate data is corrupt."; return false; }
  } else {
    return true;  // unsupported method: data bytes compared instead
  }
  if (produced != en.uncompressedSize || crc != en.crc) { error = L"CRC-32 mismatch after rename."; return false; }
  return true;
}

// ---------------------------------------------------------- ZIP rename
struct RenameRequest {
  size_t index;          // entry index (ZIP: central-directory order; RAR: file-header order)
  std::string newBase;   // new base name, UTF-8; the folder part is preserved
};

inline std::string zipNewRawName(const ZipEntry& en, const std::string& newBase, bool& utf8Flag) {
  // Keep the folder's original bytes when nothing forces UTF-8: an OEM-encoded
  // folder ("Müller/" in CP850) must not be re-spelt through CP437.
  if (isAscii(newBase) && !(en.flags & 0x0800) && !en.nameFromUnicodeExtra) {
    std::string rawFolder, rawBase;
    splitInnerPath(en.rawName, rawFolder, rawBase);
    utf8Flag = false;
    return rawFolder + newBase;
  }
  std::string folder, base;
  splitInnerPath(en.nameUtf8, folder, base);
  const std::string full = folder + newBase;
  utf8Flag = !isAscii(full);
  return full;
}

// Extra field for a renamed record: the Unicode Path field describes the old
// name, so it is removed; earlier padding is removed so it can be re-sized.
inline std::string zipRenamedExtra(const std::string& extra) {
  std::vector<ZipExtraField> keep;
  for (auto& f : splitExtra(extra))
    if (f.id != 0x7075 && f.id != kZipPadding) keep.push_back(f);
  return joinExtra(keep);
}

inline Bytes zipLocalHeaderBytes(const ZipLocalHeader& h, uint16_t flags, const std::string& rawName,
                                 const std::string& extra) {
  Bytes b;
  put32(b, kZipLocal); put16(b, h.versionNeeded); put16(b, flags); put16(b, h.method);
  put16(b, h.time); put16(b, h.date); put32(b, h.crc); put32(b, h.compressed32); put32(b, h.uncompressed32);
  put16(b, (uint16_t)rawName.size()); put16(b, (uint16_t)extra.size());
  b.insert(b.end(), rawName.begin(), rawName.end());
  b.insert(b.end(), extra.begin(), extra.end());
  return b;
}

struct ZipPlanEntry {
  bool renamed = false;
  std::string rawName;   // name to write
  uint16_t flags = 0;    // general-purpose flags to write
  std::string cdExtra;   // central-directory extra (without Zip64; rebuilt on write)
};

inline std::vector<ZipPlanEntry> zipPlan(const ZipArchive& z, const std::vector<RenameRequest>& renames) {
  std::vector<ZipPlanEntry> plan(z.entries.size());
  for (size_t i = 0; i < z.entries.size(); ++i) {
    plan[i].rawName = z.entries[i].rawName;
    plan[i].flags = z.entries[i].flags;
    plan[i].cdExtra = z.entries[i].extra;
  }
  for (const auto& r : renames) {
    if (r.index >= plan.size()) continue;
    const ZipEntry& en = z.entries[r.index];
    bool utf8 = false;
    plan[r.index].renamed = true;
    plan[r.index].rawName = zipNewRawName(en, r.newBase, utf8);
    plan[r.index].flags = utf8 ? (uint16_t)(en.flags | 0x0800) : en.flags;
    plan[r.index].cdExtra = zipRenamedExtra(en.extra);
  }
  return plan;
}

// Rebuilds the Zip64 extra (0x0001) for a central-directory record.
inline std::string zipCdExtraWithZip64(const ZipEntry& en, const std::string& extra, uint64_t newOffset,
                                       bool& usesZip64) {
  std::vector<ZipExtraField> fields;
  for (auto& f : splitExtra(extra))
    if (f.id != 0x0001) fields.push_back(f);
  std::string z64;
  auto add64 = [&](uint64_t v) { for (int i = 0; i < 8; ++i) z64 += (char)(v >> (8 * i)); };
  if (en.rawUncompressed32 == 0xFFFFFFFFu) add64(en.uncompressedSize);
  if (en.rawCompressed32 == 0xFFFFFFFFu) add64(en.compressedSize);
  if (en.rawOffset32 == 0xFFFFFFFFu || newOffset >= 0xFFFFFFFFull) add64(newOffset);
  if (en.rawDisk16 == 0xFFFF) for (int i = 0; i < 4; ++i) z64 += '\0';
  usesZip64 = !z64.empty();
  if (usesZip64) fields.insert(fields.begin(), {0x0001, z64});
  return joinExtra(fields);
}

inline Bytes zipCentralDirectory(const ZipArchive& z, const std::vector<ZipPlanEntry>& plan,
                                 const std::vector<uint64_t>& newOffsets, bool& anyZip64) {
  Bytes cd;
  anyZip64 = false;
  for (size_t i = 0; i < z.entries.size(); ++i) {
    const ZipEntry& en = z.entries[i];
    const ZipPlanEntry& pe = plan[i];
    bool z64 = false;
    const std::string extra = zipCdExtraWithZip64(en, pe.cdExtra, newOffsets[i], z64);
    anyZip64 = anyZip64 || z64;
    const bool offset64 = en.rawOffset32 == 0xFFFFFFFFu || newOffsets[i] >= 0xFFFFFFFFull;
    put32(cd, kZipCentral); put16(cd, en.versionMade);
    put16(cd, z64 ? std::max<uint16_t>(en.versionNeeded, 45) : en.versionNeeded);
    put16(cd, pe.flags); put16(cd, en.method); put16(cd, en.time); put16(cd, en.date); put32(cd, en.crc);
    put32(cd, en.rawCompressed32); put32(cd, en.rawUncompressed32);
    put16(cd, (uint16_t)pe.rawName.size()); put16(cd, (uint16_t)extra.size()); put16(cd, (uint16_t)en.comment.size());
    put16(cd, en.rawDisk16); put16(cd, en.internalAttr); put32(cd, en.externalAttr);
    put32(cd, offset64 ? 0xFFFFFFFFu : (uint32_t)newOffsets[i]);
    cd.insert(cd.end(), pe.rawName.begin(), pe.rawName.end());
    cd.insert(cd.end(), extra.begin(), extra.end());
    cd.insert(cd.end(), en.comment.begin(), en.comment.end());
  }
  return cd;
}

// Zip64 end record + locator (when needed) + classic end record.
inline Bytes zipEndRecords(const ZipArchive& z, uint64_t cdOffset, uint64_t cdSize, bool anyZip64) {
  const uint64_t count = z.entries.size();
  const bool need64 = z.zip64 || anyZip64 || count >= 0xFFFF || cdSize >= 0xFFFFFFFFull ||
                      cdOffset >= 0xFFFFFFFFull;
  Bytes b;
  if (need64) {
    const uint64_t z64At = cdOffset + cdSize;
    put32(b, kZip64Eocd); put64(b, 44); put16(b, 45); put16(b, 45); put32(b, 0); put32(b, 0);
    put64(b, count); put64(b, count); put64(b, cdSize); put64(b, cdOffset);
    put32(b, kZip64Locator); put32(b, 0); put64(b, z64At); put32(b, 1);
  }
  put32(b, kZipEocd); put16(b, 0); put16(b, 0);
  put16(b, count >= 0xFFFF ? 0xFFFF : (uint16_t)count);
  put16(b, count >= 0xFFFF ? 0xFFFF : (uint16_t)count);
  put32(b, cdSize >= 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)cdSize);
  put32(b, cdOffset >= 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)cdOffset);
  put16(b, (uint16_t)z.comment.size());
  b.insert(b.end(), z.comment.begin(), z.comment.end());
  return b;
}

// In-place feasibility: each renamed local header must keep its exact length.
// The difference is absorbed by a padding extra field (needs >= 4 bytes).
struct ZipInPlacePatch {
  uint64_t offset;
  Bytes header;     // new local header, same length as the old one
  Bytes original;   // old bytes (undo journal)
};

// With `unfit` set, entries that do not fit are listed there instead of
// failing the whole plan (partial in-place rename).
inline bool zipPlanInPlace(ByteSource& src, const ZipArchive& z, const std::vector<ZipPlanEntry>& plan,
                           std::vector<ZipInPlacePatch>& patches, ws& why, std::vector<size_t>* unfit = nullptr) {
  patches.clear();
  if (unfit) unfit->clear();
  for (size_t i = 0; i < z.entries.size(); ++i) {
    if (!plan[i].renamed) continue;
    ZipLocalHeader lh;
    if (!readLocalHeader(src, z.entries[i].localOffset, lh, why)) return false;
    const std::string baseExtra = zipRenamedExtra(lh.extra);
    const size_t oldLen = lh.rawName.size() + lh.extra.size();
    const size_t newLen = plan[i].rawName.size() + baseExtra.size();
    if (newLen > oldLen) {
      why = L"a new name is longer than the space in its local header";
      if (unfit) { unfit->push_back(i); continue; }
      return false;
    }
    std::string extra = baseExtra;
    const size_t slack = oldLen - newLen;
    if (slack != 0) {
      if (slack < 4 || slack - 4 > 0xFFFF) {
        why = L"header slack of 1-3 bytes cannot hold a padding field";
        if (unfit) { unfit->push_back(i); continue; }
        return false;
      }
      std::vector<ZipExtraField> fields = splitExtra(baseExtra);
      fields.push_back({kZipPadding, std::string(slack - 4, '\0')});
      extra = joinExtra(fields);
    }
    ZipInPlacePatch p;
    p.offset = lh.offset;
    p.header = zipLocalHeaderBytes(lh, plan[i].flags, plan[i].rawName, extra);
    if (p.header.size() != lh.headerSize()) { why = L"internal size mismatch"; return false; }
    p.original.resize(lh.headerSize());
    if (!src.readAt(lh.offset, p.original.data(), p.original.size())) { why = L"read error"; return false; }
    patches.push_back(std::move(p));
  }
  return true;
}

// ---------------------------------------------------- undo journal (in place)
// Layout: "OLSUNDO1", u64 original size, u32 region count, regions
// {u64 offset, u64 length, bytes}, u32 CRC-32 of everything before it.
inline Bytes undoJournal(uint64_t originalSize, const std::vector<std::pair<uint64_t, Bytes>>& regions) {
  Bytes b;
  const char magic[] = "OLSUNDO1";
  b.insert(b.end(), magic, magic + 8);
  put64(b, originalSize);
  put32(b, (uint32_t)regions.size());
  for (const auto& r : regions) {
    put64(b, r.first);
    put64(b, r.second.size());
    b.insert(b.end(), r.second.begin(), r.second.end());
  }
  put32(b, crc32Of(b.data(), b.size()));
  return b;
}

inline bool parseUndoJournal(const Bytes& b, uint64_t& originalSize,
                             std::vector<std::pair<uint64_t, Bytes>>& regions) {
  regions.clear();
  if (b.size() < 24 || std::memcmp(b.data(), "OLSUNDO1", 8) != 0) return false;
  if (crc32Of(b.data(), b.size() - 4) != le32(&b[b.size() - 4])) return false;
  originalSize = le64(&b[8]);
  const uint32_t count = le32(&b[16]);
  size_t p = 20;
  for (uint32_t i = 0; i < count; ++i) {
    if (p + 16 > b.size() - 4) return false;
    const uint64_t off = le64(&b[p]), len = le64(&b[p + 8]);
    p += 16;
    if (len > b.size() - 4 - p) return false;
    regions.push_back({off, Bytes(b.begin() + p, b.begin() + p + len)});
    p += len;
  }
  return p == b.size() - 4;
}

inline bool applyUndo(RandomFile& f, uint64_t originalSize, const std::vector<std::pair<uint64_t, Bytes>>& regions) {
  for (const auto& r : regions)
    if (!f.writeAt(r.first, r.second.data(), r.second.size())) return false;
  return f.truncate(originalSize) && f.flush();
}

// The in-place rename. `writeJournal` must durably store the journal before
// the archive is touched (the caller owns where it lives); on any failure the
// archive is restored from the same regions.
inline bool zipRenameInPlace(RandomFile& f, const ZipArchive& z, const std::vector<ZipPlanEntry>& plan,
                             const std::vector<ZipInPlacePatch>& patches,
                             const std::function<bool(const Bytes&)>& writeJournal, ws& error) {
  const uint64_t oldSize = f.size();
  std::vector<uint64_t> offsets;
  for (const auto& en : z.entries) offsets.push_back(en.localOffset);
  bool any64 = false;
  const Bytes cd = zipCentralDirectory(z, plan, offsets, any64);
  const Bytes end = zipEndRecords(z, z.cdOffset, cd.size(), any64);
  std::vector<std::pair<uint64_t, Bytes>> regions;
  for (const auto& p : patches) regions.push_back({p.offset, p.original});
  Bytes tail((size_t)(oldSize - z.cdOffset));
  if (!f.readAt(z.cdOffset, tail.data(), tail.size())) { error = L"Cannot read the central directory."; return false; }
  regions.push_back({z.cdOffset, std::move(tail)});
  if (!writeJournal(undoJournal(oldSize, regions))) { error = L"Undo journal could not be written; archive untouched."; return false; }
  bool ok = true;
  for (const auto& p : patches) ok = ok && f.writeAt(p.offset, p.header.data(), p.header.size());
  ok = ok && f.writeAt(z.cdOffset, cd.data(), cd.size());
  ok = ok && f.writeAt(z.cdOffset + cd.size(), end.data(), end.size());
  ok = ok && f.truncate(z.cdOffset + cd.size() + end.size()) && f.flush();
  if (!ok) {
    applyUndo(f, oldSize, regions);
    error = L"Write failed; the archive was restored from the undo journal.";
  }
  return ok;
}

// Streams the archive to `dst` with rebuilt headers; data bytes are copied
// verbatim and checksummed per span so the copy can be re-verified.
struct CopyRecord {
  uint64_t srcOffset, dstOffset, length;
  uint32_t crc;
};

inline bool copyRange(ByteSource& src, ByteSink& dst, uint64_t from, uint64_t length, Bytes& buffer,
                      uint64_t& doneBytes, uint64_t totalBytes, const Progress& progress,
                      std::vector<CopyRecord>* records, ws& error) {
  CopyRecord rec{from, dst.position(), length, 0};
  uint64_t pos = 0;
  while (pos < length) {
    const size_t want = (size_t)std::min<uint64_t>(buffer.size(), length - pos);
    if (!src.readAt(from + pos, buffer.data(), want)) { error = L"Read error while copying the archive."; return false; }
    if (!dst.write(buffer.data(), want)) { error = L"Write error (disk full?) while copying the archive."; return false; }
    rec.crc = crc32Of(buffer.data(), want, rec.crc);
    pos += want;
    doneBytes += want;
    if (progress && !progress(doneBytes, totalBytes)) { error = L"Cancelled."; return false; }
  }
  if (records && length) records->push_back(rec);
  return true;
}

inline bool zipRewriteCopy(ByteSource& src, ByteSink& dst, const ZipArchive& z,
                           const std::vector<ZipPlanEntry>& plan, const Progress& progress,
                           std::vector<CopyRecord>& records, ws& error) {
  Bytes buffer(8u << 20);
  uint64_t done = 0;
  const uint64_t total = z.fileSize;
  records.clear();
  if (!copyRange(src, dst, 0, z.firstLocalOffset, buffer, done, total, progress, &records, error)) return false;
  std::vector<size_t> order(z.entries.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return z.entries[a].localOffset < z.entries[b].localOffset; });
  std::vector<uint64_t> newOffsets(z.entries.size());
  for (size_t k = 0; k < order.size(); ++k) {
    const size_t i = order[k];
    const ZipEntry& en = z.entries[i];
    const uint64_t spanEnd = k + 1 < order.size() ? z.entries[order[k + 1]].localOffset : z.cdOffset;
    ZipLocalHeader lh;
    if (!readLocalHeader(src, en.localOffset, lh, error)) return false;
    if (lh.dataOffset > spanEnd) { error = L"Local header overlaps the next entry."; return false; }
    newOffsets[i] = dst.position();
    Bytes header = plan[i].renamed ? zipLocalHeaderBytes(lh, plan[i].flags, plan[i].rawName, zipRenamedExtra(lh.extra))
                                   : Bytes();
    if (!plan[i].renamed) {
      header.resize(lh.headerSize());
      if (!src.readAt(lh.offset, header.data(), header.size())) { error = L"Read error."; return false; }
    }
    if (!dst.write(header.data(), header.size())) { error = L"Write error (disk full?)."; return false; }
    done += lh.headerSize();
    // Entry data, data descriptor and any bytes up to the next entry, verbatim.
    if (!copyRange(src, dst, lh.dataOffset, spanEnd - lh.dataOffset, buffer, done, total, progress, &records, error))
      return false;
  }
  const uint64_t cdOffset = dst.position();
  bool any64 = false;
  const Bytes cd = zipCentralDirectory(z, plan, newOffsets, any64);
  const Bytes end = zipEndRecords(z, cdOffset, cd.size(), any64);
  if (!dst.write(cd.data(), cd.size()) || !dst.write(end.data(), end.size())) { error = L"Write error (disk full?)."; return false; }
  if (progress) progress(total, total);
  return true;
}

// Re-reads the copied spans from the new file and compares CRC-32 values.
inline bool verifyCopyRecords(ByteSource& out, const std::vector<CopyRecord>& records, const Progress& progress,
                              ws& error) {
  Bytes buffer(8u << 20);
  uint64_t total = 0, done = 0;
  for (const auto& r : records) total += r.length;
  for (const auto& r : records) {
    uint32_t crc = 0;
    for (uint64_t pos = 0; pos < r.length;) {
      const size_t want = (size_t)std::min<uint64_t>(buffer.size(), r.length - pos);
      if (!out.readAt(r.dstOffset + pos, buffer.data(), want)) { error = L"Cannot re-read the new archive."; return false; }
      crc = crc32Of(buffer.data(), want, crc);
      pos += want;
      done += want;
      if (progress && !progress(done, total)) { error = L"Cancelled."; return false; }
    }
    if (crc != r.crc) { error = L"Copied data does not match the original (CRC-32)."; return false; }
  }
  return true;
}

// Structural verification of a renamed archive against the original listing.
inline bool verifyZipRename(ByteSource& renamed, const ZipArchive& before, const std::vector<ZipPlanEntry>& plan,
                            ws& error) {
  ZipArchive after;
  if (!parseZip(renamed, after, error)) { error = L"Renamed archive does not parse: " + error; return false; }
  if (after.entries.size() != before.entries.size()) { error = L"Entry count changed."; return false; }
  for (size_t i = 0; i < before.entries.size(); ++i) {
    const ZipEntry& a = after.entries[i];
    const ZipEntry& b = before.entries[i];
    if (a.rawName != plan[i].rawName || a.crc != b.crc || a.method != b.method ||
        a.compressedSize != b.compressedSize || a.uncompressedSize != b.uncompressedSize ||
        a.externalAttr != b.externalAttr || a.time != b.time || a.date != b.date || a.comment != b.comment) {
      error = L"Entry fields changed unexpectedly: " + ols::fromAscii(b.nameUtf8.c_str());
      return false;
    }
    ZipLocalHeader lh;
    if (!readLocalHeader(renamed, a.localOffset, lh, error)) return false;
    if (lh.rawName != a.rawName) { error = L"Local and central names disagree after rename."; return false; }
    if (plan[i].renamed && !verifyZipEntryCrc(renamed, a, error)) return false;
  }
  if (after.comment != before.comment) { error = L"Archive comment changed."; return false; }
  return true;
}

// ===================================================================== RAR
enum class RarFormat { None, Rar4, Rar5 };

struct RarBlock {
  uint64_t offset = 0;        // block start (CRC field)
  uint64_t headerSize = 0;    // whole header incl. CRC and size fields
  uint64_t dataSize = 0;      // data area following the header
  uint32_t type = 0;          // RAR5: 1 main,2 file,3 service,4 crypt,5 end; RAR4: 0x73..0x7b
  uint64_t flags = 0;         // header flags
  bool isFile = false, isService = false;
  std::string name;           // UTF-8 (files and service headers)
  std::string rawNameField;   // RAR4: name bytes as stored
  uint64_t unpackedSize = 0;
  uint32_t dataCrc = 0;
  bool hasDataCrc = false;
  bool directory = false, encrypted = false, splitBefore = false, splitAfter = false;
  // RAR5 file/service: offset of the name length vint inside the header bytes
  size_t nameFieldStart = 0, nameFieldEnd = 0;
  // RAR5 layout for header rebuilds: where the fields after the data-size vint
  // start, and the extra-area size.
  size_t fieldsStart = 0;
  uint64_t extraSize = 0;
  // RAR5 redirection record (0x05): 1 Unix symlink, 2 Windows symlink,
  // 3 junction, 4 hard link, 5 file copy. Types 4 and 5 name another entry.
  uint64_t redirectType = 0;
  std::string redirectTarget;
};

struct RarArchive {
  RarFormat format = RarFormat::None;
  uint64_t fileSize = 0, sfxSize = 0, signatureSize = 0;
  uint64_t archiveFlags = 0;
  bool solid = false, volume = false, locked = false, encryptedHeaders = false, recovery = false;
  bool quickOpen = false, authenticity = false;
  std::vector<RarBlock> blocks;
  std::vector<size_t> files;  // indices into blocks of file headers, archive order
  uint64_t endOfArchive = 0;  // offset just past the end-of-archive block
  ws problem;                 // non-empty: cannot be renamed by the built-in writer
};

inline bool readVint(const uint8_t* p, size_t n, size_t& pos, uint64_t& v) {
  v = 0;
  for (int shift = 0; shift < 70 && pos < n; shift += 7) {
    const uint8_t b = p[pos++];
    v |= (uint64_t)(b & 0x7f) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}

inline void putVint(Bytes& b, uint64_t v) {
  do {
    uint8_t c = v & 0x7f;
    v >>= 7;
    if (v) c |= 0x80;
    b.push_back(c);
  } while (v);
}

// RAR4 extended Unicode file-name encoding (documented in RARLAB's format notes).
inline ws decodeRar4Unicode(const std::string& oem, const uint8_t* enc, size_t encSize) {
  ws out;
  size_t p = 0;
  if (encSize == 0) return out;
  const uint8_t high = enc[p++];
  unsigned flags = 0, bits = 0;
  while (p < encSize) {
    if (bits == 0) { flags = enc[p++]; bits = 8; }
    if (p > encSize) break;
    const unsigned mode = flags >> 6;
    flags = (flags << 2) & 0xff;
    bits -= 2;
    switch (mode) {
      case 0: if (p < encSize) out += (wchar_t)enc[p++]; break;
      case 1: if (p < encSize) out += (wchar_t)(enc[p++] + (high << 8)); break;
      case 2:
        if (p + 1 < encSize) { out += (wchar_t)(enc[p] + (enc[p + 1] << 8)); p += 2; }
        else p = encSize;
        break;
      case 3: {
        if (p >= encSize) break;
        const uint8_t len = enc[p++];
        if (len & 0x80) {
          if (p >= encSize) break;
          const uint8_t corr = enc[p++];
          for (int k = (len & 0x7f) + 2; k > 0 && out.size() < oem.size(); --k)
            out += (wchar_t)((((uint8_t)oem[out.size()] + corr) & 0xff) + (high << 8));
        } else {
          for (int k = len + 2; k > 0 && out.size() < oem.size(); --k) out += (wchar_t)(uint8_t)oem[out.size()];
        }
        break;
      }
    }
  }
  return out;
}

// Encodes a name in the RAR4 Unicode form using only mode 2 (explicit 16-bit
// characters), which every RAR4 reader decodes. OEM part: ASCII with '_'.
inline std::string encodeRar4UnicodeName(const ws& name) {
  std::string oem;
  for (wchar_t c : name) oem += c < 0x80 ? (char)c : '_';
  std::string out = oem;
  out += '\0';
  out += '\0';  // high byte (unused by mode 2)
  for (size_t i = 0; i < name.size(); i += 4) {
    const size_t group = std::min<size_t>(4, name.size() - i);
    uint8_t flags = 0;
    for (size_t k = 0; k < group; ++k) flags |= (uint8_t)(2u << (6 - 2 * k));
    out += (char)flags;
    for (size_t k = 0; k < group; ++k) {
      out += (char)(name[i + k] & 0xff);
      out += (char)((name[i + k] >> 8) & 0xff);
    }
  }
  return out;
}

inline const uint8_t kRar4Sig[7] = {0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00};
inline const uint8_t kRar5Sig[8] = {0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00};

inline bool findRarSignature(ByteSource& src, RarArchive& r) {
  const size_t window = (size_t)std::min<uint64_t>(src.size(), 4u << 20);  // SFX stubs are small
  Bytes b(window);
  if (window < 7 || !src.readAt(0, b.data(), window)) return false;
  for (size_t i = 0; i + 7 <= window; ++i) {
    if (b[i] != 0x52) continue;
    if (i + 8 <= window && std::memcmp(&b[i], kRar5Sig, 8) == 0) {
      r.format = RarFormat::Rar5; r.sfxSize = i; r.signatureSize = 8; return true;
    }
    if (std::memcmp(&b[i], kRar4Sig, 7) == 0) {
      r.format = RarFormat::Rar4; r.sfxSize = i; r.signatureSize = 7; return true;
    }
  }
  return false;
}

inline bool parseRar5(ByteSource& src, RarArchive& r, ws& error) {
  uint64_t pos = r.sfxSize + r.signatureSize;
  const uint64_t n = src.size();
  while (pos < n) {
    uint8_t head[16];
    const size_t avail = (size_t)std::min<uint64_t>(16, n - pos);
    if (avail < 6 || !src.readAt(pos, head, avail)) { error = L"Truncated RAR5 block."; return false; }
    size_t q = 4;
    uint64_t hsize = 0;
    if (!readVint(head, avail, q, hsize) || hsize == 0 || hsize > (2u << 20)) { error = L"Bad RAR5 header size."; return false; }
    const size_t full = q + (size_t)hsize;
    if (pos + full > n) { error = L"Truncated RAR5 header."; return false; }
    Bytes h(full);
    if (!src.readAt(pos, h.data(), full)) { error = L"Read error."; return false; }
    if (crc32Of(h.data() + 4, full - 4) != le32(h.data())) { error = L"RAR5 header CRC mismatch at offset " + std::to_wstring(pos) + L"."; return false; }
    RarBlock b;
    b.offset = pos;
    b.headerSize = full;
    size_t p = q;
    uint64_t type = 0, flags = 0, extraSize = 0, dataSize = 0;
    if (!readVint(h.data(), full, p, type) || !readVint(h.data(), full, p, flags)) { error = L"Bad RAR5 header."; return false; }
    if ((flags & 1) && !readVint(h.data(), full, p, extraSize)) { error = L"Bad RAR5 header."; return false; }
    if ((flags & 2) && !readVint(h.data(), full, p, dataSize)) { error = L"Bad RAR5 header."; return false; }
    b.type = (uint32_t)type;
    b.flags = flags;
    b.dataSize = dataSize;
    b.fieldsStart = p;
    b.extraSize = extraSize;
    b.splitBefore = (flags & 0x08) != 0;
    b.splitAfter = (flags & 0x10) != 0;
    if (type == 1) {
      uint64_t af = 0;
      readVint(h.data(), full, p, af);
      r.archiveFlags = af;
      r.volume = (af & 0x0001) != 0;
      r.solid = (af & 0x0004) != 0;
      r.recovery = (af & 0x0008) != 0;
      r.locked = (af & 0x0010) != 0;
    } else if (type == 2 || type == 3) {
      uint64_t fileFlags = 0, unp = 0, attr = 0, comp = 0, os = 0, nameLen = 0;
      readVint(h.data(), full, p, fileFlags);
      readVint(h.data(), full, p, unp);
      readVint(h.data(), full, p, attr);
      if (fileFlags & 0x02) p += 4;
      if (fileFlags & 0x04) { if (p + 4 <= full) { b.dataCrc = le32(&h[p]); b.hasDataCrc = true; } p += 4; }
      readVint(h.data(), full, p, comp);
      readVint(h.data(), full, p, os);
      b.nameFieldStart = p;
      if (!readVint(h.data(), full, p, nameLen) || p + nameLen > full) { error = L"Bad RAR5 file name."; return false; }
      b.name.assign((const char*)&h[p], (size_t)nameLen);
      b.nameFieldEnd = p + (size_t)nameLen;
      b.unpackedSize = unp;
      b.directory = (fileFlags & 0x01) != 0;
      b.isFile = type == 2;
      b.isService = type == 3;
      // Extra area: file encryption record (type 0x01) marks encrypted data.
      if (extraSize && extraSize <= full) {
        size_t e = full - (size_t)extraSize;
        while (e < full) {
          uint64_t recSize = 0, recType = 0;
          size_t rp = e;
          if (!readVint(h.data(), full, rp, recSize)) break;
          const size_t recEnd = rp + (size_t)recSize;
          if (!readVint(h.data(), full, rp, recType) || recEnd > full) break;
          if (recType == 0x01) b.encrypted = true;
          if (recType == 0x05) {
            uint64_t redirType = 0, redirFlags = 0, len = 0;
            if (readVint(h.data(), recEnd, rp, redirType) && readVint(h.data(), recEnd, rp, redirFlags) &&
                readVint(h.data(), recEnd, rp, len) && rp + len <= recEnd) {
              b.redirectType = redirType;
              b.redirectTarget.assign((const char*)&h[rp], (size_t)len);
            }
          }
          e = recEnd;
        }
      }
      if (type == 3 && b.name == "QO") r.quickOpen = true;
    } else if (type == 4) {
      r.encryptedHeaders = true;
    }
    r.blocks.push_back(b);
    if (b.isFile) r.files.push_back(r.blocks.size() - 1);
    pos += full + dataSize;
    if (type == 4) break;  // the rest is encrypted
    if (type == 5) { r.endOfArchive = pos; break; }
  }
  if (r.encryptedHeaders) r.problem = L"Archive headers are encrypted (password required).";
  else if (r.endOfArchive == 0) { error = L"RAR5 archive has no end-of-archive block."; return false; }
  return true;
}

inline bool parseRar4(ByteSource& src, RarArchive& r, ws& error) {
  uint64_t pos = r.sfxSize + r.signatureSize;
  const uint64_t n = src.size();
  while (pos + 7 <= n) {
    uint8_t base[7];
    if (!src.readAt(pos, base, 7)) { error = L"Read error."; return false; }
    const uint16_t headCrc = le16(base);
    const uint8_t type = base[2];
    const uint16_t flags = le16(base + 3), hsize = le16(base + 5);
    if (hsize < 7 || pos + hsize > n) { error = L"Bad RAR4 header size."; return false; }
    Bytes h(hsize);
    if (!src.readAt(pos, h.data(), hsize)) { error = L"Read error."; return false; }
    RarBlock b;
    b.offset = pos;
    b.headerSize = hsize;
    b.type = type;
    b.flags = flags;
    if (type == 0x73) {  // main header
      r.archiveFlags = flags;
      r.volume = (flags & 0x0001) != 0;
      r.locked = (flags & 0x0004) != 0;
      r.solid = (flags & 0x0008) != 0;
      r.authenticity = (flags & 0x0020) != 0;
      r.recovery = (flags & 0x0040) != 0;
      r.encryptedHeaders = (flags & 0x0080) != 0;
      if (r.encryptedHeaders) { r.blocks.push_back(b); r.problem = L"Archive headers are encrypted (password required)."; return true; }
    }
    // Header CRC: low 16 bits of CRC-32 over the header after the CRC field.
    if ((crc32Of(h.data() + 2, hsize - 2) & 0xffff) != headCrc) {
      error = L"RAR4 header CRC mismatch at offset " + std::to_wstring(pos) + L".";
      return false;
    }
    uint64_t add = 0;
    if (flags & 0x8000) {
      if (hsize < 11) { error = L"Bad RAR4 header."; return false; }
      add = le32(&h[7]);
    }
    if (type == 0x74 || type == 0x7a) {
      if (hsize < 32) { error = L"Bad RAR4 file header."; return false; }
      uint64_t pack = le32(&h[7]), unp = le32(&h[11]);
      size_t p = 32;
      if (flags & 0x100) {
        if (hsize < 40) { error = L"Bad RAR4 file header."; return false; }
        pack |= (uint64_t)le32(&h[32]) << 32;
        unp |= (uint64_t)le32(&h[36]) << 32;
        p = 40;
      }
      const uint16_t nameSize = le16(&h[26]);
      if (p + nameSize > hsize) { error = L"Bad RAR4 file name."; return false; }
      b.rawNameField.assign((const char*)&h[p], nameSize);
      b.nameFieldStart = p;
      b.nameFieldEnd = p + nameSize;
      ws wide;
      if (flags & 0x200) {
        const size_t zero = b.rawNameField.find('\0');
        if (zero != std::string::npos)
          wide = decodeRar4Unicode(b.rawNameField.substr(0, zero), (const uint8_t*)b.rawNameField.data() + zero + 1,
                                   b.rawNameField.size() - zero - 1);
        else
          wide = fromUtf8(b.rawNameField);
      } else {
        wide = fromCp437(b.rawNameField);
      }
      b.name = toUtf8(wide);
      b.dataSize = pack;
      b.unpackedSize = unp;
      b.dataCrc = le32(&h[16]);
      b.hasDataCrc = true;
      b.directory = (flags & 0xe0) == 0xe0;
      b.encrypted = (flags & 0x04) != 0;
      b.splitBefore = (flags & 0x01) != 0;
      b.splitAfter = (flags & 0x02) != 0;
      b.isFile = type == 0x74;
      b.isService = type == 0x7a;
    } else {
      b.dataSize = add;
    }
    if (type == 0x78) r.recovery = true;
    if (type == 0x76 || type == 0x79) r.authenticity = true;
    r.blocks.push_back(b);
    if (b.isFile) r.files.push_back(r.blocks.size() - 1);
    pos += hsize + b.dataSize;
    if (type == 0x7b) { r.endOfArchive = pos; break; }
  }
  if (r.endOfArchive == 0) r.endOfArchive = pos;  // very old archives may lack the end block
  return true;
}

inline bool parseRar(ByteSource& src, RarArchive& r, ws& error) {
  r = RarArchive();
  r.fileSize = src.size();
  if (!findRarSignature(src, r)) { error = L"No RAR signature found."; return false; }
  const bool ok = r.format == RarFormat::Rar5 ? parseRar5(src, r, error) : parseRar4(src, r, error);
  if (!ok) return false;
  if (r.problem.empty()) {
    if (r.volume) r.problem = L"Multi-volume RAR sets are not renamed by the built-in writer.";
    else if (r.locked) r.problem = L"The archive is locked against modification.";
    else if (r.authenticity) r.problem = L"The archive carries an authenticity signature.";
  }
  for (size_t i : r.files)
    if (r.blocks[i].splitBefore || r.blocks[i].splitAfter) r.problem = L"Files are split across volumes.";
  return true;
}

// Rebuilds a file header with a new name. Everything else, including the data
// CRC and extra records, is kept; the header CRC is recomputed.
// Rebuilds a RAR5 file/service header with an optional new name and an
// optional new hard-link/file-copy target. Everything else, including the data
// CRC and all other extra records, is kept; sizes and the header CRC are
// recomputed.
inline bool rar5RebuildHeader(const RarBlock& b, const Bytes& old, const std::string* newName,
                              const std::string* newRedirectTarget, Bytes& out, ws& error) {
  size_t q = 4;
  uint64_t hsize = 0, type = 0, flags = 0, extraSize = 0, dataSize = 0;
  readVint(old.data(), old.size(), q, hsize);
  readVint(old.data(), old.size(), q, type);
  readVint(old.data(), old.size(), q, flags);
  if (flags & 1) readVint(old.data(), old.size(), q, extraSize);
  if (flags & 2) readVint(old.data(), old.size(), q, dataSize);
  if (q != b.fieldsStart || extraSize != b.extraSize || old.size() < extraSize) { error = L"Unexpected RAR5 header layout."; return false; }
  const size_t extraStart = old.size() - (size_t)extraSize;
  if (b.nameFieldEnd > extraStart) { error = L"Unexpected RAR5 header layout."; return false; }
  Bytes extra;
  for (size_t e = extraStart; e < old.size();) {
    uint64_t recSize = 0, recType = 0;
    size_t rp = e;
    if (!readVint(old.data(), old.size(), rp, recSize)) { error = L"Bad RAR5 extra record."; return false; }
    const size_t recEnd = rp + (size_t)recSize;
    if (recEnd > old.size() || !readVint(old.data(), recEnd, rp, recType)) { error = L"Bad RAR5 extra record."; return false; }
    if (recType == 0x05 && newRedirectTarget) {
      uint64_t redirType = 0, redirFlags = 0, len = 0;
      readVint(old.data(), recEnd, rp, redirType);
      readVint(old.data(), recEnd, rp, redirFlags);
      readVint(old.data(), recEnd, rp, len);
      Bytes rec;
      putVint(rec, 0x05);
      putVint(rec, redirType);
      putVint(rec, redirFlags);
      putVint(rec, newRedirectTarget->size());
      rec.insert(rec.end(), newRedirectTarget->begin(), newRedirectTarget->end());
      rec.insert(rec.end(), old.begin() + rp + (size_t)len, old.begin() + recEnd);  // any trailing fields
      putVint(extra, rec.size());
      extra.insert(extra.end(), rec.begin(), rec.end());
    } else {
      extra.insert(extra.end(), old.begin() + e, old.begin() + recEnd);
    }
    e = recEnd;
  }
  Bytes body;
  putVint(body, type);
  putVint(body, flags);
  if (flags & 1) putVint(body, extra.size());
  if (flags & 2) putVint(body, dataSize);
  body.insert(body.end(), old.begin() + b.fieldsStart, old.begin() + b.nameFieldStart);
  if (newName) {
    putVint(body, newName->size());
    body.insert(body.end(), newName->begin(), newName->end());
  } else {
    body.insert(body.end(), old.begin() + b.nameFieldStart, old.begin() + b.nameFieldEnd);
  }
  body.insert(body.end(), old.begin() + b.nameFieldEnd, old.begin() + extraStart);
  body.insert(body.end(), extra.begin(), extra.end());
  Bytes sizeV;
  putVint(sizeV, body.size());
  out.assign(4, 0);
  out.insert(out.end(), sizeV.begin(), sizeV.end());
  out.insert(out.end(), body.begin(), body.end());
  set32(out.data(), crc32Of(out.data() + 4, out.size() - 4));
  return true;
}

// Rebuilds a file header with a new name (RAR5 via rar5RebuildHeader).
inline bool rarRenamedHeader(const RarArchive& r, const RarBlock& b, const Bytes& oldHeader,
                             const std::string& newNameUtf8, Bytes& out, ws& error) {
  if (r.format == RarFormat::Rar5) return rar5RebuildHeader(b, oldHeader, &newNameUtf8, nullptr, out, error);
  // RAR4: name field length and HEAD_SIZE change; recompute 16-bit CRC.
  bool ok = false;
  const ws wide = fromUtf8(newNameUtf8, &ok);
  std::string field;
  uint16_t flags = (uint16_t)b.flags;
  if (isAscii(newNameUtf8)) { field = newNameUtf8; flags &= (uint16_t)~0x200; }
  else { field = encodeRar4UnicodeName(wide); flags |= 0x200; }
  if (field.size() > 0xFFFF) { error = L"Name too long."; return false; }
  out.assign(oldHeader.begin(), oldHeader.begin() + b.nameFieldStart);
  out.insert(out.end(), field.begin(), field.end());
  out.insert(out.end(), oldHeader.begin() + b.nameFieldEnd, oldHeader.end());
  if (out.size() > 0xFFFF) { error = L"Header too large."; return false; }
  set16(&out[3], flags);
  set16(&out[5], (uint16_t)out.size());
  set16(&out[26], (uint16_t)field.size());
  set16(&out[0], (uint16_t)(crc32Of(out.data() + 2, out.size() - 2) & 0xffff));
  return true;
}

// RAR5 main header without the locator record (it points at quick-open and
// recovery data, which the built-in writer does not carry over) and with the
// recovery-record flag cleared.
inline Bytes rar5MainHeaderWithoutLocator(const Bytes& old) {
  size_t q = 4;
  uint64_t hsize = 0, type = 0, flags = 0, extraSize = 0, arcFlags = 0, volNum = 0;
  readVint(old.data(), old.size(), q, hsize);
  const size_t bodyStart = q;
  readVint(old.data(), old.size(), q, type);
  readVint(old.data(), old.size(), q, flags);
  if (flags & 1) readVint(old.data(), old.size(), q, extraSize);
  if (flags & 2) { uint64_t d; readVint(old.data(), old.size(), q, d); }
  readVint(old.data(), old.size(), q, arcFlags);
  if (arcFlags & 0x0002) readVint(old.data(), old.size(), q, volNum);
  const size_t fieldsEnd = q;
  const size_t extraStart = old.size() - (size_t)extraSize;
  Bytes keptExtra;
  for (size_t e = extraStart; e < old.size();) {
    uint64_t recSize = 0, recType = 0;
    size_t rp = e;
    if (!readVint(old.data(), old.size(), rp, recSize)) break;
    const size_t recEnd = rp + (size_t)recSize;
    if (!readVint(old.data(), old.size(), rp, recType) || recEnd > old.size()) break;
    if (recType != 0x01) keptExtra.insert(keptExtra.end(), old.begin() + e, old.begin() + recEnd);
    e = recEnd;
  }
  (void)bodyStart;
  (void)fieldsEnd;
  Bytes body;
  putVint(body, type);
  uint64_t newFlags = flags & ~(uint64_t)1;
  if (!keptExtra.empty()) newFlags |= 1;
  putVint(body, newFlags);
  if (!keptExtra.empty()) putVint(body, keptExtra.size());
  putVint(body, arcFlags & ~(uint64_t)0x0008);
  if (arcFlags & 0x0002) putVint(body, volNum);
  // Any further main-header fields between volume number and extra area (none
  // are defined today) are preserved as-is.
  body.insert(body.end(), old.begin() + fieldsEnd, old.begin() + extraStart);
  body.insert(body.end(), keptExtra.begin(), keptExtra.end());
  Bytes out(4, 0), sizeV;
  putVint(sizeV, body.size());
  out.insert(out.end(), sizeV.begin(), sizeV.end());
  out.insert(out.end(), body.begin(), body.end());
  set32(out.data(), crc32Of(out.data() + 4, out.size() - 4));
  return out;
}

inline Bytes rar4MainHeaderWithoutRecovery(const Bytes& old) {
  Bytes out = old;
  set16(&out[3], (uint16_t)(le16(&out[3]) & ~0x0040));
  set16(&out[0], (uint16_t)(crc32Of(out.data() + 2, out.size() - 2) & 0xffff));
  return out;
}

inline bool rarDropBlock(const RarArchive& r, const RarBlock& b) {
  if (r.format == RarFormat::Rar5) return b.isService && (b.name == "QO" || b.name == "RR");
  return b.type == 0x78 || (b.isService && b.name == "RR");
}

struct RarRewriteReport {
  bool droppedRecovery = false, droppedQuickOpen = false;
};

inline bool rarRewriteCopy(ByteSource& src, ByteSink& dst, const RarArchive& r,
                           const std::vector<RenameRequest>& renames, const Progress& progress,
                           std::vector<CopyRecord>& records, RarRewriteReport& report, ws& error) {
  if (!r.problem.empty()) { error = r.problem; return false; }
  std::map<size_t, std::string> newNames;  // block index -> new full name
  for (const auto& rq : renames) {
    if (rq.index >= r.files.size()) continue;
    const RarBlock& b = r.blocks[r.files[rq.index]];
    std::string folder, base;
    splitInnerPath(b.name, folder, base);
    newNames[r.files[rq.index]] = folder + rq.newBase;
  }
  std::map<std::string, std::string> renamedPaths;  // old full name -> new full name
  for (const auto& nn : newNames) renamedPaths[r.blocks[nn.first].name] = nn.second;
  Bytes buffer(8u << 20);
  uint64_t done = 0;
  records.clear();
  if (!copyRange(src, dst, 0, r.sfxSize + r.signatureSize, buffer, done, r.fileSize, progress, &records, error)) return false;
  for (size_t i = 0; i < r.blocks.size(); ++i) {
    const RarBlock& b = r.blocks[i];
    if (rarDropBlock(r, b)) {
      if (b.name == "QO") report.droppedQuickOpen = true;
      else report.droppedRecovery = true;
      done += b.headerSize + b.dataSize;
      continue;
    }
    Bytes header((size_t)b.headerSize);
    if (!src.readAt(b.offset, header.data(), header.size())) { error = L"Read error."; return false; }
    Bytes out;
    const bool mainRar5 = r.format == RarFormat::Rar5 && b.type == 1;
    const bool mainRar4 = r.format == RarFormat::Rar4 && b.type == 0x73;
    auto it = newNames.find(i);
    const std::string* retarget = nullptr;
    if (r.format == RarFormat::Rar5 && (b.redirectType == 4 || b.redirectType == 5)) {
      auto t = renamedPaths.find(b.redirectTarget);
      if (t != renamedPaths.end()) retarget = &t->second;
    }
    if (retarget) {
      // Hard links and file copies name their source entry: follow the rename.
      if (!rar5RebuildHeader(b, header, it != newNames.end() ? &it->second : nullptr, retarget, out, error)) return false;
    } else if (it != newNames.end()) {
      if (!rarRenamedHeader(r, b, header, it->second, out, error)) return false;
    } else if (mainRar5) {
      out = rar5MainHeaderWithoutLocator(header);
    } else if (mainRar4 && r.recovery) {
      out = rar4MainHeaderWithoutRecovery(header);
    } else {
      out = std::move(header);
    }
    if (!dst.write(out.data(), out.size())) { error = L"Write error (disk full?)."; return false; }
    done += b.headerSize;
    if (!copyRange(src, dst, b.offset + b.headerSize, b.dataSize, buffer, done, r.fileSize, progress, &records, error))
      return false;
  }
  // Anything after the end-of-archive block (appended data) is kept.
  if (r.endOfArchive < r.fileSize &&
      !copyRange(src, dst, r.endOfArchive, r.fileSize - r.endOfArchive, buffer, done, r.fileSize, progress, &records, error))
    return false;
  if (progress) progress(r.fileSize, r.fileSize);
  return true;
}

inline bool verifyRarRename(ByteSource& renamed, const RarArchive& before, const std::vector<RenameRequest>& renames,
                            ws& error) {
  RarArchive after;
  if (!parseRar(renamed, after, error)) { error = L"Renamed archive does not parse: " + error; return false; }
  if (after.files.size() != before.files.size()) { error = L"File count changed."; return false; }
  std::map<size_t, std::string> expect;
  for (const auto& rq : renames) {
    std::string folder, base;
    splitInnerPath(before.blocks[before.files[rq.index]].name, folder, base);
    expect[rq.index] = folder + rq.newBase;
  }
  for (size_t k = 0; k < before.files.size(); ++k) {
    const RarBlock& a = after.blocks[after.files[k]];
    const RarBlock& b = before.blocks[before.files[k]];
    auto it = expect.find(k);
    const std::string& wantName = it == expect.end() ? b.name : it->second;
    std::string wantTarget = b.redirectTarget;
    if (b.redirectType == 4 || b.redirectType == 5) {
      for (const auto& e : expect)
        if (before.blocks[before.files[e.first]].name == b.redirectTarget) wantTarget = e.second;
    }
    if (a.name != wantName || a.dataSize != b.dataSize || a.unpackedSize != b.unpackedSize ||
        a.dataCrc != b.dataCrc || a.hasDataCrc != b.hasDataCrc || a.encrypted != b.encrypted ||
        a.redirectType != b.redirectType || a.redirectTarget != wantTarget) {
      error = L"File header changed unexpectedly: " + fromUtf8(b.name);
      return false;
    }
  }
  return true;
}

// ============================================================ orchestration
enum class ArchiveKind { None, Zip, Rar };

inline ArchiveKind archiveKindForName(const ws& name) {
  const ws n = ols::lower(name);
  auto ends = [&](const wchar_t* ext) {
    const size_t len = std::wcslen(ext);
    return n.size() > len && n.compare(n.size() - len, len, ext) == 0;
  };
  if (ends(L".zip")) return ArchiveKind::Zip;
  if (ends(L".rar")) return ArchiveKind::Rar;
  return ArchiveKind::None;
}

// One WinOLS project inside an archive.
struct ArchiveProject {
  size_t index = 0;          // ZIP: central-directory index; RAR: file-header order
  std::string innerPath;     // UTF-8, as stored ('/' or '\\' separators)
  uint64_t size = 0;         // uncompressed size
  bool encrypted = false;
};

struct ArchiveListing {
  ArchiveKind kind = ArchiveKind::None;
  ZipArchive zip;
  RarArchive rar;
  std::vector<ArchiveProject> projects;
  std::vector<std::string> allNames;  // every entry name (collision checks)
  ws problem;                         // why renaming is not possible ("" = renamable)
};

inline bool listArchive(ByteSource& src, ArchiveKind kind, ArchiveListing& out, ws& error) {
  out = ArchiveListing();
  out.kind = kind;
  if (kind == ArchiveKind::Zip) {
    if (!parseZip(src, out.zip, error)) return false;
    out.problem = out.zip.problem;
    for (const auto& en : out.zip.entries) {
      out.allNames.push_back(en.nameUtf8);
      if (en.isDirectory() || !endsWithOlsName(en.nameUtf8)) continue;
      out.projects.push_back({en.cdIndex, en.nameUtf8, en.uncompressedSize, en.encrypted()});
    }
    return true;
  }
  if (kind == ArchiveKind::Rar) {
    if (!parseRar(src, out.rar, error)) return false;
    out.problem = out.rar.problem;
    for (size_t k = 0; k < out.rar.files.size(); ++k) {
      const RarBlock& b = out.rar.blocks[out.rar.files[k]];
      out.allNames.push_back(b.name);
      if (b.directory || !endsWithOlsName(b.name)) continue;
      out.projects.push_back({k, b.name, b.unpackedSize, b.encrypted});
    }
    return true;
  }
  error = L"Not an archive.";
  return false;
}

// Platform hooks for the rename transaction.
struct ArchiveFileOps {
  struct WriteFile : ByteSink, virtual ByteSource {
    virtual bool flush() = 0;
  };
  virtual ~ArchiveFileOps() = default;
  virtual std::unique_ptr<ByteSource> openRead(const ws& path) = 0;
  virtual std::unique_ptr<RandomFile> openReadWrite(const ws& path) = 0;
  virtual std::unique_ptr<WriteFile> create(const ws& path) = 0;  // fails if it exists
  virtual bool writeDurable(const ws& path, const Bytes& data) = 0;
  virtual bool readAll(const ws& path, Bytes& data) = 0;
  virtual bool exists(const ws& path) = 0;
  virtual bool remove(const ws& path) = 0;
  virtual bool replace(const ws& target, const ws& replacement) = 0;  // atomic on one volume
  virtual uint64_t freeSpace(const ws& pathInFolder) = 0;
};

inline ws undoJournalPath(const ws& archive) { return archive + L".olsrenamer-undo"; }
inline ws tempArchivePath(const ws& archive) { return archive + L".olsrenamer-tmp"; }

// Rolls back an in-place rename interrupted by a crash or power loss. Call
// before reading an archive. Returns true when something was restored.
inline bool recoverInterruptedRename(ArchiveFileOps& ops, const ws& archive, ws& message) {
  const ws journal = undoJournalPath(archive);
  if (!ops.exists(journal)) return false;
  Bytes data;
  uint64_t originalSize = 0;
  std::vector<std::pair<uint64_t, Bytes>> regions;
  if (!ops.readAll(journal, data) || !parseUndoJournal(data, originalSize, regions)) {
    // A torn journal means the archive was never touched (the journal is
    // written and flushed before the first archive write).
    ops.remove(journal);
    message = L"Removed an incomplete undo journal; the archive had not been modified.";
    return true;
  }
  auto f = ops.openReadWrite(archive);
  if (!f || !applyUndo(*f, originalSize, regions)) {
    message = L"An interrupted rename could not be rolled back; undo journal kept: " + journal;
    return true;
  }
  f.reset();
  ops.remove(journal);
  message = L"Rolled back an interrupted in-place rename.";
  return true;
}

struct ArchiveRenameOptions {
  bool allowInPlace = true;       // ZIP header patching when names fit
  // Full rewrite (temporary copy of the whole archive). When it is not allowed
  // or there is not enough free space, ZIP entries that fit are renamed in
  // place and the rest are reported in ArchiveRenameResult::skipped.
  bool allowRewrite = true;
  bool verifyCopiedData = true;   // re-read and CRC-check every copied byte
  std::function<bool(const ws& archive, const RarArchive& rar, const std::vector<RenameRequest>& renames,
                     ws& error)> externalRar;  // e.g. WinRAR's "rar rn"; empty = built-in writer
  std::function<bool(const ws& archive, const std::vector<RenameRequest>& renames, ws& error)> deepVerifyRar;
};

struct ArchiveRenameResult {
  bool ok = false;
  ws method;   // "in place", "rewrite", "WinRAR"
  ws error;
  ws notes;    // e.g. dropped recovery record
  std::vector<size_t> skipped;  // entry indices left unrenamed (need a rewrite)
};

inline ArchiveRenameResult renameInArchive(ArchiveFileOps& ops, const ws& archive, const ArchiveListing& listing,
                                           const std::vector<RenameRequest>& renames,
                                           const ArchiveRenameOptions& options, const Progress& progress) {
  ArchiveRenameResult res;
  if (renames.empty()) { res.ok = true; res.method = L"nothing to do"; return res; }
  // Re-read the archive: it must be exactly what was scanned.
  auto src = ops.openRead(archive);
  if (!src) { res.error = L"Cannot open the archive."; return res; }
  ArchiveListing now;
  ws err;
  if (!listArchive(*src, listing.kind, now, err)) { res.error = err; return res; }
  if (now.allNames != listing.allNames || src->size() != (listing.kind == ArchiveKind::Zip ? listing.zip.fileSize : listing.rar.fileSize)) {
    res.error = L"The archive changed since it was scanned; scan again.";
    return res;
  }
  if (listing.kind == ArchiveKind::Rar && options.externalRar && !listing.rar.encryptedHeaders) {
    src.reset();
    if (options.externalRar(archive, now.rar, renames, err)) {
      auto check = ops.openRead(archive);
      RarArchive after;
      ws verr;
      if (!check || !verifyRarRename(*check, now.rar, renames, verr)) {
        res.error = L"WinRAR reported success but verification failed: " + verr;
        return res;
      }
      res.ok = true;
      res.method = L"WinRAR";
      return res;
    }
    res.notes = L"WinRAR rename failed (" + err + L"); used the built-in writer. ";
    src = ops.openRead(archive);
    if (!src) { res.error = L"Cannot reopen the archive."; return res; }
  }
  if (!now.problem.empty()) { res.error = now.problem; return res; }

  const uint64_t margin = 64ull << 20;
  const bool canRewrite = options.allowRewrite && ops.freeSpace(archive) >= src->size() + margin;
  if (listing.kind == ArchiveKind::Zip && options.allowInPlace) {
    auto plan = zipPlan(now.zip, renames);
    std::vector<ZipInPlacePatch> patches;
    std::vector<size_t> unfit;
    ws why;
    bool inPlace = zipPlanInPlace(*src, now.zip, plan, patches, why);
    if (!inPlace && !canRewrite) {
      // No rewrite possible: rename what fits, report the rest.
      if (!zipPlanInPlace(*src, now.zip, plan, patches, why, &unfit)) { res.error = why; return res; }
      for (size_t i : unfit) {
        plan[i].renamed = false;
        plan[i].rawName = now.zip.entries[i].rawName;
        plan[i].flags = now.zip.entries[i].flags;
        plan[i].cdExtra = now.zip.entries[i].extra;
      }
      res.skipped = unfit;
      inPlace = !patches.empty();
      if (!inPlace) {
        res.error = options.allowRewrite ? L"Not enough free space for a temporary copy, and no new name fits in place."
                                         : L"No new name fits in place and full rewrites are switched off.";
        return res;
      }
      res.notes += std::to_wstring(unfit.size()) + L" name(s) need a full rewrite and were left unchanged" +
                   (options.allowRewrite ? ws(L" (not enough free space). ") : ws(L" (rewrites are switched off). "));
    }
    if (inPlace) {
      src.reset();
      auto rw = ops.openReadWrite(archive);
      if (!rw) { res.error = L"Cannot open the archive for writing (read-only or in use)."; return res; }
      const ws journal = undoJournalPath(archive);
      auto writeJournal = [&](const Bytes& b) { return ops.writeDurable(journal, b); };
      if (!zipRenameInPlace(*rw, now.zip, plan, patches, writeJournal, err)) {
        res.error = err;
        rw.reset();
        ops.remove(journal);
        return res;
      }
      rw.reset();
      auto check = ops.openRead(archive);
      ws verr;
      if (!check || !verifyZipRename(*check, now.zip, plan, verr)) {
        check.reset();
        ws msg;
        recoverInterruptedRename(ops, archive, msg);  // restores from the journal
        res.error = L"Verification failed (" + verr + L"); the archive was restored.";
        return res;
      }
      check.reset();
      ops.remove(journal);
      res.ok = true;
      res.method = L"in place";
      return res;
    }
    res.notes += L"In-place rename not possible (" + why + L"). ";
  }
  if (!canRewrite) {
    res.error = options.allowRewrite
                    ? L"Not enough free space for a temporary copy (" + std::to_wstring((src->size() + margin) >> 30) +
                          L" GiB needed on the archive's drive)."
                    : ws(L"This archive needs a full rewrite, and full rewrites are switched off.");
    return res;
  }

  // Full streamed rewrite to a temporary file next to the archive.
  const ws temp = tempArchivePath(archive);
  if (ops.exists(temp)) ops.remove(temp);
  auto out = ops.create(temp);
  if (!out) { res.error = L"Cannot create the temporary file."; return res; }
  std::vector<CopyRecord> records;
  RarRewriteReport report;
  bool ok = listing.kind == ArchiveKind::Zip
                ? zipRewriteCopy(*src, *out, now.zip, zipPlan(now.zip, renames), progress, records, err)
                : rarRewriteCopy(*src, *out, now.rar, renames, progress, records, report, err);
  ok = ok && out->flush();
  if (ok && options.verifyCopiedData) ok = verifyCopyRecords(*out, records, nullptr, err);
  if (ok) {
    ok = listing.kind == ArchiveKind::Zip ? verifyZipRename(*out, now.zip, zipPlan(now.zip, renames), err)
                                          : verifyRarRename(*out, now.rar, renames, err);
  }
  out.reset();
  src.reset();
  if (ok && listing.kind == ArchiveKind::Rar && options.deepVerifyRar) ok = options.deepVerifyRar(temp, renames, err);
  if (!ok) {
    ops.remove(temp);
    res.error = err.empty() ? ws(L"Rewrite failed.") : err;
    return res;
  }
  if (!ops.replace(archive, temp)) {
    ops.remove(temp);
    res.error = L"Could not replace the archive with the renamed copy.";
    return res;
  }
  if (report.droppedRecovery) res.notes += L"Recovery record removed (re-add it in WinRAR if needed). ";
  if (report.droppedQuickOpen) res.notes += L"Quick-open data removed. ";
  res.ok = true;
  res.method = L"rewrite";
  return res;
}

}  // namespace olsarc
