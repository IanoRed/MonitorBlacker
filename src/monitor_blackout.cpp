#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:wWinMainCRTStartup")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <cstring>
#include "resource.h"

// Timing constants
#define AUTO_REBLACK_MS      300000
#define FORCED_REBLACK_MS    1000
#define POLL_ACTIVE_MS       250
#define POLL_IDLE_MS         1000
#define IDLE_THRESHOLD_MS    2000
#define HOTPLUG_TIMER_MS     5000
#define HOLDREVEAL_TIMER_MS  80

// Colors
#define COLOR_WIDGET_BG      RGB(25, 25, 25)
#define COLOR_WIDGET_HEADER  RGB(15, 15, 15)
#define COLOR_WIDGET_BTN     RGB(50, 50, 50)
#define COLOR_WIDGET_BTN_HVR RGB(70, 70, 70)
#define COLOR_WIDGET_TEXT    RGB(220, 220, 220)
#define COLOR_CLOSE_NORMAL   RGB(50, 50, 50)
#define COLOR_CLOSE_HOVER    RGB(200, 50, 50)
#define COLOR_STATUS_BLACK   RGB(220, 50, 50)
#define COLOR_STATUS_CLEAR   RGB(50, 200, 50)

// Dimensions
#define WIDGET_WIDTH         400
#define WIDGET_HEADER_H      32
#define WIDGET_ROW_HEIGHT    40
#define WIDGET_PADDING       10
#define WIDGET_BTN_W         70
#define WIDGET_BTN_H         26
#define WIDGET_CORNER_RAD    10
#define CLOSE_BTN_SIZE       24

// Hotkey IDs
#define HOTKEY_TOGGLE_ALL    1000
#define HOTKEY_TOGGLE_BASE   1001
#define HOTKEY_CYCLE_ALL     1100
#define HOTKEY_CYCLE_BASE    1101

// Messages
#define WM_TRAYICON          (WM_USER + 1)
#define WM_RESCAN            (WM_USER + 2)

// Timer IDs
#define TIMER_POLL           1
#define TIMER_HOTPLUG        2
#define TIMER_HOLDREVEAL     3

// Enums
enum class OverlayState {
    VisibleBlack,
    TemporarilyRevealed,
    Cleared
};

enum class MonitorMode {
    Manual,
    Auto,
    Forced
};

// MonitorInfo struct
struct MonitorInfo {
    HMONITOR hMonitor;
    RECT rcMonitor;
    std::wstring displayName;
    bool isPrimary;
    int index;
    HWND hWndOverlay;
    OverlayState state;
    MonitorMode mode;
    ULONGLONG clearedTime;
    UINT32 targetId;
};

// Saved state for hotplug restoration
struct SavedMonitorState {
    OverlayState state;
    MonitorMode mode;
};

// Globals
static HINSTANCE g_hInstance = NULL;
static HWND g_hWndMain = NULL;
static HWND g_hWndWidget = NULL;
static NOTIFYICONDATAW g_nid = {};
static std::vector<MonitorInfo> g_monitors;
static UINT g_taskbarCreatedMsg = 0;
static bool g_widgetVisible = false;
static HANDLE g_hMutex = NULL;
static HFONT g_hFont = NULL;
static HFONT g_hFontBold = NULL;
static std::vector<UINT32> g_lastTargetIds;
static POINT g_lastCursorPos = {};
static ULONGLONG g_lastCursorMoveTime = 0;

// Widget state
static bool g_closeHovered = false;
static bool g_dragging = false;
static POINT g_dragStartCursor = {};
static RECT g_dragStartRect = {};

// Button hit-test tracking
struct ButtonRect {
    RECT rc;
    int monitorIndex;
    int buttonType; // 0=toggle, 1=mode
};
static std::vector<ButtonRect> g_buttonRects;
static RECT g_closeButtonRect = {};
static RECT g_rescanButtonRect = {};
static int g_hoveredButtonIdx = -1; // index into g_buttonRects
static bool g_rescanHovered = false;

