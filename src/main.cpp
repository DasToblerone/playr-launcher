// PLAYR-Launcher
//
// A Steam game randomizer: it rolls a game from your library, with filters for what may come up, and starts it with one
// click. Games are read from the local Steam libraries, cover art comes from Steam's CDN, and everything is drawn with
// GDI+ in a dark style with a purple accent.

#include "platform/Gdi.h"

#include "model/Library.h"
#include "net/SteamStore.h"
#include "steam/Steam.h"
#include "storage/Storage.h"
#include "ui/Draw.h"
#include "ui/Layout.h"
#include "ui/MainWindow.h"
#include "ui/UiState.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    using namespace playr;

    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    HDC screen = GetDC(nullptr);
    g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, screen);

    InitStorage();
    LoadSettings();
    LoadTypes();
    LoadOwned();
    LoadSteamUser();
    LoadAvatar();
    MergeGames(ScanSteamGames());

    HWND hwnd = CreateMainWindow(instance);
    if (!hwnd) {
        Gdiplus::GdiplusShutdown(gdiToken);
        CoUninitialize();
        return 1;
    }
    QueueBackgroundWork();

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    // Skip static destructors: background threads may still be inside a request.
    ExitProcess((UINT)msg.wParam);
}
