// Windows file-system layer: scan, analyse, plan names, rename in place, write a log.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shlwapi.h>

#include <atomic>
#include <functional>

#include "ols_core.hpp"

namespace olsfs {
using ols::ws;

struct Item {
  ws dir;     // full directory path, no trailing slash
  ws name;    // current file name
  ws originalName;  // name when scanned
  ws rel;     // path relative to the scanned root (for display)
  ols::Analysis a;
  ws finalName;
  ws mapsText;
  ws status;
  bool read = false;
  bool willRename = false;
  bool renamed = false;
  bool failed = false;
  ws path() const { return dir + L"\\" + name; }
};

struct Scan {
  ws root;
  std::vector<Item> items;
  std::map<ws, std::set<ws>> names;  // lower(dir) -> lower(entry names), files and folders
};

inline ws lastError(DWORD e) {
  wchar_t* buf = nullptr;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, e, 0, (LPWSTR)&buf, 0, nullptr);
  ws s = n && buf ? ws(buf, n) : (L"Error " + std::to_wstring(e));
  if (buf) LocalFree(buf);
  while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
  return s;
}

inline bool endsWithOls(const ws& n) {
  return n.size() >= 4 && ols::lower(n.substr(n.size() - 4)) == L".ols";
}

inline void scanDir(Scan& sc, const ws& dir, const ws& rel, bool recursive, std::atomic<bool>& cancel,
                    const std::function<void(size_t)>& onFound) {
  if (cancel) return;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                              nullptr, FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) return;
  std::vector<ws> subdirs;
  auto& taken = sc.names[ols::lower(dir)];
  do {
    ws name = fd.cFileName;
    if (name == L"." || name == L"..") continue;
    taken.insert(ols::lower(name));
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (recursive && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) subdirs.push_back(name);
    } else if (endsWithOls(name)) {
      Item it;
      it.dir = dir;
      it.name = name;
      it.originalName = name;
      it.rel = rel.empty() ? name : rel + L"\\" + name;
      sc.items.push_back(std::move(it));
      onFound(sc.items.size());
    }
  } while (!cancel && FindNextFileW(h, &fd));
  FindClose(h);
  for (const ws& d : subdirs) {
    if (cancel) return;
    scanDir(sc, dir + L"\\" + d, rel.empty() ? d : rel + L"\\" + d, recursive, cancel, onFound);
  }
}

inline bool scanFolder(Scan& sc, ws root, bool recursive, std::atomic<bool>& cancel,
                       const std::function<void(size_t)>& onFound) {
  while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
  if (root.size() == 2 && root[1] == L':') root += L"\\";  // "C:" -> "C:\"
  sc.root = root;
  sc.items.clear();
  sc.names.clear();
  DWORD attr = GetFileAttributesW(root.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
  ws base = root;
  if (base.size() == 3 && base[1] == L':' && base[2] == L'\\') base.pop_back();  // "C:\" -> "C:"
  scanDir(sc, base, L"", recursive, cancel, onFound);
  std::sort(sc.items.begin(), sc.items.end(), [](const Item& a, const Item& b) {
    return StrCmpLogicalW(a.rel.c_str(), b.rel.c_str()) < 0;
  });
  return true;
}

inline bool readFile(const ws& path, ols::Bytes& out, ws& err) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    err = lastError(GetLastError());
    return false;
  }
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (1LL << 30)) {
    CloseHandle(h);
    err = L"File is larger than 1 GB.";
    return false;
  }
  out.resize((size_t)sz.QuadPart);
  size_t got = 0;
  while (got < out.size()) {
    DWORD n = 0;
    if (!ReadFile(h, out.data() + got, (DWORD)std::min<size_t>(out.size() - got, 1 << 24), &n, nullptr) ||
        n == 0)
      break;
    got += n;
  }
  CloseHandle(h);
  if (got != out.size()) {
    err = L"The file could not be read completely.";
    return false;
  }
  return true;
}

inline ws mapWarning(const ols::Analysis& a) {
  for (const ws& w : a.warnings)
    if (w.rfind(L"No map directory", 0) == 0 || w.rfind(L"Map directory", 0) == 0 || w.rfind(L"Read ", 0) == 0)
      return w;
  return L"";
}

inline void analyzeAll(Scan& sc, std::atomic<bool>& cancel,
                       const std::function<void(size_t, size_t)>& progress) {
  size_t total = sc.items.size();
  for (size_t i = 0; i < total; i++) {
    if (cancel) return;
    Item& it = sc.items[i];
    ols::Bytes data;
    ws err;
    if (!readFile(it.path(), data, err)) {
      it.a.ok = false;
      it.status = L"Cannot read: " + err;
      it.failed = true;
    } else {
      it.a = ols::analyzeOls(data.data(), data.size());
      it.read = true;
      if (it.a.ok) {
        it.mapsText = std::to_wstring(it.a.definedCount);
        ws w = mapWarning(it.a);
        it.status = w.empty() ? ws(L"Ready") : L"Check: " + w;
      } else {
        it.status = it.a.error;
        it.failed = true;
      }
    }
    progress(i + 1, total);
  }
}

