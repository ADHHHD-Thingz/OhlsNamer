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
#include <mutex>
#include <thread>

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
  ws journalPath;
  std::vector<Item> items;
  std::vector<ws> scanErrors;
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
  if (h == INVALID_HANDLE_VALUE) {
    DWORD e=GetLastError();
    sc.scanErrors.push_back(dir+L": "+lastError(e));
    return;
  }
  std::vector<ws> subdirs;
  auto& taken = sc.names[ols::lower(dir)];
  BOOL more = TRUE;
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
  } while (!cancel && (more = FindNextFileW(h, &fd)));
  if (!cancel && !more) {
    DWORD e=GetLastError();
    if(e!=ERROR_NO_MORE_FILES)sc.scanErrors.push_back(dir+L": "+lastError(e));
  }
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
  sc.scanErrors.clear();
  sc.journalPath.clear();
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

inline bool readExactly(HANDLE h, uint8_t* dst, size_t count) {
  size_t got = 0;
  while (got < count) {
    DWORD n = 0;
    if (!ReadFile(h, dst + got, (DWORD)std::min<size_t>(count - got, 1 << 24), &n, nullptr) || n == 0)
      return false;
    got += n;
  }
  return true;
}

// Fast map finding loader. Reads the first 1 MiB, asks the core where the
// map/ID area ends (M1 version_data_start) and keeps reading only until the
// map area plus the first embedded ROM image are in memory. Later ROM versions
// (tuned copies) are never read. Whenever the boundary cannot be proven the
// loader falls back to the whole file, so results match a full read.
inline bool readProject(const ws& path, ols::Bytes& out, uint64_t& fileSize, ws& err) {
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
  fileSize = (uint64_t)sz.QuadPart;
  size_t loaded = (size_t)std::min<uint64_t>(fileSize, ols::kInitialLoadBytes);
  out.resize(loaded);
  bool ok = readExactly(h, out.data(), loaded);
  for (int round = 0; ok && round < 6 && loaded < fileSize; ++round) {
    const uint64_t need = ols::requiredLoadLength(out.data(), loaded, fileSize);
    if (need <= loaded) break;
    out.resize((size_t)need);
    ok = readExactly(h, out.data() + loaded, (size_t)need - loaded);
    loaded = (size_t)need;
  }
  CloseHandle(h);
  if (!ok) {
    err = L"The file could not be read completely.";
    return false;
  }
  return true;
}

inline bool readFile(const ws& path, ols::Bytes& out, ws& err) {
  uint64_t fileSize = 0;
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
  fileSize = (uint64_t)sz.QuadPart;
  out.resize((size_t)fileSize);
  const bool ok = readExactly(h, out.data(), out.size());
  CloseHandle(h);
  if (!ok) {
    err = L"The file could not be read completely.";
    return false;
  }
  return true;
}

// Analysis workers. Parsing is CPU-bound once files come from an SSD, so use
// every logical processor (16 on a Ryzen 7 7800X3D / 7700X: 8 cores, SMT),
// capped by memory: a worker can hold a large project buffer plus parsed map
// records (~256 MiB worst case seen in the corpus), and at most half of the
// currently free RAM is committed to the pool.
inline size_t analysisWorkerCount(size_t total) {
  if (total == 0) return 0;
  const unsigned logical = std::max(1u, std::thread::hardware_concurrency());
  size_t workers = std::min<size_t>(logical, 32);
  MEMORYSTATUSEX mem{};
  mem.dwLength = sizeof(mem);
  if (GlobalMemoryStatusEx(&mem)) {
    const unsigned long long perWorker = 256ull << 20;
    const unsigned long long byMemory = std::max<unsigned long long>(1, (mem.ullAvailPhys / 2) / perWorker);
    workers = (size_t)std::min<unsigned long long>(workers, byMemory);
  }
  return std::max<size_t>(1, std::min(workers, total));
}

// The detail dialog shows the first records only; keep that many per project
// so a 4,000-project scan does not hold millions of map labels in memory.
static constexpr size_t kStoredMapDetails = 40;

