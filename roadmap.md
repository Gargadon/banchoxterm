# BanchoXterm — Roadmap

Objetivo: convertir BanchoXterm en una alternativa real a MobaXterm.

## Estado actual

Ya implementado:

- **SSH** con libssh2 (password, clave privada, agente, X11 forwarding en Linux)
- **Verificación de host key** (`known_hosts`, formato OpenSSH, diálogo de confianza)
- **SFTP** (navegar, subir, bajar, borrar, renombrar, chmod, crear carpeta, subir carpetas, editar con auto-upload)
- **Túneles SSH** (local, remoto, dinámico/SOCKS5)
- **Telnet**, **Serial** (solo Linux), **terminal local** (ConPTY en Windows vía el proceso GPL separado `banchoxterm-term.exe`; QTermWidget directo en Linux)
- **RDP embebido** en Windows vía ActiveQt (`QAxWidget` + `mstscax.dll`, con fallback a `mstsc.exe`); en Linux vía `xfreerdp`
- **VNC embebido** vía `libvncclient` (renderizador propio + entrada de teclado/ratón)
- **Gestor de sesiones** (JSON, grupos/carpetas, importar/exportar), pestañas, multi-input
- **Monitor remoto** (CPU/RAM/disco/uptime; Linux `/proc` + fallback PowerShell en Windows)
- **Macros de teclado**, **búsqueda global** (Windows), **logging de sesiones**, **auto-reconexión**
- Temas claro/oscuro, i18n (en/es/pt), master password + keyring (Windows Credential Manager / secret-tool)
- App licencia MIT (libre de código GPL) + proceso terminal GPL `banchoxterm-term.exe`
  separado, CI (MSVC) + instalador NSIS + ZIP portable

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

- [x] **Aislamiento de QTermWidget (GPL) en proceso aparte**: el fork vendado en
      `third_party/qtermwidget` (parches MSVC: mmap→malloc en `BlockArray`/`History`,
      `Pty` stub multiplataforma, `wcwidth` de respaldo, `startExternal()`/`feedData()`)
      ya NO enlaza en `banchoxterm`. Se construye como `qtermwidget_win` solo para el
      proceso GPL `banchoxterm-term.exe`, que la app lanza y embebe por HWND
      (`SetParent`/`MoveWindow`/`ShowWindow`). La comunicación es por named-pipe IPC
      (`src/termipc.h`, protocolo propio MIT): Ready/HWND+cols/rows, Input,
      SizeChanged, Title, Cwd, CopyAvailable, ContextMenu, FeedData, SetFont/Color,
      Copy/Paste/Zoom, ToggleSearchBar, Close, etc. `banchoxterm.exe` queda libre de
      código GPL (verificado con dumpbin: solo Qt + sistema).
- [x] **Cablear ConPTY a QTermWidget en Windows (vía term host)**: `TerminalTab` usa
      `TerminalHostClient` (spawn, embed, routing IPC) con `applyTerminalSize`/
      `feedTerminalData` compartidos entre ConPTY y SSH. Color schemes en Windows:
      lista fija en `settingsdialog` (ya no depende del path de build). Nota: la
      búsqueda global sigue deshabilitada en Windows (QTermWidget no expone búsqueda
      programática; se usa su barra integrada). Pendiente: verificación visual
      interactiva del embedding (render, teclado, resize, menú contexto).
- [x] **Rendimiento de red**: `SshConnection` usa polling (`QTimer` 15 ms +
      `select()`); migrado a socket notifier event-driven (`QSocketNotifier`
      Read siempre armado + Write solo cuando `libssh2_session_block_directions`
      reporta `LIBSSH2_SESSION_BLOCK_OUTBOUND`; notificador propio por socket X11;
      se eliminó el `QTimer` de 15 ms. El paso de transporte lo dispara
      `libssh2_channel_read`, que lee el socket real).
- [x] **Rendimiento de pintado**: caché de highlighting en `VtTerminalWidget`
      (los regex se recalculan solo cuando cambia el contenido/scroll, no en
      cada parpadeo del cursor).
- [x] **Tests**: añadidos tests del parser VT (render, clear, cursor) y arreglado
      `testMasterPasswordEncryption` (faltaba el namespace de `QSettings`).
      16/16 en verde.
- [x] **UI de `known_hosts`**: diálogo "Manage Known Hosts" en Ajustes →
      Seguridad (listar y eliminar).
- [x] **UX de sesiones**: clonar/duplicar sesión (menú contextual) y reordenar
      con drag & drop (persistido). Falta barra "QuickConnect".