// Forward declarations
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WidgetWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK OverlayWndProc(HWND, UINT, WPARAM, LPARAM);
static void EnumerateMonitors();
static void CreateOverlays();
static void DestroyOverlays();
static void UpdateOverlayVisibility(int index, bool shouldBeBlack);
static void ApplyModeLogic();
static void UpdateWidgetUI();
static void ToggleMonitor(int index);
static void ToggleAll();
static void CycleMode(int index);
static void CycleAllModes();
static void RegisterHotkeys();
static void UnregisterHotkeys();
static void CreateTrayIcon();
static void RemoveTrayIcon();
static void ShowWidget();
static void HideWidget();
static void ToggleWidget();
static void Cleanup();
static void CheckHotplug();
static void CheckHoldReveal();
static void RescanMonitors();
static bool IsOurWindow(HWND hWnd);
static int DpiScale(int value);
static void GetDisplayConfigTargetIds(std::vector<UINT32>& ids);
static UINT32 GetTargetIdForMonitor(const wchar_t* deviceName);
static void CreateWidgetWindow();
static void CreateFonts();
static void DestroyFonts();

// IsOurWindow helper
static bool IsOurWindow(HWND hWnd) {
    if (hWnd == NULL) return false;
    if (hWnd == g_hWndWidget) return true;
    if (hWnd == g_hWndMain) return true;
    for (size_t i = 0; i < g_monitors.size(); i++) {
        if (hWnd == g_monitors[i].hWndOverlay) return true;
    }
    return false;
}

// DPI scaling helper
static int DpiScale(int value) {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return MulDiv(value, dpi, 96);
}

// Font creation
static void CreateFonts() {
    if (g_hFont) return;
    g_hFont = CreateFontW(-DpiScale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_hFontBold = CreateFontW(-DpiScale(13), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void DestroyFonts() {
    if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
    if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
}

// DISPLAYCONFIG target ID lookup
static void GetDisplayConfigTargetIds(std::vector<UINT32>& ids) {
    ids.clear();
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), NULL) != ERROR_SUCCESS) return;
    for (UINT32 i = 0; i < pathCount; i++) {
        ids.push_back(paths[i].targetInfo.id);
    }
    std::sort(ids.begin(), ids.end());
}

static UINT32 GetTargetIdForMonitor(const wchar_t* deviceName) {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), NULL) != ERROR_SUCCESS) return 0;
    for (UINT32 i = 0; i < pathCount; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
        sourceName.header.id = paths[i].sourceInfo.id;
        sourceName.header.size = sizeof(sourceName);
        if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
            if (wcscmp(sourceName.viewGdiDeviceName, deviceName) == 0) {
                return paths[i].targetInfo.id;
            }
        }
    }
    return 0;
}

// Monitor enumeration callback
static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM) {
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMonitor, &mi)) return TRUE;

    MonitorInfo info = {};
    info.hMonitor = hMonitor;
    info.rcMonitor = mi.rcMonitor;
    info.displayName = mi.szDevice;
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    info.index = (int)g_monitors.size();
    info.hWndOverlay = NULL;
    info.state = OverlayState::Cleared;
    info.mode = MonitorMode::Manual;
    info.clearedTime = GetTickCount64();
    info.targetId = GetTargetIdForMonitor(mi.szDevice);

    g_monitors.push_back(info);
    return TRUE;
}

static void EnumerateMonitors() {
    g_monitors.clear();
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    // Sort so primary is first
    std::sort(g_monitors.begin(), g_monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.isPrimary != b.isPrimary) return a.isPrimary > b.isPrimary;
        return a.rcMonitor.left < b.rcMonitor.left;
    });
    for (int i = 0; i < (int)g_monitors.size(); i++) {
        g_monitors[i].index = i;
    }
    // Set defaults: primary = Manual+Cleared, others = Forced+VisibleBlack
    for (int i = 0; i < (int)g_monitors.size(); i++) {
        if (g_monitors[i].isPrimary) {
            g_monitors[i].mode = MonitorMode::Manual;
            g_monitors[i].state = OverlayState::Cleared;
        } else {
            g_monitors[i].mode = MonitorMode::Forced;
            g_monitors[i].state = OverlayState::VisibleBlack;
        }
    }
}

