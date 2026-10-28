// UnRAR library adapter. Contains no RAR writing code.
//
// "UnRAR source code may be used in any software to handle RAR archives
// without limitations free of charge, but cannot be used to develop RAR
// (WinRAR) compatible archiver and to re-create RAR compression algorithm,
// which is proprietary. Distribution of modified UnRAR source code in separate
// form or as a part of other software is permitted, provided that full text of
// this paragraph, starting from "UnRAR source code" words, is included in
// license, or in documentation if license is not available, and in source code
// comments of resulting package." (third_party/unrar/license.txt)
#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#endif
#define RARDLL
#include "third_party/unrar/raros.hpp"  // defines _UNIX on non-Windows hosts
#include "third_party/unrar/dll.hpp"

#include <mutex>
#include <vector>

#include "ols_unrar.hpp"

namespace olsunrar {
namespace {

std::mutex& libraryMutex() {
  static std::mutex m;
  return m;
}

struct Session {
  const Callbacks* cb = nullptr;
  size_t current = 0;
  bool cancelled = false;
  bool needPassword = false;
};

std::string toUtf8(const wchar_t* w) {
  std::string o;
  for (; w && *w; ++w) {
    uint32_t u = (uint32_t)*w;
    if (sizeof(wchar_t) == 2 && u >= 0xd800 && u < 0xdc00 && w[1] >= 0xdc00 && w[1] < 0xe000) {
      u = 0x10000 + ((u - 0xd800) << 10) + ((uint32_t)w[1] - 0xdc00);
      ++w;
    }
    if (u < 0x80) o += (char)u;
    else if (u < 0x800) { o += (char)(0xc0 | (u >> 6)); o += (char)(0x80 | (u & 0x3f)); }
    else if (u < 0x10000) { o += (char)(0xe0 | (u >> 12)); o += (char)(0x80 | ((u >> 6) & 0x3f)); o += (char)(0x80 | (u & 0x3f)); }
    else { o += (char)(0xf0 | (u >> 18)); o += (char)(0x80 | ((u >> 12) & 0x3f)); o += (char)(0x80 | ((u >> 6) & 0x3f)); o += (char)(0x80 | (u & 0x3f)); }
  }
  for (auto& c : o) if (c == '\\') c = '/';
  return o;
}

int CALLBACK onMessage(UINT msg, LPARAM user, LPARAM p1, LPARAM p2) {
  Session* s = reinterpret_cast<Session*>(user);
  switch (msg) {
    case UCM_PROCESSDATA:
      if (s->cb->cancelled && s->cb->cancelled()) { s->cancelled = true; return -1; }
      if (s->cb->data && !s->cb->data(s->current, reinterpret_cast<const uint8_t*>(p1), (size_t)p2)) {
        s->cancelled = true;
        return -1;
      }
      return 1;
    case UCM_NEEDPASSWORD:
    case UCM_NEEDPASSWORDW:
      s->needPassword = true;
      return -1;  // no passwords: encrypted entries stay review-only
    case UCM_CHANGEVOLUME:
    case UCM_CHANGEVOLUMEW:
      return p2 == RAR_VOL_NOTIFY ? 1 : -1;  // never prompt for a missing volume
    case UCM_LARGEDICT:
      return 0;  // refuse dictionaries above UnRAR's default limit (memory safety)
  }
  return 0;
}

ws describe(int code) {
  switch (code) {
    case ERAR_BAD_DATA: return L"RAR data is corrupt (CRC error).";
    case ERAR_BAD_ARCHIVE: return L"Not a valid RAR archive.";
    case ERAR_UNKNOWN_FORMAT: return L"Unknown RAR format.";
    case ERAR_EOPEN: return L"Cannot open the archive.";
    case ERAR_EREAD: return L"Read error.";
    case ERAR_NO_MEMORY: return L"Not enough memory to decode.";
    case ERAR_MISSING_PASSWORD: case ERAR_BAD_PASSWORD: return L"Password required.";
    case ERAR_LARGE_DICT: return L"Dictionary larger than the decoder limit.";
    default: return L"UnRAR error " + std::to_wstring(code) + L".";
  }
}

}  // namespace

int libraryVersion() { return RARGetDllVersion(); }

bool streamEntries(const ws& archivePath, const Callbacks& cb, ws& error) {
  std::lock_guard<std::mutex> lock(libraryMutex());
  Session session;
  session.cb = &cb;
  RAROpenArchiveDataEx open{};
#ifdef _WIN32
  std::vector<wchar_t> path(archivePath.begin(), archivePath.end());
  path.push_back(0);
  open.ArcNameW = path.data();
#else
  std::string narrow;
  for (wchar_t c : archivePath) {
    uint32_t u = (uint32_t)c;
    if (u < 0x80) narrow += (char)u;
    else if (u < 0x800) { narrow += (char)(0xc0 | (u >> 6)); narrow += (char)(0x80 | (u & 0x3f)); }
    else { narrow += (char)(0xe0 | (u >> 12)); narrow += (char)(0x80 | ((u >> 6) & 0x3f)); narrow += (char)(0x80 | (u & 0x3f)); }
  }
  open.ArcName = &narrow[0];
#endif
  open.OpenMode = RAR_OM_EXTRACT;
  open.Callback = onMessage;
  open.UserData = reinterpret_cast<LPARAM>(&session);
  HANDLE h = RAROpenArchiveEx(&open);
  if (!h || open.OpenResult != ERAR_SUCCESS) {
    error = describe(open.OpenResult);
    if (h) RARCloseArchive(h);
    return false;
  }
  bool ok = true;
  size_t index = 0;
  RARHeaderDataEx header{};
  for (;;) {
    const int rc = RARReadHeaderEx(h, &header);
    if (rc == ERAR_END_ARCHIVE) break;
    if (rc != ERAR_SUCCESS) { error = describe(rc); ok = false; break; }
    EntryInfo info;
    info.index = index;
    info.nameUtf8 = toUtf8(header.FileNameW);
    info.unpackedSize = ((uint64_t)header.UnpSizeHigh << 32) | header.UnpSize;
    info.encrypted = (header.Flags & RHDF_ENCRYPTED) != 0;
    info.directory = (header.Flags & RHDF_DIRECTORY) != 0;
    const bool wanted = !info.directory && !info.encrypted && cb.want && cb.want(info);
    session.current = index;
    session.needPassword = false;
    const int prc = RARProcessFileW(h, wanted ? RAR_TEST : RAR_SKIP, nullptr, nullptr);
    if (session.cancelled) { error = L"Cancelled."; ok = false; break; }
    if (wanted && cb.finished)
      cb.finished(index, prc == ERAR_SUCCESS, prc == ERAR_SUCCESS ? ws() : describe(prc));
    if (prc != ERAR_SUCCESS && !wanted) { error = describe(prc); ok = false; break; }
    ++index;
    if (cb.cancelled && cb.cancelled()) { error = L"Cancelled."; ok = false; break; }
  }
  RARCloseArchive(h);
  return ok;
}

}  // namespace olsunrar
