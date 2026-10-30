// Main.cpp — native Win32 GUI for the DLL injector.
//
// Dependency-free: links only against system libraries, ships as a single
// portable .exe. The visual language (one dark surface system, a single
// violet accent, 4pt spacing, designed button states, feedback on every
// action) follows the project's web design guidelines, adapted to Win32.
#include <Windows.h>
#include <CommCtrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <string>
#include <vector>

#include "Injector.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Comdlg32.lib")

// Enable Common Controls v6 (visual styles) without a separate .manifest line.
#pragma comment(linker,                                                     \
    "\"/manifestdependency:type='win32' "                                   \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "           \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "          \
    "language='*'\"")

// ---- Palette (GitHub-dark inspired; one accent, neutrals do the work) ----
#define C_BG        RGB(13, 17, 23)
#define C_SURFACE   RGB(22, 27, 34)
#define C_SURFACE2  RGB(28, 33, 41)
#define C_HOVER     RGB(38, 44, 53)
#define C_BORDER    RGB(48, 54, 61)
#define C_TEXT      RGB(230, 237, 243)
#define C_MUTED     RGB(139, 148, 158)
#define C_ACCENT    RGB(124, 108, 240)
#define C_ACCENT_HI RGB(146, 131, 255)
#define C_ACCENT_LO RGB(97, 84, 214)
#define C_SUCCESS   RGB(63, 185, 80)
#define C_DANGER    RGB(248, 81, 73)
#define C_WARNING   RGB(210, 153, 34)

// ---- Control IDs ----
enum {
    ID_SEARCH = 1001,
    ID_REFRESH,
    ID_LIST,
    ID_DLLPATH,
    ID_BROWSE,
    ID_INJECT,
};

#define WM_APP_INJECT_DONE (WM_APP + 1)

// ---- Layout constants (4pt grid) ----
static const int PAD = 20;
static const int HEADER_H = 72;
static const int ROW_H = 36;
static const int PANEL_H = 180;
static const int GAP = 12;
static const int RAD = 10;

// ---- Globals ----
static HINSTANCE g_inst = nullptr;
static HWND g_hwnd = nullptr;
static HWND g_search = nullptr, g_refresh = nullptr, g_list = nullptr;
static HWND g_dllPath = nullptr, g_browse = nullptr, g_inject = nullptr;

static HFONT g_fontBase = nullptr, g_fontTitle = nullptr;
static HFONT g_fontSmall = nullptr, g_fontBtn = nullptr;
static HBRUSH g_brBg = nullptr, g_brSurface = nullptr, g_brSurface2 = nullptr;

static std::vector<inj::ProcessInfo> g_procs;   // Full snapshot.
static std::vector<inj::ProcessInfo> g_view;    // Currently shown (filtered).
static DWORD g_selectedPid = 0;

static std::wstring g_status = L"Ready. Select a target process and a DLL.";
static COLORREF g_statusColor = C_MUTED;
static std::wstring g_hint = L"No DLL selected";
static COLORREF g_hintColor = C_MUTED;
static bool g_injecting = false;

// Cached layout rects for painting.
static RECT g_rcPanel{}, g_rcSearchField{}, g_rcDllField{};

// Per-button state for owner-draw hover handling.
struct BtnData {
    bool hover = false;
    bool primary = false;
};

// ------------------------------------------------------------------ helpers

static int Dpi() {
    using PFN = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<PFN>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    UINT d = (fn && g_hwnd) ? fn(g_hwnd) : 96;
    return d ? static_cast<int>(d) : 96;
}

static int Scale(int px) { return MulDiv(px, Dpi(), 96); }

static HFONT MakeFont(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, Dpi(), 72), 0, 0, 0, weight, FALSE, FALSE,
                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void FillRound(HDC dc, RECT rc, int rad, COLORREF fill, bool border,
                      COLORREF borderColor) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = border ? CreatePen(PS_SOLID, 1, borderColor) : nullptr;
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, p ? p : GetStockObject(NULL_PEN));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    if (p) DeleteObject(p);
}

// --------------------------------------------------------------- status/hint

static void SetStatus(const std::wstring& text, COLORREF color) {
    g_status = text;
    g_statusColor = color;
    InvalidateRect(g_hwnd, &g_rcPanel, FALSE);
}

