# BanchoXterm — Roadmap

Objetivo: convertir BanchoXterm en una alternativa real a MobaXterm.

## Estado actual

Ya implementado:

- **SSH** con libssh2 (password, clave privada, agente, X11 forwarding en Linux)
- **Verificación de host key** (`known_hosts`, formato OpenSSH, diálogo de confianza)
- **SFTP** (navegar, subir, bajar, borrar, renombrar, chmod, crear carpeta, subir carpetas, editar con auto-upload)
- **Túneles SSH** (local, remoto, dinámico/SOCKS5)
- **Telnet**, **Serial** (QSerialPort nativo en Windows; herramientas del sistema en Linux), **terminal local** (ConPTY en Windows alimentando QTermWidget en-proceso; QTermWidget directo en Linux)
- **RDP embebido** en Windows vía ActiveQt (`QAxWidget` + `mstscax.dll`, con fallback a `mstsc.exe`); en Linux vía `xfreerdp`
- **VNC embebido** vía `libvncclient` (renderizador propio + entrada de teclado/ratón)
- **Gestor de sesiones** (JSON, grupos/carpetas, importar/exportar, favoritos, recientes e importación OpenSSH), pestañas, multi-input con confirmación
- **Monitor remoto** (CPU/RAM/disco/uptime; Linux `/proc` + fallback PowerShell en Windows)
- **Macros de teclado**, **búsqueda global** (Windows), **logging de sesiones**, **auto-reconexión**
- Temas claro/oscuro, i18n (en/es/pt), master password + keyring (Windows Credential Manager / secret-tool)
- App licencia GPL-2.0-or-later (enlaza QTermWidget directamente en todas las
  plataformas), CI (MSVC) + instalador NSIS + ZIP portable

## Prioridades

### P1 — Diferenciadores clave frente a MobaXterm

- [x] **Split-view / multi-terminal en rejilla**: cuatro grupos de pestañas
      independientes, modos simple/split/2x2, movimiento entre paneles y
      persistencia del modo/panel activo. Pendiente: restauración completa de
      cada sesión y mejoras de acoplamiento.
- [x] **SSH gateway / jump host (ProxyJump)**: el bastión se autentica y
      verifica por separado; el destino usa una sesión libssh2 sobre un canal
      `direct-tcpip`, conservando SFTP, shell, túneles y monitorización.
- [x] **Keyboard-interactive / 2FA**: `libssh2_userauth_keyboard_interactive`
      con diálogo de prompts (OTP/MFA) en `SshConnection`.
- [ ] **X server en Windows**: X11 forwarding permite conectar con un servidor
      X externo en `127.0.0.1:6000` (VcXsrv/X410/Xming); pendiente empaquetar o
      integrar una opción open-source de X server en la distribución.
- [x] **FTP/FTPS**: cliente pasivo con navegación, subida, descarga, borrado,
      creación y renombrado; FTPS explícito con validación de certificado es el
      modo predeterminado y FTP plano queda como compatibilidad opt-in.

### P2 — Calidad y robustez

- [x] ~~**Aislamiento de QTermWidget (GPL) en proceso aparte**~~ **REVERTIDO**: cuando
      la app era MIT, el fork vendado en `third_party/qtermwidget` (parches MSVC:
      mmap→malloc en `BlockArray`/`History`, `Pty` stub multiplataforma, `wcwidth`
      de respaldo, `startExternal()`/`feedData()`) se compilaba como `qtermwidget_win`
      solo para el proceso GPL `banchoxterm-term.exe`, que la app lanzaba y embebía
      por HWND (`SetParent`/`MoveWindow`/`ShowWindow`) con named-pipe IPC
      (`src/termipc.h`). Al pasar la app a GPL-2.0-or-later se eliminó todo el
      aparato de aislamiento: `banchoxterm-term.exe`, `TerminalHostClient`,
      `termipc.h` y `src/termhost/`. Ahora el fork se compila como `qtermwidget6`
      (estático) y se enlaza directo en `banchoxterm` en todas las plataformas,
      sin dependencia del `qtermwidget6` de la distro ni de `lxqt-build-tools`.
