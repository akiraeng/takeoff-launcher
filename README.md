<h1 align="center">Takeoff</h1>

<p align="center">
  <strong>A focused, lightning-fast native Windows app launcher.</strong><br>
  Press <strong>Alt+Space</strong>, type what you need, and press <strong>Enter</strong>.
</p>

<p align="center">
  <img src="docs/screenshot.png" alt="Takeoff" width="750" />
</p>

<p align="center">
  <a href="https://github.com/akiraeng/takeoff-launcher/releases/latest">
    <img src="https://img.shields.io/badge/Download-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white" alt="Download for Windows" />
  </a>
</p>

<p align="center">
  <b>⚡ &lt; 500 KB binary</b> &nbsp;&bull;&nbsp;
  <b>🧠 ~10 MB RAM</b> &nbsp;&bull;&nbsp;
  <b>🚀 &lt; 1 ms search</b> &nbsp;&bull;&nbsp;
  <b>🔒 Zero telemetry</b>
</p>

---

## Why Takeoff?

* **Ultra-Lightweight** — Built in pure **C++**, **Win32**, and **Direct2D**. No Electron, no Chromium runtime. The entire executable is **under 500 KB** and uses just **~10 MB RAM**.
* **Instant Search** — In-memory indexing and weighted fuzzy matching return results in under a millisecond.
* **Smart Matching** — Easily finds apps by acronyms and prefixes:
  ```text
  vsc      → Visual Studio Code
  tm       → Task Manager
  wu       → Windows Update
  dev man  → Device Manager
  ```
* **System Tools Included** — Instantly launch Control Panel applets, Windows Settings, Device Manager, and Services alongside desktop apps.
* **Keyboard-First** — Launch, elevate to admin (`Ctrl+Enter`), and trigger quick-launch slots (`Alt+1`–`8`) without touching your mouse.
* **Privacy First** — Zero telemetry, zero analytics, and zero background services. Search history is never written to disk.

---

## Shortcuts

| Shortcut | Action |
| --- | --- |
| `Alt+Space` | Open or dismiss Takeoff |
| `Enter` | Launch selected application |
| `Ctrl+Enter` | Run as administrator |
| `Alt+1` ... `Alt+8` | Quick-launch visible result |
| `Ctrl+K` | Open actions menu (copy path, admin, etc.) |
| `Escape` | Clear query or close |

*Hotkeys can be customized anytime from the in-app Settings (gear icon).*

---

## Build from Source

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The compiled binary will be at `build/Release/Takeoff.exe`.

---

## License

Takeoff is open source under the [MIT License](LICENSE).
