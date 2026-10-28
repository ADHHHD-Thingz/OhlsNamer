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

#include "ols_archive_scan.hpp"
#include "ols_core.hpp"

namespace olsfs {
using ols::ws;

struct Item {
  ws dir;     // full directory path, no trailing slash (archive items: "<archive>|<inner folder>")
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
  // Projects inside ZIP/RAR archives (never extracted).
  int archive = -1;      // index into Scan::archives, -1 for plain files
  size_t project = 0;    // index into the archive's listing.projects
  ws innerFolder;        // folder inside the archive, '\\' separators, may be empty
  bool inArchive() const { return archive >= 0; }
  ws path() const {
    if (inArchive()) return dir.substr(0, dir.find(L'|')) + L" \x203a " + (innerFolder.empty() ? ws() : innerFolder + L"\\") + name;
    return dir + L"\\" + name;
  }
};

struct ArchiveInfo {
  ws path;   // archive file
  ws rel;    // relative to the scan root
  olsarc::ArchiveListing listing;
  ws note;   // recovery or listing remark
};

struct Scan {
  ws root;
  ws journalPath;
  std::vector<Item> items;
  std::vector<ArchiveInfo> archives;
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

// ------------------------------------------------- Win32 archive file access
struct WinFile : olsarc::RandomFile, olsarc::ArchiveFileOps::WriteFile {
  HANDLE h = INVALID_HANDLE_VALUE;
  uint64_t pos = 0;
  ~WinFile() override { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
  uint64_t size() const override {
    LARGE_INTEGER sz;
    return GetFileSizeEx(h, &sz) ? (uint64_t)sz.QuadPart : 0;
  }
  bool readAt(uint64_t off, void* dst, size_t n) override {
    uint8_t* p = static_cast<uint8_t*>(dst);
    while (n) {
      OVERLAPPED o{};
      o.Offset = (DWORD)off;
      o.OffsetHigh = (DWORD)(off >> 32);
      DWORD got = 0;
      const DWORD want = (DWORD)std::min<size_t>(n, 1u << 24);
      if (!ReadFile(h, p, want, &got, &o) || got == 0) return false;
      p += got; n -= got; off += got;
    }
    return true;
  }
  bool writeAt(uint64_t off, const void* src, size_t n) override {
    const uint8_t* p = static_cast<const uint8_t*>(src);
    while (n) {
      OVERLAPPED o{};
      o.Offset = (DWORD)off;
      o.OffsetHigh = (DWORD)(off >> 32);
      DWORD put = 0;
      const DWORD want = (DWORD)std::min<size_t>(n, 1u << 24);
      if (!::WriteFile(h, p, want, &put, &o) || put == 0) return false;
      p += put; n -= put; off += put;
    }
    return true;
  }
  bool write(const void* src, size_t n) override {
    if (!writeAt(pos, src, n)) return false;
    pos += n;
    return true;
  }
  uint64_t position() const override { return pos; }
  bool truncate(uint64_t newSize) override {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)newSize;
    return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && SetEndOfFile(h);
  }
  bool flush() override { return FlushFileBuffers(h) != 0; }
};

struct WinArchiveOps : olsarc::ArchiveFileOps {
  static std::unique_ptr<WinFile> open(const ws& path, DWORD access, DWORD share, DWORD disposition, DWORD flags) {
    auto f = std::make_unique<WinFile>();
    f->h = CreateFileW(path.c_str(), access, share, nullptr, disposition, flags, nullptr);
    if (f->h == INVALID_HANDLE_VALUE) return nullptr;
    return f;
  }
  std::unique_ptr<olsarc::ByteSource> openRead(const ws& path) override {
    return open(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN);
  }
  std::unique_ptr<olsarc::RandomFile> openReadWrite(const ws& path) override {
    return open(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
  }
  std::unique_ptr<WriteFile> create(const ws& path) override {
    return open(path, GENERIC_READ | GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN);
  }
  bool writeDurable(const ws& path, const olsarc::Bytes& data) override {
    auto f = open(path, GENERIC_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH);
    return f && f->write(data.data(), data.size()) && f->flush();
  }
  bool readAll(const ws& path, olsarc::Bytes& data) override {
    auto f = openRead(path);
    if (!f) return false;
    data.resize((size_t)f->size());
    return data.empty() || f->readAt(0, data.data(), data.size());
  }
  bool exists(const ws& path) override { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
  bool remove(const ws& path) override { return DeleteFileW(path.c_str()) != 0; }
  bool replace(const ws& target, const ws& replacement) override {
    return MoveFileExW(replacement.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  }
  uint64_t freeSpace(const ws& path) override {
    ws folder = path.substr(0, path.find_last_of(L"\\/") + 1);
    ULARGE_INTEGER avail;
    return GetDiskFreeSpaceExW(folder.c_str(), &avail, nullptr, nullptr) ? (uint64_t)avail.QuadPart : 0;
  }
};

// WinRAR's console Rar.exe, when installed: "rar rn" keeps recovery records and
// quick-open data, so it is preferred over the built-in writer for RAR files.
inline ws findRarExe() {
  for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
    for (const wchar_t* value : {L"exe64", L"exe32"}) {
      wchar_t buf[MAX_PATH * 2];
      DWORD bytes = sizeof(buf);
      if (RegGetValueW(root, L"SOFTWARE\\WinRAR", value, RRF_RT_REG_SZ, nullptr, buf, &bytes) == ERROR_SUCCESS) {
        ws exe = buf;
        const size_t slash = exe.find_last_of(L"\\/");
        if (slash != ws::npos) {
          ws rar = exe.substr(0, slash + 1) + L"Rar.exe";
          if (GetFileAttributesW(rar.c_str()) != INVALID_FILE_ATTRIBUTES) return rar;
        }
      }
    }
  }
  for (const wchar_t* candidate : {L"C:\\Program Files\\WinRAR\\Rar.exe", L"C:\\Program Files (x86)\\WinRAR\\Rar.exe"})
    if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) return candidate;
  return ws();
}

inline ws quoteArg(const ws& a) { return L"\"" + a + L"\""; }

// Runs "Rar.exe rn" once for all renames (one atomic WinRAR update).
inline bool winrarRename(const ws& rarExe, const ws& archive, const olsarc::RarArchive& rar,
                         const std::vector<olsarc::RenameRequest>& renames, ws& error) {
  ws cmd = quoteArg(rarExe) + L" rn -idq -y -- " + quoteArg(archive);
  for (const auto& r : renames) {
    if (r.index >= rar.files.size()) { error = L"bad index"; return false; }
    std::string folder, base;
    olsarc::splitInnerPath(rar.blocks[rar.files[r.index]].name, folder, base);
    ws oldName = olsarc::fromUtf8(rar.blocks[rar.files[r.index]].name);
    ws newName = olsarc::fromUtf8(folder + r.newBase);
    for (auto& c : oldName) if (c == L'/') c = L'\\';
    for (auto& c : newName) if (c == L'/') c = L'\\';
    cmd += L" " + quoteArg(oldName) + L" " + quoteArg(newName);
  }
  if (cmd.size() > 32000) { error = L"too many names for one WinRAR command"; return false; }
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> line(cmd.begin(), cmd.end());
  line.push_back(0);
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    error = L"cannot start Rar.exe";
    return false;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code != 0) { error = L"Rar.exe exit code " + std::to_wstring(code); return false; }
  return true;
}

inline bool endsWithOls(const ws& n) {
  return n.size() >= 4 && ols::lower(n.substr(n.size() - 4)) == L".ols";
}

inline void scanDir(Scan& sc, const ws& dir, const ws& rel, bool recursive, std::atomic<bool>& cancel,
                    const std::function<void(size_t)>& onFound, std::vector<std::pair<ws, ws>>* archives) {
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
    } else if (archives && olsarc::archiveKindForName(name) != olsarc::ArchiveKind::None) {
      archives->push_back({dir + L"\\" + name, rel.empty() ? name : rel + L"\\" + name});
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
    scanDir(sc, dir + L"\\" + d, rel.empty() ? d : rel + L"\\" + d, recursive, cancel, onFound, archives);
  }
}

// Lists the projects inside one archive (central directory / block headers
// only) and adds them as items. An interrupted in-place rename is rolled back
// first.
inline void addArchiveItems(Scan& sc, const ws& path, const ws& rel, const std::function<void(size_t)>& onFound) {
  WinArchiveOps ops;
  ArchiveInfo info;
  info.path = path;
  info.rel = rel;
  ws msg;
  if (olsarc::recoverInterruptedRename(ops, path, msg)) {
    info.note = msg;
    sc.scanErrors.push_back(path + L": " + msg);
  }
  auto src = ops.openRead(path);
  ws err;
  if (!src) { sc.scanErrors.push_back(path + L": " + lastError(GetLastError())); return; }
  if (!olsarc::listArchive(*src, olsarc::archiveKindForName(path), info.listing, err)) {
    sc.scanErrors.push_back(path + L": " + err);
    return;
  }
  if (info.listing.projects.empty()) return;
  if (info.listing.kind == olsarc::ArchiveKind::Rar && info.listing.rar.volume) {
    sc.scanErrors.push_back(path + L": multi-volume RAR sets are not supported.");
    return;
  }
  const int index = (int)sc.archives.size();
  // Names already used in each folder of the archive (collision checks).
  for (const auto& n : info.listing.allNames) {
    std::string folder, base;
    olsarc::splitInnerPath(olsarc::slashNormalized(n), folder, base);
    ws f = olsarc::fromUtf8(folder);
    if (!f.empty()) f.pop_back();
    for (auto& c : f) if (c == L'/') c = L'\\';
    sc.names[ols::lower(path + L"|" + f)].insert(ols::lower(olsarc::fromUtf8(base)));
  }
  for (size_t k = 0; k < info.listing.projects.size(); ++k) {
    std::string folder, base;
    olsarc::splitInnerPath(olsarc::slashNormalized(info.listing.projects[k].innerPath), folder, base);
    Item it;
    it.archive = index;
    it.project = k;
    it.innerFolder = olsarc::fromUtf8(folder);
    if (!it.innerFolder.empty()) it.innerFolder.pop_back();
    for (auto& c : it.innerFolder) if (c == L'/') c = L'\\';
    it.dir = path + L"|" + it.innerFolder;
    it.name = it.originalName = olsarc::fromUtf8(base);
    it.rel = rel + L" \x203a " + (it.innerFolder.empty() ? ws() : it.innerFolder + L"\\") + it.name;
    sc.items.push_back(std::move(it));
    onFound(sc.items.size());
  }
  sc.archives.push_back(std::move(info));
}

inline bool scanFolder(Scan& sc, ws root, bool recursive, std::atomic<bool>& cancel,
                       const std::function<void(size_t)>& onFound, bool lookInsideArchives = true) {
  while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
  if (root.size() == 2 && root[1] == L':') root += L"\\";  // "C:" -> "C:\"
  sc.root = root;
  sc.items.clear();
  sc.scanErrors.clear();
  sc.journalPath.clear();
  sc.names.clear();
  sc.archives.clear();
  DWORD attr = GetFileAttributesW(root.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
  ws base = root;
  if (base.size() == 3 && base[1] == L':' && base[2] == L'\\') base.pop_back();  // "C:\" -> "C:"
  std::vector<std::pair<ws, ws>> archives;
  scanDir(sc, base, L"", recursive, cancel, onFound, lookInsideArchives ? &archives : nullptr);
  for (const auto& a : archives) {
    if (cancel) break;
    addArchiveItems(sc, a.first, a.second, onFound);
  }
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

inline void finishItem(Item& it, bool readOk, const ws& err) {
  if (!readOk) {
    it.a.ok = false;
    it.status = L"Cannot read: " + err;
    it.failed = true;
    return;
  }
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

// Work units: one plain file, one ZIP project (random access, parallel), or
// one whole RAR archive (one sequential decoding pass, required for solid
// archives).
struct AnalysisJob {
  size_t item = 0;
  int rarArchive = -1;
  std::vector<size_t> rarItems;
};

inline void analyzeAll(Scan& sc, std::atomic<bool>& cancel,
                       const std::function<void(size_t, size_t)>& progress) {
  const size_t total = sc.items.size();
  if (total == 0) { progress(0, 0); return; }

  std::vector<AnalysisJob> jobs;
  std::map<int, size_t> rarJob;
  for (size_t i = 0; i < total; ++i) {
    const Item& it = sc.items[i];
    if (it.inArchive() && sc.archives[(size_t)it.archive].listing.kind == olsarc::ArchiveKind::Rar) {
      auto found = rarJob.find(it.archive);
      if (found == rarJob.end()) {
        AnalysisJob job;
        job.rarArchive = it.archive;
        found = rarJob.emplace(it.archive, jobs.size()).first;
        jobs.push_back(std::move(job));
      }
      jobs[found->second].rarItems.push_back(i);
    } else {
      AnalysisJob job;
      job.item = i;
      jobs.push_back(std::move(job));
    }
  }
  // Start the long sequential RAR passes first so they overlap the rest.
  std::stable_sort(jobs.begin(), jobs.end(), [](const AnalysisJob& a, const AnalysisJob& b) {
    return (a.rarArchive >= 0) > (b.rarArchive >= 0);
  });

  const size_t workerCount = std::min(analysisWorkerCount(total), jobs.size());
  std::atomic<size_t> next{0};
  size_t completed = 0;  // guarded by progressMutex
  std::mutex progressMutex;
  auto tick = [&]() {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress(++completed, total);
  };

  auto worker = [&]() {
    // Keep the window responsive while every core is busy parsing.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    ols::Bytes data;  // reused per worker to avoid re-allocating large buffers
    while (!cancel.load()) {
      const size_t j = next.fetch_add(1);
      if (j >= jobs.size()) break;
      const AnalysisJob& job = jobs[j];
      if (job.rarArchive >= 0) {
        const ArchiveInfo& arc = sc.archives[(size_t)job.rarArchive];
        std::map<size_t, size_t> itemOfProject;
        for (size_t i : job.rarItems) itemOfProject[sc.items[i].project] = i;
        ws err;
        olsarc::analyzeRarProjects(arc.path, arc.listing, [&](olsarc::ProjectAnalysis&& r) {
          auto f = itemOfProject.find(r.project);
          if (f == itemOfProject.end()) return;
          Item& it = sc.items[f->second];
          it.a = std::move(r.analysis);
          if (!r.ok) it.a.ok = false;
          finishItem(it, r.ok, r.error);
          tick();
        }, [&] { return cancel.load(); }, err);
        continue;
      }
      Item& it = sc.items[job.item];  // each index is claimed by exactly one worker
      ws err;
      bool readOk = false;
      if (it.inArchive()) {
        const ArchiveInfo& arc = sc.archives[(size_t)it.archive];
        WinArchiveOps ops;
        auto src = ops.openRead(arc.path);
        if (!src) {
          err = lastError(GetLastError());
        } else {
          olsarc::ProjectAnalysis r = olsarc::analyzeZipProject(*src, arc.listing, it.project);
          readOk = r.ok;
          err = r.error;
          if (r.ok) it.a = std::move(r.analysis);
        }
      } else {
        uint64_t fileSize = 0;
        readOk = readProject(it.path(), data, fileSize, err);
        if (readOk) {
          it.a = ols::analyzeOls(data.data(), data.size(), fileSize);
          if (it.a.needsFullFile) {  // boundary unproven in the prefix: use the whole file
            readOk = readFile(it.path(), data, err);
            if (readOk) it.a = ols::analyzeOls(data.data(), data.size());
          }
        }
        if (data.capacity() > (64u << 20)) ols::Bytes().swap(data);  // don't pin huge buffers
      }
      finishItem(it, readOk, err);
      tick();
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
  return ols::finalProposedName(it.a, it.name);
}

inline void computeFinal(Scan& sc) {
  std::map<ws, std::vector<size_t>> groups;
  for (size_t i = 0; i < sc.items.size(); i++) groups[ols::lower(sc.items[i].dir)].push_back(i);
  for (auto& g : groups) {
    std::set<ws> taken = sc.names[g.first];
    // Only files that may be renamed release their current names; a review-only
    // file keeps its name, so nothing can be planned onto it.
    auto archiveProblem = [&](const Item& it) -> ws {
      return it.inArchive() ? sc.archives[(size_t)it.archive].listing.problem : ws();
    };
    for (size_t i : g.second)
      if (sc.items[i].a.ok && sc.items[i].a.renameSafe && archiveProblem(sc.items[i]).empty())
        taken.erase(ols::lower(sc.items[i].name));
    for (size_t i : g.second) {
      Item& it = sc.items[i];
      if (!it.a.ok) continue;
      it.a.proposed = preserveOriginalNameForPartialIdentity(it);
      if (!it.a.renameSafe || !archiveProblem(it).empty()) {
        // Shown for review only; it must not reserve a name another file could use.
        it.finalName = it.a.proposed;
        it.willRename = false;
        it.status = it.a.renameSafe ? L"Archive cannot be modified: " + archiveProblem(it)
                                    : ws(L"Review only - not renamed automatically");
        continue;
      }
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

struct ArchiveRenameSettings {
  bool allowRewrite = true;       // full temporary copy when names do not fit in place
  bool useWinRar = true;          // prefer Rar.exe "rn" for RAR archives when installed
  std::atomic<bool>* cancel = nullptr;
  // archive index, archives done, archives total, bytes done, bytes total
  std::function<void(size_t, size_t, size_t, uint64_t, uint64_t)> progress;
};

// Renames the projects inside each archive: one transaction per archive
// (in place when names fit, otherwise a verified temporary copy).
inline void renameArchives(Scan& sc, const ArchiveRenameSettings& settings) {
  std::map<int, std::vector<size_t>> byArchive;
  for (size_t i = 0; i < sc.items.size(); ++i)
    if (sc.items[i].inArchive() && sc.items[i].willRename && !sc.items[i].renamed)
      byArchive[sc.items[i].archive].push_back(i);
  const ws rarExe = settings.useWinRar ? findRarExe() : ws();
  size_t doneArchives = 0;
  for (auto& entry : byArchive) {
    ArchiveInfo& arc = sc.archives[(size_t)entry.first];
    std::vector<olsarc::RenameRequest> requests;
    std::map<size_t, size_t> itemOfEntry;  // archive entry index -> item
    for (size_t i : entry.second) {
      const Item& it = sc.items[i];
      const size_t entryIndex = arc.listing.projects[it.project].index;
      requests.push_back({entryIndex, olsarc::toUtf8(it.finalName)});
      itemOfEntry[entryIndex] = i;
    }
    auto markAll = [&](bool ok, const ws& status) {
      for (size_t i : entry.second) {
        Item& it = sc.items[i];
        it.failed = !ok;
        it.status = status;
        if (ok) { it.renamed = true; it.name = it.finalName; }
      }
    };
    if (settings.cancel && settings.cancel->load()) { markAll(false, L"Not renamed: cancelled"); continue; }
    olsarc::ArchiveRenameOptions options;
    options.allowRewrite = settings.allowRewrite;
    if (!rarExe.empty())
      options.externalRar = [&](const ws& archive, const olsarc::RarArchive& rar,
                                const std::vector<olsarc::RenameRequest>& renames, ws& error) {
        return winrarRename(rarExe, archive, rar, renames, error);
      };
    const size_t index = (size_t)entry.first;
    const size_t total = byArchive.size();
    auto progress = [&](uint64_t done, uint64_t all) {
      if (settings.progress) settings.progress(index, doneArchives, total, done, all);
      return !(settings.cancel && settings.cancel->load());
    };
    WinArchiveOps ops;
    const olsarc::ArchiveRenameResult res = olsarc::renameInArchive(ops, arc.path, arc.listing, requests, options, progress);
    ++doneArchives;
    if (!res.ok) {
      markAll(false, L"Not renamed: " + res.error);
    } else {
      markAll(true, L"Renamed inside archive (" + res.method + L")" + (res.notes.empty() ? ws() : L" | " + res.notes));
      for (size_t skipped : res.skipped) {
        auto f = itemOfEntry.find(skipped);
        if (f == itemOfEntry.end()) continue;
        Item& it = sc.items[f->second];
        it.renamed = false;
        it.name = it.originalName;
        it.failed = true;
        it.status = L"Not renamed: the new name needs a full archive rewrite (" +
                    ws(settings.allowRewrite ? L"not enough free space" : L"rewrites are switched off") + L")";
      }
    }
    if (!sc.journalPath.empty()) {
      for (size_t i : entry.second) {
        const Item& it = sc.items[i];
        appendRecoveryEvent(sc.journalPath, it.dir + L"\\" + it.originalName, it.dir + L"\\" + it.finalName,
                            it.renamed ? ws(L"renamed inside archive") : it.status, it.path());
      }
    }
  }
}

// Renames plain files in passes so chains (A->B while B->C) work; breaks swaps
// through a temp name. saveLogs=false skips the CSV recovery plan (and its
// per-move events); the caller also skips writeLog.
inline void renamePlainFiles(Scan& sc, const std::function<void(size_t, size_t)>& progress, bool saveLogs) {
  struct P {
    size_t idx;
    ws cur;
    bool done = false;
  };
  std::vector<P> pend;
  for (size_t i = 0; i < sc.items.size(); i++)
    if (sc.items[i].willRename && !sc.items[i].renamed && !sc.items[i].inArchive())
      pend.push_back({i, sc.items[i].path()});
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

// Plain files first, then one transaction per archive.
inline void renameAll(Scan& sc, const std::function<void(size_t, size_t)>& progress, bool saveLogs = true,
                      const ArchiveRenameSettings* archives = nullptr) {
  renamePlainFiles(sc, progress, saveLogs);
  const bool anyArchive = std::any_of(sc.items.begin(), sc.items.end(), [](const Item& it) {
    return it.inArchive() && it.willRename && !it.renamed;
  });
  if (!anyArchive) return;
  if (!archives) {
    for (auto& it : sc.items)
      if (it.inArchive() && it.willRename && !it.renamed) { it.failed = true; it.status = L"Not renamed: archive renaming not enabled"; }
    return;
  }
  if (saveLogs && sc.journalPath.empty() && !writeRecoveryJournal(sc, sc.journalPath)) {
    for (auto& it : sc.items)
      if (it.inArchive() && it.willRename && !it.renamed) {
        it.failed = true;
        it.status = L"Not renamed: recovery plan could not be written";
      }
    return;
  }
  renameArchives(sc, *archives);
}

inline void writeLog(const Scan& sc, ws& outPath) {
  outPath.clear();
  if (std::none_of(sc.items.begin(), sc.items.end(), [](const Item& it) { return it.willRename; })) return;
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
