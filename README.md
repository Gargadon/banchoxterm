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

### Requirements

- CMake 3.16+
- C++17 compiler (GCC, Clang, or MSVC)
- Qt 6.6+ (Core, Widgets, Gui, Network)
- [QTermWidget6](https://github.com/lxqt/qtermwidget) (Linux only)
- [libssh2](https://www.libssh2.org)

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential cmake qt6-base-dev libqtermwidget6-dev libssh2-1-dev ninja-build

# Install dependencies (Fedora)
sudo dnf install cmake qt6-qtbase-devel qtermwidget6-devel libssh2-devel ninja-build

# Build
cmake -B build -G Ninja
cmake --build build

# Run
./build/banchoxterm
```

### Windows (MSYS2)

```bash
# Install dependencies
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qtermwidget6 \
          mingw-w64-x86_64-libssh2

# Build
cmake -B build -G Ninja
cmake --build build
```

## Limitations

- **FTP** is passive-mode only (no TLS/FTPS, no chmod).
- **X11 forwarding** on Windows requires a local X server (e.g. VcXsrv, X410 or Xming) listening on `127.0.0.1:6000`.
- **RDP** on Windows is embedded via the native Remote Desktop ActiveX control
  when Qt ActiveQt is available (falls back to `mstsc.exe` otherwise). On Linux
  it uses `xfreerdp`. **VNC** uses the embedded `libvncclient`.

## License

Released under the [MIT License](LICENSE). See [third-party-licenses.txt](third-party-licenses.txt) for the licenses of bundled/third-party components.
