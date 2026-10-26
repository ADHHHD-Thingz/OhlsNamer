// OLS Renamer - Win32 GUI. Renames WinOLS projects to
// "Brand Model ECU SoftwareVersion (named maps).ols" from their header.
#define NOMINMAX
#include <thread>

#include "ols_fs.hpp"

#include <commctrl.h>
#include <shlobj.h>

using ols::ws;

enum {
  ID_FOLDER = 101, ID_BROWSE, ID_SUB, ID_SCAN, ID_RENAME, ID_CANCEL, ID_LIST, ID_STATUS, ID_PROGRESS
};
enum {
  WM_SCAN_FOUND = WM_APP + 1,   // wParam = files found so far
  WM_READ_PROGRESS,             // wParam = done, lParam = total
  WM_SCAN_DONE,
  WM_RENAME_PROGRESS,           // wParam = done, lParam = total
  WM_RENAME_DONE
};

struct App {
  HWND hwnd = nullptr, edFolder, btnBrowse, chkSub, btnScan, btnRename, btnCancel, lblFolder, progress,
       status, list;
  HFONT font = nullptr;
  int dpi = 96;
  olsfs::Scan scan;
  std::thread worker;
  std::atomic<bool> busy{false}, cancel{false};
  bool scanned = false;
  std::atomic<bool> renaming{false};
  ws lastLog;
} app;

static int S(int v) { return MulDiv(v, app.dpi, 96); }

static void setStatus(const ws& s) { SetWindowTextW(app.status, s.c_str()); }

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
  EnableWindow(app.btnRename, !b && app.scanned && olsfs::renameCount(app.scan) > 0);
  EnableWindow(app.btnCancel, b && !app.renaming);
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
  int x = W - m - btnW;
  MoveWindow(app.btnCancel, x, y, btnW, rowH, TRUE);
  x -= btnW + gap + S(20);
  MoveWindow(app.btnRename, x, y, btnW + S(20), rowH, TRUE);
  x -= btnW + gap;
  MoveWindow(app.btnScan, x, y, btnW, rowH, TRUE);
  y += rowH + gap + S(2);
  MoveWindow(app.progress, m, y, W - 2 * m, S(18), TRUE);
  y += S(18) + S(4);
  MoveWindow(app.status, m, y, W - 2 * m, S(18), TRUE);
  y += S(18) + gap;
  MoveWindow(app.list, m, y, W - 2 * m, std::max(S(60), H - y - m), TRUE);
  int lw = W - 2 * m - GetSystemMetrics(SM_CXVSCROLL) - S(4);
  ListView_SetColumnWidth(app.list, 0, lw * 30 / 100);
  ListView_SetColumnWidth(app.list, 1, lw * 40 / 100);
  ListView_SetColumnWidth(app.list, 2, lw * 7 / 100);
  ListView_SetColumnWidth(app.list, 3, lw * 23 / 100);
}

static void applyFont(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)app.font, TRUE); }