inline ws mapWarning(const ols::Analysis& a) {
  for (const ws& w : a.warnings)
    if (w.rfind(L"No map directory", 0) == 0 || w.rfind(L"Map directory", 0) == 0 || w.rfind(L"Read ", 0) == 0)
      return w;
  return L"";
}

inline void analyzeAll(Scan& sc, std::atomic<bool>& cancel,
                       const std::function<void(size_t, size_t)>& progress) {
  const size_t total = sc.items.size();
  if (total == 0) { progress(0, 0); return; }

  const size_t workerCount = analysisWorkerCount(total);
  std::atomic<size_t> next{0};
  size_t completed = 0;  // guarded by progressMutex
  std::mutex progressMutex;

  auto worker = [&]() {
    // Keep the window responsive while every core is busy parsing.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    ols::Bytes data;  // reused per worker to avoid re-allocating large buffers
    while (!cancel.load()) {
      const size_t i = next.fetch_add(1);
      if (i >= total) break;
      Item& it = sc.items[i];  // each index is claimed by exactly one worker
      ws err;
      uint64_t fileSize = 0;
      bool readOk = readProject(it.path(), data, fileSize, err);
      if (readOk) {
        it.a = ols::analyzeOls(data.data(), data.size(), fileSize);
        if (it.a.needsFullFile) {  // boundary unproven in the prefix: use the whole file
          readOk = readFile(it.path(), data, err);
          if (readOk) it.a = ols::analyzeOls(data.data(), data.size());
        }
      }
      if (data.capacity() > (64u << 20)) ols::Bytes().swap(data);  // don't pin huge buffers
      if (!readOk) {
        it.a.ok = false;
        it.status = L"Cannot read: " + err;
        it.failed = true;
      } else {
        if (it.a.mapDetails.size() > kStoredMapDetails) {
          it.a.mapDetails.resize(kStoredMapDetails);
          it.a.mapDetails.shrink_to_fit();
        }
        it.read = true;
        if (it.a.ok) {
          it.mapsText = std::to_wstring(it.a.definedCount);
          ws w = mapWarning(it.a);
          ws evidence = L"v" + std::to_wstring(it.a.formatVersion) + L" | ECU maker: " + it.a.ecuManufacturer +
                        L" | SW source: " + it.a.softwareSource;
          if (!it.a.renameSafe) it.status = L"Review only: " + evidence + (w.empty() ? ws() : L" | " + w);
          else it.status = evidence + (w.empty() ? ws(L" | Ready") : L" | Check: " + w);
        } else {
          it.status = it.a.error;
          it.failed = true;
        }
      }
      {
        std::lock_guard<std::mutex> lock(progressMutex);
        progress(++completed, total);
      }
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(workerCount);
  for (size_t i = 0; i < workerCount; ++i) workers.emplace_back(worker);
  for (auto& t : workers) t.join();
}

// Same rules as withFinalNames() in the web app: per folder, names of files that
// are not going to be renamed (other files, folders, unreadable projects) stay reserved.
inline ws preserveOriginalNameForPartialIdentity(const Item& it) {
  const auto& a = it.a;
  if (!ols::isUnknownNamePart(a.brand) && !ols::isUnknownNamePart(a.model) &&
      !ols::isUnknownNamePart(a.ecu))
    return a.proposed;

  ws originalStem = it.name;
  if (endsWithOls(originalStem)) originalStem.resize(originalStem.size() - 4);
  ws normalizedOriginal = ols::lower(ols::jsTrim(originalStem));
  if (normalizedOriginal.empty() || normalizedOriginal == L"project" ||
      normalizedOriginal == L"unknown" || normalizedOriginal == L"ols project" ||
      normalizedOriginal == L"untitled" || normalizedOriginal == L"new project")
    return a.proposed;

  // The parser cannot reliably assign words in an existing filename to missing
  // metadata slots. Keep the complete original stem as a labelled fallback
  // rather than silently replacing it with an "Unknown" component.
  ws proposed = a.proposed;
  size_t suffixAt = proposed.rfind(L" (");
  if (suffixAt == ws::npos || proposed.size() < 5 ||
      ols::lower(proposed.substr(proposed.size() - 4)) != L".ols")
    return a.proposed;
  ws base = proposed.substr(0, suffixAt);
  ws suffix = proposed.substr(suffixAt);
  // A stem this tool produced earlier ends with its own map-count suffix,
  // e.g. "Chrysler Voyager 0281010814 (17)". Drop it before comparing, so a
  // re-scan does not nest the old name: "... [Chrysler Voyager ... (17)] (17)".
  {
    const size_t open = originalStem.rfind(L" (");
    if (open != ws::npos && originalStem.size() > open + 3 && originalStem.back() == L')') {
      const ws inner = originalStem.substr(open + 2, originalStem.size() - open - 3);
      const bool count = inner == L"Nil" ||
          (!inner.empty() && std::all_of(inner.begin(), inner.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }));
      if (count) originalStem.resize(open);
    }
  }
  ws fallback = ols::filenamePart(originalStem);
  if (fallback.empty()) return a.proposed;
  if (base.empty()) base = fallback;
  else if (ols::lower(base) != ols::lower(fallback)) base += L" [" + fallback + L"]";
  const size_t maxBase = 240 > suffix.size() ? 240 - suffix.size() : 1;
  if (base.size() > maxBase) base.resize(maxBase);
  while (!base.empty() && (base.back() == L' ' || base.back() == L'.')) base.pop_back();
  return base + suffix;
}

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
      it.a.proposed = preserveOriginalNameForPartialIdentity(it);
      it.finalName = ols::disambiguate(it.a.proposed, taken);
      it.willRename = it.a.renameSafe && it.finalName != it.name;
      if (!it.a.renameSafe) it.status = L"Review only - not renamed automatically";
      else if (ols::isUnknownNamePart(it.a.brand) || ols::isUnknownNamePart(it.a.model) ||
               ols::isUnknownNamePart(it.a.ecu))
        it.status = L"Partial identity; original filename retained in proposal";
      else if (!it.willRename) it.status = L"Already named";
    }
  }
}

