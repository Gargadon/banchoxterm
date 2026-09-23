# BanchoXterm — auditoría y trabajo pendiente

Este documento resume el estado actual del proyecto y las funciones necesarias
para convertir BanchoXterm en una herramienta integral para terminales locales
y remotas.

## Funcionalidad ya implementada

### Protocolos y conexiones

- SSH con contraseña, clave privada, agente SSH y keyboard-interactive.
- Verificación de host keys y administración básica de `known_hosts`.
- ProxyJump mediante bastion host.
- X11 forwarding.
- Túneles SSH locales, remotos y dinámicos mediante SOCKS5.
- SFTP.
- FTP/FTPS básico.
- Telnet.
- Terminal local.
- ConPTY para shells locales en Windows.
- Conexiones seriales mediante `picocom`, `screen` o `minicom`.
- RDP, usando ActiveX en Windows cuando está disponible y `mstsc` como
  alternativa.
- VNC embebido cuando la plataforma lo permite.
- Inicio y empaquetado opcional de VcXsrv en Windows.

### Gestión de sesiones

- Pestañas y sesiones guardadas.
- Grupos y favoritos.
- Duplicación y reordenamiento de sesiones.
- Sesiones recientes.
- Importación y exportación JSON.
- Importación de configuraciones OpenSSH.
- Importación de registros de PuTTY.
- Restauración del workspace y de las pestañas abiertas.
- Reconexión automática.

### Terminal e interfaz

- Vistas divididas y cuadrícula 2x2.
- Pestañas desacoplables.
- Multi-input para enviar comandos a varias sesiones.
- Macros de texto.
- Command Palette.
- Menú contextual del terminal.
- Caracteres especiales, incluyendo `Break (Cisco)` y controles como
  `Ctrl+C`, `Ctrl+D`, `Ctrl+Z`, Escape, Enter, Tab y Backspace.
- Búsqueda en sesiones.
- Scrollback configurable.
- Fuentes y esquemas de color configurables.
- Temas claro, oscuro y del sistema.
- Barra Ribbon.
- Internacionalización en inglés, español y portugués.
- Registro de salida de sesiones.
- Actualizador automático.

### Transferencia y administración remota

- Navegación SFTP con panel remoto y panel local.
- Subida y descarga de archivos.
- Subida recursiva de carpetas.
- Creación, renombrado y eliminación de archivos y carpetas.
- Cambio de permisos mediante chmod.
- Editor remoto externo.
- Drag & drop entre el panel local y el remoto.
- Monitorización remota de CPU, RAM, disco y uptime.

### Seguridad

- Almacenamiento de credenciales en Windows Credential Manager.
- Almacenamiento mediante Secret Service/`secret-tool` en Linux.
- Cifrado adicional mediante master password.
- Cifrado de credenciales con Argon2id y XChaCha20-Poly1305.
- No se guardan contraseñas directamente en el JSON de sesiones.

## Funcionalidad incompleta o que necesita revisión

### Búsqueda global — completada

- [x] Exponer la búsqueda del historial de QTermWidget a BanchoXterm.
- [x] Aplicar búsqueda hacia delante y hacia atrás.
- [x] Respetar la opción de mayúsculas/minúsculas.
- [x] Seleccionar y enfocar la pestaña donde se encontró la coincidencia.
- [x] Mostrar el resultado en el diálogo global.

### Macros y automatización

Las macros actuales solo envían texto plano. Falta añadir:

- [x] Secuencias de varios pasos con texto, pausas, expectativas y respuestas.
- [x] Pausas configurables mediante `{{PAUSE:milisegundos}}`.
- [x] Envío de caracteres especiales mediante tokens (`{{BREAK}}`, `{{CTRL+C}}`,
  `{{ENTER}}`, `{{TAB}}`, `{{ESC}}`, `{{BACKSPACE}}` y `{{TELNET-ESC}}`).
- [x] Espera de prompts o expresiones regulares mediante `{{EXPECT:regex}}`
  con tiempo máximo de 30 segundos.
