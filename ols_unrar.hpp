// Streams RAR entries through the UnRAR library (third_party/unrar, extraction
// only; see THIRD_PARTY_NOTICES.md). Decoded bytes are handed to callbacks in
// memory; nothing is written to disk.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace olsunrar {
using ws = std::wstring;

struct EntryInfo {
  size_t index = 0;         // file-header order (matches olsarc::RarArchive::files)
  std::string nameUtf8;     // '/' separators
  uint64_t unpackedSize = 0;
  bool encrypted = false, directory = false;
};

struct Callbacks {
  // Return true to decode this entry (RAR_TEST), false to skip it.
  std::function<bool(const EntryInfo&)> want;
  // Decoded data for the current wanted entry. Return false to cancel the scan.
  std::function<bool(size_t index, const uint8_t* data, size_t size)> data;
  // Called after each wanted entry: ok=false with a reason on CRC/format errors.
  std::function<void(size_t index, bool ok, const ws& error)> finished;
  std::function<bool()> cancelled;
};

// One sequential pass over the archive (required for solid archives). Calls are
// serialized process-wide because UnRAR keeps some error state in globals; each
// call may still use UnRAR's own multi-threaded RAR5 decoder.
bool streamEntries(const ws& archivePath, const Callbacks& cb, ws& error);

// Header-only listing through UnRAR (RAR_OM_LIST: nothing is decoded, so it is
// fast even for solid archives). Used when the built-in header walk refuses an
// archive. Entries reached before an error are still delivered; the function
// then returns false with the reason. volume=true for multi-volume sets
// (their indices would not match streamEntries, so callers refuse them).
struct ListResult {
  bool volume = false;
  bool solid = false;
  size_t entries = 0;
};
bool listEntries(const ws& archivePath, const std::function<void(const EntryInfo&)>& entry, ListResult& result,
                 ws& error);

int libraryVersion();
}  // namespace olsunrar