inline size_t renameCount(const Scan& sc) {
  size_t n = 0;
  for (const auto& it : sc.items)
    if (it.willRename && !it.renamed) n++;
  return n;
}

inline bool appendRecoveryEvent(const ws& path,const ws& source,const ws& target,const ws& status,const ws& current) {
  auto quote=[](const ws& v){ws q=L"\"";for(wchar_t c:v){if(c==L'\"')q+=L'\"';q+=c;}return q+L"\"";};
  ws line=L"event,"+quote(source)+L","+quote(target)+L","+quote(status)+L","+quote(current)+L"\r\n";
  std::string bytes=ols::utf8(line);
  HANDLE h=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)return false;
  size_t off=0;bool ok=true;
  while(off<bytes.size()) {
    DWORD wrote=0;DWORD want=(DWORD)std::min<size_t>(bytes.size()-off,1<<20);
    if(!WriteFile(h,bytes.data()+off,want,&wrote,nullptr)||wrote==0){ok=false;break;}
    off+=wrote;
  }
  ok=FlushFileBuffers(h)&&ok;ok=CloseHandle(h)&&ok;
  return ok;
}
inline bool writeRecoveryJournal(const Scan& sc, ws& outPath) {
  SYSTEMTIME t; GetLocalTime(&t);
  wchar_t stamp[64];
  swprintf(stamp, 64, L"%04d-%02d-%02d %02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  auto quote=[](const ws& v){ws q=L"\"";for(wchar_t c:v){if(c==L'\"')q+=L'\"';q+=c;}return q+L"\"";};
  ws text=L"Record type,Source,Target,Status,Current path\r\n";
  for(const auto& it:sc.items) if(it.willRename&&!it.renamed)
    text+=L"planned,"+quote(it.path())+L","+quote(it.dir+L"\\"+it.finalName)+L",planned,"+quote(it.path())+L"\r\n";
  std::string bytes="\xEF\xBB\xBF"+ols::utf8(text);
  ws prefix=sc.root; if(!prefix.empty()&&prefix.back()!=L'\\')prefix+=L"\\";
  for(int attempt=1;attempt<=100;++attempt) {
    outPath=prefix+L"OLS Renamer recovery plan "+stamp+(attempt==1?ws():L" "+std::to_wstring(attempt))+L".csv";
    HANDLE h=CreateFileW(outPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) {DWORD e=GetLastError();if(e==ERROR_FILE_EXISTS||e==ERROR_ALREADY_EXISTS)continue;outPath.clear();return false;}
    size_t offset=0;bool ok=true;
    while(offset<bytes.size()) {
      DWORD wrote=0;DWORD want=(DWORD)std::min<size_t>(bytes.size()-offset,1<<20);
      if(!WriteFile(h,bytes.data()+offset,want,&wrote,nullptr)||wrote==0){ok=false;break;}
      offset+=wrote;
    }
    ok=FlushFileBuffers(h)&&ok;ok=CloseHandle(h)&&ok;
    if(!ok){DeleteFileW(outPath.c_str());outPath.clear();return false;}
    return true;
  }
  outPath.clear();return false;
}

