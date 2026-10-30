# 🔧 DLL Injector

A fast, lightweight DLL injector for Windows with a modern native GUI. Built in
C++ with the Win32 API — **no runtime, no dependencies, a single portable
`.exe`** that launches instantly.

> ⚠️ For **educational** and **legitimate** use only (debugging, modding,
> instrumentation of software you own or are authorized to modify). You are
> responsible for complying with applicable terms of service and laws.

---

## ✨ Features

- **Live process browser** — every running process with its PID and
  architecture, filterable by name or PID as you type.
- **Architecture safety** — reads the DLL's PE header and the target's
  architecture and blocks mismatched `x86`/`x64` injections before they fail
  silently (the single most common cause of "nothing happened").
- **Classic injection** — `VirtualAllocEx` → `WriteProcessMemory` →
  `CreateRemoteThread(LoadLibraryW)`, with Unicode paths, least-privilege
  handles and `SeDebugPrivilege`.
- **Responsive UI** — injection runs off the UI thread; clear success/error
  feedback for every action with precise Win32 error messages.
- **Modern dark interface** — DPI-aware, dark title bar, one accent colour,
  designed button states.
- Builds for both **x64** and **x86**.

---

## 🚀 Getting Started

### Requirements

- Windows 10 or later (64-bit or 32-bit)
- Administrator privileges (to open other processes)

### How to use

1. Launch `DLLInjector.exe` (it requests elevation automatically).
2. Pick the target in the process list — type to filter.
3. **Browse…** to a DLL, or paste its full path. The hint line confirms the DLL
   matches the target's architecture.
4. Click **Inject DLL** (or double-click the process). The status line reports
   the result.

> Inject an **x86** DLL into an **x86** process, and an **x64** DLL into an
> **x64** process. The injector warns you on a mismatch.

---

## 🛠️ Build from Source

**Requirements:** Visual Studio 2022 with the *Desktop development with C++*
workload (MSVC v143, Windows 10/11 SDK).

```bash
git clone https://github.com/masterkpg/DLLInjector.git
```

- **Visual Studio:** open `DLLinjector2.sln`, pick `Release` + `x64` (or `x86`),
  and build (`Ctrl+Shift+B`).
- **Command line:**
  ```bash
  msbuild DLLinjector2.sln /p:Configuration=Release /p:Platform=x64
  ```

Output: `x64/Release/DLLInjector.exe` (or `Release/DLLInjector.exe` for x86).

### Project layout

| Path               | Purpose                                             |
| ------------------ | --------------------------------------------------- |
| `src/Injector.h`   | Injection core API                                  |
| `src/Injector.cpp` | Process enumeration, PE/arch detection, injection   |
| `src/Main.cpp`     | Win32 GUI (dark theme, owner-drawn controls)        |
| `src/app.manifest` | Elevation, per-monitor DPI, UTF-8                   |

---

## 📜 License

Licensed under the [MIT License](LICENSE).

---

## 🙌 Credits

Developed by **MasterKPG**. Contributions and forks welcome.

---

## ⚠️ Disclaimer

Provided **as-is**, without warranty. The author is **not responsible** for any
damage or misuse. Always ensure you have proper authorization before injecting
into any process.
