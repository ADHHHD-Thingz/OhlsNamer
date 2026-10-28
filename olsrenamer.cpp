// OLS Renamer - Win32 GUI. Renames WinOLS projects to
// "Brand Model ECU SoftwareVersion (named maps).ols" from their header.
#define NOMINMAX
#include <algorithm>
#include <climits>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>
#include <cwchar>

#include "ols_fs.hpp"

#include <commctrl.h>
#include <shlobj.h>

using ols::ws;

enum {
  ID_FOLDER = 101, ID_BROWSE, ID_SUB, ID_SCAN, ID_RENAME, ID_CANCEL, ID_LIST, ID_STATUS, ID_PROGRESS, ID_FILTER, ID_CHANGES, ID_SAVELOG,
  ID_ARCHIVES, ID_REWRITE, ID_TRANSFER,
  IDM_MOVE = 300, IDM_COPY, IDM_USENEW, IDM_SELECTALL
};
enum {
  WM_SCAN_FOUND = WM_APP + 1,   // wParam = files found so far
  WM_READ_PROGRESS,             // wParam = done, lParam = total
  WM_SCAN_DONE,
  WM_RENAME_PROGRESS,           // wParam = done, lParam = total
  WM_RENAME_DONE,
  WM_ARCHIVE_PROGRESS,          // wParam = per-mille of the current archive, lParam = archive index
  WM_TRANSFER_PROGRESS,         // wParam = done, lParam = total
  WM_TRANSFER_DONE
};

struct App {
  HWND hwnd = nullptr, edFolder, btnBrowse, chkSub, btnScan, btnRename, btnCancel, lblFolder, progress,
       status, list, edFilter, chkChanges, chkSaveLog = nullptr, chkArchives = nullptr, chkRewrite = nullptr,
       btnTransfer = nullptr;
  HFONT font = nullptr;
  int dpi = 96;
  std::vector<ws> recentDirs;
  std::vector<size_t> visibleItems;
  ws lastDirectory;
  olsfs::Scan scan;
  std::thread worker;
  std::atomic<bool> busy{false}, cancel{false};
  bool scanned = false;
  std::atomic<bool> renaming{false};
  ws lastLog;
  std::atomic<size_t> archivesDone{0}, archivesTotal{0};
  int sortColumn = 0;          // 0 File, 1 New name, 2 Maps, 3 Status
  bool sortDescending = false;
  bool useNewNames = false;    // move/extract with the planned new names
  olsfs::TransferSummary lastTransfer;
  olsfs::TransferMode lastMode = olsfs::TransferMode::Move;
  ws lastTransferDir;
} app;

static int S(int v) { return MulDiv(v, app.dpi, 96); }

static void setStatus(const ws& s) { SetWindowTextW(app.status, s.c_str()); }

static ws hex(size_t v) {
  wchar_t buf[32];
  swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%llX", static_cast<unsigned long long>(v));
  return buf;
}

static constexpr size_t kRecentDirectoryLimit = 10;
static constexpr wchar_t kSettingsKey[] = L"Software\\OhlsNamer";

static bool readRegString(HKEY key, const wchar_t* name, ws& value) {
  DWORD type = 0, bytes = 0;
  if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
      type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 65536)
    return false;
  std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
  if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes) != ERROR_SUCCESS)
    return false;
  buffer.back() = L'\0';
  value = buffer.data();
  return !value.empty();
}

static void loadSettings() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
  readRegString(key, L"LastDirectory", app.lastDirectory);
  app.recentDirs.clear();
  for (size_t i = 0; i < kRecentDirectoryLimit; ++i) {
    wchar_t name[32];
    swprintf(name, sizeof(name) / sizeof(name[0]), L"Recent%u", static_cast<unsigned>(i));
    ws path;
    if (readRegString(key, name, path) && std::find_if(app.recentDirs.begin(), app.recentDirs.end(),
        [&](const ws& existing) { return ols::lower(existing) == ols::lower(path); }) == app.recentDirs.end())
      app.recentDirs.push_back(path);
  }
  DWORD type = 0, bytes = sizeof(DWORD), recursive = BST_CHECKED;
  if (RegQueryValueExW(key, L"IncludeSubfolders", nullptr, &type,
                       reinterpret_cast<BYTE*>(&recursive), &bytes) == ERROR_SUCCESS &&
      type == REG_DWORD)
    SendMessageW(app.chkSub, BM_SETCHECK, recursive ? BST_CHECKED : BST_UNCHECKED, 0);
  DWORD saveLogs = 1;
  bytes = sizeof(DWORD);
  if (RegQueryValueExW(key, L"SaveLogs", nullptr, &type, reinterpret_cast<BYTE*>(&saveLogs), &bytes) ==
          ERROR_SUCCESS &&
      type == REG_DWORD && app.chkSaveLog)
    SendMessageW(app.chkSaveLog, BM_SETCHECK, saveLogs ? BST_CHECKED : BST_UNCHECKED, 0);
  DWORD sortValue = 0;
  bytes = sizeof(DWORD);
  if (RegQueryValueExW(key, L"TransferUseNewNames", nullptr, &type, reinterpret_cast<BYTE*>(&sortValue), &bytes) == ERROR_SUCCESS &&
      type == REG_DWORD)
    app.useNewNames = sortValue != 0;
  readRegString(key, L"LastTransferDirectory", app.lastTransferDir);
  bytes = sizeof(DWORD);
  if (RegQueryValueExW(key, L"SortColumn", nullptr, &type, reinterpret_cast<BYTE*>(&sortValue), &bytes) == ERROR_SUCCESS &&
      type == REG_DWORD && sortValue <= 3)
    app.sortColumn = (int)sortValue;
  bytes = sizeof(DWORD);
  if (RegQueryValueExW(key, L"SortDescending", nullptr, &type, reinterpret_cast<BYTE*>(&sortValue), &bytes) == ERROR_SUCCESS &&
      type == REG_DWORD)
    app.sortDescending = sortValue != 0;
  for (auto pair : {std::make_pair(L"ScanArchives", app.chkArchives), std::make_pair(L"AllowArchiveRewrite", app.chkRewrite)}) {
    DWORD v = 1;
    bytes = sizeof(DWORD);
    if (pair.second && RegQueryValueExW(key, pair.first, nullptr, &type, reinterpret_cast<BYTE*>(&v), &bytes) == ERROR_SUCCESS &&
        type == REG_DWORD)
      SendMessageW(pair.second, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
  }
  RegCloseKey(key);
  SendMessageW(app.edFolder, CB_RESETCONTENT, 0, 0);
  for (const ws& path : app.recentDirs) SendMessageW(app.edFolder, CB_ADDSTRING, 0, (LPARAM)path.c_str());
  SendMessageW(app.edFolder, CB_SETMINVISIBLE, 10, 0);
  if (!app.lastDirectory.empty()) SetWindowTextW(app.edFolder, app.lastDirectory.c_str());
  else if (!app.recentDirs.empty()) SetWindowTextW(app.edFolder, app.recentDirs.front().c_str());
}

