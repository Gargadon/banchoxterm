# BanchoXterm — Roadmap

Objetivo: convertir BanchoXterm en una alternativa real a MobaXterm.

## Estado actual

Ya implementado:

- SSH con libssh2 (password, clave privada, agente, X11 forwarding en Linux)
- SFTP (navegar, subir, bajar, borrar, editar con auto-upload)
- Túneles SSH (local, remoto, dinámico/SOCKS5)
- Telnet, Serial (solo Linux), terminal local (ConPTY en Windows)
- Gestor de sesiones (JSON), pestañas, multi-input, monitor remoto
- Temas claro/oscuro, i18n (en/es/pt), master password + keyring
- RDP/VNC (lanzadores de clientes externos)

## Prioridades

### P0 — Seguridad (innegociable)

- [x] **Verificación de host key / `known_hosts`** (`sshconnection.cpp`).
  Se valida la huella del servidor con `libssh2_knownhost_*`, se guarda en
  `known_hosts` (formato OpenSSH) y se muestra un diálogo de confirmación al
  primer contacto o con una advertencia ante un cambio de clave.

### P1 — Funcionalidades clave de MobaXterm

- [x] **X11 server en Windows** (o desactivar la opción correctamente).
  La opción "Enable X11 Forwarding" se oculta en Windows (`sessiondialog.cpp`).
- [x] **Importar/exportar sesiones** (`SessionManager::exportSessions` /
  `importSessions` + botones Import/Export en el sidebar).
- [x] **Carpetas/grupos de sesiones**: campo `group` en `Session` y sidebar
  con `QTreeWidget` (grupos como nodos, sesiones como hijos).
- [x] **Logging/grabación de sesiones**: opción en Ajustes (habilitar +
  directorio) y captura de salida en `TerminalTab` (SSH en Linux/Windows y
  ConPTY en Windows). La sesión local de Linux (QTermWidget) no se captura.
- [x] **FTP** documentado como no soportado (solo SFTP), ver `README.md`.

### P2 — Robustez y UX

- [x] Monitorización remota en hosts Windows: fallback a un comando PowerShell
      cuando el sondeo `/proc` de Linux no produce resultados (`sshconnection.cpp`).
- [x] Terminal Windows (`VtTerminalWidget`): bracketed paste (DECSET 2004) y
      true-color ya presente; se añadió bracketed paste en el pegado.
- [x] Auto-reconexión de sesiones: opción "Auto-reconnect on disconnect" en SSH
      (`session.h` + `TerminalTab::maybeScheduleReconnect` + `MainWindow`).
- [x] Macros de teclado: menú "Macros" con diálogo de gestión (nombre + texto).
- [x] Búsqueda global entre sesiones: "Find in All Sessions" (Ctrl+Shift+F).
      Solo funcional en Windows (el `QTermWidget` de Linux no expone búsqueda
      programática; usa su propia barra de búsqueda por pestaña).
- [x] SFTP: crear carpeta, renombrar, chmod y subida recursiva de carpetas.

Notas / limitaciones:
- La monitorización Windows está implementada pero no probada contra un
  servidor SSH Windows real.
- La sesión local de Linux (QTermWidget) no se captura en el log de sesión.

### P3 — Distribución

- [x] Licencia open source (MIT) — `LICENSE`, `README.md` y el "About" actualizados.
- [x] Instaladores de Windows generados por GitHub Actions: NSIS + ZIP portable
      (`.github/workflows/ci.yml` + `packaging/windows/installer.nsi`).
- [ ] (Opcional) Paquete para Linux (AppImage) y macOS.
- [ ] Icono `.ico` de la aplicación para el instalador NSIS.

Nota: aunque el código propio es MIT, el binario enlaza QTermWidget (GPL-2.0),
por lo que la distribución combinada queda sujeta a GPL. Ver
`third-party-licenses.txt`.

## Convención

- Marcar con `[x]` los elementos completados.
- Los elementos P0 bloquean cualquier release.
