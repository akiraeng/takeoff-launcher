# Takeoff

[![CI](https://github.com/akiraredddd/Takeoff/actions/workflows/ci.yml/badge.svg)](https://github.com/akiraredddd/Takeoff/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/akiraredddd/Takeoff?color=blue)](https://github.com/akiraredddd/Takeoff/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6.svg?logo=windows)](README.md)

A focused, lightning-fast native Windows app launcher. Press **Alt+Space**, type an app name, then press **Enter**.

Takeoff is written in pure C++ with native Win32, Direct2D, and DirectWrite rather than a heavy browser runtime or Electron wrapper. It keeps an in-memory index of Start Menu applications and system administration tools, loads icons on demand, and searches with an instant weighted fuzzy matcher.

**Privacy First**: Zero telemetry, zero analytics tracking, and no background network services. An optional update checker queries public GitHub Releases once every 24 hours (enabled by default, can be toggled in Settings).

## Features

- **Blazing Fast**: Native Win32 / Direct2D interface launches and searches in under a millisecond with minimal memory usage.
- **Native Acrylic Backdrop**: True DWM transient acrylic backdrop on Windows 11 and compositor acrylic accent on Windows 10 (with graceful solid fallback for high contrast or remote desktop).
- **Intelligent Fuzzy Matcher**: Supports exact matches, acronyms (`vsc` for Visual Studio Code, `tm` for Task Manager, `wu` for Windows Update), multi-token prefixes (`win upd`), and built-in system aliases.
- **System Tools Indexing**: Quickly launch Control Panel tools, Device Manager, Services, Task Manager, and Windows Settings alongside user applications.
- **Session Recency Ranking**: Recent apps naturally rank first without persisting personal history to disk.
- **Keyboard-Driven Actions**: Press `Ctrl+K` or right-click to run as administrator, copy the application name, or copy the target executable path.
- **Quick-Launch Slots**: Jump immediately to any of the first 8 visible results using `Alt+1` through `Alt+8`.
- **Configurable Shortcuts**: Customize global launcher, actions, administrator, and quick-launch hotkeys from the in-app Settings page.
- **System Tray & Windows Startup**: Optional notification area icon and seamless Windows login auto-start (`--startup` flag).

## Download

Pre-built x64 binaries are available under [Releases](https://github.com/akiraredddd/Takeoff/releases):

1. Download `Takeoff-v1.0.0-windows-x64.zip` from the latest release.
2. Extract the archive to a folder of your choice.
3. Run `Takeoff.exe`. Press `Alt+Space` to summon the launcher anytime!

## Requirements

- Windows 10 version 1809 or newer, or Windows 11 (x64)
- For building from source: Visual Studio 2022 (with **Desktop development with C++**) or CMake 3.21+ with Windows SDK

## Build

1. Open `Takeoff.sln` in Visual Studio.
2. Select `Release` and `x64`.
3. Choose **Build > Build Solution**.
4. Run `x64\Release\Takeoff.exe`.

Alternatively, with CMake installed:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Controls

| Key | Action |
| --- | --- |
| `Alt+Space` | Open or close |
| Type | Search installed Start Menu apps |
| `Up` / `Down` | Select |
| `Tab` / `Shift+Tab` | Next / previous result |
| `Page Up` / `Page Down`, mouse wheel | Move through results |
| `Enter` | Open normally |
| `Ctrl+Enter` | Open as administrator |
| `Alt+1` ... `Alt+8` | Launch the corresponding visible result normally |
| `Ctrl+K` | Open / close actions (open as administrator, copy name, copy launch path) |
| Right-click a result | Open actions at the pointer (open as administrator, copy, or copy path) |
| `Escape` | Close actions, clear the search, then dismiss |
| `Left` / `Right`, `Home` / `End` | Move the text caret |
| `Shift` + text navigation | Select text |
| `Ctrl+Left` / `Ctrl+Right` | Move by word |
| `Ctrl+Backspace` / `Ctrl+Delete` | Delete a word |
| `Ctrl+A` / `Ctrl+L` | Select the query |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut / paste text |

The shortcuts above are the defaults. The launcher shortcut, actions shortcut,
administrator shortcut, and quick-launch shortcut can be changed from
**Settings**. On the settings page, use `Up` / `Down` to select a setting and
`Left` / `Right`, `Enter`, or `Space` to change it. Press `Escape` or
`Alt+Left` to return to search.

## Settings

Click the gear icon in the search header to configure Takeoff:

- Choose the global shortcut that opens the launcher.
- Choose the actions-menu, administrator, and quick-launch shortcuts.
- Start Takeoff automatically when you sign in to Windows.
- Show a Takeoff icon in the notification area's hidden-icons section.

Settings are stored for the current Windows user and apply immediately. The
notification-area icon is disabled by default. When enabled, click it to open
Takeoff, or right-click it to open Takeoff, open Settings, or exit.

Click a result to open it as administrator. Press Ctrl+Enter to open the
selected application as administrator from the keyboard. Click or drag in the search field to position the
caret or select text. The clear button resets the query. With no query, the
launcher lists all indexed applications, with recently launched apps first.
The footer's primary action and the first actions-menu item open the selected
application as administrator. Alt+1 through Alt+8 launch normally.
Recent apps are kept only for the current session, not written to disk.
The panel keeps a stable height while searching and shows loading and
no-results states instead of collapsing.

## Tests

The CMake build includes dependency-free search and text-editing regression tests:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

On an interactive Windows desktop, the optional UI smoke test checks the actual
acrylic backdrop, caret blinking, editing, results, and actions. It requires
Python with Pillow and tkinter:

```powershell
py tests/ui_smoke.py build/Release/TakeoffUiTests.exe
```

It uses fixture applications, a separate window/mutex, and a generated backdrop.
It does not launch apps, touch the clipboard, or replace a running launcher.
Screenshots are saved under `build/ui-smoke`.

Takeoff remains running when dismissed. It starts with Windows only when
**Run at startup** is enabled in Settings.

## Contributing

Contributions, bug reports, and feature proposals are warmly welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, testing details, and coding standards, and review our [Code of Conduct](CODE_OF_CONDUCT.md).

## Security

To report security issues responsibly, please consult our [Security Policy](SECURITY.md).

## License

Takeoff is free and open source software licensed under the [MIT License](LICENSE).
Copyright (c) 2026 Akira.

