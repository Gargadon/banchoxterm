# BanchoXterm

A multi-protocol terminal emulator and remote session manager for Linux and Windows.

[![CI](https://github.com/Gargadon/banchoxterm/actions/workflows/ci.yml/badge.svg)](https://github.com/Gargadon/banchoxterm/actions/workflows/ci.yml)

## Features

- **Multi-tab terminal** — run multiple sessions side by side
- **SSH client** — connect with key-based auth, password auth, SSH agent, and X11 forwarding
- **SFTP sidebar** — browse, upload, download, delete, and edit remote files directly
- **Telnet client** — connect to telnet hosts
- **Serial console** — supports picocom, screen, and minicom
- **Local terminal** — open local shell sessions
- **Multi-input** — send commands to all open terminals simultaneously
- **Remote monitoring** — CPU, RAM, disk, and uptime stats in the status bar
- **Session manager** — save, organize, and quickly reconnect to your sessions
- **SSH tunnels** — local, remote, and dynamic (SOCKS5) port forwarding
- **Session logging** — record terminal output to a file
- **Dark & Light themes** — the default interface adapts to the system or can be forced to light/dark mode
- **Internationalization** — English, Spanish, and Portuguese

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

- CMake 3.16+
- C++17 compiler (GCC, Clang, or MSVC)
- Qt 6.6+ (Core, Widgets, Gui, Network, Test, LinguistTools)
- [libssh2](https://www.libssh2.org) — fetched automatically by CMake (FetchContent)

> QTermWidget is compiled directly from the vendored `third_party/qtermwidget`
> submodule on every platform (same fork, same emulator code on Linux and
> Windows), instead of relying on a distro/system package.

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake ninja-build \
  qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-tools-dev-tools \
  libssl-dev zlib1g-dev

# Install dependencies (Fedora)
sudo dnf install cmake ninja-build \
  qt6-qtbase-devel qt6-qtserialport-devel qt6-qttools-devel \
  openssl-devel zlib-devel

# Build
cmake -B build -G Ninja
cmake --build build

# Run
./build/banchoxterm
```

### Windows (MSVC)

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

- **FTP** is passive-mode only (no TLS/FTPS, no chmod) and has no folder upload,
  no byte-level progress, and no drag & drop (use the SFTP sidebar for those).
- **SFTP drag & drop** only works *within* the app: local files dropped on the
  sidebar are uploaded, and remote files can be dragged out of the sidebar to a
  chosen local folder. Dragging remote files to another application (e.g.
  Explorer) is not supported.
- **X11 forwarding** on Windows can use an external X server. BanchoXterm
  auto-starts VcXsrv when `vcxsrv.exe` is installed or placed in
  `xservers/vcxsrv-x64/` (or `xservers/vcxsrv-arm64/`) next to the application.
  A custom executable can be selected with the `x11/vcxsrvPath` setting or the
  `BANCHOTERM_VCXSRV` environment variable. X410 and Xming remain compatible
  when already running on `127.0.0.1:6000`.
- **RDP** on Windows is embedded via the native Remote Desktop ActiveX control
  when Qt ActiveQt is available (falls back to `mstsc.exe` otherwise). On Linux
  it uses `xfreerdp`.
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