static void saveSettings() {
  HKEY key = nullptr;
  DWORD disposition = 0;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      &disposition) != ERROR_SUCCESS) return;
  DWORD recursive = SendMessageW(app.chkSub, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1u : 0u;
  RegSetValueExW(key, L"IncludeSubfolders", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&recursive), sizeof(recursive));
  const DWORD useNewNames = app.useNewNames ? 1u : 0u;
  RegSetValueExW(key, L"TransferUseNewNames", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&useNewNames), sizeof(useNewNames));
  if (!app.lastTransferDir.empty())
    RegSetValueExW(key, L"LastTransferDirectory", 0, REG_SZ, reinterpret_cast<const BYTE*>(app.lastTransferDir.c_str()),
                   static_cast<DWORD>((app.lastTransferDir.size() + 1) * sizeof(wchar_t)));
  const DWORD sortColumn = (DWORD)app.sortColumn, sortDescending = app.sortDescending ? 1u : 0u;
  RegSetValueExW(key, L"SortColumn", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sortColumn), sizeof(sortColumn));
  RegSetValueExW(key, L"SortDescending", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sortDescending), sizeof(sortDescending));
  for (auto pair : {std::make_pair(L"SaveLogs", app.chkSaveLog), std::make_pair(L"ScanArchives", app.chkArchives),
                    std::make_pair(L"AllowArchiveRewrite", app.chkRewrite)}) {
    if (!pair.second) continue;
    DWORD v = SendMessageW(pair.second, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1u : 0u;
    RegSetValueExW(key, pair.first, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
  }
  if (!app.lastDirectory.empty())
    RegSetValueExW(key, L"LastDirectory", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(app.lastDirectory.c_str()),
                   static_cast<DWORD>((app.lastDirectory.size() + 1) * sizeof(wchar_t)));
  for (size_t i = 0; i < kRecentDirectoryLimit; ++i) {
    wchar_t name[32];
    swprintf(name, sizeof(name) / sizeof(name[0]), L"Recent%u", static_cast<unsigned>(i));
    if (i < app.recentDirs.size()) {
      const ws& path = app.recentDirs[i];
      RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(path.c_str()),
                     static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
    } else {
      RegDeleteValueW(key, name);
    }
  }
  RegCloseKey(key);
}

static void rememberDirectory(const ws& path) {
  if (path.empty()) return;
  app.lastDirectory = path;
  app.recentDirs.erase(std::remove_if(app.recentDirs.begin(), app.recentDirs.end(),
      [&](const ws& existing) { return ols::lower(existing) == ols::lower(path); }), app.recentDirs.end());
  app.recentDirs.insert(app.recentDirs.begin(), path);
  if (app.recentDirs.size() > kRecentDirectoryLimit) app.recentDirs.resize(kRecentDirectoryLimit);
  SendMessageW(app.edFolder, CB_RESETCONTENT, 0, 0);
  for (const ws& recent : app.recentDirs) SendMessageW(app.edFolder, CB_ADDSTRING, 0, (LPARAM)recent.c_str());
  SetWindowTextW(app.edFolder, path.c_str());
  saveSettings();
}


static void setMarquee(bool on) {
  LONG st = GetWindowLongW(app.progress, GWL_STYLE);
  if (on) {
    SetWindowLongW(app.progress, GWL_STYLE, st | PBS_MARQUEE);
    SendMessageW(app.progress, PBM_SETMARQUEE, TRUE, 25);
  } else {
    SendMessageW(app.progress, PBM_SETMARQUEE, FALSE, 0);
    SetWindowLongW(app.progress, GWL_STYLE, st & ~PBS_MARQUEE);
  }
}

static void setProgress(size_t done, size_t total) {
  setMarquee(false);
  SendMessageW(app.progress, PBM_SETRANGE32, 0, 10000);
  SendMessageW(app.progress, PBM_SETPOS, total ? (WPARAM)((done * 10000ull) / total) : 0, 0);
}

static void updateButtons() {
  bool b = app.busy;
  EnableWindow(app.btnScan, !b);
  EnableWindow(app.btnBrowse, !b);
  EnableWindow(app.edFolder, !b);
  EnableWindow(app.chkSub, !b);
  if (app.chkSaveLog) EnableWindow(app.chkSaveLog, !b);
  if (app.chkArchives) EnableWindow(app.chkArchives, !b);
  if (app.btnTransfer)
    EnableWindow(app.btnTransfer, !b && app.scanned && ListView_GetSelectedCount(app.list) > 0);
  if (app.chkRewrite) EnableWindow(app.chkRewrite, !b);
  EnableWindow(app.btnRename, !b && app.scanned && olsfs::renameCount(app.scan) > 0);
  // During renaming, Cancel stops archive rewrites (plain renames are instant).
  EnableWindow(app.btnCancel, b);
  if (app.edFilter) EnableWindow(app.edFilter, !b);
  if (app.chkChanges) EnableWindow(app.chkChanges, !b);
}

static void layout() {
  RECT rc;
  GetClientRect(app.hwnd, &rc);
  int W = rc.right, H = rc.bottom, m = S(12), gap = S(8), rowH = S(26), btnW = S(100);
  int y = m;
  int labelW = S(52);
  MoveWindow(app.lblFolder, m, y + S(4), labelW, rowH - S(4), TRUE);
  MoveWindow(app.btnBrowse, W - m - btnW, y, btnW, rowH, TRUE);
  MoveWindow(app.edFolder, m + labelW, y, W - 2 * m - labelW - btnW - gap, rowH, TRUE);
  y += rowH + gap;
  MoveWindow(app.chkSub, m, y + S(3), S(150), rowH - S(4), TRUE);
  MoveWindow(app.chkSaveLog, m + S(158), y + S(3), S(170), rowH - S(4), TRUE);
  int x = W - m - btnW;
  MoveWindow(app.btnCancel, x, y, btnW, rowH, TRUE);
  x -= btnW + gap + S(20);
  MoveWindow(app.btnRename, x, y, btnW + S(20), rowH, TRUE);
  x -= btnW + gap;
  MoveWindow(app.btnScan, x, y, btnW, rowH, TRUE);
  y += rowH;
  MoveWindow(app.chkArchives, m, y + S(3), S(250), rowH - S(4), TRUE);
  MoveWindow(app.chkRewrite, m + S(258), y + S(3), S(330), rowH - S(4), TRUE);
  MoveWindow(app.btnTransfer, W - m - S(170), y, S(170), rowH, TRUE);
  y += rowH + gap + S(2);
  MoveWindow(app.progress, m, y, W - 2 * m, S(18), TRUE);
  y += S(18) + S(4);
  MoveWindow(app.status, m, y, W - 2 * m, S(18), TRUE);
  y += S(18) + gap;
  MoveWindow(app.edFilter, m, y, W - 2 * m - S(190) - gap, rowH, TRUE);
  MoveWindow(app.chkChanges, W - m - S(190), y, S(190), rowH, TRUE);
  y += rowH + gap;
  MoveWindow(app.list, m, y, W - 2 * m, std::max(S(60), H - y - m), TRUE);
  int lw = W - 2 * m - GetSystemMetrics(SM_CXVSCROLL) - S(4);
  ListView_SetColumnWidth(app.list, 0, lw * 30 / 100);
  ListView_SetColumnWidth(app.list, 1, lw * 40 / 100);
  ListView_SetColumnWidth(app.list, 2, lw * 7 / 100);
  ListView_SetColumnWidth(app.list, 3, lw * 23 / 100);
}

static void applyFont(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)app.font, TRUE); }