- [x] **Cablear ConPTY a QTermWidget en Windows (en-proceso)**: `TerminalTab` usa un
      único `QTermWidget` en todas las plataformas; en Windows arranca el emulador
      en modo `startExternal()` y alimenta ConPTY telnet/local vía `feedData()`/
      `sendData()` con `applyTerminalSize` compartido entre ConPTY y SSH. Color
      schemes en Windows: `QTermWidget::availableColorSchemes()` (ya no es lista
      fija). Nota: la búsqueda global sigue deshabilitada en Windows (QTermWidget
      no expone búsqueda programática; se usa su barra integrada). Pendiente:
      verificación visual interactiva del render, teclado, resize y menú contexto
      en Windows.
- [x] **Rendimiento de red**: `SshConnection` usa polling (`QTimer` 15 ms +
      `select()`); migrado a socket notifier event-driven (`QSocketNotifier`
      Read siempre armado + Write solo cuando `libssh2_session_block_directions`
      reporta `LIBSSH2_SESSION_BLOCK_OUTBOUND`; notificador propio por socket X11;
      se eliminó el `QTimer` de 15 ms. El paso de transporte lo dispara
      `libssh2_channel_read`, que lee el socket real).
- [x] **Rendimiento de CPU**: guard anti-spin en `SshConnection::onSocketActivity()`
      (`m_activityProgress`/`m_socketKickPending`: el re-kick con
      `QTimer::singleShot(0)` solo se re-encola mientras las máquinas de estado
      realmente consumieron datos, evitando girar el event loop al máximo si
      libssh2 bufferiza sin progreso en el socket); `readShell()` acumula todos
      los chunks del pase y emite un único `shellDataReceived` (la señal cruza
      hilos vía cola, así que se eliminan cientos de entregas/copias por segundo
      en salida masiva); en Windows el timer de ConPTY pasó de fijo 10 ms a
      adaptativo (10→50 ms, sube 10 ms por pase sin salida y vuelve a 10 ms al
      llegar datos). Verificado en Linux (build limpio + tests). Pendiente:
      verificación en Windows.
- [x] **Rendimiento de UI**: iconos de carpeta/archivo del árbol SFTP cacheados
      como estáticos (`folderIcon()`/`fileIcon()` en `sftpsidebar.cpp`); antes se
      re-decodificaba el SVG por cada item, costoso con directorios de miles de
      archivos.
- [x] **Rendimiento de pintado**: caché de highlighting en `VtTerminalWidget`
      (los regex se recalculan solo cuando cambia el contenido/scroll, no en
      cada parpadeo del cursor).
- [x] **Tests**: añadidos tests del parser VT (render, clear, cursor) y arreglado
      `testMasterPasswordEncryption` (faltaba el namespace de `QSettings`).
      16/16 en verde.
- [x] **UI de `known_hosts`**: diálogo "Manage Known Hosts" en Ajustes →
      Seguridad (listar y eliminar).
- [x] **UX de sesiones**: clonar/duplicar sesión, reordenar con drag & drop
      (persistido) y barra "QuickConnect" (`usuario@host[:puerto]` o búsqueda
      por sesión guardada).

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
      embebe el icono en `banchoxterm.exe`.
- [x] **Documentar las limitaciones vigentes**: sección "Limitations" del README
      ampliada (FTP solo pasivo sin progreso por byte ni carpetas, drag & drop
      SFTP solo intra-app, VNC con encodings limitados y sin mods complejos,
      monitor Windows no probado, auto-updater no firmado y dependiente de
      `tar.exe` en portable, datos portables no compartidos con la instalación,
      logging que no captura sesiones locales Linux).

## Inversión 3 — capa de producto y experiencia operativa

