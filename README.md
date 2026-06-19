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
- Cursor-aware: overlay clears automatically when the mouse enters a blacked monitor
- Foreground-aware: overlay clears when a window is focused on that monitor
- Dark-themed floating widget with per-monitor toggle and mode buttons
- System tray integration with right-click menu
- Global hotkeys
- DPI-aware (scales on 125%, 150%, 200% etc.)
- Adaptive polling (250ms active, 1000ms idle)
- Monitor hot-plug detection via DISPLAYCONFIG
- Single-instance enforcement via named mutex
- All monitors supported including primary (primary defaults to Manual + Cleared)

## Hotkeys

| Hotkey | Action |
|---|---|
| Ctrl+0 | Toggle all monitors (homogenize state) |
| Ctrl+1..9 | Toggle display N |
| Ctrl+Alt+B | Cycle all monitors mode (Manual, Auto, Forced) |
| Ctrl+Alt+1..9 | Cycle mode for display N |

## Modes

| Mode | Behavior |
|---|---|
| Manual | Clears on activity, never re-blacks automatically. User toggles manually. |
| Auto | Clears on activity, re-blacks after 5 minutes of no activity on that monitor. |
| Forced | Clears on activity, re-blacks after 1 second. Hold-to-reveal supported. |

## Architecture

Monitor Blackout is implemented as a compact, single-file Win32 C++ application (approximately 1200 lines) built around a simple event-driven state machine per monitor.

- **Single-file Win32 C++ application (~1200 lines)**
- **Entry point:** `wWinMain` creates a message-only window for hotkeys, timers, and tray icon messages
- **Overlays:** per-monitor `WS_POPUP | WS_EX_LAYERED | WS_EX_TOPMOST` fullscreen black windows
- **Polling:** `WM_TIMER` drives `ApplyModeLogic()` which checks cursor position and foreground window
- **Widget:** owner-drawn `WS_POPUP` with dark theme, `BS_OWNERDRAW` buttons, custom close button, draggable header
- **Hotplug:** periodic `DISPLAYCONFIG` enumeration detects monitor connect/disconnect
- **State machine:** each monitor has an `OverlayState` enum (`VisibleBlack`, `TemporarilyRevealed`, `Cleared`) for clean transitions
- **DPI:** `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` with scaled widget dimensions
- **Tray:** `NOTIFYICONDATA` with custom icon loaded from embedded resource
- **Cleanup:** all GDI objects, hooks, hotkeys, timers properly freed on exit

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

- Download `monitor_blackout.exe` from the root of this repository
- Double-click to run. No installation, no dependencies, no admin rights required.
- The application appears as a system tray icon. Right-click for options, double-click to show/hide widget.

## Auto-Start on Login

To have Monitor Blackout start automatically every time Windows boots:

1. Press Win+R to open the Run dialog
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