// ---------------------------------------------------------------- sorting
// Text shown in the "New name" column (same rules as LVN_GETDISPINFO).
static const ws& newNameText(const olsfs::Item& it) {
  static const ws empty, same = L"(unchanged)";
  return !it.a.ok ? empty : (it.willRename || it.renamed ? it.finalName : same);
}

// Orders two rows by the active column. Rows without a value for that column
// (unreadable projects, unchanged names) always sort after rows that have one,
// in either direction; ties fall back to the file path so the order is stable.
static bool rowLess(size_t ia, size_t ib) {
  const olsfs::Item& a = app.scan.items[ia];
  const olsfs::Item& b = app.scan.items[ib];
  int cmp = 0;
  switch (app.sortColumn) {
    case 1: {
      const int ra = !a.a.ok ? 2 : (a.willRename || a.renamed ? 0 : 1);
      const int rb = !b.a.ok ? 2 : (b.willRename || b.renamed ? 0 : 1);
      if (ra != rb) return ra < rb;
      cmp = StrCmpLogicalW(newNameText(a).c_str(), newNameText(b).c_str());
      break;
    }
    case 2: {
      if (a.a.ok != b.a.ok) return a.a.ok;  // unreadable projects last
      if (a.a.definedCount != b.a.definedCount) cmp = a.a.definedCount < b.a.definedCount ? -1 : 1;
      break;
    }
    case 3:
      cmp = StrCmpLogicalW(a.status.c_str(), b.status.c_str());
      break;
    default:
      cmp = StrCmpLogicalW(a.rel.c_str(), b.rel.c_str());
      break;
  }
  if (cmp != 0) return app.sortDescending ? cmp > 0 : cmp < 0;
  const int byPath = StrCmpLogicalW(a.rel.c_str(), b.rel.c_str());
  if (byPath != 0) return byPath < 0;
  return ia < ib;
}

static void updateSortArrows() {
  HWND header = ListView_GetHeader(app.list);
  if (!header) return;
  const int count = Header_GetItemCount(header);
  for (int i = 0; i < count; ++i) {
    HDITEMW hd{};
    hd.mask = HDI_FORMAT;
    if (!Header_GetItem(header, i, &hd)) continue;
    hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
    if (i == app.sortColumn) hd.fmt |= app.sortDescending ? HDF_SORTDOWN : HDF_SORTUP;
    Header_SetItem(header, i, &hd);
  }
}

