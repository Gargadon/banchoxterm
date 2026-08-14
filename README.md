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
- **Dark & Light themes** — Tokyo Night (dark) and Classic (light) styles
- **Internationalization** — English, Spanish, and Portuguese

## Install

Pre-built binaries are published on the [Releases](https://github.com/Gargadon/banchoxterm/releases) page:

- **Windows**: NSIS installer (`BanchoXterm-Setup.exe`) or a portable ZIP.
- **Linux**: a standalone binary (requires Qt 6 and QTermWidget installed).

## Build from source

The repository uses Git submodules for its vendored QTermWidget port, so a
fresh clone needs an extra step before configuring:

```bash
git clone git@github.com:Gargadon/banchoxterm.git
cd banchoxterm
git submodule update --init --recursive
```

### Requirements

- CMake 3.16+
- C++17 compiler (GCC, Clang, or MSVC)
- Qt 6.6+ (Core, Widgets, Gui, Network, Test, LinguistTools)
- [libssh2](https://www.libssh2.org) — fetched automatically by CMake (FetchContent)

> QTermWidget is handled per-platform: on **Linux** it is fetched from upstream
> via FetchContent; on **Windows** it is compiled from the vendored
> `third_party/qtermwidget` submodule.

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake qt6-base-dev libssh2-1-dev ninja-build

# Install dependencies (Fedora)
sudo dnf install cmake qt6-qtbase-devel libssh2-devel ninja-build

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

The build produces `banchoxterm.exe` plus `banchoxterm-term.exe` (the GPL
terminal host that embeds QTermWidget, launched by the main app over IPC).

## Limitations

- **FTP** is passive-mode only (no TLS/FTPS, no chmod) and has no folder upload,
  no byte-level progress, and no drag & drop (use the SFTP sidebar for those).
- **SFTP drag & drop** only works *within* the app: local files dropped on the
  sidebar are uploaded, and remote files can be dragged out of the sidebar to a
  chosen local folder. Dragging remote files to another application (e.g.
  Explorer) is not supported.
- **X11 forwarding** on Windows requires a local X server (e.g. VcXsrv, X410 or
  Xming) listening on `127.0.0.1:6000`.
- **RDP** on Windows is embedded via the native Remote Desktop ActiveX control
  when Qt ActiveQt is available (falls back to `mstsc.exe` otherwise). On Linux
  it uses `xfreerdp`.
- **VNC** supports a limited set of encodings (Raw, Hextile, CopyRect; Tight and
  ZRLE are disabled because the embedded libvncclient builds without zlib) and
  does not support complex key modifiers (e.g. Ctrl+letter).
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

Released under the [MIT License](LICENSE). See [third-party-licenses.txt](third-party-licenses.txt) for the licenses of bundled/third-party components.