static std::wstring GetEditText(HWND edit) {
    int n = GetWindowTextLengthW(edit);
    std::wstring s(n, L'\0');
    if (n) GetWindowTextW(edit, s.data(), n + 1);
    return s;
}

static const inj::ProcessInfo* SelectedProc() {
    if (!g_selectedPid) return nullptr;
    for (const auto& p : g_view)
        if (p.pid == g_selectedPid) return &p;
    return nullptr;
}

static void UpdateHint() {
    std::wstring path = GetEditText(g_dllPath);
    if (path.empty()) {
        g_hint = L"No DLL selected";
        g_hintColor = C_MUTED;
    } else {
        inj::Arch dllArch = inj::GetDllArch(path);
        if (dllArch == inj::Arch::Unknown) {
            g_hint = L"Not a valid DLL";
            g_hintColor = C_DANGER;
        } else {
            std::wstring base = std::wstring(L"DLL ") + inj::ArchName(dllArch);
            const inj::ProcessInfo* p = SelectedProc();
            if (p && p->arch != inj::Arch::Unknown && p->arch != dllArch) {
                g_hint = base + L"  ·  mismatch with " +
                         inj::ArchName(p->arch) + L" target";
                g_hintColor = C_DANGER;
            } else if (p && p->arch != inj::Arch::Unknown) {
                g_hint = base + L"  ·  matches target";
                g_hintColor = C_SUCCESS;
            } else {
                g_hint = base;
                g_hintColor = C_MUTED;
            }
        }
    }
    InvalidateRect(g_hwnd, &g_rcPanel, FALSE);
}

// ------------------------------------------------------------- process list

static void PopulateList() {
    std::wstring filter = GetEditText(g_search);
    for (auto& c : filter) c = static_cast<wchar_t>(towlower(c));

    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_list);
    g_view.clear();

    for (const auto& p : g_procs) {
        if (!filter.empty()) {
            std::wstring name = p.name;
            for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
            if (name.find(filter) == std::wstring::npos &&
                std::to_wstring(p.pid).find(filter) == std::wstring::npos) {
                continue;
            }
        }
        g_view.push_back(p);
    }

    for (size_t i = 0; i < g_view.size(); ++i) {
        const auto& p = g_view[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = static_cast<int>(i);
        it.pszText = const_cast<LPWSTR>(p.name.c_str());
        it.lParam = static_cast<LPARAM>(p.pid);
        ListView_InsertItem(g_list, &it);

        std::wstring pid = std::to_wstring(p.pid);
        ListView_SetItemText(g_list, static_cast<int>(i), 1,
                             const_cast<LPWSTR>(pid.c_str()));
        ListView_SetItemText(g_list, static_cast<int>(i), 2,
                             const_cast<LPWSTR>(inj::ArchName(p.arch)));
    }

    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
}

static void Refresh() {
    SetStatus(L"Scanning running processes…", C_MUTED);
    UpdateWindow(g_hwnd);
    g_procs = inj::EnumerateProcesses();
    g_selectedPid = 0;
    PopulateList();
    SetStatus(std::to_wstring(g_procs.size()) +
                  L" processes found. Select a target.",
              C_MUTED);
    UpdateHint();
}

// --------------------------------------------------------------- injection

struct InjectArgs {
    HWND hwnd;
    DWORD pid;
    std::wstring path;
};

static DWORD WINAPI InjectWorker(LPVOID param) {
    auto* args = static_cast<InjectArgs*>(param);
    auto* result = new inj::InjectResult(inj::InjectDll(args->pid, args->path));
    PostMessageW(args->hwnd, WM_APP_INJECT_DONE, 0,
                 reinterpret_cast<LPARAM>(result));
    delete args;
    return 0;
}

static void DoInject() {
    if (g_injecting) return;

    const inj::ProcessInfo* p = SelectedProc();
    if (!p) {
        SetStatus(L"Select a target process first.", C_WARNING);
        return;
    }
    std::wstring path = GetEditText(g_dllPath);
    if (path.empty()) {
        SetStatus(L"Choose a DLL to inject.", C_WARNING);
        return;
    }

    g_injecting = true;
    EnableWindow(g_inject, FALSE);
    SetStatus(L"Injecting into " + p->name + L" (PID " +
                  std::to_wstring(p->pid) + L")…",
              C_ACCENT_HI);

    auto* args = new InjectArgs{g_hwnd, p->pid, path};
    HANDLE h = CreateThread(nullptr, 0, InjectWorker, args, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        delete args;
        g_injecting = false;
        EnableWindow(g_inject, TRUE);
        SetStatus(L"Could not start injection thread.", C_DANGER);
    }
}

