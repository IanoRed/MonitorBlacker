# Monitor Blackout

![Platform](https://img.shields.io/badge/platform-Windows-0078D6?logo=windows&logoColor=white)
![Language](https://img.shields.io/badge/language-C%2B%2B-00599C?logo=c%2B%2B&logoColor=white)
![License](https://img.shields.io/badge/license-GPLv3-blue)

Lightweight native Win32 utility that blacks out monitors with fullscreen overlays to preserve OLED panels and protect privacy when displays are not in use.

## Why

- OLED displays can suffer burn-in and long-term pixel degradation when static content is displayed for extended periods.
- Secondary monitors often show sensitive information that may be visible to passersby.
- Monitor Blackout blacks out monitors instantly, preserving panel life and ensuring privacy when screens are not actively used.
- A near-zero CPU and memory footprint makes it suitable for always-on use.

## Features

- Three modes: Manual (toggle only), Auto (re-blacks after 5 minutes of inactivity), Forced (re-blacks after 1 second)
- Per-monitor detection checkboxes: independently enable or disable cursor detection and foreground window detection for each display
- Cursor-aware: overlay clears automatically when the mouse enters a blacked monitor (when enabled)
- Foreground-aware: overlay clears when a window is focused on that monitor (when enabled)
- Hold-to-reveal: in Forced mode, hold Ctrl+N to temporarily peek at display N; release to re-black
- Dark-themed floating widget with per-monitor toggle, mode buttons, and detection checkboxes
- Rescan button to force monitor re-enumeration
- System tray integration with right-click menu
- Global hotkeys
- DPI-aware (scales on 125%, 150%, 200% etc.)
- Adaptive polling (250ms active, 1000ms idle)
- Monitor hot-plug detection via DISPLAYCONFIG target IDs
- Single-instance enforcement via named mutex
- Graceful shutdown handling (never blocks Windows restart/logoff)
- All monitors supported including primary (primary defaults to Manual + Cleared)

## Hotkeys

| Hotkey | Action |
|---|---|
| Ctrl+0 | Toggle all monitors (homogenize state) |
| Ctrl+1..9 | Toggle display N (quick tap toggles, long hold peeks in Forced mode) |
| Ctrl+Alt+B | Cycle all monitors mode (Manual, Auto, Forced) |
| Ctrl+Alt+1..9 | Cycle mode for display N |

## Modes

| Mode | Behavior |
|---|---|
| Manual | Clears on activity, never re-blacks automatically. User toggles manually. |
| Auto | Clears on activity, re-blacks after 5 minutes of no activity on that monitor. |
| Forced | Clears on activity, re-blacks after 1 second. Hold-to-reveal supported. |

## Widget

The floating widget provides per-monitor controls:

- **Toggle button** (Clear/Black): immediately toggles the overlay state
- **Mode button** (Manual/Auto/Forced): cycles through modes
- **Win checkbox**: when checked, foreground windows on that monitor trigger de-blacking
- **Mouse checkbox**: when checked, cursor entering that monitor triggers de-blacking
- **Rescan Monitors button**: forces re-enumeration of connected displays

The widget starts hidden. Double-click the tray icon or right-click and select "Show Widget" to open it.

## Architecture

Monitor Blackout is implemented as a compact, single-file Win32 C++ application built around a simple event-driven state machine per monitor.

- **Entry point:** `wWinMain` creates a message-only window for hotkeys, timers, and tray icon messages
- **Overlays:** per-monitor `WS_POPUP | WS_EX_LAYERED | WS_EX_TOPMOST` fullscreen black windows
- **Polling:** `WM_TIMER` drives `ApplyModeLogic()` which checks cursor position and foreground window against per-monitor detection flags
- **Widget:** owner-drawn `WS_POPUP` with dark theme, custom-painted buttons and checkboxes, draggable header
- **Hotplug:** periodic `DISPLAYCONFIG` enumeration compares target ID sets to detect monitor connect/disconnect
- **State machine:** each monitor has an `OverlayState` enum (`VisibleBlack`, `TemporarilyRevealed`, `Cleared`) for clean transitions
- **Hold-to-reveal:** `GetAsyncKeyState` polling (80ms interval) detects key hold vs. quick tap for non-flickering peek behavior
- **DPI:** `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` with scaled widget dimensions
- **Tray:** `NOTIFYICONDATA` with custom icon loaded from embedded resource; handles `TaskbarCreated` message for Explorer crash recovery
- **Shutdown:** handles `WM_QUERYENDSESSION` and `WM_ENDSESSION` to never block Windows shutdown
- **Cleanup:** all GDI objects, hotkeys, timers, and mutex properly freed on exit

## Folder Structure

```text
monitor-blackout/
├── monitor_blackout.exe    <- Pre-built binary (Windows x64)
├── README.md
├── LICENSE
└── src/
    ├── monitor_blackout.cpp
    ├── resource.h
    ├── resource.rc
    └── app.ico
```

## Download and Run

- Download `monitor_blackout.exe` from the root of this repository.
- Double-click to run. No installation, no dependencies, no admin rights required.
- The application appears as a system tray icon. Right-click for options, double-click to show/hide widget.

## Auto-Start on Login

To have Monitor Blackout start automatically every time Windows boots:

1. Press `Win+R` to open the Run dialog
2. Type `shell:startup` and press Enter
3. Create a shortcut to `monitor_blackout.exe` in the folder that opens

The application will now launch silently at every login.

## Build from Source

### Prerequisites

- MSYS2 with UCRT64 toolchain (MinGW-w64 GCC), or
- Visual Studio Build Tools with "Desktop development with C++" workload

### MinGW (MSYS2 UCRT64)

```bat
cd src
windres resource.rc -o resource.o
g++ -std=c++17 -municode -O2 -static -mwindows monitor_blackout.cpp resource.o -o ../monitor_blackout.exe -luser32 -lgdi32 -lshell32 -lole32 -ladvapi32 -lcomctl32
```

### MSVC (Developer Command Prompt)

```bat
cd src
rc /nologo resource.rc
cl /nologo /std:c++17 /W4 /O2 /DUNICODE /D_UNICODE /EHsc monitor_blackout.cpp resource.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib /OUT:../monitor_blackout.exe
```

## Requirements

- Windows 10 or 11 (x64)
- No runtime dependencies (statically linked)
- No .NET, no Python, no external DLLs

## License

This project is licensed under the GNU General Public License v3.0. See LICENSE for details.