- [x] Respuestas automáticas mediante `{{RESPOND:texto}}` después de un
  `EXPECT` satisfecho.
- [x] Variables y sustitución de parámetros (`{{HOST}}`, `{{USER}}`,
  `{{PORT}}`, `{{SESSION}}`, `{{REMOTE_DIR}}`, `{{SERIAL_PORT}}` y
  `{{ENV:NOMBRE}}`).
- [x] Ejecución sobre la sesión actual o sobre todas las sesiones de su grupo,
  con confirmación previa.
- [x] Registro persistente de resultados y errores, consultable desde el gestor
  de macros.
- [x] Importación y exportación de macros en JSON.

El objetivo sería ofrecer una automatización tipo Expect, pero integrada en la
interfaz y con confirmaciones para comandos peligrosos.

### Transferencias SFTP

El SFTP actual es funcional, pero falta convertirlo en un gestor de
transferencias completo:

- [x] Cola de trabajos serializada para archivos y carpetas SFTP.
- [x] Cancelación de trabajos pendientes sin interrumpir el trabajo activo.
- [x] Cancelación cooperativa del trabajo SFTP activo.
- [x] Pausa y reanudación cooperativas del trabajo SFTP activo.
- [x] Reintentos automáticos (hasta dos reintentos por trabajo).
- [x] Transferencias paralelas configurables para SFTP, con sesiones SSH
  independientes y un límite ajustable de 1 a 8 trabajos.
- [x] Progreso por archivo; la cola muestra los trabajos pendientes.
- [x] Verificación de tamaño en descargas y subidas SFTP.
- [x] Sincronización básica de archivos de las carpetas activas en ambas
  direcciones, con exclusión segura de directorios y conflictos de tipo.
- [x] Comparación de la carpeta local y remota activas, mostrando entradas
  exclusivas, tipos distintos y diferencias de tamaño.
- [x] Mejor manejo de conflictos de nombres al descargar archivos.

La cola mantiene el modo secuencial por defecto. Para SFTP permite activar
transferencias paralelas configurables; cada trabajo usa su propia sesión SSH
y sus propios handles SFTP para evitar compartir estado de libssh2 entre hilos.

### FTP/FTPS

El cliente FTP funciona en modo pasivo y permite operaciones básicas, pero
necesita:

- [x] Mejor progreso de transferencias, con señales por bloques para descarga y subida.
- [x] Subida recursiva de carpetas FTP, creando los directorios remotos.
- [x] Drag & drop de archivos entre los paneles local y FTP.
- [x] Cola de transferencias compartida con SFTP.
- [x] Mejor soporte de listados de servidores no Unix, incluyendo formato
  DOS/Windows de `LIST` con tipo y tamaño.
- [x] Cancelación cooperativa de transferencias FTP activas.
- [x] Manejo de pérdida del canal de control FTP con hasta tres reconexiones
  automáticas, cancelables al desconectar manualmente.
- [x] Validación explícita de certificados TLS y mensajes detallados de FTPS.
- [x] Configuración TLS avanzada de FTPS (versión mínima, almacén de CA y
  política de certificados por sesión).

### Conexiones seriales

La conexión serial usa `QSerialPort` de forma nativa; aún faltan algunas
funciones avanzadas:

- [x] Paridad.
- [x] Bits de datos.
- [x] Bits de parada.
- [x] Control de flujo RTS/CTS y XON/XOFF.
- [x] DTR/RTS, persistidos por sesión y aplicados al abrir el puerto.
- [x] Break físico.
- [x] Envío de archivos mediante XMODEM con CRC16/checksum, ACK/NAK,
  reintentos y cancelación.
- [x] Envío de un archivo mediante YMODEM con cabecera, CRC16, ACK/NAK,
  reintentos y cancelación.
- [x] Envío de archivos mediante ZMODEM mediante el emisor estándar `sz` de
  `lrzsz`, con puente bidireccional hacia el puerto serial, cancelación y
  diagnóstico de estado. El ejecutable se detecta en tiempo de ejecución.