Esta inversión no es una sola función: es la capa que convierte un cliente SSH
con varios protocolos en una herramienta diaria de trabajo. MobaXterm combina
sesiones guardadas, pestañas, paneles, ejecución múltiple, utilidades y
distribución portable; BanchoXterm debe alcanzar una experiencia equivalente
con componentes libres y una arquitectura mantenible.

### Fase 3.1 — llegar al host en segundos

- [x] QuickConnect: `usuario@host[:puerto]`, Enter para conectar y búsqueda por
  nombre o endpoint guardado.
- [x] Historial de conexiones recientes y favoritos, sin guardar secretos en
  texto plano.
- [ ] Paleta global (`Ctrl+K`) para abrir sesión, cambiar panel, ejecutar macro,
  buscar en sesiones y activar acciones.

### Fase 3.2 — organizar trabajo paralelo

- [x] Rejilla 2x2 con cuatro grupos de pestañas independientes.
- [x] Desmontar una pestaña en ventana propia y volver a acoplarla.
- [ ] Guardar/restaurar el layout al iniciar, incluyendo sesión, panel y
  directorio remoto.
- [x] MultiExec con confirmación visible y lista de destinos; la ejecución
  masiva es explícita para evitar comandos accidentales. Pendiente: exclusiones
  y selección individual de destinos.

### Fase 3.3 — sesiones como activo portable

- [ ] Variables por perfil (`${USER}`, `${HOST}`, `${ENV}`) y plantillas para
  crear muchas sesiones sin duplicar configuración.
- [x] Importador desde OpenSSH `config`, con mapeo de host, usuario, puerto,
  clave e indicación de ProxyJump. Pendiente: PuTTY y vista previa.
- [ ] Exportación/importación de grupos, permisos de archivo documentados y
  separación clara entre configuración y secretos del keyring.
- [ ] Sesiones compartidas opcionales mediante archivos versionables, sin
  exportar contraseñas ni claves privadas por accidente.

### Fase 3.4 — distribución confiable

- [x] ZIP portable e instalador NSIS; el updater verifica el SHA-256 publicado
  antes de ejecutar el artefacto descargado.
- [ ] Firmar instalador y ejecutables, publicar checksums y generar SBOM por
  release.
- [ ] Paquete de herramientas opcionales (por ejemplo, X server) separado del
  núcleo para no mezclar licencias ni elevar el tamaño de la instalación.
- [ ] Actualizaciones con rollback y canal estable/preview.

### Criterio de salida de la inversión 3

Un usuario debe poder importar o crear una sesión, encontrarla desde QuickConnect,
abrir cuatro destinos, ejecutar una acción controlada en varios paneles, cerrar
la aplicación y recuperar el mismo layout; todo ello desde un ZIP verificable y
sin que los secretos aparezcan en los archivos compartidos.

## Notas / limitaciones conocidas

- FTP/FTPS: solo pasivo, sin chmod, sin subida de carpetas ni progreso por byte
  (el progreso por byte y las colas son de SFTP). FTPS requiere certificados
  válidos cuando se mantiene activada la verificación.
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
- El código propio es GPL-2.0-or-later y enlaza QTermWidget (también GPL-2.0-or-later)
  directamente en todas las plataformas (Qt LGPLv3 dinámico + libssh2 BSD aparte).
  QTermWidget se distribuye con su fuente en `third_party/` (ver
  `third-party-licenses.txt`).
- En Windows el fork vendado se compila sin KPty (`qtermwidget6` estático), por
  lo que las sesiones locales/telnet usan ConPTY en modo `startExternal()`/`feedData()`;
  en Linux el mismo target compila con KPty (`kprocess.cpp`/`kpty*.cpp`) para sus
  PTY nativos.

## Convención

- Marcar con `[x]` los elementos completados.
- Los elementos P1 son los diferenciadores que faltan para competir de tú a tú
  con MobaXterm; P2/P3 son robustez y pulido.
