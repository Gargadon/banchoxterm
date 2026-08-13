# BanchoXterm — Roadmap

Objetivo: convertir BanchoXterm en una alternativa real a MobaXterm.

## Estado actual

Ya implementado:

- **SSH** con libssh2 (password, clave privada, agente, X11 forwarding en Linux)
- **Verificación de host key** (`known_hosts`, formato OpenSSH, diálogo de confianza)
- **SFTP** (navegar, subir, bajar, borrar, renombrar, chmod, crear carpeta, subir carpetas, editar con auto-upload)
- **Túneles SSH** (local, remoto, dinámico/SOCKS5)
- **Telnet**, **Serial** (solo Linux), **terminal local** (ConPTY en Windows / QTermWidget en Linux)
- **RDP embebido** en Windows vía ActiveQt (`QAxWidget` + `mstscax.dll`, con fallback a `mstsc.exe`); en Linux vía `xfreerdp`
- **VNC embebido** vía `libvncclient` (renderizador propio + entrada de teclado/ratón)
- **Gestor de sesiones** (JSON, grupos/carpetas, importar/exportar), pestañas, multi-input
- **Monitor remoto** (CPU/RAM/disco/uptime; Linux `/proc` + fallback PowerShell en Windows)
- **Macros de teclado**, **búsqueda global** (Windows), **logging de sesiones**, **auto-reconexión**
- Temas claro/oscuro, i18n (en/es/pt), master password + keyring (Windows Credential Manager / secret-tool)
- Licencia MIT, CI (MSVC) + instalador NSIS + ZIP portable

## Prioridades

### P1 — Diferenciadores clave frente a MobaXterm

- [x] **Split-view / multi-terminal en rejilla**: dos paneles de pestañas
      (`QSplitter` con dos `QTabWidget`), menú "View → Toggle Split View" y
      "Move Tab to Other Pane".
- [ ] **SSH gateway / jump host (ProxyJump)**: campo `jumpHost` en `Session`,
      conexión en dos saltos en `SshConnection` (direct-tcpip sobre el bastión).
- [x] **Keyboard-interactive / 2FA**: `libssh2_userauth_keyboard_interactive`
      con diálogo de prompts (OTP/MFA) en `SshConnection`.
- [x] **X server en Windows**: X11 forwarding habilitado en Windows usando un
      servidor X local en `127.0.0.1:6000` (VcXsrv/X410/Xming); se sondea el
      servidor antes de activarlo y se usa auth vacía.
- [x] **FTP**: cliente FTP pasivo básico (`FtpClient` con QTcpSocket):
      navegar, subir, bajar, borrar, crear carpeta y renombrar. Sin TLS/chmod.

### P2 — Calidad y robustez

- [ ] **Unificar el emulador de terminal**: Windows usa `VtTerminalWidget`
      (básico) y Linux usa `QTermWidget`. Inconsistencias: búsqueda global solo
      Windows, log no captura la sesión local Linux, y `VtTerminalWidget` carece
      de OSC 52 (clipboard), OSC 8 (links), sixel, etc.
- [ ] **Eliminar la dependencia de QTermWidget (GPL)** moviendo Linux al mismo
      emulador propio; simplifica la distribución (ver nota de licencia).
- [ ] **Rendimiento de red**: `SshConnection` usa polling (`QTimer` 15 ms +
      `select()`); migrar a socket notifier event-driven.
- [ ] **Rendimiento de pintado**: `VtTerminalWidget` repinta toda la pantalla en
      cada `update()`; usar dirty rects.
- [ ] **Tests**: solo `test_session.cpp` (12 tests; `testMasterPasswordEncryption`
      falla por `QSettings` sin org name). Falta: parser VT, SFTP, túnel,
      host-key, import/export, VNC.
- [ ] **UI de `known_hosts`**: listar/eliminar hosts guardados.
- [ ] **UX de sesiones**: drag & drop para reordenar, clonar/duplicar sesión,
      barra "QuickConnect".

### P3 — Pulido y distribución

- [ ] Configuración ampliada: scrollback configurable (hoy fijo en 5000),
      keep-alive, ciphers/algoritmos, fuente por sesión.
- [ ] SFTP: barra de progreso, cola de descargas, drag & drop local↔remoto.
- [ ] AppImage (Linux) y macOS.
- [ ] Auto-updater y edición portable.
- [ ] Icono `.ico` para el instalador NSIS.
- [ ] Documentar las limitaciones vigentes (VNC sin mods complejos, monitor
      Windows no probado contra un servidor real, etc.).

## Notas / limitaciones conocidas

- La monitorización Windows está implementada pero no probada contra un
  servidor SSH Windows real.
- La sesión local de Linux (QTermWidget) no se captura en el log de sesión.
- VNC embebido: sin soporte de mods complejos (Ctrl+tecla) y encodings limitados
  (Raw/Hextile/CopyRect; Tight/ZRLE deshabilitados al no enlazar zlib).
- Aunque el código propio es MIT, el binario enlaza QTermWidget (GPL-2.0), por
  lo que la distribución combinada queda sujeta a GPL. Ver
  `third-party-licenses.txt`.

## Convención

- Marcar con `[x]` los elementos completados.
- Los elementos P1 son los diferenciadores que faltan para competir de tú a tú
  con MobaXterm; P2/P3 son robustez y pulido.