- [x] Registro de la sesión sin depender del programa externo; el log nativo
  recibe también los datos de conexiones seriales.

Los caracteres especiales de terminal no sustituyen al break físico de una
conexión serial.

### Túneles SSH

Los tres tipos principales ya existen, pero necesitan más robustez:

- [x] Estado visible de cada túnel mediante mensajes de estado y diagnóstico
  con tipo, puerto y número de conexiones activas.
- [x] Inicio y detención individual de túneles desde el panel SSH/SFTP sin
  desconectar la sesión.
- [x] Mensajes claros cuando un túnel no puede iniciar, incluyendo el puerto
  posiblemente ocupado.
- [x] Reintento y recuperación de túneles que no pudieron iniciar, con hasta
  tres intentos mientras la sesión SSH siga conectada.
- [x] Soporte IPv6 en SOCKS5.
- [x] Autenticación SOCKS5 opcional con usuario y contraseña configurables; la
  contraseña se guarda en el keyring y no en el JSON.
- [x] Validación de puertos y destinos en el editor de túneles.
- [x] Estadísticas de conexiones activas por túnel, visibles en diagnóstico.

### VNC y RDP

VNC tiene soporte limitado de encodings y de combinaciones complejas de
teclas. Falta evaluar:

- [x] Preferencia de encodings Tight y ZRLE con fallback Hextile/Raw.
- [x] Mejor mapeo de teclado VNC para navegación, teclado numérico, bloqueos,
  teclas del sistema y F1–F24.
- [x] Portapapeles bidireccional VNC mediante XCutText.
- [x] Escalado VNC conservando proporción, modo 1:1 y pantalla completa desde
  el menú contextual.
- [x] Reconexión automática VNC y RDP cuando la sesión lo solicita, tanto para
  clientes embebidos como externos.

La edición Windows ARM64 no incluye actualmente VNC embebido por la
compatibilidad de libvncclient con MSVC ARM64.

### Perfiles de terminal

Falta separar mejor la configuración global de la configuración de cada sesión:

- [x] Perfil de fuente y tamaño por sesión, con herencia global.
- [x] Perfil de colores por sesión, con herencia de la configuración global.
- [x] Encoding por sesión (`UTF-8` o locale del sistema), limitado por el
  codec que expone QTermWidget.
- [x] Variable `TERM` y `LANG` por sesión.
- [x] Comportamiento de Backspace y Enter por sesión, con secuencias configurables.
- [x] Tamaño inicial de terminal por sesión.
- [x] Scrollback por perfil.
- [x] Perfil por fabricante para Cisco IOS, Juniper Junos, MikroTik RouterOS
  y FortiOS, con valores iniciales editables por sesión.

## Experiencia de administración de servidores

### Biblioteca de caracteres especiales

La primera versión ya incluye Break de Cisco y controles comunes. Debe
ampliarse con:

- [x] Plantillas iniciales de `TERM` y Break asociadas al perfil del fabricante.
- [x] Break serial real mediante `QSerialPort::setBreakEnabled`.
- [x] Ctrl+Shift+6/Break configurable por sesión mediante una secuencia hexadecimal.
- [x] Secuencias de escape Telnet.
- [x] Comandos específicos por protocolo: pausa/reanudación cooperativa para
  SFTP/FTP, cancelación cooperativa para SFTP/FTP y cancelación CAN para
  XMODEM/YMODEM; la interfaz oculta las acciones no compatibles.
- [x] Personalización por usuario de caracteres especiales mediante secuencias
  hexadecimales persistidas en QSettings.

### Comandos y seguridad operacional

Sería útil añadir:

- [x] Historial de comandos enviados por sesión.
- [x] Historial global con búsqueda.
- [x] Confirmación para comandos potencialmente destructivos en multi-input.
- [x] Detección configurable de prompts mediante expresión regular por sesión,
  registrada en el diagnóstico.
