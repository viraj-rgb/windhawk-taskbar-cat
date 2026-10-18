// Standalone runner: draws the cat over the taskbar from a normal process, so
// changes can be tried without loading the mod into explorer.
//
//   harness.exe        run until Ctrl+C
//   harness.exe 20     run for 20 seconds

#include <cstdio>
#include <cstdlib>

// Route mod logging to the console (the editing stub is a no-op).
#define Wh_Log(message, ...) \
    (fwprintf(stderr, L"[cat] " message L"\n", ##__VA_ARGS__))

#include "../taskbar-cat.wh.cpp"

static HANDLE g_quitEvent = nullptr;

static BOOL WINAPI OnConsoleCtrl(DWORD) {
    SetEvent(g_quitEvent);
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    // Match explorer, which is per-monitor DPI aware; otherwise taskbar
    // coordinates come back virtualized.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int seconds = argc > 1 ? _wtoi(argv[1]) : 0;
    g_quitEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_quitEvent) {
        return 1;
    }
    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);

    if (!Wh_ModInit()) {
        fwprintf(stderr, L"Wh_ModInit failed\n");
        return 1;
    }
    WaitForSingleObject(g_quitEvent, seconds > 0 ? seconds * 1000 : INFINITE);
    Wh_ModUninit();
    CloseHandle(g_quitEvent);
    return 0;
}