### P3 — Pulido y distribución

- [x] **Configuración ampliada**: scrollback configurable por sesión (antes fijo en
      5000), keep-alive SSH (intervalo en segundos vía `libssh2_keepalive_config`/
      `libssh2_keepalive_send` sobre el diseño event-driven), ciphers/KEX/MAC por
      sesión (`libssh2_session_method_pref`) y fuente por sesión (diálogo de sesión →
      "Advanced"; si no se elige, hereda la global).
- [x] **SFTP mejorado**: barra de progreso con bytes/total (`transferProgress` en
      `SshConnection`, emitida desde `downloadFile`/`uploadFile`/`uploadOneFile`),
      cola de transferencias serializada (multi-descarga y multi-subida con
      resolución de colisiones de nombre local; subida de carpetas también pasa
      por la cola; la UI de acciones se desactiva mientras hay transferencias
      activas) y drag & drop: soltar archivos/carpetas desde el explorador en el
      árbol sube al directorio actual, y arrastrar archivos remotos desde el
      árbol descarga a una carpeta local elegida al soltar.
- [ ] AppImage (Linux) y macOS.
- [x] **Auto-updater y edición portable**: chequeo de actualizaciones contra GitHub
      Releases (Help → "Check for Updates...", compara el tag con `BANCHO_VERSION`
      definido por CMake; instalado descarga y lanza el instalador NSIS, portable
      descarga el ZIP y lo reemplaza con un script PowerShell que espera a que la
      app cierre y relanza). Edición portable: `--portable` o marcador
      `portable.ini` junto al exe redirige QSettings a un INI en el directorio de
      la app y `sessions.json`/`known_hosts` a la misma carpeta
      (`src/apppaths.{h,cpp}`, `AppPaths::configDir()`). CI crea el marcador antes
      de empaquetar el ZIP portable y pasa la versión del tag a CMake/NSIS.
- [x] **Icono `.ico` para el instalador NSIS**: `packaging/windows/banchoxterm.ico`
      generado desde `icons/logo.svg` (16/24/32/48/64/128/256 px, multi-frame
      ICO), `MUI_ICON`/`MUI_UNICON` en el instalador y `banchoxterm.rc` que
      embebe el icono en `banchoxterm.exe` y `banchoxterm-term.exe`.
- [x] **Documentar las limitaciones vigentes**: sección "Limitations" del README
      ampliada (FTP solo pasivo sin progreso por byte ni carpetas, drag & drop
      SFTP solo intra-app, VNC con encodings limitados y sin mods complejos,
      monitor Windows no probado, auto-updater no firmado y dependiente de
      `tar.exe` en portable, datos portables no compartidos con la instalación,
      logging que no captura sesiones locales Linux).

## Notas / limitaciones conocidas

- FTP: solo pasivo, sin TLS/chmod, sin subida de carpetas ni progreso por byte
  (el progreso por byte y las colas son de SFTP).
- El drag & drop SFTP funciona solo dentro de la app: soltar archivos locales
  sube; arrastrar remotos descarga a una carpeta elegida. No se puede arrastrar
  a otra aplicación (Explorer).
- La monitorización Windows está implementada pero no probada contra un
  servidor SSH Windows real.
- La sesión local de Linux (QTermWidget) no se captura en el log de sesión.
- La edición portable guarda ajustes/sesiones/known_hosts junto al exe y no los
  comparte con una instalación normal del mismo equipo. El auto-updater portable
  depende de `tar.exe` (Windows 10+) y el instalador NSIS no está firmado
  (SmartScreen).
- VNC embebido: sin soporte de mods complejos (Ctrl+tecla) y encodings limitados
  (Raw/Hextile/CopyRect; Tight/ZRLE deshabilitados al no enlazar zlib).
- El código propio es MIT y `banchoxterm.exe` NO enlaza código GPL (solo Qt LGPLv3
  dinámico + libssh2 BSD). QTermWidget (GPL-2.0) vive únicamente dentro del proceso
  separado `banchoxterm-term.exe`, distribuido bajo GPL con su fuente en
  `third_party/` (ver `third-party-licenses.txt`).
- En Linux el binario aún enlaza QTermWidget directamente (obra combinada GPL); si
  se quisiera hacer la app propietaria, habría que aislarlo igual que en Windows o
  usar un widget LGPL (p. ej. VTE).

## Convención

- Marcar con `[x]` los elementos completados.
- Los elementos P1 son los diferenciadores que faltan para competir de tú a tú
  con MobaXterm; P2/P3 son robustez y pulido.