- [x] Sesiones de solo lectura persistidas por perfil, con bloqueo de entrada,
  pegado, macros y caracteres especiales.
- [x] Modo “broadcast seguro” para multi-input, con selección individual de sesiones.
- [x] Vista previa del comando y de los destinos antes de enviar.
- [x] Limitación de velocidad al enviar comandos masivos (75 ms entre sesiones).

### Diagnóstico

El panel de diagnóstico ya está disponible desde Tools y la paleta de comandos.
Incluye:

- [x] Log SSH detallado de conexión TCP, métodos de autenticación, handshake,
  host key, autenticación de usuario y subsistema SFTP; se muestra y exporta
  desde Diagnósticos.
- [x] Estado del handshake, informado cuando la conexión SSH queda establecida.
- [x] Latencia de establecimiento SSH y mensaje de handshake completado.
- [x] Reconexiones.
- [x] Estado de túneles.
- [x] Errores de autenticación registrados con código y detalle de libssh2.
- [x] Errores de SFTP registrados en el diagnóstico de la pestaña SSH.
- [x] Errores de conexión y de operaciones FTP/FTPS identificados por protocolo
  en el diagnóstico y en los informes exportados.
- [x] Exportación de un informe de diagnóstico en texto.

## Seguridad pendiente

- [x] Integración multiplataforma con Windows Credential Manager y Secret
  Service; en KDE/Linux se añade fallback directo mediante `kwallet-query`.
  macOS Keychain queda fuera del alcance por la ausencia de una plataforma de
  prueba.
- [x] Bloqueo manual de la aplicación mediante Master Password (`Ctrl+Alt+L`).
- [x] Acción explícita para limpiar de forma segura el portapapeles y la
  selección primaria de X11.
- [x] Política básica para contraseña maestra: mínimo de 8 caracteres y
  máximo de 5 intentos al desbloquear la aplicación.
- [x] Opciones claras para confiar o rechazar cambios de host key, con la
  decisión registrada en el diagnóstico SSH.
- [x] Exportación e importación cifrada de sesiones, macros y referencias de
  credenciales mediante `.bancho.enc`, protegida por Master Password.
- [x] Archivos de sesión y exportaciones JSON/OpenSSH se escriben con permisos
  privados para el propietario (lectura/escritura, sin acceso de grupo u otros).

## Importación, exportación y compatibilidad

Ya se importan JSON, OpenSSH, PuTTY, MobaXterm y SecureCRT. Sería conveniente añadir:

- [x] MobaXterm (`.mxtsessions`, SSH bookmarks).
- [x] SecureCRT (`.ini`, SSH session profiles).
- [x] Royal TS legacy XML documents (`.rtsx`/`.rts`) with SSH, Telnet, RDP
  and VNC connections; encrypted/compressed `.rtsz` documents remain
  unsupported.
- [x] Exportación compatible con OpenSSH (`Host`, `HostName`, `User`, `Port`,
  `IdentityFile` y `ProxyJump`; sin credenciales).
- [x] Exportación e importación de macros junto con las sesiones en un bundle JSON;
  se mantienen compatibles los JSON antiguos.
- [x] Importación de grupos y favoritos (los campos ya forman parte del JSON de sesiones).
- [x] Detección y resolución de conflictos de IDs y nombres durante la importación.

## Calidad y pruebas

La cobertura actual se concentra principalmente en serialización e importación
de sesiones. Faltan:

- [x] Pruebas de SSH con un servidor local de prueba, incluyendo autenticación
  por clave y verificación de host key.
- [x] Pruebas de SFTP contra el servidor local, incluyendo inicialización y
  listado de directorio.
- [x] Pruebas de túneles locales, remotos y SOCKS5 con tráfico TCP real.
- [x] Prueba de integración FTP con servidor local (login, modo pasivo,
  listado, descarga y subida).
- [x] Prueba de integración FTPS explícita (AUTH TLS, CA personalizado,
  autenticación, PBSZ y PROT P).