// Renames in passes so chains (A->B while B->C) work; breaks swaps through a temp name.
// saveLogs=false skips the CSV recovery plan (and its per-move events); the
// caller also skips writeLog. Renaming itself is unchanged.
inline void renameAll(Scan& sc, const std::function<void(size_t, size_t)>& progress,
                      bool saveLogs = true) {
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
    if(!sc.journalPath.empty()) {
      ws source=it.dir+L"\\"+it.originalName;
      ws target=it.dir+L"\\"+it.finalName;
      ws current=ok?it.path():p.cur;
      if(!appendRecoveryEvent(sc.journalPath,source,target,ok?ws(L"renamed"):it.status,current))
        it.status+=L" [recovery journal update failed]";
    }
    progress(finished, total);
  };
  progress(0, total);
  if (total) {
    std::set<ws> movingSources;
    for (const auto& p : pend) movingSources.insert(ols::lower(p.cur));
    ws preflightError;
    for (const auto& p : pend) {
      const Item& it=sc.items[p.idx];
      if (GetFileAttributesW(p.cur.c_str())==INVALID_FILE_ATTRIBUTES) {
        preflightError=L"source file changed or disappeared before rename: "+p.cur;break;
      }
      ws target=it.dir+L"\\"+it.finalName;
      DWORD attr=GetFileAttributesW(target.c_str());
      if(attr!=INVALID_FILE_ATTRIBUTES && !movingSources.count(ols::lower(target)) && ols::lower(target)!=ols::lower(p.cur)) {
        preflightError=L"target already exists: "+target;break;
      }
      if(attr==INVALID_FILE_ATTRIBUTES) {
        DWORD e=GetLastError();
        if(e!=ERROR_FILE_NOT_FOUND&&e!=ERROR_PATH_NOT_FOUND) {preflightError=L"cannot validate target: "+target+L" ("+lastError(e)+L")";break;}
      }
    }
    if(!preflightError.empty()) {
      for(auto& p:pend) if(!p.done) finish(p,false,L"preflight failed; no files were moved: "+preflightError);
      return;
    }
    if(!saveLogs) {
      sc.journalPath.clear();
    } else if(!writeRecoveryJournal(sc,sc.journalPath)) {
      for(auto& p:pend) if(!p.done) finish(p,false,L"recovery plan could not be written; no files were moved");
      return;
    }
  }
  // Case-only renames need a temporary path on case-insensitive filesystems.
  for(auto& p:pend) {
    if(p.done) continue;
    Item& it=sc.items[p.idx];ws target=it.dir+L"\\"+it.finalName;
    if(ols::lower(target)==ols::lower(p.cur)&&target!=p.cur) {
      ws tmp;
      for(int attempt=1;attempt<=100;++attempt) {
        tmp=it.dir+L"\\~olsrenamer-"+std::to_wstring(GetCurrentProcessId())+L"-case-"+std::to_wstring(++tmpCounter)+L".tmp";
        if(GetFileAttributesW(tmp.c_str())==INVALID_FILE_ATTRIBUTES&&MoveFileExW(p.cur.c_str(),tmp.c_str(),0)) {p.cur=tmp;break;}
        if(attempt==100) finish(p,false,L"could not create temporary path for case-only rename");
      }
    }
  }
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
        // If this item was parked under a temporary name, restore it before reporting failure.
        if (ols::lower(p.cur) != ols::lower(it.path())) {
          ws originalPath=it.dir+L"\\"+it.originalName;
          if (MoveFileExW(p.cur.c_str(),originalPath.c_str(),0)) {
            p.cur=originalPath;
          } else {
            DWORD rollbackError=GetLastError();
            msg+=L"; ROLLBACK FAILED; file remains at "+p.cur+L" ("+lastError(rollbackError)+L")";
            ws recoveryPath=p.cur;
            size_t slash=recoveryPath.find_last_of(L"\\/");
            it.name=slash==ws::npos?recoveryPath:recoveryPath.substr(slash+1);
            size_t relSlash=it.rel.find_last_of(L"\\/");
            it.rel=(relSlash==ws::npos?ws():it.rel.substr(0,relSlash+1))+it.name;
          }
        }
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
  for (auto& p : pend) {  // anything left (unresolvable): restore parked files and report failures.
    if (p.done) continue;
    Item& it = sc.items[p.idx];
    ws orig = it.dir + L"\\" + it.originalName;
    if (ols::lower(p.cur) != ols::lower(orig)) {
      if (!MoveFileExW(p.cur.c_str(), orig.c_str(), 0)) {
        DWORD rollbackError=GetLastError();
        ws recoveryPath=p.cur;
        size_t slash=recoveryPath.find_last_of(L"\\/");
        it.name=slash==ws::npos?recoveryPath:recoveryPath.substr(slash+1);
        size_t relSlash=it.rel.find_last_of(L"\\/");
        it.rel=(relSlash==ws::npos?ws():it.rel.substr(0,relSlash+1))+it.name;
        finish(p,false,L"ROLLBACK FAILED; file remains at "+recoveryPath+L" ("+lastError(rollbackError)+L")");
        continue;
      }
    }
    finish(p, false, L"a file with that name already exists; parked file restored");
  }
}

