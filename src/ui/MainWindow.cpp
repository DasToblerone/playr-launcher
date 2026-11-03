#include "ui/MainWindow.h"

#include "app/Messages.h"
#include "model/Library.h"
#include "storage/Storage.h"
#include "ui/ArtCache.h"
#include "ui/Categories.h"
#include "ui/ContextMenu.h"
#include "ui/Draw.h"
#include "ui/EditControls.h"
#include "ui/Feedback.h"
#include "ui/GameActions.h"
#include "ui/HitTest.h"
#include "ui/Input.h"
#include "ui/Layout.h"
#include "ui/UiState.h"

using namespace Gdiplus;

namespace playr {

HWND g_mainWnd = nullptr;

// F11: borderless fullscreen on the current monitor, and back to the previous window position.
void ToggleFullscreen(HWND hwnd) {
    static WINDOWPLACEMENT previous{ sizeof(WINDOWPLACEMENT) };
    if (!g_fullscreen) {
        MONITORINFO monitor{ sizeof(monitor) };
        if (!GetWindowPlacement(hwnd, &previous) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            return;
        SetWindowLongPtrW(hwnd, GWL_STYLE, (WINDOW_STYLE & ~FRAME_STYLE) | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = true;
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, WINDOW_STYLE | WS_VISIBLE);
        SetWindowPlacement(hwnd, &previous);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
}

namespace {

void OnCreate(HWND hwnd) {
    BOOL dark = TRUE;
    COLORREF caption = RGB(0, 0, 0), captionText = RGB(179, 179, 179);
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &captionText, sizeof(captionText));
    CreateEditControls(hwnd);
    RefreshGames();
}

void OnDestroy() {
    SaveSettings();
    DestroyEditControls();
    ClearArt();
    delete g_avatar;
    delete g_avatarBaked;
    g_avatar = g_avatarBaked = nullptr;
    PostQuitMessage(0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_SIZE:
        if (g_renameEdit) Relayout(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        RECT r{ 0, 0, S(kMinClientWidth), S(kMinClientHeight) };
        AdjustWindowRectEx(&r, WINDOW_STYLE, FALSE, 0);
        // The title bar is part of the client area (see WM_NCCALCSIZE), so only the side and bottom borders count.
        ((MINMAXINFO*)lp)->ptMinTrackSize = { r.right - r.left, r.bottom };
        return 0;
    }

    // The window draws its own title bar: the client area starts at the very top, like Spotify's.
    case WM_NCCALCSIZE:
        if (wp) {
            auto* params = (NCCALCSIZE_PARAMS*)lp;
            LONG top = params->rgrc[0].top;
            DefWindowProcW(hwnd, msg, wp, lp);  // the usual client rectangle with the side and bottom borders
            params->rgrc[0].top = top;
            // A maximized window reaches beyond the screen by the width of its frame.
            if (IsZoomed(hwnd)) params->rgrc[0].top += GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);  // side and bottom borders, window edges
        if (hit != HTCLIENT) return hit;
        POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &p);
        if (!g_fullscreen) {
            int caption = CaptionButtonAt(p);
            if (caption >= 0) return caption == 0 ? HTMINBUTTON : caption == 1 ? HTMAXBUTTON : HTCLOSE;
            int edge = GetSystemMetrics(SM_CYSIZEFRAME);  // the top edge resizes, too
            if (!IsZoomed(hwnd) && p.y < edge) {
                RECT client;
                GetClientRect(hwnd, &client);
                return p.x < edge ? HTTOPLEFT : p.x >= client.right - edge ? HTTOPRIGHT : HTTOP;
            }
        }
        // The empty part of the top bar moves the window; the search field and the avatar area stay clickable.
        if (In(g_L.topBar, p) && !In(g_L.search, p)) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_NCMOUSEMOVE: {
        POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(hwnd, &p);
        int hover = g_fullscreen ? -1 : CaptionButtonAt(p);
        if (hover != g_captionHover) {
            g_captionHover = hover;
            InvalidateRect(hwnd, &g_L.topBar, FALSE);
        }
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
        TrackMouseEvent(&track);
        break;
    }

    case WM_NCMOUSELEAVE:
        g_captionHover = g_captionPressed = -1;
        InvalidateRect(hwnd, &g_L.topBar, FALSE);
        break;

    case WM_NCLBUTTONDOWN:
        if (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) {
            g_captionPressed = wp == HTMINBUTTON ? 0 : wp == HTMAXBUTTON ? 1 : 2;
            return 0;
        }
        break;

    case WM_NCLBUTTONUP:
        if (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) {
            int button = wp == HTMINBUTTON ? 0 : wp == HTMAXBUTTON ? 1 : 2;
            bool pressed = g_captionPressed == button;
            g_captionPressed = -1;
            if (pressed) {
                WPARAM command = button == 0 ? SC_MINIMIZE : button == 1 ? (IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE) : SC_CLOSE;
                PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
            }
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGB(255, 255, 255));
        SetBkColor((HDC)wp, RGB(31, 31, 31));
        return (LRESULT)g_editBrush;

    case WM_MOUSEMOVE:
        OnMouseMove(hwnd, wp, lp);
        return 0;

    case WM_MOUSELEAVE:
        g_tracking = false;
        g_hover = Hover{};
        Redraw(hwnd);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && (g_drag.active || g_hover.Any())) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_MOUSEWHEEL: {
        CloseMenu(hwnd);
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (In(g_L.sidebar, p)) {
            g_sideScroll -= delta * S(kSideRow) / WHEEL_DELTA;
            Relayout(hwnd);  // moves the rows
        } else {
            g_scroll -= delta * g_L.wheelStep / WHEEL_DELTA;
            ClampScroll();
        }
        UpdateHover(hwnd, p);
        Redraw(hwnd);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
        OnMouseDown(hwnd, msg, lp);
        return 0;

    case WM_LBUTTONUP:
        DropOnCategory(hwnd);
        return 0;

    case WM_RBUTTONUP: {
        POINT p = ClientPoint(lp);
        int chip = HitChip(p), row = HitRow(p);
        CloseMenu(hwnd);
        if (chip >= 1) {
            OpenMenu(hwnd, p, chip, -1, { Menu::Rename, Menu::Delete });  // "Alle Spiele", the first row, is built in
        } else if (row >= 0 && g_active >= 1) {
            // A game inside a category can be taken out of it again; it stays in the library and other categories.
            OpenMenu(hwnd, p, g_active, g_visible[row], { Menu::RemoveFromCategory });
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_drag.row >= 0) { g_drag = DragState{}; Redraw(hwnd); }
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(hwnd, wp);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_SEARCH_EDIT) {
            WORD code = HIWORD(wp);
            if (code == EN_CHANGE) ApplySearch(hwnd);
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateRect(hwnd, &g_L.search, FALSE);
            return 0;
        }
        if (id == ID_RENAME_EDIT) {
            if (HIWORD(wp) == EN_KILLFOCUS) EndRename(hwnd, false);  // clicking away cancels
            return 0;
        }
        switch (id - ID_ACTION) {
        case BTN_SCAN: Rescan(hwnd); return 0;
        case BTN_IMPORT: ImportFromSteam(hwnd); return 0;
        case BTN_RANDOM: SelectRandomGame(hwnd); return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wp == TIMER_ANIMATION) TickBadges(hwnd);
        return 0;

    case WM_COVER:
        OnCoverDownloaded(hwnd, (unsigned long long)wp);
        return 0;

    case WM_SCANDONE:
        OnScanDone(hwnd, (std::map<std::wstring, Game>*)lp);
        return 0;

    case WM_OWNED:
        OnOwned(hwnd, (OwnedResult*)lp);
        return 0;

    case WM_CLASSIFIED:
        OnClassified(hwnd, std::to_wstring((unsigned long long)wp), lp != 0);
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND CreateMainWindow(HINSTANCE instance) {
    const wchar_t className[] = L"PlayrLauncherWindow";
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);

    RECT frame{ 0, 0, S(kStartClientWidth), S(kStartClientHeight) };
    AdjustWindowRectEx(&frame, WINDOW_STYLE, FALSE, 0);
    int width = frame.right - frame.left, height = frame.bottom;  // no title bar above the client area
    RECT work{ 0, 0, width, height };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + std::max(0, (int)(work.right - work.left - width) / 2);
    int y = work.top + std::max(0, (int)(work.bottom - work.top - height) / 2);
    HWND hwnd = CreateWindowExW(0, className, L"PLAYR-Launcher", WINDOW_STYLE, x, y, width, height,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) return nullptr;
    g_mainWnd = hwnd;
    Relayout(hwnd);
    return hwnd;
}

}  // namespace playr
