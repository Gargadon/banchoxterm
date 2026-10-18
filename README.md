# BanchoXterm

A multi-protocol terminal emulator and remote session manager for Linux and Windows.

[![CI](https://github.com/Gargadon/banchoxterm/actions/workflows/ci.yml/badge.svg)](https://github.com/Gargadon/banchoxterm/actions/workflows/ci.yml)

## Features

- **Multi-tab terminal** — run multiple sessions side by side
- **SSH client** — connect with key-based auth, password auth, SSH agent, and X11 forwarding
- **SFTP sidebar** — browse, upload, download, delete, and edit remote files directly
  with a flat local file list, folder navigation, creation and renaming, and recursive
  downloads directly to the open local folder without a destination prompt
- **FTP/FTPS** — browse, transfer files and folders, drag & drop, pause/cancel,
  automatic reconnect, and configurable TLS validation
- **Telnet client** — connect to telnet hosts
- **Serial console** — supports picocom, screen, and minicom, with XMODEM/YMODEM
  and optional ZMODEM sending through `sz` from lrzsz
- **Local terminal** — open local shell sessions
- **Multi-input** — send commands to selected terminals, with a separate sensitive mode for temporary entries
- **Remote monitoring** — CPU, RAM, disk, and uptime stats in the status bar
- **Session manager** — save, organize, and quickly reconnect to your sessions
- **SSH tunnels** — local, remote, and dynamic (SOCKS5) port forwarding
- **Session logging** — record terminal output to a file
- **Dark & Light themes** — the default interface adapts to the system or can be forced to light/dark mode
- **Internationalization** — English, Spanish, and Portuguese

## SSH resource monitoring

Configure monitoring in **Configuration > Appearance > SSH resource monitoring**.
It is enabled by default and updates every 3 seconds. Choose an interval from
1 to 3600 seconds, or disable it entirely. Changes apply to open SSH sessions.

Monitoring reuses one persistent SSH command channel, requesting each sample
through its standard input. Changing the interval does not open another channel.
Disabling monitoring closes the remote input loop and hides the metrics bar.
This reduces repeated SSH session log entries; server process auditing may still
record the commands used to collect metrics.

## Terminal keyboard shortcuts

Application defaults leave plain `Ctrl+letter` combinations available to terminal
programs, including nano and readline:

| Action | Default shortcut |
| --- | --- |
| Close session | `Ctrl+Shift+W` |
| Quit application | `Ctrl+Shift+Q` |
| Find in the current terminal | `Ctrl+Shift+F` |
| Find in all sessions | `Ctrl+Alt+Shift+F` |
| Copy / paste | `Ctrl+Shift+C` / `Ctrl+Shift+V` |
| Zoom in / out | `Ctrl+Shift++` / `Ctrl+Shift+-` |
| Command palette | `Ctrl+Shift+P` |
| Toggle split view | `Ctrl+Shift+O` |
| Toggle grid view | `Ctrl+Alt+Shift+G` |
| Move tab to other pane | `Ctrl+Alt+Shift+M` |
| Detach tab | `Ctrl+Alt+Shift+D` |
| Lock application | `Ctrl+Alt+Shift+L` |

Customize these in **Configuration > Shortcuts**. Clear a field to disable its
shortcut, or use **Restore default shortcuts**. Press **OK** to save and apply to
open windows and terminals; **Cancel** leaves the assignments unchanged. Duplicate
assignments are rejected. Shortcuts are stored by action, independently of the UI
language. Plain `Ctrl+W`, `Ctrl+F`, `Ctrl+Q` and `Escape` are not application-wide
shortcuts. Escape still closes the search bar when its field has focus.

For additional conflicts, enable **Tools > Send shortcuts to terminal** (also in
the terminal’s right-click menu). While that viewport has focus, application
shortcuts are forwarded to the terminal. The mode belongs to each tab and starts
disabled in new sessions. A compact bar provides **Restore shortcuts**, so you can
turn it off with the mouse; the menu option also toggles it off. Other tabs,
search fields, dialogs and menus retain their normal behavior. Copy/paste remain
available through mouse menus. OS-reserved key combinations are outside this mode.

## Sensitive multi-input

Open **Tools > Sensitive Multi-Input** to add named entries such as a password or
proxy command. Values are hidden in the editor and exist only in memory until
BanchoXterm closes. Select an entry, click **Send**, and choose the active writable
terminal sessions that should receive it. The value is sent unchanged, followed
by Enter. Closing the dialog keeps the entries available during the current run;
**Delete** removes an entry.

Sensitive sends bypass both the multi-input dropdown history and the application’s
global/session command histories. Exact echoed values are replaced by `[redacted]`
in application-owned session logs and saved output, including values split across
network packets. The live terminal can still display what the server echoes.
Server-side logs and shell history are controlled by the server; transformed or
reformatted echoes may not match the filter. This feature does not erase entries
previously sent through normal input. Normal multi-input retains its usual history.

## Macros

Create macros in **Macros > Manage Macros**. Line breaks in the editor only
format the macro; use `{{ENTER}}` explicitly to execute each command.
`{{PAUSE: 500}}` waits 500 milliseconds before continuing.

```text
echo "First command"{{ENTER}}
{{PAUSE: 500}}
echo "Second command"{{ENTER}}
```

The **Quick reference** button in the macro manager lists all supported tokens:

| Token | Effect |
| --- | --- |
| `{{ENTER}}` | Execute the command. |
| `{{TAB}}`, `{{ESC}}`, `{{BACKSPACE}}` | Send the corresponding key. |
| `{{CTRL+C}}`, `{{CTRL+D}}`, `{{CTRL+Z}}` | Send the corresponding control character. |
| `{{BREAK}}`, `{{TELNET-ESC}}` | Send Ctrl+^ (0x1E) or Ctrl+] (0x1D). BREAK sends a character, not a protocol Break signal. |
| `{{PAUSE: 500}}` | Wait in milliseconds (0–999999). Spaces around the number are allowed. |
| `{{EXPECT:regex}}` | Wait up to 30 seconds for a regex in new terminal output. |
| `{{RESPOND:text}}` | Send text immediately, without adding Enter. |
| `{{HOST}}`, `{{USER}}`, `{{PORT}}`, `{{SESSION}}` | Insert session details. |
| `{{REMOTE_DIR}}`, `{{SERIAL_PORT}}` | Insert the configured remote directory or serial port. |
| `{{ENV:USER}}` | Insert a local environment variable; unset variables produce empty text. |

Tokens are case-sensitive. `EXPECT` and `RESPOND` cannot contain `}` or nested
tokens. After `EXPECT`, use plain text followed by `{{ENTER}}` to send a response
and execute it. Shell commands depend on the terminal's shell, rather than a
macro command whitelist.

## Terminal appearance

In **Configuration > Appearance**, set **Terminal background opacity** (0-100%)
and optionally enable background blur on Windows 11 (22H2 or later) or a
supported Linux compositor (such as KWin). Text and
the cursor remain opaque. Restart once to enable transparency; subsequent
opacity and blur changes apply to open terminal windows immediately. The native
window title bar is preserved. Linux transparency requires a compositing desktop.
Linux blur requires the optional KF6WindowSystem development package at build
time and compositor support at runtime. Settings are global and persist between
launches. A value of 100% keeps the default opaque appearance.

Internal startup/RAM/CPU measurement instructions are in [benchmarks/README.md](benchmarks/README.md).

## Install

Pre-built binaries are published on the [Releases](https://github.com/Gargadon/banchoxterm/releases) page:

- **Windows**: NSIS installer (`BanchoXterm-Setup.exe`) or a portable ZIP.

Linux packages are not currently published by the project; build from source
using the instructions below.

## Build from source

The repository uses Git submodules for its vendored QTermWidget port, so a
fresh clone needs an extra step before configuring:

```bash
git clone git@github.com:Gargadon/banchoxterm.git
cd banchoxterm
git submodule update --init --recursive
```

On Windows, VcXsrv is packaged as a separate companion process; it is not
linked into `banchoxterm.exe`. To include local VcXsrv builds in the Windows
package, configure with the package directories before building:

```powershell
cmake -B build -A x64 `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 `
  -DBANCHO_VCXSRV_DIR=C:\path\to\vcxsrv-package `
  -DBANCHO_VCXSRV_ARM64_DIR=C:\path\to\vcxsrv-package-arm64
cmake --build build --config Release --target banchoxterm
```

The configure step copies them to `build/xservers/vcxsrv-x64` and
`build/xservers/vcxsrv-arm64`. The Windows packaging workflow then places the
matching directory under `xservers/` in both the NSIS installer and portable
ZIP. If a package is not supplied, the application is still packaged and can
use an installed X server such as Xming or X410.

### Requirements

CI builds use Qt 6.8.3 on Linux and Windows (x64 and ARM64), pinned by
`BANCHO_QT_VERSION` in `.github/workflows/ci.yml`.

- CMake 3.16+
- C++17 compiler (GCC, Clang, or MSVC)
- Qt 6.6+ (Core, Widgets, Gui, Network, Test, LinguistTools)
- [libssh2](https://www.libssh2.org) — fetched automatically by CMake (FetchContent)
- Linux RDP: [FreeRDP](https://www.freerdp.com/) 3 and WinPR 3 are required
  for compilation and at runtime. Install `freerdp3-dev`, `libwinpr3-dev`,
  and `pkg-config` on Debian/Ubuntu; `freerdp-devel`, `winpr-devel`, and
  `pkgconf-pkg-config` on Fedora; or `freerdp` and `pkgconf` on Arch Linux.
  CMake requires the `freerdp3` and `winpr3` pkg-config modules; `xfreerdp`
  is not required.
- Linux password storage: `secret-tool` (`libsecret-tools` on Debian/Ubuntu,
  `libsecret` on Fedora). This is a runtime dependency.
- Optional Linux keyring providers (choose one if your desktop does not already
  provide a Secret Service): GNOME Keyring (`gnome-keyring`) or KDE KWallet
  with Secret Service support enabled. BanchoXterm also falls back to
  `kwallet-query` when the Secret Service command is unavailable.
- Optional ZMODEM sender: `lrzsz` (`sz`) for serial file transfers.

> QTermWidget is compiled directly from the vendored `third_party/qtermwidget`
> submodule on every platform (same fork, same emulator code on Linux and
> Windows), instead of relying on a distro/system package.

### Linux

Saving and retrieving passwords requires a running Secret Service provider in
the user's desktop session. Use the desktop's existing provider, or install
GNOME Keyring or a KWallet version with Secret Service support (KWallet package
names vary by distribution and KDE version). These are alternative providers;
installing both is not required. For GNOME Keyring, use
`sudo apt install gnome-keyring` on Debian/Ubuntu or
`sudo dnf install gnome-keyring` on Fedora, and ensure it is running in your
desktop session.

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake ninja-build \
  qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-tools-dev-tools \
  libssl-dev zlib1g-dev libsecret-tools lrzsz pkg-config freerdp3-dev libwinpr3-dev

# Install dependencies (Fedora)
sudo dnf install cmake ninja-build \
  qt6-qtbase-devel qt6-qtserialport-devel qt6-qttools-devel \
  openssl-devel zlib-devel libsecret lrzsz pkgconf-pkg-config freerdp-devel winpr-devel

# Build
cmake -B build -G Ninja
cmake --build build

# Run
./build/banchoxterm
```

### Windows (MSVC)

Windows builds bundle the official Microsoft ConPTY runtime and OpenConsole
host (downloaded and verified by CMake) so terminal color queries work even
when the Windows system console host lacks support. Keep `conpty.dll`,
`OpenConsole.exe`, and `conpty-LICENSE.txt` beside `banchoxterm.exe` when
distributing a Windows build.

Build with the MSVC toolchain (Visual Studio 2017+ or Build Tools) and a Qt 6
build configured for MSVC (e.g. `msvc2022_64` from the online installer). From
a **Developer Command Prompt**:

```bat
set PATH=C:\Qt\6.x.x\msvc2022_64\bin;%PATH%
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Or with Ninja + jom:

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Qt\6.x.x\msvc2022_64\bin;C:\Qt\Tools\CMake_64\bin;%PATH%
cmake -B build -G Ninja
cmake --build build
```

The build produces a single `banchoxterm` executable (QTermWidget is linked
directly in-process on every platform; on Windows the vendored fork runs
without KPty, so local shells are bridged through ConPTY).

## Limitations

- **FTP/FTPS** currently uses passive mode and does not provide chmod. FTPS
  supports TLS 1.2+ or TLS 1.3-only, a custom CA bundle, and an explicitly
  opt-in policy for invalid certificates.
- **SFTP drag & drop** only works *within* the app: local files dropped on the
  sidebar are uploaded, and remote files can be dragged to the open local
  folder or onto a local subfolder. Dragging remote files to another application (e.g.
  Explorer) is not supported.
- **X11 forwarding** on Windows can use an external X server. BanchoXterm
  auto-starts VcXsrv when `vcxsrv.exe` is installed or placed in
  `xservers/vcxsrv-x64/` (or `xservers/vcxsrv-arm64/`) next to the application.
  A custom executable can be selected with the `x11/vcxsrvPath` setting or the
  `BANCHOTERM_VCXSRV` environment variable. X410 and Xming remain compatible
  when already running on `127.0.0.1:6000`.
- **RDP** on Windows is embedded via the native Remote Desktop ActiveX control.
  Windows builds require Qt ActiveQt (`AxContainer`); if the Windows control
  cannot be created at runtime, the app falls back to a separate `mstsc.exe`
  window. Linux builds require the FreeRDP 3 and WinPR 3 development libraries
  (`freerdp3` and `winpr3` pkg-config modules). RDP runs inside the tab using
  `libfreerdp`, with Qt rendering and input on both X11 and Wayland; no external
  `xfreerdp` executable is needed. A graphical dialog collects the username,
  optional domain, and password. Passwords can be saved in the same credential
  store as SSH, from the session settings or connection dialog; saved credentials
  connect without prompting. Use `DOMAIN\username` in the profile for domain
  accounts. The desktop scales to fit the tab; clipboard,
  audio, drive redirection, and dynamic remote resolution are not yet supported.
  Certificate validation follows the previous client's ignore policy.
- **VNC** supports a limited set of encodings (Raw, Hextile, CopyRect; Tight and
  ZRLE are disabled because the embedded libvncclient builds without zlib).
  VNC is not currently available in the Windows ARM64 build because the bundled
  libvncclient dependency does not yet support MSVC ARM64, and it does not
  support complex key modifiers (e.g. Ctrl+letter).
- **Remote monitoring** is implemented for Windows hosts but has not been tested
  against a real Windows SSH server.
- **Auto-update** checks GitHub Releases and downloads the matching edition
  (installer or portable ZIP). The portable edition relies on `tar.exe`
  (bundled with Windows 10+), and the installer is not code-signed, so Windows
  may show a SmartScreen warning.
- **Portable edition** stores settings, sessions, and known hosts next to the
  executable. These files are not shared with an installed copy of the app.
- **Session logging** does not capture Linux local (QTermWidget) sessions.

## License

Released under the [GNU General Public License v2 or later](LICENSE) (GPL-2.0-or-later).
See [third-party-licenses.txt](third-party-licenses.txt) for the licenses of bundled/third-party components.