// Overlay creation/destruction
static void CreateOverlays() {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = g_hInstance;
        wc.lpszClassName = L"MonitorBlackoutOverlay";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    for (int i = 0; i < (int)g_monitors.size(); i++) {
        MonitorInfo& mon = g_monitors[i];
        int w = mon.rcMonitor.right - mon.rcMonitor.left;
        int h = mon.rcMonitor.bottom - mon.rcMonitor.top;
        mon.hWndOverlay = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            L"MonitorBlackoutOverlay", L"",
            WS_POPUP,
            mon.rcMonitor.left, mon.rcMonitor.top, w, h,
            NULL, NULL, g_hInstance, NULL);
        SetLayeredWindowAttributes(mon.hWndOverlay, 0, 255, LWA_ALPHA);

        // Show overlay if state is VisibleBlack
        if (mon.state == OverlayState::VisibleBlack) {
            ShowWindow(mon.hWndOverlay, SW_SHOWNOACTIVATE);
            SetWindowPos(mon.hWndOverlay, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

static void DestroyOverlays() {
    for (size_t i = 0; i < g_monitors.size(); i++) {
        if (g_monitors[i].hWndOverlay) {
            DestroyWindow(g_monitors[i].hWndOverlay);
            g_monitors[i].hWndOverlay = NULL;
        }
    }
}

// UpdateOverlayVisibility
static void UpdateOverlayVisibility(int index, bool shouldBeBlack) {
    if (index < 0 || index >= (int)g_monitors.size()) return;

    MonitorInfo& mon = g_monitors[index];

    if (shouldBeBlack) {
        if (mon.state != OverlayState::VisibleBlack) {
            ShowWindow(mon.hWndOverlay, SW_SHOWNOACTIVATE);
            SetWindowPos(mon.hWndOverlay, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            mon.state = OverlayState::VisibleBlack;
        }
    } else {
        if (mon.state != OverlayState::Cleared) {
            ShowWindow(mon.hWndOverlay, SW_HIDE);
            mon.state = OverlayState::Cleared;
            mon.clearedTime = GetTickCount64();
        }
    }

    UpdateWidgetUI();
}

// ApplyModeLogic - CRITICAL FUNCTION
static void ApplyModeLogic() {
    POINT cursorPos;
    GetCursorPos(&cursorPos);

    HWND hFgWnd = GetForegroundWindow();
    if (IsOurWindow(hFgWnd)) {
        hFgWnd = NULL;
    }

    HMONITOR hFgMonitor = NULL;
    if (hFgWnd != NULL) {
        hFgMonitor = MonitorFromWindow(hFgWnd, MONITOR_DEFAULTTONULL);
    }

    for (int i = 0; i < (int)g_monitors.size(); i++) {
        MonitorInfo& mon = g_monitors[i];

        bool cursorOnMonitor = PtInRect(&mon.rcMonitor, cursorPos) != 0;
        bool fgWindowOnMonitor = (hFgMonitor != NULL && hFgMonitor == mon.hMonitor);
        bool activityDetected = cursorOnMonitor || fgWindowOnMonitor;

        if (mon.state == OverlayState::VisibleBlack) {
            if (activityDetected) {
                ShowWindow(mon.hWndOverlay, SW_HIDE);
                mon.state = OverlayState::Cleared;
                mon.clearedTime = GetTickCount64();
                UpdateWidgetUI();
            }
        } else if (mon.state == OverlayState::Cleared) {
            if (!activityDetected) {
                switch (mon.mode) {
                    case MonitorMode::Manual:
                        // Never re-black automatically
                        break;
                    case MonitorMode::Auto:
                        if ((GetTickCount64() - mon.clearedTime) >= AUTO_REBLACK_MS) {
                            ShowWindow(mon.hWndOverlay, SW_SHOWNOACTIVATE);
                            SetWindowPos(mon.hWndOverlay, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                            mon.state = OverlayState::VisibleBlack;
                            UpdateWidgetUI();
                        }
                        break;
                    case MonitorMode::Forced:
                        if ((GetTickCount64() - mon.clearedTime) >= FORCED_REBLACK_MS) {
                            ShowWindow(mon.hWndOverlay, SW_SHOWNOACTIVATE);
                            SetWindowPos(mon.hWndOverlay, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                            mon.state = OverlayState::VisibleBlack;
                            UpdateWidgetUI();
                        }
                        break;
                }
            }
        }
        // TemporarilyRevealed is handled by CheckHoldReveal

        // Reassert TOPMOST for visible overlays
        if (mon.state == OverlayState::VisibleBlack) {
            SetWindowPos(mon.hWndOverlay, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        }
    }
}

// Hold-to-reveal logic using GetAsyncKeyState polling
static void CheckHoldReveal() {
    bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    for (int i = 0; i < (int)g_monitors.size() && i < 9; i++) {
        MonitorInfo& mon = g_monitors[i];
        bool numHeld = (GetAsyncKeyState('1' + i) & 0x8000) != 0;

        if (ctrlHeld && numHeld) {
            if (mon.state == OverlayState::VisibleBlack) {
                ShowWindow(mon.hWndOverlay, SW_HIDE);
                mon.state = OverlayState::TemporarilyRevealed;
                UpdateWidgetUI();
            }
        } else {
            if (mon.state == OverlayState::TemporarilyRevealed) {
                ShowWindow(mon.hWndOverlay, SW_SHOWNOACTIVATE);
                SetWindowPos(mon.hWndOverlay, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                mon.state = OverlayState::VisibleBlack;
                UpdateWidgetUI();
            }
        }
    }
}

// Toggle functions
static void ToggleMonitor(int index) {
    if (index < 0 || index >= (int)g_monitors.size()) return;
    MonitorInfo& mon = g_monitors[index];
    if (mon.state == OverlayState::VisibleBlack || mon.state == OverlayState::TemporarilyRevealed) {
        UpdateOverlayVisibility(index, false);
    } else {
        UpdateOverlayVisibility(index, true);
    }
}

static void ToggleAll() {
    // If any are blacked, clear all; otherwise black all non-primary
    bool anyBlacked = false;
    for (size_t i = 0; i < g_monitors.size(); i++) {
        if (g_monitors[i].state == OverlayState::VisibleBlack ||
            g_monitors[i].state == OverlayState::TemporarilyRevealed) {
            anyBlacked = true;
            break;
        }
    }
    for (int i = 0; i < (int)g_monitors.size(); i++) {
        if (anyBlacked) {
            UpdateOverlayVisibility(i, false);
        } else {
            if (!g_monitors[i].isPrimary) {
                UpdateOverlayVisibility(i, true);
            }
        }
    }
}

static void CycleMode(int index) {
    if (index < 0 || index >= (int)g_monitors.size()) return;
    MonitorInfo& mon = g_monitors[index];
    switch (mon.mode) {
        case MonitorMode::Manual: mon.mode = MonitorMode::Auto; break;
        case MonitorMode::Auto: mon.mode = MonitorMode::Forced; break;
        case MonitorMode::Forced: mon.mode = MonitorMode::Manual; break;
    }
    UpdateWidgetUI();
}

static void CycleAllModes() {
    for (int i = 0; i < (int)g_monitors.size(); i++) {
        CycleMode(i);
    }
}

// Mode to string
static const wchar_t* ModeToString(MonitorMode m) {
    switch (m) {
        case MonitorMode::Manual: return L"Manual";
        case MonitorMode::Auto: return L"Auto";
        case MonitorMode::Forced: return L"Forced";
    }
    return L"?";
}

// Widget UI update
static void UpdateWidgetUI() {
    if (g_hWndWidget && g_widgetVisible) {
        InvalidateRect(g_hWndWidget, NULL, TRUE);
    }
}

// Calculate widget height
static int GetWidgetHeight() {
    int rows = (int)g_monitors.size();
    return DpiScale(WIDGET_HEADER_H + rows * WIDGET_ROW_HEIGHT + WIDGET_ROW_HEIGHT + WIDGET_PADDING);
}

// Create widget window
static void CreateWidgetWindow() {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WidgetWndProc;
        wc.hInstance = g_hInstance;
        wc.lpszClassName = L"MonitorBlackoutWidget";
        wc.hbrBackground = CreateSolidBrush(COLOR_WIDGET_BG);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    int widgetW = DpiScale(WIDGET_WIDTH);
    int widgetH = GetWidgetHeight();

    // Position at bottom-right of primary monitor work area
    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int margin = DpiScale(16);
    int posX = workArea.right - widgetW - margin;
    int posY = workArea.bottom - widgetH - margin;

    g_hWndWidget = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MonitorBlackoutWidget", L"Monitor Blackout",
        WS_POPUP,
        posX, posY, widgetW, widgetH,
        NULL, NULL, g_hInstance, NULL);

    // Rounded corners
    HRGN hRgn = CreateRoundRectRgn(0, 0, widgetW + 1, widgetH + 1,
        DpiScale(WIDGET_CORNER_RAD), DpiScale(WIDGET_CORNER_RAD));
    SetWindowRgn(g_hWndWidget, hRgn, TRUE);
}

static void ShowWidget() {
    if (!g_hWndWidget) return;
    int widgetW = DpiScale(WIDGET_WIDTH);
    int widgetH = GetWidgetHeight();

    // Position at bottom-right of primary monitor work area
    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int margin = DpiScale(16);
    int posX = workArea.right - widgetW - margin;
    int posY = workArea.bottom - widgetH - margin;

    SetWindowPos(g_hWndWidget, HWND_TOPMOST, posX, posY, widgetW, widgetH,
        SWP_NOACTIVATE);
    HRGN hRgn = CreateRoundRectRgn(0, 0, widgetW + 1, widgetH + 1,
        DpiScale(WIDGET_CORNER_RAD), DpiScale(WIDGET_CORNER_RAD));
    SetWindowRgn(g_hWndWidget, hRgn, TRUE);
    ShowWindow(g_hWndWidget, SW_SHOWNOACTIVATE);
    g_widgetVisible = true;
    UpdateWidgetUI();
}

static void HideWidget() {
    if (!g_hWndWidget) return;
    ShowWindow(g_hWndWidget, SW_HIDE);
    g_widgetVisible = false;
}

static void ToggleWidget() {
    if (g_widgetVisible) HideWidget();
    else ShowWidget();
}

// Widget window proc
static LRESULT CALLBACK WidgetWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        // Double buffer
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

        // Background
        HBRUSH bgBrush = CreateSolidBrush(COLOR_WIDGET_BG);
        FillRect(hdcMem, &rc, bgBrush);
        DeleteObject(bgBrush);

        // Header background
        RECT headerRect = { 0, 0, rc.right, DpiScale(WIDGET_HEADER_H) };
        HBRUSH headerBrush = CreateSolidBrush(COLOR_WIDGET_HEADER);
        FillRect(hdcMem, &headerRect, headerBrush);
        DeleteObject(headerBrush);

        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, COLOR_WIDGET_TEXT);

        HFONT hOldFont = (HFONT)SelectObject(hdcMem, g_hFontBold);

        // Title text
        RECT titleRect = { DpiScale(WIDGET_PADDING), 0, rc.right - DpiScale(CLOSE_BTN_SIZE + WIDGET_PADDING), DpiScale(WIDGET_HEADER_H) };
        DrawTextW(hdcMem, L"Monitor Blackout Manager", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Close button
        g_closeButtonRect = { rc.right - DpiScale(CLOSE_BTN_SIZE + 4), DpiScale(4),
            rc.right - DpiScale(4), DpiScale(CLOSE_BTN_SIZE + 4) };
        HBRUSH closeBrush = CreateSolidBrush(g_closeHovered ? COLOR_CLOSE_HOVER : COLOR_CLOSE_NORMAL);
        FillRect(hdcMem, &g_closeButtonRect, closeBrush);
        DeleteObject(closeBrush);
        DrawTextW(hdcMem, L"\u00D7", -1, &g_closeButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdcMem, g_hFont);

        // Clear button rects
        g_buttonRects.clear();

        // Monitor rows
        int startY = DpiScale(WIDGET_HEADER_H + 4);
        for (int i = 0; i < (int)g_monitors.size(); i++) {
            MonitorInfo& mon = g_monitors[i];
            int rowY = startY + i * DpiScale(WIDGET_ROW_HEIGHT);

            // Label
            wchar_t label[128];
            wsprintfW(label, L"Display %d:%s", i + 1, mon.isPrimary ? L" (Primary)" : L"");
            RECT labelRect = { DpiScale(WIDGET_PADDING), rowY, DpiScale(140), rowY + DpiScale(WIDGET_ROW_HEIGHT) };
            DrawTextW(hdcMem, label, -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Toggle button
            int btnX1 = DpiScale(145);
            int btnY1 = rowY + (DpiScale(WIDGET_ROW_HEIGHT) - DpiScale(WIDGET_BTN_H)) / 2;
            RECT btnToggle = { btnX1, btnY1, btnX1 + DpiScale(WIDGET_BTN_W), btnY1 + DpiScale(WIDGET_BTN_H) };

            ButtonRect br1;
            br1.rc = btnToggle;
            br1.monitorIndex = i;
            br1.buttonType = 0;
            int btn1Idx = (int)g_buttonRects.size();
            g_buttonRects.push_back(br1);

            bool btn1Hovered = (g_hoveredButtonIdx == btn1Idx);
            HBRUSH tbBrush = CreateSolidBrush(btn1Hovered ? COLOR_WIDGET_BTN_HVR : COLOR_WIDGET_BTN);
            FillRect(hdcMem, &btnToggle, tbBrush);
            DeleteObject(tbBrush);
            const wchar_t* toggleStr = (mon.state == OverlayState::VisibleBlack || mon.state == OverlayState::TemporarilyRevealed) ? L"Clear" : L"Black";
            DrawTextW(hdcMem, toggleStr, -1, &btnToggle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Mode button
            int btnX2 = DpiScale(220);
            RECT btnMode = { btnX2, btnY1, btnX2 + DpiScale(WIDGET_BTN_W), btnY1 + DpiScale(WIDGET_BTN_H) };

            ButtonRect br2;
            br2.rc = btnMode;
            br2.monitorIndex = i;
            br2.buttonType = 1;
            int btn2Idx = (int)g_buttonRects.size();
            g_buttonRects.push_back(br2);

            bool btn2Hovered = (g_hoveredButtonIdx == btn2Idx);
            HBRUSH mbBrush = CreateSolidBrush(btn2Hovered ? COLOR_WIDGET_BTN_HVR : COLOR_WIDGET_BTN);
            FillRect(hdcMem, &btnMode, mbBrush);
            DeleteObject(mbBrush);
            DrawTextW(hdcMem, ModeToString(mon.mode), -1, &btnMode, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Status text
            int statusX = DpiScale(295);
            RECT statusRect = { statusX, rowY, rc.right - DpiScale(WIDGET_PADDING), rowY + DpiScale(WIDGET_ROW_HEIGHT) };
            if (mon.state == OverlayState::VisibleBlack || mon.state == OverlayState::TemporarilyRevealed) {
                SetTextColor(hdcMem, COLOR_STATUS_BLACK);
                DrawTextW(hdcMem, L"[BLACKED]", -1, &statusRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else {
                SetTextColor(hdcMem, COLOR_STATUS_CLEAR);
                DrawTextW(hdcMem, L"[CLEAR]", -1, &statusRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            SetTextColor(hdcMem, COLOR_WIDGET_TEXT);
        }

        // Rescan button
        int rescanY = startY + (int)g_monitors.size() * DpiScale(WIDGET_ROW_HEIGHT) + DpiScale(6);
        g_rescanButtonRect = { DpiScale(WIDGET_PADDING), rescanY,
            DpiScale(WIDGET_PADDING) + DpiScale(120), rescanY + DpiScale(WIDGET_BTN_H) };
        HBRUSH rsBrush = CreateSolidBrush(g_rescanHovered ? COLOR_WIDGET_BTN_HVR : COLOR_WIDGET_BTN);
        FillRect(hdcMem, &g_rescanButtonRect, rsBrush);
        DeleteObject(rsBrush);
        DrawTextW(hdcMem, L"Rescan Monitors", -1, &g_rescanButtonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdcMem, hOldFont);

        // Blit
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        POINT pt = { mx, my };

        // Close button hit test
        if (PtInRect(&g_closeButtonRect, pt)) {
            HideWidget();
            return 0;
        }

        // Button hit tests
        for (int i = 0; i < (int)g_buttonRects.size(); i++) {
            if (PtInRect(&g_buttonRects[i].rc, pt)) {
                if (g_buttonRects[i].buttonType == 0) {
                    ToggleMonitor(g_buttonRects[i].monitorIndex);
                } else {
                    CycleMode(g_buttonRects[i].monitorIndex);
                }
                return 0;
            }
        }

        // Rescan button
        if (PtInRect(&g_rescanButtonRect, pt)) {
            PostMessageW(g_hWndMain, WM_RESCAN, 0, 0);
            return 0;
        }

        // Start drag (only from header area)
        if (my < DpiScale(WIDGET_HEADER_H)) {
            g_dragging = true;
            GetCursorPos(&g_dragStartCursor);
            GetWindowRect(hWnd, &g_dragStartRect);
            SetCapture(hWnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        POINT pt = { mx, my };

        if (g_dragging) {
            POINT curPt;
            GetCursorPos(&curPt);
            int dx = curPt.x - g_dragStartCursor.x;
            int dy = curPt.y - g_dragStartCursor.y;
            SetWindowPos(hWnd, NULL,
                g_dragStartRect.left + dx, g_dragStartRect.top + dy,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        // Hover tracking
        bool needRepaint = false;

        bool wasCloseHovered = g_closeHovered;
        g_closeHovered = PtInRect(&g_closeButtonRect, pt) != 0;
        if (wasCloseHovered != g_closeHovered) needRepaint = true;

        int oldHovered = g_hoveredButtonIdx;
        g_hoveredButtonIdx = -1;
        for (int i = 0; i < (int)g_buttonRects.size(); i++) {
            if (PtInRect(&g_buttonRects[i].rc, pt)) {
                g_hoveredButtonIdx = i;
                break;
            }
        }
        if (oldHovered != g_hoveredButtonIdx) needRepaint = true;

        bool wasRescanHovered = g_rescanHovered;
        g_rescanHovered = PtInRect(&g_rescanButtonRect, pt) != 0;
        if (wasRescanHovered != g_rescanHovered) needRepaint = true;

        if (needRepaint) InvalidateRect(hWnd, NULL, TRUE);

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_closeHovered = false;
        g_hoveredButtonIdx = -1;
        g_rescanHovered = false;
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;

    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

// Overlay window proc
static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH br = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(hdc, &rc, br);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

// Hotkey registration
static void RegisterHotkeys() {
    if (!g_hWndMain) return;
    // Ctrl+0 = toggle all
    RegisterHotKey(g_hWndMain, HOTKEY_TOGGLE_ALL, MOD_CONTROL, '0');
    // Ctrl+1..9 = toggle display N
    for (int i = 0; i < 9; i++) {
        RegisterHotKey(g_hWndMain, HOTKEY_TOGGLE_BASE + i, MOD_CONTROL, '1' + i);
    }
    // Ctrl+Alt+B = cycle all modes
    RegisterHotKey(g_hWndMain, HOTKEY_CYCLE_ALL, MOD_CONTROL | MOD_ALT, 'B');
    // Ctrl+Alt+1..9 = cycle mode for display N
    for (int i = 0; i < 9; i++) {
        RegisterHotKey(g_hWndMain, HOTKEY_CYCLE_BASE + i, MOD_CONTROL | MOD_ALT, '1' + i);
    }
}

static void UnregisterHotkeys() {
    if (!g_hWndMain) return;
    UnregisterHotKey(g_hWndMain, HOTKEY_TOGGLE_ALL);
    for (int i = 0; i < 9; i++) {
        UnregisterHotKey(g_hWndMain, HOTKEY_TOGGLE_BASE + i);
    }
    UnregisterHotKey(g_hWndMain, HOTKEY_CYCLE_ALL);
    for (int i = 0; i < 9; i++) {
        UnregisterHotKey(g_hWndMain, HOTKEY_CYCLE_BASE + i);
    }
}

// System tray
static void CreateTrayIcon() {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hWndMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    wcscpy_s(g_nid.szTip, L"Monitor Blackout");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// Hotplug detection
static void CheckHotplug() {
    std::vector<UINT32> currentIds;
    GetDisplayConfigTargetIds(currentIds);
    if (currentIds != g_lastTargetIds) {
        g_lastTargetIds = currentIds;
        PostMessageW(g_hWndMain, WM_RESCAN, 0, 0);
    }
}

// RescanMonitors - saves and restores state
static void RescanMonitors() {
    // Save state per target ID
    std::map<UINT32, SavedMonitorState> savedStates;
    for (size_t i = 0; i < g_monitors.size(); i++) {
        if (g_monitors[i].targetId != 0) {
            SavedMonitorState ss;
            ss.state = g_monitors[i].state;
            ss.mode = g_monitors[i].mode;
            savedStates[g_monitors[i].targetId] = ss;
        }
    }

    UnregisterHotkeys();
    DestroyOverlays();
    EnumerateMonitors();

    // Restore saved state where target IDs match
    for (size_t i = 0; i < g_monitors.size(); i++) {
        if (g_monitors[i].targetId != 0) {
            auto it = savedStates.find(g_monitors[i].targetId);
            if (it != savedStates.end()) {
                g_monitors[i].state = it->second.state;
                g_monitors[i].mode = it->second.mode;
                if (g_monitors[i].state == OverlayState::Cleared) {
                    g_monitors[i].clearedTime = GetTickCount64();
                }
            }
        }
    }

    CreateOverlays();
    RegisterHotkeys();
    GetDisplayConfigTargetIds(g_lastTargetIds);

    // Rebuild widget if visible
    if (g_widgetVisible) {
        int widgetW = DpiScale(WIDGET_WIDTH);
        int widgetH = GetWidgetHeight();
        SetWindowPos(g_hWndWidget, HWND_TOPMOST, 0, 0, widgetW, widgetH,
            SWP_NOMOVE | SWP_NOACTIVATE);
        HRGN hRgn = CreateRoundRectRgn(0, 0, widgetW + 1, widgetH + 1,
            DpiScale(WIDGET_CORNER_RAD), DpiScale(WIDGET_CORNER_RAD));
        SetWindowRgn(g_hWndWidget, hRgn, TRUE);
        InvalidateRect(g_hWndWidget, NULL, TRUE);
    }

    UpdateWidgetUI();
}

// Main window proc
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hWnd, TIMER_POLL, POLL_ACTIVE_MS, NULL);
        SetTimer(hWnd, TIMER_HOTPLUG, HOTPLUG_TIMER_MS, NULL);
        SetTimer(hWnd, TIMER_HOLDREVEAL, HOLDREVEAL_TIMER_MS, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_POLL) {
            ApplyModeLogic();

            // Adaptive polling based on cursor movement
            POINT curPos;
            GetCursorPos(&curPos);
            if (curPos.x != g_lastCursorPos.x || curPos.y != g_lastCursorPos.y) {
                g_lastCursorPos = curPos;
                g_lastCursorMoveTime = GetTickCount64();
            }

            bool anyNonManual = false;
            for (size_t i = 0; i < g_monitors.size(); i++) {
                if (g_monitors[i].state != OverlayState::Cleared || g_monitors[i].mode != MonitorMode::Manual) {
                    anyNonManual = true;
                    break;
                }
            }

            ULONGLONG timeSinceMove = GetTickCount64() - g_lastCursorMoveTime;
            UINT interval;
            if (anyNonManual || timeSinceMove < IDLE_THRESHOLD_MS) {
                interval = POLL_ACTIVE_MS;
            } else {
                interval = POLL_IDLE_MS;
            }
            SetTimer(hWnd, TIMER_POLL, interval, NULL);
        } else if (wParam == TIMER_HOTPLUG) {
            CheckHotplug();
        } else if (wParam == TIMER_HOLDREVEAL) {
            CheckHoldReveal();
        }
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE_ALL) {
            ToggleAll();
        } else if (wParam >= (WPARAM)HOTKEY_TOGGLE_BASE && wParam < (WPARAM)(HOTKEY_TOGGLE_BASE + 9)) {
            ToggleMonitor((int)(wParam - HOTKEY_TOGGLE_BASE));
        } else if (wParam == HOTKEY_CYCLE_ALL) {
            CycleAllModes();
        } else if (wParam >= (WPARAM)HOTKEY_CYCLE_BASE && wParam < (WPARAM)(HOTKEY_CYCLE_BASE + 9)) {
            CycleMode((int)(wParam - HOTKEY_CYCLE_BASE));
        }
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            ToggleWidget();
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, g_widgetVisible ? L"Hide Widget" : L"Show Widget");
            AppendMenuW(hMenu, MF_STRING, 2, L"Toggle All");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 3, L"Quit");
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == 1) ToggleWidget();
            else if (cmd == 2) ToggleAll();
            else if (cmd == 3) PostQuitMessage(0);
        }
        return 0;

    case WM_RESCAN:
        RescanMonitors();
        return 0;

    case WM_DISPLAYCHANGE:
        PostMessageW(hWnd, WM_RESCAN, 0, 0);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_POLL);
        KillTimer(hWnd, TIMER_HOTPLUG);
        KillTimer(hWnd, TIMER_HOLDREVEAL);
        PostQuitMessage(0);
        return 0;

    default:
        if (msg == g_taskbarCreatedMsg && g_taskbarCreatedMsg != 0) {
            CreateTrayIcon();
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

// Cleanup
static void Cleanup() {
    UnregisterHotkeys();
    RemoveTrayIcon();
    DestroyOverlays();
    if (g_hWndWidget) { DestroyWindow(g_hWndWidget); g_hWndWidget = NULL; }
    if (g_hWndMain) { DestroyWindow(g_hWndMain); g_hWndMain = NULL; }
    DestroyFonts();
    if (g_hMutex) { CloseHandle(g_hMutex); g_hMutex = NULL; }
}

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    FreeConsole();

    // DPI awareness
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pfn = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn) {
            pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    // Single instance mutex
    g_hMutex = CreateMutexW(NULL, TRUE, L"MonitorBlackoutManager_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_hMutex);
        return 0;
    }

    g_hInstance = hInstance;

    // Register TaskbarCreated message
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Init common controls
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Create fonts
    CreateFonts();

    // Initialize cursor tracking
    GetCursorPos(&g_lastCursorPos);
    g_lastCursorMoveTime = GetTickCount64();

    // Register main window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MonitorBlackoutMain";
    RegisterClassExW(&wc);

    // Create main (hidden) message window
    g_hWndMain = CreateWindowExW(0, L"MonitorBlackoutMain", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    // Enumerate and create
    EnumerateMonitors();
    CreateOverlays();
    GetDisplayConfigTargetIds(g_lastTargetIds);

    // Create widget (hidden by default)
    CreateWidgetWindow();

    // Tray icon
    CreateTrayIcon();

    // Register hotkeys
    RegisterHotkeys();

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Cleanup();
    return 0;
}