static void refreshList() {
  ListView_SetItemCountEx(app.list, app.scan.items.size(), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
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
  app.scan = olsfs::Scan();
  app.scanned = false;
  app.cancel = false;
  app.busy = true;
  refreshList();
  setMarquee(true);
  setStatus(L"Looking for .ols files...");
  updateButtons();
  HWND hwnd = app.hwnd;
  app.worker = std::thread([root, recursive, hwnd]() {
    std::atomic<bool>& cancel = app.cancel;
    bool ok = olsfs::scanFolder(app.scan, root, recursive, cancel, [hwnd](size_t n) {
      if (n % 25 == 0) PostMessageW(hwnd, WM_SCAN_FOUND, n, 0);
    });
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

static void startRename() {
  if (app.busy || olsfs::renameCount(app.scan) == 0) return;
  ws msg = L"Rename " + std::to_wstring(olsfs::renameCount(app.scan)) +
           L" file(s) in place?\n\nThe files are renamed on disk and this cannot be undone automatically. "
           L"A CSV log of the old and new names is saved in the folder.";
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
  app.worker = std::thread([hwnd]() {
    olsfs::renameAll(app.scan, [hwnd](size_t d, size_t t) { PostMessageW(hwnd, WM_RENAME_PROGRESS, d, t); });
    olsfs::writeLog(app.scan, app.lastLog);
    PostMessageW(hwnd, WM_RENAME_DONE, 0, 0);
  });
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
    if (act->iItem >= 0 && (size_t)act->iItem < app.scan.items.size()) {
      const auto& it = app.scan.items[(size_t)act->iItem];
      const auto& a = it.a;
      if (!a.ok) {
        MessageBoxW(app.hwnd, a.error.c_str(), L"OLS analysis", MB_ICONWARNING);
        return 0;
      }
      ws details = L"File: " + it.rel +
        L"\nWinOLS format version: " + std::to_wstring(a.formatVersion) +
        L"\nHeader complete: " + ws(a.headerComplete ? L"yes" : L"no") +
        L"\nDeclared size consistent: " + ws(a.formatSizeConsistent ? L"yes" : L"no") +
        L"\n\nVehicle brand: " + a.brand +
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
        L"\nAutomatic rename safe: " + ws(a.renameSafe ? L"yes" : L"no") +
        L"\n\nProposed filename: " + a.proposed;
      if (!a.warnings.empty()) {
        details += L"\n\nWarnings:";
        for (const auto& w : a.warnings) details += L"\n- " + w;
      }
      MessageBoxW(app.hwnd, details.c_str(), L"OLS analysis details", MB_OK | (a.renameSafe ? MB_ICONINFORMATION : MB_ICONWARNING));
    }
    return 0;
  }
  if (nh->code == LVN_GETDISPINFOW) {
    auto* di = (NMLVDISPINFOW*)nh;
    if (!(di->item.mask & LVIF_TEXT) || (size_t)di->item.iItem >= app.scan.items.size()) return 0;
    const auto& it = app.scan.items[di->item.iItem];
    const ws* s = &it.status;
    static const ws empty, same = L"(unchanged)";
    switch (di->item.iSubItem) {
      case 0: s = &it.rel; break;
      case 1: s = !it.a.ok ? &empty : (it.willRename || it.renamed ? &it.finalName : &same); break;
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
      size_t i = (size_t)cd->nmcd.dwItemSpec;
      if (i < app.scan.items.size()) {
        const auto& it = app.scan.items[i];
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
      app.edFolder = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_FOLDER, WS_EX_CLIENTEDGE);
      app.btnBrowse = mk(L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, ID_BROWSE);
      app.chkSub = mk(L"BUTTON", L"Include subfolders", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SUB);
      SendMessageW(app.chkSub, BM_SETCHECK, BST_CHECKED, 0);
      app.btnScan = mk(L"BUTTON", L"Scan", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_SCAN);
      app.btnRename = mk(L"BUTTON", L"Rename files", WS_TABSTOP | BS_PUSHBUTTON, ID_RENAME);
      app.btnCancel = mk(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, ID_CANCEL);
      app.progress = mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, ID_PROGRESS);
      app.status = mk(L"STATIC", L"Choose a folder, or drop one onto this window.", SS_LEFT | SS_ENDELLIPSIS, ID_STATUS);
      app.list = mk(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
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
      DragAcceptFiles(h, TRUE);
      updateButtons();
      return 0;
    }
    case WM_SIZE:
      layout();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = (MINMAXINFO*)l;
      mm->ptMinTrackSize.x = S(640);
      mm->ptMinTrackSize.y = S(380);
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case ID_BROWSE: browse(); break;
        case ID_SCAN:
        case IDOK: startScan(); break;
        case ID_RENAME: startRename(); break;
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
        setProgress(1, 1);
        setStatus(app.scan.items.empty() ? ws(L"No .ols files were found in that folder.") : summary());
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
    case WM_RENAME_DONE: {
      joinWorker();
      app.busy = false;
      app.renaming = false;
      setProgress(1, 1);
      ws s = summary();
      if (!app.lastLog.empty()) s += L"  |  log saved";
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
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int show) {
  INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES};
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