- [x] Pruebas de transferencias FTPS por canal de datos protegido (PASV,
  listado, descarga y subida).
- [x] Pruebas de importadores con fixtures persistentes representativos de
  exportaciones reales de OpenSSH, PuTTY, MobaXterm, SecureCRT y Royal TS.
- [x] Pruebas del keyring y del master password (el caso de keyring se omite
  explícitamente cuando el proveedor del sistema no está instalado).
- [x] Pruebas del terminal y de caracteres especiales mediante eventos reales
  de teclado en `VtTerminalWidget`.
- [x] Pruebas headless de la interfaz y del ciclo de vida de pestañas de
  `MainWindow`, incluyendo apertura de una sesión local y cierre natural de
  la pestaña.
- [x] Prueba de ciclo de vida de `TerminalTab` con una sesión local real.
- [x] Prueba de arranque y cierre de `MainWindow` en modo headless.
- [x] Prueba de reconexión del canal de control FTP/FTPS tras una pérdida de conexión.
- [x] Prueba de reconexión SSH/SFTP contra el servidor local, incluyendo una
  nueva conexión y una operación de listado posterior.
- [x] Pruebas de reconexión para VNC y RDP: se verifica el fallo del cliente,
  la solicitud automática y la recreación de la pestaña por `MainWindow`.
- [x] Cobertura automatizada específica de Linux y Windows: la CI compila y
  ejecuta `ctest` en Ubuntu, Windows x64 y Windows ARM64 (con VNC desactivado
  en ARM64 por la limitación del proveedor).
- [x] Pruebas de empaquetado e instalación mediante `cmake --install`,
  incluyendo binario, recursos de QTermWidget, `.desktop` e icono Linux.

También conviene corregir pequeños detalles de interfaz.

- [x] Verificar que el botón “Manage Known Hosts” aparezca una sola vez en la configuración.

## Plataformas

### Linux

Linux es una plataforma de desarrollo y prueba principal. Falta decidir si se
publicarán paquetes oficiales además de permitir la compilación manual.

### Windows

Windows x64 y ARM64 son objetivos de distribución. Deben seguir probándose
las diferencias entre ConPTY, Qt, VcXsrv, ActiveX, libvncclient y el keyring
de Windows.

### macOS

No se planifica soporte para macOS por ahora.

No contamos con equipos de prueba con macOS. Ya han aparecido suficientes
problemas al desarrollar en Linux y descubrir después diferencias de
comportamiento en Windows. Implementar macOS sin hardware de prueba implicaría
trabajar a ciegas y no permitiría validar correctamente terminales, keychain,
PTY, empaquetado, menús ni integración del sistema.

El soporte macOS solo debería evaluarse cuando exista al menos un entorno real
de prueba mantenido por el proyecto. No debe tratarse como una prioridad antes
de completar y estabilizar Linux y Windows.

## Orden recomendado

1. Corregir la búsqueda global, que actualmente no realiza búsquedas reales.
2. Completar pruebas automatizadas de las rutas críticas.
3. Crear el motor de automatización de macros.
4. Mejorar la cola y recuperación de transferencias SFTP.
5. Añadir configuración serial nativa y break físico.
6. Crear perfiles de terminal y perfiles por fabricante.
7. Mejorar diagnóstico, túneles y reconexión.
8. Añadir importadores de MobaXterm y SecureCRT.
9. Reforzar seguridad, exportación cifrada y gestión de credenciales.
10. Evaluar paquetes Linux y distribución adicional.
11. Evaluar macOS únicamente cuando haya hardware de prueba.

## Meta del proyecto

La meta no es únicamente soportar muchos protocolos. BanchoXterm debe permitir
abrir una sesión, transferir archivos, diagnosticar problemas, ejecutar tareas
repetitivas, administrar varios equipos y recuperar una conexión desde una
sola aplicación, con un comportamiento consistente y verificable en Linux y
Windows.