inline void writeLog(const Scan& sc, ws& outPath) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t stamp[64];
  swprintf(stamp, 64, L"%04d-%02d-%02d %02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  ws prefix=sc.root;
  if(!prefix.empty()&&prefix.back()!=L'\\')prefix+=L"\\";
  ws base=prefix+L"OLS Renamer log "+stamp;
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
  HANDLE h=INVALID_HANDLE_VALUE;
  for(int attempt=1;attempt<=100;++attempt) {
    outPath=base+(attempt==1?ws():L" "+std::to_wstring(attempt))+L".csv";
    h=CreateFileW(outPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h!=INVALID_HANDLE_VALUE)break;
    DWORD e=GetLastError();
    if(e==ERROR_FILE_EXISTS||e==ERROR_ALREADY_EXISTS)continue;
    outPath.clear();return;
  }
  if(h==INVALID_HANDLE_VALUE){outPath.clear();return;}
  size_t off=0;bool ok=true;
  while(off<bytes.size()) {
    DWORD n=0;DWORD want=(DWORD)std::min<size_t>(bytes.size()-off,1<<20);
    if(!WriteFile(h,bytes.data()+off,want,&n,nullptr)||n==0){ok=false;break;}
    off+=n;
  }
  ok=FlushFileBuffers(h)&&ok;
  ok=CloseHandle(h)&&ok;
  if(!ok)outPath.clear();
}
}  // namespace olsfs