static void refreshList() {
  // Keep the selected projects selected (and the focused one visible) across
  // filtering and sorting.
  std::set<size_t> selectedItems;
  size_t focusedItem = SIZE_MAX;
  if (app.list) {
    for (int row = ListView_GetNextItem(app.list, -1, LVNI_SELECTED); row >= 0;
         row = ListView_GetNextItem(app.list, row, LVNI_SELECTED))
      if ((size_t)row < app.visibleItems.size()) selectedItems.insert(app.visibleItems[(size_t)row]);
    const int focused = ListView_GetNextItem(app.list, -1, LVNI_FOCUSED);
    if (focused >= 0 && (size_t)focused < app.visibleItems.size()) focusedItem = app.visibleItems[(size_t)focused];
  }
  app.visibleItems.clear();
  wchar_t filterBuf[1024] = {};
  if (app.edFilter) GetWindowTextW(app.edFilter, filterBuf, 1024);
  const ws filter = ols::lower(ols::jsTrim(filterBuf));
  const bool onlyChanges = app.chkChanges &&
      SendMessageW(app.chkChanges, BM_GETCHECK, 0, 0) == BST_CHECKED;
  for (size_t i = 0; i < app.scan.items.size(); ++i) {
    const auto& it = app.scan.items[i];
    if (onlyChanges && !it.willRename && !it.renamed) continue;
    if (!filter.empty()) {
      const ws haystack = ols::lower(it.rel + L" " + it.name + L" " + it.finalName +
                                     L" " + it.status + L" " + it.a.brand + L" " +
                                     it.a.model + L" " + it.a.ecu + L" " + it.a.ecuManufacturer);
      if (haystack.find(filter) == ws::npos) continue;
    }
    app.visibleItems.push_back(i);
  }
  std::sort(app.visibleItems.begin(), app.visibleItems.end(), rowLess);
  ListView_SetItemCountEx(app.list, static_cast<int>(std::min<size_t>(
      app.visibleItems.size(), static_cast<size_t>(INT_MAX))),
      LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
  ListView_SetItemState(app.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  for (size_t row = 0; row < app.visibleItems.size() && !selectedItems.empty(); ++row) {
    const size_t item = app.visibleItems[row];
    if (selectedItems.count(item)) ListView_SetItemState(app.list, (int)row, LVIS_SELECTED, LVIS_SELECTED);
    if (item == focusedItem) {
      ListView_SetItemState(app.list, (int)row, LVIS_FOCUSED, LVIS_FOCUSED);
      ListView_EnsureVisible(app.list, (int)row, FALSE);
    }
  }
  updateSortArrows();
  InvalidateRect(app.list, nullptr, TRUE);
}

static ws summary() {
  size_t total = app.scan.items.size(), ok = 0, unread = 0, ren = 0, done = 0, bad = 0;
  for (auto& it : app.scan.items) {
    if (it.a.ok) ok++;
    else unread++;
    if (it.willRename && !it.renamed) ren++;
    if (it.renamed) done++;
    if (it.willRename && it.failed) bad++;
  }
  ws s = std::to_wstring(total) + L" OLS files  |  " + std::to_wstring(ok) + L" readable";
  if (unread) s += L"  |  " + std::to_wstring(unread) + L" unreadable (left alone)";
  if (done) s += L"  |  " + std::to_wstring(done) + L" renamed";
  if (bad) s += L"  |  " + std::to_wstring(bad) + L" failed";
  if (ren) s += L"  |  " + std::to_wstring(ren) + L" ready to rename";
  if (!app.scan.scanErrors.empty()) s += L"  |  " + std::to_wstring(app.scan.scanErrors.size()) + L" scan warning(s)";
  return s;
}

static void joinWorker() {
  if (app.worker.joinable()) app.worker.join();
}

static void startScan() {
  if (app.busy) return;
  wchar_t buf[2048];
  GetWindowTextW(app.edFolder, buf, 2048);
  ws root = ols::jsTrim(buf);
  if (root.size() > 1 && root.front() == L'"' && root.back() == L'"') root = root.substr(1, root.size() - 2);
  if (root.empty()) {
    MessageBoxW(app.hwnd, L"Choose a folder first.", L"OLS Renamer", MB_ICONINFORMATION);
    return;
  }
  joinWorker();
  bool recursive = SendMessageW(app.chkSub, BM_GETCHECK, 0, 0) == BST_CHECKED;
  const bool archives = !app.chkArchives || SendMessageW(app.chkArchives, BM_GETCHECK, 0, 0) == BST_CHECKED;
  app.scan = olsfs::Scan();
  app.scanned = false;
  app.cancel = false;
  app.busy = true;
  refreshList();
  setMarquee(true);
  setStatus(L"Looking for .ols files...");
  updateButtons();
  HWND hwnd = app.hwnd;
  app.worker = std::thread([root, recursive, archives, hwnd]() {
    std::atomic<bool>& cancel = app.cancel;
    bool ok = olsfs::scanFolder(app.scan, root, recursive, cancel, [hwnd](size_t n) {
      if (n % 25 == 0) PostMessageW(hwnd, WM_SCAN_FOUND, n, 0);
    }, archives);
    if (!ok) {
      app.scan.items.clear();
      PostMessageW(hwnd, WM_SCAN_DONE, 0, 1);  // lParam 1 = folder not found
      return;
    }
    PostMessageW(hwnd, WM_SCAN_FOUND, app.scan.items.size(), 0);
    size_t lastPosted = 0;
    olsfs::analyzeAll(app.scan, cancel, [hwnd, &lastPosted](size_t d, size_t t) {
      if (d == t || d - lastPosted >= std::max<size_t>(1, t / 400)) {
        lastPosted = d;
        PostMessageW(hwnd, WM_READ_PROGRESS, d, t);
      }
    });
    if (!cancel) olsfs::computeFinal(app.scan);
    PostMessageW(hwnd, WM_SCAN_DONE, cancel ? 2 : 0, 0);  // 2 = cancelled
  });
}

static bool saveLogsEnabled() {
  return !app.chkSaveLog || SendMessageW(app.chkSaveLog, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void startRename() {
  if (app.busy || olsfs::renameCount(app.scan) == 0) return;
  const bool saveLogs = saveLogsEnabled();
  const bool allowRewrite = !app.chkRewrite || SendMessageW(app.chkRewrite, BM_GETCHECK, 0, 0) == BST_CHECKED;
  size_t inArchives = 0;
  std::set<int> archivesTouched;
  for (const auto& it : app.scan.items)
    if (it.willRename && !it.renamed && it.inArchive()) { ++inArchives; archivesTouched.insert(it.archive); }
  ws msg = L"Rename " + std::to_wstring(olsfs::renameCount(app.scan)) +
           L" file(s) in place?\n\nThe files are renamed on disk and this cannot be undone automatically. " +
           ws(saveLogs ? L"A recovery plan and a CSV log of the old and new names are saved in the folder."
                       : L"Log saving is OFF: no recovery plan or CSV log will be written.");
  if (inArchives)
    msg += L"\n\n" + std::to_wstring(inArchives) + L" of them are inside " + std::to_wstring(archivesTouched.size()) +
           L" archive(s). Archive headers are rewritten in place when the new names fit; otherwise " +
           (allowRewrite ? ws(L"a verified temporary copy replaces the archive (needs free space equal to its size).")
                         : ws(L"those names are left unchanged (full rewrites are switched off).")) +
           L" Compressed data is never re-compressed.";
  if (MessageBoxW(app.hwnd, msg.c_str(), L"OLS Renamer", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    return;
  joinWorker();
  app.busy = true;
  app.renaming = true;
  app.cancel = false;
  setProgress(0, 1);
  setStatus(L"Renaming...");
  updateButtons();
  HWND hwnd = app.hwnd;
  app.lastLog.clear();
  app.worker = std::thread([hwnd, saveLogs, allowRewrite]() {
    olsfs::ArchiveRenameSettings settings;
    settings.allowRewrite = allowRewrite;
    settings.cancel = &app.cancel;
    settings.progress = [hwnd](size_t index, size_t done, size_t total, uint64_t bytes, uint64_t all) {
      app.archivesDone = done;
      app.archivesTotal = total;
      static uint64_t lastTick = 0;
      const uint64_t now = GetTickCount64();
      if (bytes == all || now - lastTick > 100) {
        lastTick = now;
        PostMessageW(hwnd, WM_ARCHIVE_PROGRESS, all ? (WPARAM)((bytes * 1000) / all) : 0, (LPARAM)index);
      }
    };
    olsfs::renameAll(app.scan, [hwnd](size_t d, size_t t) { PostMessageW(hwnd, WM_RENAME_PROGRESS, d, t); },
                     saveLogs, &settings);
    if (saveLogs) olsfs::writeLog(app.scan, app.lastLog);
    PostMessageW(hwnd, WM_RENAME_DONE, 0, 0);
  });
}

// ------------------------------------------------------ move / copy / extract
static std::vector<size_t> selectedItemIndices() {
  std::vector<size_t> out;
  for (int row = ListView_GetNextItem(app.list, -1, LVNI_SELECTED); row >= 0;
       row = ListView_GetNextItem(app.list, row, LVNI_SELECTED))
    if ((size_t)row < app.visibleItems.size()) out.push_back(app.visibleItems[(size_t)row]);
  return out;
}

static int CALLBACK browseInit(HWND h, UINT msg, LPARAM, LPARAM data) {
  if (msg == BFFM_INITIALIZED && data) SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data);
  return 0;
}

static ws pickFolder(const wchar_t* title, const ws& start) {
  BROWSEINFOW bi = {};
  bi.hwndOwner = app.hwnd;
  bi.lpszTitle = title;
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
  bi.lpfn = browseInit;
  bi.lParam = (LPARAM)(start.empty() ? nullptr : start.c_str());
  LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
  if (!pidl) return ws();
  wchar_t path[MAX_PATH * 2];
  ws out = SHGetPathFromIDListW(pidl, path) ? ws(path) : ws();
  CoTaskMemFree(pidl);
  return out;
}

static void startTransfer(olsfs::TransferMode mode) {
  if (app.busy || !app.scanned) return;
  const std::vector<size_t> items = selectedItemIndices();
  if (items.empty()) return;
  size_t plain = 0, archived = 0;
  std::set<int> archives;
  for (size_t i : items) {
    if (app.scan.items[i].inArchive()) { ++archived; archives.insert(app.scan.items[i].archive); }
    else ++plain;
  }
  const bool move = mode == olsfs::TransferMode::Move;
  const ws dest = pickFolder(move ? L"Move / extract the selected projects to:" : L"Copy / extract the selected projects to:",
                             app.lastTransferDir.empty() ? app.lastDirectory : app.lastTransferDir);
  if (dest.empty()) return;
  ws msg;
  if (plain) msg += ws(move ? L"Move " : L"Copy ") + std::to_wstring(plain) + L" file(s)";
  if (archived) msg += ws(plain ? L" and extract " : L"Extract ") + std::to_wstring(archived) + L" project(s) from " +
                       std::to_wstring(archives.size()) + L" archive(s)";
  msg += L" to\n" + dest + L" ?\n\n" +
         ws(app.useNewNames ? L"Projects planned for renaming get their new names; others keep their names. "
                            : L"Names are kept. ") +
         L"Existing files are never overwritten (a number is added instead)." +
         ws(archived ? L" Archives are not modified." : L"");
  if (MessageBoxW(app.hwnd, msg.c_str(), L"OLS Renamer", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
  app.lastTransferDir = dest;
  saveSettings();
  joinWorker();
  app.busy = true;
  app.cancel = false;
  app.lastMode = mode;
  setProgress(0, 1);
  setStatus(L"Working...");
  updateButtons();
  HWND hwnd = app.hwnd;
  const bool useNew = app.useNewNames;
  const bool saveLogs = saveLogsEnabled();
  app.worker = std::thread([hwnd, items, dest, mode, useNew, saveLogs]() {
    olsfs::TransferSettings settings;
    settings.mode = mode;
    settings.useNewNames = useNew;
    settings.saveLog = saveLogs;
    settings.cancel = &app.cancel;
    settings.progress = [hwnd](size_t d, size_t t) { PostMessageW(hwnd, WM_TRANSFER_PROGRESS, d, t); };
    app.lastTransfer = olsfs::transferItems(app.scan, items, dest, settings);
    PostMessageW(hwnd, WM_TRANSFER_DONE, 0, 0);
  });
}

static void showTransferMenu(POINT at) {
  if (app.busy || !app.scanned) return;
  const bool any = ListView_GetSelectedCount(app.list) > 0;
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | (any ? 0 : MF_GRAYED), IDM_MOVE, L"Move / extract selected to folder...");
  AppendMenuW(menu, MF_STRING | (any ? 0 : MF_GRAYED), IDM_COPY, L"Copy / extract selected to folder...");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (app.useNewNames ? MF_CHECKED : 0), IDM_USENEW, L"Use the planned new names");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_SELECTALL, L"Select all\tCtrl+A");
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, at.x, at.y, 0, app.hwnd, nullptr);
  DestroyMenu(menu);
}

static void browse() {
  BROWSEINFOW bi = {};
  bi.hwndOwner = app.hwnd;
  bi.lpszTitle = L"Choose the folder with your .ols files";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
  LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
  if (!pidl) return;
  wchar_t path[MAX_PATH * 2];
  if (SHGetPathFromIDListW(pidl, path)) {
    SetWindowTextW(app.edFolder, path);
    CoTaskMemFree(pidl);
    startScan();
    return;
  }
  CoTaskMemFree(pidl);
}

static LRESULT onNotify(LPNMHDR nh) {
  if (nh->hwndFrom != app.list) return 0;
  if (nh->code == NM_DBLCLK) {
    const auto* act = reinterpret_cast<const NMITEMACTIVATE*>(nh);
    if (act->iItem >= 0 && (size_t)act->iItem < app.visibleItems.size()) {
      const auto& it = app.scan.items[app.visibleItems[(size_t)act->iItem]];
      const auto& a = it.a;
      if (!a.ok) {
        MessageBoxW(app.hwnd, a.error.c_str(), L"OLS analysis", MB_ICONWARNING);
        return 0;
      }
      ws archiveInfo;
      if (it.inArchive()) {
        const auto& arc = app.scan.archives[(size_t)it.archive];
        archiveInfo = L"\nArchive: " + arc.path + L" (" +
                      (arc.listing.kind == olsarc::ArchiveKind::Zip ? ws(L"ZIP") : ws(arc.listing.rar.solid ? L"solid RAR" : L"RAR")) +
                      L")\nPath inside archive: " + olsarc::fromUtf8(arc.listing.projects[it.project].innerPath) +
                      (arc.listing.problem.empty() ? ws() : L"\nArchive cannot be modified: " + arc.listing.problem);
      }
      ws details = L"File: " + it.rel + archiveInfo +
        L"\nWinOLS format version: " + std::to_wstring(a.formatVersion) +
        L"\nHeader complete: " + ws(a.headerComplete ? L"yes" : L"no") +
        L"\nDeclared size consistent: " + ws(a.formatSizeConsistent ? L"yes" : L"no") +
        L"\n\nVehicle brand: " + a.brand +
        L"\nBrand source: " + a.brandSource +
        L"\nVehicle model: " + a.model +
        L"\nVehicle year: " + a.vehicleYear +
        L"\nEngine code: " + a.engineCode +
        L"\nECU model/family: " + a.ecu +
        L"\nECU identity source: " + a.ecuSource +
        L"\nECU manufacturer: " + a.ecuManufacturer +
        L"\nHardware number: " + a.hardwareNumber +
        L"\nSoftware number: " + a.software +
        L"\nSoftware source: " + a.softwareSource +
        L"\nProduction number: " + a.productionNumber +
        L"\nOriginal source filename: " + a.originalFileName +
        L"\nEmbedded HEX/BIN filename: " + a.embeddedHexFilename +
        L"\n\nMap records recovered: " + std::to_wstring(a.mapCount) +
        L"\nDefined maps: " + std::to_wstring(a.definedCount) +
        L"\nSkipped maps: " + std::to_wstring(a.skippedCount) +
        L"\nMap extraction source: " + ols::fromAscii(a.mapSource.c_str()) +
        L"\nMap/ID area: 0x0-0x" + hex(a.mapAreaEnd) + L" (map directory at 0x" + hex(a.mapDirectoryOffset) + L")" +
        L"\nFirst ROM image: 0x" + hex(a.romImageStart) + L"-0x" + hex(a.romImageEnd) +
        L"\nBytes read: " + std::to_wstring(a.bytesLoaded) + L" of " + std::to_wstring(a.fileSize) +
        L"\nAutomatic rename safe: " + ws(a.renameSafe ? L"yes" : L"no") +
        L"\n\nProposed filename: " + a.proposed;
      if (!a.metadata.empty()) {
        details += L"\n\nParsed header fields:";
        for (const auto& field : a.metadata) details += L"\n" + field.first + L": " + field.second;
      }
      if (!a.mapDetails.empty()) {
        details += L"\n\nMap records (first 40):";
        const size_t shown=std::min<size_t>(40,a.mapDetails.size());
        for(size_t i=0;i<shown;++i) {
          const auto& m=a.mapDetails[i];
          details += L"\n[" + ws(m.defined ? L"defined" : L"skipped") + L"] " + (m.name.empty() ? ws(L"(unnamed)") : m.name);
          if(!m.description.empty())details += L" — " + m.description;
        }
        if(a.mapCount>shown)details += L"\n... and " + std::to_wstring(a.mapCount-shown) + L" more map records";
      }
      if (!a.warnings.empty()) {
        details += L"\n\nWarnings:";
        for (const auto& w : a.warnings) details += L"\n- " + w;
      }
      MessageBoxW(app.hwnd, details.c_str(), L"OLS analysis details", MB_OK | (a.renameSafe ? MB_ICONINFORMATION : MB_ICONWARNING));
    }
    return 0;
  }
  if (nh->code == NM_RCLICK) {
    POINT pt;
    GetCursorPos(&pt);
    showTransferMenu(pt);
    return 0;
  }
  if (nh->code == LVN_ITEMCHANGED || nh->code == LVN_ODSTATECHANGED) {
    updateButtons();
    return 0;
  }
  if (nh->code == LVN_KEYDOWN) {
    const auto* kd = reinterpret_cast<const NMLVKEYDOWN*>(nh);
    if (kd->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
      ListView_SetItemState(app.list, -1, LVIS_SELECTED, LVIS_SELECTED);
      updateButtons();
    }
    return 0;
  }
  if (nh->code == LVN_COLUMNCLICK) {
    const int column = reinterpret_cast<const NMLISTVIEW*>(nh)->iSubItem;
    if (column < 0 || column > 3) return 0;
    if (column == app.sortColumn) app.sortDescending = !app.sortDescending;
    else { app.sortColumn = column; app.sortDescending = false; }
    if (!app.busy) refreshList();  // during a scan the list is rebuilt when it finishes
    else updateSortArrows();
    saveSettings();
    return 0;
  }
  if (nh->code == LVN_GETDISPINFOW) {
    auto* di = (NMLVDISPINFOW*)nh;
    if (!(di->item.mask & LVIF_TEXT) || (size_t)di->item.iItem >= app.visibleItems.size()) return 0;
    const auto& it = app.scan.items[app.visibleItems[(size_t)di->item.iItem]];
    const ws* s = &it.status;
    switch (di->item.iSubItem) {
      case 0: s = &it.rel; break;
      case 1: s = &newNameText(it); break;
      case 2: s = &it.mapsText; break;
      default: s = &it.status; break;
    }
    di->item.pszText = const_cast<wchar_t*>(s->c_str());
    return 0;
  }
  if (nh->code == NM_CUSTOMDRAW) {
    auto* cd = (NMLVCUSTOMDRAW*)nh;
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
      size_t row = (size_t)cd->nmcd.dwItemSpec;
      if (row < app.visibleItems.size()) {
        const auto& it = app.scan.items[app.visibleItems[row]];
        if (it.failed) cd->clrText = RGB(176, 32, 32);
        else if (it.renamed) cd->clrText = RGB(20, 120, 50);
        else if (it.a.ok && !it.willRename) cd->clrText = RGB(110, 110, 110);
      }
      return CDRF_DODEFAULT;
    }
  }
  return 0;
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      app.hwnd = h;
      HDC dc = GetDC(nullptr);
      app.dpi = GetDeviceCaps(dc, LOGPIXELSX);
      ReleaseDC(nullptr, dc);
      NONCLIENTMETRICSW ncm = {sizeof(ncm)};
      SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
      app.font = CreateFontIndirectW(&ncm.lfMessageFont);
      HINSTANCE hi = GetModuleHandleW(nullptr);
      auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int id, DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h,
                                 (HMENU)(INT_PTR)id, hi, nullptr);
        applyFont(c);
        return c;
      };
      app.lblFolder = mk(L"STATIC", L"Folder:", SS_LEFT, 0);
      app.edFolder = mk(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, ID_FOLDER, WS_EX_CLIENTEDGE);
      app.btnBrowse = mk(L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, ID_BROWSE);
      app.chkSub = mk(L"BUTTON", L"Include subfolders", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SUB);
      SendMessageW(app.chkSub, BM_SETCHECK, BST_CHECKED, 0);
      app.chkSaveLog = mk(L"BUTTON", L"Save rename logs (CSV)", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SAVELOG);
      SendMessageW(app.chkSaveLog, BM_SETCHECK, BST_CHECKED, 0);
      app.chkArchives = mk(L"BUTTON", L"Look inside ZIP/RAR archives", WS_TABSTOP | BS_AUTOCHECKBOX, ID_ARCHIVES);
      SendMessageW(app.chkArchives, BM_SETCHECK, BST_CHECKED, 0);
      app.chkRewrite = mk(L"BUTTON", L"Allow full archive rewrite (needs free space)", WS_TABSTOP | BS_AUTOCHECKBOX, ID_REWRITE);
      SendMessageW(app.chkRewrite, BM_SETCHECK, BST_CHECKED, 0);
      app.btnTransfer = mk(L"BUTTON", L"Move / extract selected...", WS_TABSTOP | BS_PUSHBUTTON, ID_TRANSFER);
      loadSettings();
      app.btnScan = mk(L"BUTTON", L"Scan", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_SCAN);
      app.btnRename = mk(L"BUTTON", L"Rename files", WS_TABSTOP | BS_PUSHBUTTON, ID_RENAME);
      app.btnCancel = mk(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, ID_CANCEL);
      app.progress = mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, ID_PROGRESS);
      app.status = mk(L"STATIC", L"Choose a folder, or drop one onto this window.", SS_LEFT | SS_ENDELLIPSIS, ID_STATUS);
      app.edFilter = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_FILTER, WS_EX_CLIENTEDGE);
      SendMessageW(app.edFilter, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by filename, make, ECU, or status...");
      app.chkChanges = mk(L"BUTTON", L"Only rename candidates", WS_TABSTOP | BS_AUTOCHECKBOX, ID_CHANGES);
      app.list = mk(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
                    ID_LIST, WS_EX_CLIENTEDGE);
      ListView_SetExtendedListViewStyle(app.list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER |
                                                      LVS_EX_LABELTIP);
      const wchar_t* cols[] = {L"File", L"New name", L"Maps", L"Status"};
      for (int i = 0; i < 4; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<wchar_t*>(cols[i]);
        c.cx = 100;
        SendMessageW(app.list, LVM_INSERTCOLUMNW, i, (LPARAM)&c);
      }
      updateSortArrows();  // show the remembered sort column
      DragAcceptFiles(h, TRUE);
      updateButtons();
      return 0;
    }
    case WM_SIZE:
      layout();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = (MINMAXINFO*)l;
      mm->ptMinTrackSize.x = S(800);
      mm->ptMinTrackSize.y = S(380);
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case ID_BROWSE: browse(); break;
        case ID_FOLDER:
          if (HIWORD(w) == CBN_SELCHANGE) {
            LRESULT selected = SendMessageW(app.edFolder, CB_GETCURSEL, 0, 0);
            if (selected != CB_ERR) {
              LRESULT length = SendMessageW(app.edFolder, CB_GETLBTEXTLEN, selected, 0);
              if (length > 0 && length < 32768) {
                std::vector<wchar_t> path(static_cast<size_t>(length) + 1, L'\0');
                SendMessageW(app.edFolder, CB_GETLBTEXT, selected, (LPARAM)path.data());
                SetWindowTextW(app.edFolder, path.data());
              }
            }
          }
          break;
        case ID_SCAN:
        case IDOK: startScan(); break;
        case ID_RENAME: startRename(); break;
        case ID_TRANSFER: {
          RECT r;
          GetWindowRect(app.btnTransfer, &r);
          showTransferMenu(POINT{r.left, r.bottom});
          break;
        }
        case IDM_MOVE: startTransfer(olsfs::TransferMode::Move); break;
        case IDM_COPY: startTransfer(olsfs::TransferMode::Copy); break;
        case IDM_USENEW:
          app.useNewNames = !app.useNewNames;
          saveSettings();
          break;
        case IDM_SELECTALL:
          ListView_SetItemState(app.list, -1, LVIS_SELECTED, LVIS_SELECTED);
          updateButtons();
          break;
        case ID_FILTER:
          if (HIWORD(w) == EN_CHANGE && !app.busy) refreshList();
          break;
        case ID_CHANGES:
          if (!app.busy) refreshList();
          break;
        case ID_SAVELOG:
        case ID_ARCHIVES:
        case ID_REWRITE:
          if (HIWORD(w) == BN_CLICKED) saveSettings();
          break;
        case ID_CANCEL:
          app.cancel = true;
          setStatus(L"Cancelling...");
          break;
      }
      return 0;
    case WM_NOTIFY:
      return onNotify((LPNMHDR)l);
    case WM_DROPFILES: {
      wchar_t path[MAX_PATH * 2];
      if (DragQueryFileW((HDROP)w, 0, path, MAX_PATH * 2) && !app.busy) {
        DWORD a = GetFileAttributesW(path);
        if (a != INVALID_FILE_ATTRIBUTES) {
          ws p = path;
          if (!(a & FILE_ATTRIBUTE_DIRECTORY)) p = p.substr(0, p.find_last_of(L'\\'));
          SetWindowTextW(app.edFolder, p.c_str());
          startScan();
        }
      }
      DragFinish((HDROP)w);
      return 0;
    }
    case WM_SCAN_FOUND:
      setStatus(L"Found " + std::to_wstring(w) + L" .ols files...");
      return 0;
    case WM_READ_PROGRESS: {
      setProgress(w, l);
      int pct = l ? (int)((w * 100) / (size_t)l) : 0;
      setStatus(L"Reading " + std::to_wstring(w) + L" of " + std::to_wstring(l) + L"  (" + std::to_wstring(pct) + L"%)");
      return 0;
    }
    case WM_SCAN_DONE: {
      joinWorker();
      app.busy = false;
      if (l == 1) {
        app.scanned = false;
        setProgress(0, 1);
        setStatus(L"That folder does not exist.");
        MessageBoxW(h, L"That folder could not be opened.", L"OLS Renamer", MB_ICONWARNING);
      } else if (w == 2) {
        app.scan = olsfs::Scan();
        app.scanned = false;
        setProgress(0, 1);
        setStatus(L"Cancelled.");
      } else {
        app.scanned = true;
        if (!app.scan.root.empty()) rememberDirectory(app.scan.root);
        setProgress(1, 1);
        setStatus(app.scan.items.empty() ? ws(L"No .ols files were found in that folder.") : summary());
        if(!app.scan.scanErrors.empty()) {
          ws msg=L"Some folders could not be scanned ("+std::to_wstring(app.scan.scanErrors.size())+L").\n\nFirst error: "+app.scan.scanErrors.front();
          MessageBoxW(h,msg.c_str(),L"Scan incomplete",MB_OK|MB_ICONWARNING);
        }
      }
      refreshList();
      updateButtons();
      return 0;
    }
    case WM_RENAME_PROGRESS: {
      setProgress(w, l);
      setStatus(L"Renaming " + std::to_wstring(w) + L" of " + std::to_wstring(l));
      return 0;
    }
    case WM_ARCHIVE_PROGRESS: {
      setProgress(w, 1000);
      const size_t index = (size_t)l;
      ws name = index < app.scan.archives.size() ? app.scan.archives[index].rel : ws();
      setStatus(L"Archive " + std::to_wstring(app.archivesDone + 1) + L" of " + std::to_wstring(app.archivesTotal) +
                L": " + name + L"  (" + std::to_wstring(w / 10) + L"%)");
      return 0;
    }
    case WM_TRANSFER_PROGRESS: {
      setProgress(w, l);
      setStatus(L"Moving / extracting " + std::to_wstring(w) + L" of " + std::to_wstring(l));
      return 0;
    }
    case WM_TRANSFER_DONE: {
      joinWorker();
      app.busy = false;
      setProgress(1, 1);
      const auto& t = app.lastTransfer;
      ws s;
      auto part = [&](size_t n, const wchar_t* label) { if (n) s += (s.empty() ? ws() : ws(L"  |  ")) + std::to_wstring(n) + L" " + label; };
      part(t.moved, L"moved");
      part(t.copied, L"copied");
      part(t.extracted, L"extracted");
      part(t.skipped, L"skipped");
      part(t.failed, L"failed");
      if (s.empty()) s = L"Nothing to do.";
      if (!t.logPath.empty()) s += L"  |  log saved";
      setStatus(s);
      refreshList();
      updateButtons();
      if (t.failed)
        MessageBoxW(h, (std::to_wstring(t.failed) + L" item(s) could not be moved or extracted. The Status column shows why.").c_str(),
                    L"OLS Renamer", MB_ICONWARNING);
      return 0;
    }
    case WM_RENAME_DONE: {
      joinWorker();
      app.busy = false;
      app.renaming = false;
      setProgress(1, 1);
      ws s = summary();
      if (!app.lastLog.empty()) s += L"  |  log saved";
      if (!app.scan.journalPath.empty()) s += L"  |  recovery plan saved";
      if (!saveLogsEnabled()) s += L"  |  log saving off";
      setStatus(s);
      refreshList();
      updateButtons();
      return 0;
    }
    case WM_CLOSE:
      if (app.busy) {
        if (app.renaming) {
          MessageBoxW(h, L"Renaming is in progress. Please wait until it finishes.", L"OLS Renamer",
                      MB_ICONINFORMATION);
          return 0;
        }
        app.cancel = true;
        joinWorker();
      }
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      saveSettings();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int show) {
  INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&ic);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"OlsRenamerWindow";
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  RegisterClassExW(&wc);
  HDC dc = GetDC(nullptr);
  int dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(nullptr, dc);
  HWND h = CreateWindowExW(0, wc.lpszClassName, L"OLS Renamer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           MulDiv(980, dpi, 96), MulDiv(640, dpi, 96), nullptr, nullptr, hi, nullptr);
  ShowWindow(h, show);
  UpdateWindow(h);
  ws arg = cmd ? cmd : L"";
  arg = ols::jsTrim(arg);
  if (arg.size() > 1 && arg.front() == L'"' && arg.back() == L'"') arg = arg.substr(1, arg.size() - 2);
  if (!arg.empty()) {
    SetWindowTextW(app.edFolder, arg.c_str());
    startScan();
  }
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(h, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  CoUninitialize();
  return (int)msg.wParam;
}