static void BrowseForDll() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Dynamic-link libraries (*.dll)\0*.dll\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select a DLL to inject";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_dllPath, file);
        UpdateHint();
    }
}

// ------------------------------------------------------------- button paint

static void DrawButton(const DRAWITEMSTRUCT* dis) {
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    auto* data = reinterpret_cast<BtnData*>(
        GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA));
    const bool primary = data && data->primary;
    const bool hover = data && data->hover;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    COLORREF fill, textColor;
    bool border = false;
    COLORREF borderColor = C_BORDER;

    if (primary) {
        fill = disabled ? C_SURFACE2
                        : pressed ? C_ACCENT_LO : hover ? C_ACCENT_HI : C_ACCENT;
        textColor = disabled ? C_MUTED : RGB(255, 255, 255);
    } else {
        fill = pressed ? C_SURFACE : hover ? C_HOVER : C_SURFACE2;
        textColor = disabled ? C_MUTED : C_TEXT;
        border = true;
    }

    FillRound(dc, rc, RAD, fill, border, borderColor);

    wchar_t text[128] = L"";
    GetWindowTextW(dis->hwndItem, text, 128);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColor);
    SelectObject(dc, g_fontBtn);
    DrawTextW(dc, text, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK BtnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            auto* d = reinterpret_cast<BtnData*>(
                GetWindowLongPtrW(h, GWLP_USERDATA));
            if (d && !d->hover) {
                d->hover = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(h, nullptr, TRUE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            auto* d = reinterpret_cast<BtnData*>(
                GetWindowLongPtrW(h, GWLP_USERDATA));
            if (d && d->hover) {
                d->hover = false;
                InvalidateRect(h, nullptr, TRUE);
            }
            break;
        }
        case WM_NCDESTROY: {
            delete reinterpret_cast<BtnData*>(
                GetWindowLongPtrW(h, GWLP_USERDATA));
            RemoveWindowSubclass(h, BtnProc, 0);
            break;
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static HWND MakeButton(HWND parent, int id, const wchar_t* label,
                       bool primary) {
    HWND b = CreateWindowExW(
        0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_inst,
        nullptr);
    auto* d = new BtnData{false, primary};
    SetWindowLongPtrW(b, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    SetWindowSubclass(b, BtnProc, 0, 0);
    return b;
}

// -------------------------------------------------------------------- layout

static void Layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int pad = Scale(PAD), gap = Scale(GAP), row = Scale(ROW_H);
    const int panelH = Scale(PANEL_H), headerH = Scale(HEADER_H);
    const int x = pad, w = W - 2 * pad;
    const int btnW = Scale(104);

    // Toolbar: search field + refresh.
    const int toolbarY = headerH + Scale(14);
    g_rcSearchField = {x, toolbarY, x + w - btnW - gap, toolbarY + row};
    MoveWindow(g_search, g_rcSearchField.left + Scale(12),
               g_rcSearchField.top + Scale(2), w - btnW - gap - Scale(24),
               row - Scale(4), TRUE);
    MoveWindow(g_refresh, x + w - btnW, toolbarY, btnW, row, TRUE);

    // Bottom action panel.
    const int panelY = H - pad - panelH;
    g_rcPanel = {x, panelY, x + w, panelY + panelH};
    const int cx = x + Scale(16), cw = w - Scale(32);

    // List fills the space between toolbar and panel.
    const int listTop = toolbarY + row + Scale(16);
    MoveWindow(g_list, x, listTop, w, panelY - Scale(14) - listTop, TRUE);

    // Panel content: DLL field + browse, inject button.
    const int fieldY = panelY + Scale(42);
    g_rcDllField = {cx, fieldY, cx + cw - btnW - gap, fieldY + row};
    MoveWindow(g_dllPath, g_rcDllField.left + Scale(12),
               g_rcDllField.top + Scale(2), cw - btnW - gap - Scale(24),
               row - Scale(4), TRUE);
    MoveWindow(g_browse, cx + cw - btnW, fieldY, btnW, row, TRUE);

    const int injectY = panelY + Scale(90);
    MoveWindow(g_inject, cx, injectY, cw, Scale(42), TRUE);

    // Column widths for the list.
    const int lw = w - GetSystemMetrics(SM_CXVSCROLL) - 4;
    ListView_SetColumnWidth(g_list, 1, Scale(90));
    ListView_SetColumnWidth(g_list, 2, Scale(70));
    ListView_SetColumnWidth(g_list, 0, lw - Scale(160));
}

// --------------------------------------------------------------------- paint

static void PaintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Double-buffer to avoid flicker.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    FillRect(mem, &rc, g_brBg);
    SetBkMode(mem, TRANSPARENT);

    const int pad = Scale(PAD);

    // Header: accent logo tile with a downward "inject" glyph.
    RECT logo = {pad, Scale(20), pad + Scale(36), Scale(56)};
    FillRound(mem, logo, Scale(9), C_ACCENT, false, 0);
    {
        const int cxp = (logo.left + logo.right) / 2;
        const int cyp = (logo.top + logo.bottom) / 2;
        POINT tri[3] = {{cxp - Scale(7), cyp - Scale(5)},
                        {cxp + Scale(7), cyp - Scale(5)},
                        {cxp, cyp + Scale(7)}};
        HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
        HGDIOBJ ob = SelectObject(mem, wb);
        HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
        Polygon(mem, tri, 3);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(wb);
    }

    RECT title = {pad + Scale(48), Scale(18), rc.right - pad, Scale(42)};
    SelectObject(mem, g_fontTitle);
    SetTextColor(mem, C_TEXT);
    DrawTextW(mem, L"DLL Injector", -1, &title,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT sub = {pad + Scale(48), Scale(44), rc.right - pad, Scale(62)};
    SelectObject(mem, g_fontSmall);
    SetTextColor(mem, C_MUTED);
    DrawTextW(mem, L"Load a DLL into a running process", -1, &sub,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Search + DLL fields (rounded borders behind the edit controls).
    FillRound(mem, g_rcSearchField, RAD, C_SURFACE2, true, C_BORDER);
    FillRound(mem, g_rcDllField, RAD, C_SURFACE2, true, C_BORDER);

    // Action panel surface.
    FillRound(mem, g_rcPanel, Scale(14), C_SURFACE, true, C_BORDER);

    const int cx = g_rcPanel.left + Scale(16);
    const int cw = (g_rcPanel.right - g_rcPanel.left) - Scale(32);

    RECT label = {cx, g_rcPanel.top + Scale(14), cx + cw / 2,
                  g_rcPanel.top + Scale(34)};
    SelectObject(mem, g_fontSmall);
    SetTextColor(mem, C_MUTED);
    DrawTextW(mem, L"DLL FILE", -1, &label, DT_LEFT | DT_SINGLELINE);

    RECT hint = {cx + cw / 2, g_rcPanel.top + Scale(14), cx + cw,
                 g_rcPanel.top + Scale(34)};
    SetTextColor(mem, g_hintColor);
    DrawTextW(mem, g_hint.c_str(), -1, &hint,
              DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT status = {cx, g_rcPanel.top + Scale(144), cx + cw,
                   g_rcPanel.top + Scale(166)};
    SelectObject(mem, g_fontBase);
    SetTextColor(mem, g_statusColor);
    DrawTextW(mem, g_status.c_str(), -1, &status,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ------------------------------------------------------------------ wndproc

static void SetupList() {
    ListView_SetExtendedListViewStyle(
        g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(g_list, L"DarkMode_Explorer", nullptr);
    ListView_SetBkColor(g_list, C_SURFACE2);
    ListView_SetTextBkColor(g_list, C_SURFACE2);
    ListView_SetTextColor(g_list, C_TEXT);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<LPWSTR>(L"Process");
    col.cx = Scale(300);
    ListView_InsertColumn(g_list, 0, &col);
    col.pszText = const_cast<LPWSTR>(L"PID");
    col.cx = Scale(90);
    ListView_InsertColumn(g_list, 1, &col);
    col.pszText = const_cast<LPWSTR>(L"Arch");
    col.cx = Scale(70);
    ListView_InsertColumn(g_list, 2, &col);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;

            // Dark title bar (Win10 2004+/Win11).
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));

            g_fontBase = MakeFont(10, FW_NORMAL);
            g_fontTitle = MakeFont(17, FW_SEMIBOLD);
            g_fontSmall = MakeFont(9, FW_NORMAL);
            g_fontBtn = MakeFont(10, FW_SEMIBOLD);

            g_search = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(ID_SEARCH), g_inst, nullptr);
            SendMessageW(g_search, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"Filter by name or PID…"));
            SendMessageW(g_search, WM_SETFONT,
                         reinterpret_cast<WPARAM>(g_fontBase), TRUE);

            g_refresh = MakeButton(hwnd, ID_REFRESH, L"Refresh", false);

            g_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                         LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                     0, 0, 0, 0, hwnd,
                                     reinterpret_cast<HMENU>(ID_LIST), g_inst,
                                     nullptr);
            SendMessageW(g_list, WM_SETFONT,
                         reinterpret_cast<WPARAM>(g_fontBase), TRUE);
            SetupList();

            g_dllPath = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(ID_DLLPATH), g_inst, nullptr);
            SendMessageW(g_dllPath, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"C:\\path\\to\\your.dll"));
            SendMessageW(g_dllPath, WM_SETFONT,
                         reinterpret_cast<WPARAM>(g_fontBase), TRUE);

            g_browse = MakeButton(hwnd, ID_BROWSE, L"Browse…", false);
            g_inject = MakeButton(hwnd, ID_INJECT, L"Inject DLL", true);

            inj::EnableDebugPrivilege();
            Layout(hwnd);
            Refresh();
            return 0;
        }

        case WM_SIZE:
            Layout(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = Scale(720);
            mmi->ptMinTrackSize.y = Scale(560);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  // Painted in WM_PAINT.

        case WM_PAINT:
            PaintWindow(hwnd);
            return 0;

        case WM_CTLCOLOREDIT: {
            auto hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, C_TEXT);
            SetBkColor(hdc, C_SURFACE2);
            return reinterpret_cast<LRESULT>(g_brSurface2);
        }

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (dis->CtlType == ODT_BUTTON) {
                DrawButton(dis);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == ID_REFRESH && code == BN_CLICKED) Refresh();
            else if (id == ID_BROWSE && code == BN_CLICKED) BrowseForDll();
            else if (id == ID_INJECT && code == BN_CLICKED) DoInject();
            else if (id == ID_SEARCH && code == EN_CHANGE) PopulateList();
            else if (id == ID_DLLPATH && code == EN_CHANGE) UpdateHint();
            return 0;
        }

        case WM_NOTIFY: {
            auto* nmh = reinterpret_cast<NMHDR*>(lp);
            if (nmh->idFrom == ID_LIST) {
                if (nmh->code == LVN_ITEMCHANGED) {
                    auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
                    if (nm->uChanged & LVIF_STATE &&
                        (nm->uNewState & LVIS_SELECTED)) {
                        g_selectedPid = static_cast<DWORD>(nm->lParam);
                        UpdateHint();
                    }
                } else if (nmh->code == NM_DBLCLK) {
                    DoInject();
                }
            }
            return 0;
        }

        case WM_APP_INJECT_DONE: {
            auto* result = reinterpret_cast<inj::InjectResult*>(lp);
            g_injecting = false;
            EnableWindow(g_inject, TRUE);
            SetStatus(result->message,
                      result->success ? C_SUCCESS : C_DANGER);
            MessageBeep(result->success ? MB_OK : MB_ICONHAND);
            delete result;
            return 0;
        }

        case WM_DESTROY:
            if (g_fontBase) DeleteObject(g_fontBase);
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            if (g_fontBtn) DeleteObject(g_fontBtn);
            if (g_brBg) DeleteObject(g_brBg);
            if (g_brSurface) DeleteObject(g_brSurface);
            if (g_brSurface2) DeleteObject(g_brSurface2);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES |
                                              ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    g_brBg = CreateSolidBrush(C_BG);
    g_brSurface = CreateSolidBrush(C_SURFACE);
    g_brSurface2 = CreateSolidBrush(C_SURFACE2);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = L"DllInjectorWindow";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    const int w = 880, h = 640;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"DLL Injector",
        WS_OVERLAPPEDWINDOW, (sw - w) / 2, (sh - h) / 2, w, h, nullptr, nullptr,
        inst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &msg)) continue;  // Tab navigation.
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