// Same rules as withFinalNames() in the web app: per folder, names of files that
// are not going to be renamed (other files, folders, unreadable projects) stay reserved.
inline void computeFinal(Scan& sc) {
  std::map<ws, std::vector<size_t>> groups;
  for (size_t i = 0; i < sc.items.size(); i++) groups[ols::lower(sc.items[i].dir)].push_back(i);
  for (auto& g : groups) {
    std::set<ws> taken = sc.names[g.first];
    for (size_t i : g.second)
      if (sc.items[i].a.ok) taken.erase(ols::lower(sc.items[i].name));
    for (size_t i : g.second) {
      Item& it = sc.items[i];
      if (!it.a.ok) continue;
      it.finalName = ols::disambiguate(it.a.proposed, taken);
      it.willRename = it.finalName != it.name;
      if (!it.willRename) it.status = L"Already named";
    }
  }
}

inline size_t renameCount(const Scan& sc) {
  size_t n = 0;
  for (const auto& it : sc.items)
    if (it.willRename && !it.renamed) n++;
  return n;
}

// Renames in passes so chains (A->B while B->C) work; breaks swaps through a temp name.
inline void renameAll(Scan& sc, const std::function<void(size_t, size_t)>& progress) {
  struct P {
    size_t idx;
    ws cur;
    bool done = false;
  };
  std::vector<P> pend;
  for (size_t i = 0; i < sc.items.size(); i++)
    if (sc.items[i].willRename && !sc.items[i].renamed) pend.push_back({i, sc.items[i].path()});
  size_t total = pend.size(), finished = 0;
  int tmpCounter = 0;
  auto finish = [&](P& p, bool ok, const ws& msg) {
    Item& it = sc.items[p.idx];
    p.done = true;
    finished++;
    if (ok) {
      it.renamed = true;
      it.failed = false;
      it.name = it.finalName;
      it.status = L"Renamed";
    } else {
      it.failed = true;
      it.status = L"Not renamed: " + msg;
    }
    progress(finished, total);
  };
  progress(0, total);
  for (size_t guard = 0; guard < total + 8 && finished < total; guard++) {
    bool any = false;
    std::set<ws> pendingCur;
    for (auto& p : pend)
      if (!p.done) pendingCur.insert(ols::lower(p.cur));
    std::vector<size_t> blocked;
    for (size_t k = 0; k < pend.size(); k++) {
      P& p = pend[k];
      if (p.done) continue;
      Item& it = sc.items[p.idx];
      ws target = it.dir + L"\\" + it.finalName;
      if (MoveFileExW(p.cur.c_str(), target.c_str(), 0)) {
        pendingCur.erase(ols::lower(p.cur));
        any = true;
        finish(p, true, L"");
        continue;
      }
      DWORD e = GetLastError();
      if ((e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) && pendingCur.count(ols::lower(target)) &&
          ols::lower(target) != ols::lower(p.cur)) {
        blocked.push_back(k);  // target is another file that is about to move away
      } else {
        ws msg = (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS)
                     ? ws(L"a file with that name already exists")
                     : lastError(e);
        pendingCur.erase(ols::lower(p.cur));
        finish(p, false, msg);
        any = true;
      }
    }
    if (!any && !blocked.empty()) {  // swap/cycle: park the first blocked file under a temp name
      P& p = pend[blocked[0]];
      Item& it = sc.items[p.idx];
      ws tmp = it.dir + L"\\~olsrenamer-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
               std::to_wstring(++tmpCounter) + L".tmp";
      if (MoveFileExW(p.cur.c_str(), tmp.c_str(), 0)) {
        p.cur = tmp;
      } else {
        finish(p, false, lastError(GetLastError()));
      }
    }
  }
  for (auto& p : pend) {  // anything left (unresolvable): put parked files back and report
    if (p.done) continue;
    Item& it = sc.items[p.idx];
    ws orig = it.path();
    if (ols::lower(p.cur) != ols::lower(orig)) MoveFileExW(p.cur.c_str(), orig.c_str(), 0);
    finish(p, false, L"a file with that name already exists");
  }
}

inline void writeLog(const Scan& sc, ws& outPath) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t stamp[64];
  swprintf(stamp, 64, L"%04d-%02d-%02d %02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  outPath = sc.root;
  if (!outPath.empty() && outPath.back() != L'\\') outPath += L"\\";
  outPath += ws(L"OLS Renamer log ") + stamp + L".csv";
  auto q = [](const ws& s) {
    ws o = L"\"";
    for (wchar_t c : s) {
      if (c == L'"') o += L'"';
      o += c;
    }
    return o + L"\"";
  };
  ws text = L"Folder,Old name,New name,Result\r\n";
  for (const auto& it : sc.items) {
    if (!it.willRename) continue;
    text += q(it.dir) + L"," + q(it.originalName) + L"," + q(it.finalName) + L"," + q(it.status) + L"\r\n";
  }
  std::string bytes = "\xEF\xBB\xBF" + ols::utf8(text);
  HANDLE h = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    outPath.clear();
    return;
  }
  DWORD n;
  WriteFile(h, bytes.data(), (DWORD)bytes.size(), &n, nullptr);
  CloseHandle(h);
}
}  // namespace olsfs
