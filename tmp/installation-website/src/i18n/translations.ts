export type Locale = 'es' | 'en' | 'pt';

export const defaultLocale: Locale = 'es';
export const locales: Locale[] = ['es', 'en', 'pt'];

export const translations = {
  es: {
    site: {
      title: "BanchoXterm — Emulador de Terminal Multiprotocolo & Gestor de Sesiones",
      description: "Emulador de terminal multiprotocolo de alto rendimiento en C++17 y Qt 6 para Linux y Windows. Soporta SSH, SFTP, FTP/FTPS, Serial con ZMODEM, túneles SSH y multi-input sin Electron."
    },
    nav: {
      features: "Características",
      protocols: "Protocolos",
      preview: "Vista Previa",
      install: "Instalación",
      download: "Descargar",
      langSelect: "Idioma"
    },
    hero: {
      badgeOpenSource: "Código Abierto • GPL v3",
      badgeNoElectron: "Sin Electron: 100% Nativo en C++17 & Qt 6",
      titleLine1: "El emulador de terminal",
      titleLine2: "multiprotocolo definitivo",
      subtitle: "Conéctate a tus servidores remotos con la máxima velocidad y control. SSH con X11, explorador SFTP integrado, FTPS seguro, consola Serial con ZMODEM, túneles SSH y multi-input simultáneo en una interfaz moderna para Linux y Windows.",
      ctaDownload: "Descargar BanchoXterm",
      ctaGitHub: "Ver en GitHub",
      quickCloneLabel: "Clonar y construir:",
      copied: "✓ Copiado",
      mockup: {
        tabSsh: "prod-app-01 (SSH)",
        tabSftp: "SFTP: /var/www",
        tabSerial: "Serial: /dev/ttyUSB0",
        tabMulti: "Multi-Input (3 servidores)",
        sidebarTitle: "SESIONES GUARDADAS",
        cloudCluster: "Cloud Cluster",
        networking: "Networking & Lab",
        statusOnline: "SSH: CONECTADO",
        cpu: "CPU",
        ram: "RAM",
        disk: "DISCO",
        uptime: "UPTIME"
      }
    },
    features: {
      tag: "Capacidades Principales",
      title: "Todo lo que un SysAdmin y Desarrollador necesita",
      desc: "Diseñado para reemplazar herramientas pesadas y fragmentadas con una solución integral, rápida y confiable.",
      items: [
        {
          icon: "/icons/terminal.svg",
          title: "Terminal Multi-Pestañas y Split",
          description: "Organiza múltiples sesiones locales y remotas simultáneamente. Abre pestañas independientes o divide la pantalla para comparar logs y shells lado a lado.",
          tag: "Productividad"
        },
        {
          icon: "/icons/multiinput.svg",
          title: "Multi-Input Broadcast",
          description: "Envía comandos de teclado a todas las terminales abiertas a la vez. Perfecto para actualizar paquetes, reiniciar clusters o consultar estados en lote.",
          tag: "Orquestación"
        },
        {
          icon: "/icons/file.svg",
          title: "Explorador SFTP Integrado",
          description: "Navega directorios remotos en el panel lateral, descarga, sube mediante drag & drop, administra permisos y edita archivos de texto en vivo sin salir de la terminal.",
          tag: "Archivos"
        },
        {
          icon: "/icons/tunnel.svg",
          title: "Túneles SSH (Local, Remote, SOCKS5)",
          description: "Crea port forwardings locales, remotos y proxies dinámicos SOCKS5 fácilmente organizados. Salta firewalls y accede a bases de datos privadas con un clic.",
          tag: "Seguridad"
        },
        {
          icon: "/icons/cpu.svg",
          title: "Monitoreo Remoto en Vivo",
          description: "Supervisa en tiempo real el consumo de CPU, memoria RAM, disco y tiempo de actividad (uptime) en la barra de estado de cada sesión SSH activa.",
          tag: "Telemetría"
        },
        {
          icon: "/icons/serial.svg",
          title: "Consola Serial & ZMODEM",
          description: "Soporte para hardware UART/RS-232, routers, switches y placas IoT con baudrates configurables, control de flujo y transferencia de ficheros con XMODEM/ZMODEM.",
          tag: "Hardware"
        },
        {
          icon: "/icons/server.svg",
          title: "FTP / FTPS con TLS 1.3",
          description: "Conexiones FTP seguras con validación estricta de certificados, modo pasivo, políticas personalizadas de CA y reconexión automática resiliente.",
          tag: "Redes"
        },
        {
          icon: "/icons/gear.svg",
          title: "100% Nativo en C++17 y Qt 6",
          description: "Cero pesadez de Chromium o Electron. Inicio instantáneo, latencia mínima al teclear y huella de memoria minúscula gracias al motor QTermWidget.",
          tag: "Rendimiento"
        },
        {
          icon: "/icons/theme.svg",
          title: "Temas, Paletas e i18n",
          description: "Modo oscuro y claro automático o forzado, paletas de colores personalizables y soporte multi-idioma nativo para Español, Inglés y Portugués.",
          tag: "Personalización"
        }
      ]
    },
    protocols: {
      tag: "Multiprotocolo Real",
      title: "Todos tus canales de conexión en una sola herramienta",
      desc: "Desde la nube hasta el cable serie de tu rack: administra infraestructura moderna y hardware de telecomunicaciones sin cambiar de ventana.",
      items: [
        {
          id: "ssh",
          title: "SSH & X11",
          badge: "Acceso Remoto",
          desc: "Conexiones SSH robustas basadas en libssh2. Compatible con llaves públicas/privadas (Ed25519, RSA), agentes SSH, contraseñas seguras y reenvío gráfico X11.",
          highlights: [
            "Autenticación con claves protegidas y SSH Agent",
            "Reenvío de aplicaciones gráficas X11 (VcXsrv integrado en Windows)",
            "Gestor de túneles SSH: Local, Remote y Dynamic SOCKS5",
            "Integración con el Secret Service de Linux (GNOME Keyring / KWallet)"
          ]
        },
        {
          id: "sftp",
          title: "SFTP & Transferencia",
          badge: "Explorador de Archivos",
          desc: "Un cliente de archivos remoto siempre a la mano en el panel lateral. Olvídate de abrir un FileZilla o WinSCP separado para modificar configuraciones.",
          highlights: [
            "Navegador de árbol remoto con permisos, dueños y tamaños",
            "Subida y descarga mediante arrastrar y soltar (drag & drop)",
            "Editor integrado para modificar archivos de configuración en el servidor",
            "Operaciones en segundo plano sin interrumpir tu sesión de terminal"
          ]
        },
        {
          id: "serial",
          title: "Consola Serial & ZMODEM",
          badge: "Hardware & IoT",
          desc: "Diseñado para ingenieros de redes, sysadmins y makers que interactúan con switches Cisco, routers Mikrotik, Raspberry Pi o placas de desarrollo.",
          highlights: [
            "Compatible con picocom, screen y minicom",
            "Detección y configuración de puertos COM y /dev/ttyUSB*",
            "Transferencia de firmwares y archivos con XMODEM y YMODEM",
            "Soporte nativo ZMODEM mediante integración con 'sz' (lrzsz)"
          ]
        },
        {
          id: "ftp",
          title: "FTP / FTPS Seguro",
          badge: "Protocolos Clásicos",
          desc: "Acceso a servidores de almacenamiento tradicionales con capa de seguridad moderna criptográfica TLS 1.2 o TLS 1.3.",
          highlights: [
            "Cifrado FTPS moderno y selección de TLS 1.2 o TLS 1.3-only",
            "Validación de CA personalizada y certificados autofirmados",
            "Transferencia recursiva de carpetas y archivos",
            "Reconexión automática resiliente ante caídas de enlace"
          ]
        }
      ]
    },
    comparison: {
  "tag": "La ventaja de lo nativo",
  "title": "Abre la app. Conecta. Trabaja.",
  "desc1": "BanchoXterm lleva tus conexiones al escritorio con C++17 y Qt 6. La interfaz se ejecuta directamente en tu equipo, sin un navegador ni un servidor web intermediario.",
  "desc2": "Dedica tu tiempo a tus servidores: sin instalar Docker, mantener contenedores ni configurar puertos web, volúmenes o claves de cifrado para desplegar la herramienta.",
  "tableHeaderFeature": "Característica",
  "tableHeaderApp": "BanchoXterm",
  "tableHeaderOther": "Solución web en Docker",
  "rows": [
    [
      "Interfaz",
      "Nativa: C++17 + Qt 6",
      "Navegador + backend + motor de conexiones"
    ],
    [
      "Infraestructura adicional",
      "Sin Docker ni servidor web",
      "Docker y un servicio web activo"
    ],
    [
      "Configuración inicial",
      "Abre la app y crea tu sesión",
      "Contenedor, red, volumen y clave de cifrado"
    ],
    [
      "Acceso a tu red y VPN",
      "Desde tu equipo, directamente",
      "Desde la red del servidor o contenedor"
    ],
    [
      "Disponibilidad",
      "Abre la app cuando la necesites",
      "Depende del servicio web y su motor"
    ],
    [
      "Mantenimiento de la herramienta",
      "Actualiza la aplicación",
      "Actualiza imágenes y administra el despliegue"
    ]
  ]
},
    install: {
      tag: "Descarga e Instalación",
      title: "Comienza a usar BanchoXterm hoy",
      desc: "Descarga los ejecutables binarios para Windows o compila directamente en tu distribución de Linux preferida con unos pocos comandos.",
      windowsTitle: "Windows 10 / 11",
      windowsMeta: "x64 y ARM64 • Binarios listos",
      windowsDesc: "Incluye soporte para ConPTY, integración con servidor X11 (VcXsrv) y almacenamiento de credenciales protegido por DPAPI.",
      btnInstaller: "Instalador (.exe)",
      btnPortable: "Versión Portable (.zip)",
      linuxTitle: "Linux (Ubuntu / Fedora / Arch)",
      linuxMeta: "Compilación Nativa con Ninja & CMake",
      linuxDesc: "Compatible con Secret Service (GNOME Keyring / KWallet), QTermWidget nativo y terminales del sistema de alta resolución.",
      btnViewLinux: "Ver Instrucciones de Build",
      btnGithubGuide: "Guía en GitHub",
      copyText: "Copiar",
      stepDeps: "1. Instalar dependencias necesarias",
      stepClone: "2. Clonar el repositorio y submódulos",
      stepBuild: "3. Compilar y ejecutar",
      msvcStep: "Compilar con MSVC en Developer Command Prompt"
    },
    footer: {
      desc: "Emulador de terminal multiprotocolo y gestor de sesiones remotas nativo para Linux y Windows. Creado con C++17 y Qt 6.",
      license: "Licenciado bajo GNU GPL v3.0",
      projectTitle: "Proyecto",
      repo: "Repositorio GitHub",
      releases: "Lanzamientos & Binarios",
      issues: "Reportar un error",
      licenseLink: "Licencia GPL-3.0",
      protocolsTitle: "Protocolos",
      featuresTitle: "Funcionalidades",
      copyright: "BanchoXterm. Proyecto de código abierto mantenido por",
      madeWith: "Hecho con ❤️ y Astro"
    }
  },

  en: {
    site: {
      title: "BanchoXterm — Multi-protocol Terminal Emulator & Session Manager",
      description: "High-performance multi-protocol terminal emulator built in C++17 and Qt 6 for Linux and Windows. Supports SSH, SFTP, FTP/FTPS, Serial with ZMODEM, SSH tunnels, and multi-input without Electron."
    },
    nav: {
      features: "Features",
      protocols: "Protocols",
      preview: "Preview",
      install: "Installation",
      download: "Download",
      langSelect: "Language"
    },
    hero: {
      badgeOpenSource: "Open Source • GPL v3",
      badgeNoElectron: "No Electron: 100% Native C++17 & Qt 6",
      titleLine1: "The definitive",
      titleLine2: "multi-protocol terminal",
      subtitle: "Connect to remote infrastructure with maximum speed and control. SSH with X11, integrated SFTP file manager, secure FTPS, Serial console with ZMODEM, SSH tunnels, and simultaneous multi-input in a sleek interface for Linux and Windows.",
      ctaDownload: "Download BanchoXterm",
      ctaGitHub: "View on GitHub",
      quickCloneLabel: "Clone and build:",
      copied: "✓ Copied",
      mockup: {
        tabSsh: "prod-app-01 (SSH)",
        tabSftp: "SFTP: /var/www",
        tabSerial: "Serial: /dev/ttyUSB0",
        tabMulti: "Multi-Input (3 servers)",
        sidebarTitle: "SAVED SESSIONS",
        cloudCluster: "Cloud Cluster",
        networking: "Networking & Lab",
        statusOnline: "SSH: CONNECTED",
        cpu: "CPU",
        ram: "RAM",
        disk: "DISK",
        uptime: "UPTIME"
      }
    },
    features: {
      tag: "Key Capabilities",
      title: "Everything a SysAdmin & Developer Needs",
      desc: "Engineered to replace sluggish, fragmented tools with a fast, reliable, all-in-one native terminal solution.",
      items: [
        {
          icon: "/icons/terminal.svg",
          title: "Multi-Tab & Split Terminal",
          description: "Organize multiple local and remote sessions at once. Open independent tabs or split screens horizontally and vertically to compare logs side by side.",
          tag: "Productivity"
        },
        {
          icon: "/icons/multiinput.svg",
          title: "Multi-Input Broadcast",
          description: "Type once and send keystrokes to all open terminals simultaneously. Ideal for patching clusters, restarting services, or querying status across machines.",
          tag: "Orchestration"
        },
        {
          icon: "/icons/file.svg",
          title: "Integrated SFTP Browser",
          description: "Browse remote directory trees in the sidebar, transfer files with drag-and-drop, manage permissions, and edit remote configuration files in-place.",
          tag: "Files"
        },
        {
          icon: "/icons/tunnel.svg",
          title: "SSH Tunnels (Local, Remote, SOCKS5)",
          description: "Easily configure local, remote, and dynamic SOCKS5 port forwarding. Bypass firewalls and securely reach private cluster databases with one click.",
          tag: "Security"
        },
        {
          icon: "/icons/cpu.svg",
          title: "Live Remote System Monitoring",
          description: "Monitor CPU load, RAM utilization, disk capacity, and uptime in real time right in the status bar of every connected SSH session.",
          tag: "Telemetry"
        },
        {
          icon: "/icons/serial.svg",
          title: "Serial Console & ZMODEM",
          description: "Direct connection to UART/RS-232 hardware, network switches, and IoT microcontrollers with configurable baudrates and XMODEM/ZMODEM file transfers.",
          tag: "Hardware"
        },
        {
          icon: "/icons/server.svg",
          title: "FTP / FTPS with TLS 1.3",
          description: "Secure FTP connections with strict certificate validation, passive mode, custom CA bundles, and resilient auto-reconnect on network drops.",
          tag: "Networking"
        },
        {
          icon: "/icons/gear.svg",
          title: "100% Native C++17 & Qt 6",
          description: "Zero Chromium or Electron bloat. Instant startup, ultra-low typing latency, and minimal RAM footprint powered by an in-process QTermWidget engine.",
          tag: "Performance"
        },
        {
          icon: "/icons/theme.svg",
          title: "Themes, Palettes & i18n",
          description: "Dark and light interface modes, customizable terminal color schemes, and native internationalization in English, Spanish, and Portuguese.",
          tag: "Customization"
        }
      ]
    },
    protocols: {
      tag: "True Multi-Protocol",
      title: "All Your Connection Channels in One Tool",
      desc: "From cloud clusters to serial console cables in your server rack: administer modern infrastructure and network hardware without switching apps.",
      items: [
        {
          id: "ssh",
          title: "SSH & X11",
          badge: "Remote Access",
          desc: "Robust SSH connections powered by libssh2. Supports public/private keys (Ed25519, RSA), SSH agents, secure passwords, and graphical X11 forwarding.",
          highlights: [
            "Protected key-based auth and SSH Agent integration",
            "X11 GUI forwarding (auto-launches companion VcXsrv on Windows)",
            "Built-in SSH tunnel manager: Local, Remote, and Dynamic SOCKS5",
            "Native Linux Secret Service integration (GNOME Keyring / KWallet)"
          ]
        },
        {
          id: "sftp",
          title: "SFTP & Transfer",
          badge: "File Explorer",
          desc: "A remote file manager always available in the sidebar. No need to launch separate FTP tools like FileZilla or WinSCP just to tweak a config file.",
          highlights: [
            "Remote tree browser displaying permissions, ownership, and sizes",
            "Upload and download via seamless drag & drop within the app",
            "In-app editor to modify server configuration files directly",
            "Background transfer engine without interrupting active shell sessions"
          ]
        },
        {
          id: "serial",
          title: "Serial Console & ZMODEM",
          badge: "Hardware & IoT",
          desc: "Built for network engineers, sysadmins, and embedded developers working with Cisco switches, Mikrotik routers, or dev boards.",
          highlights: [
            "Compatible with picocom, screen, and minicom workflows",
            "Hardware COM port and /dev/ttyUSB* autodetect with baudrate picker",
            "Direct firmware and file transfer via XMODEM and YMODEM",
            "Native ZMODEM file sending through integrated 'sz' (lrzsz)"
          ]
        },
        {
          id: "ftp",
          title: "FTP / Secure FTPS",
          badge: "Classic Protocols",
          desc: "Access traditional file storage servers backed by modern cryptographic layers with TLS 1.2 and TLS 1.3 support.",
          highlights: [
            "Modern FTPS encryption with TLS 1.2 or TLS 1.3-only options",
            "Custom CA bundle validation and self-signed certificate policies",
            "Recursive folder transfer, queue pausing, and cancellation",
            "Resilient automatic reconnect handling unstable networks"
          ]
        }
      ]
    },
    comparison: {
  "tag": "The native advantage",
  "title": "Open the app. Connect. Work.",
  "desc1": "BanchoXterm brings your connections to the desktop with C++17 and Qt 6. The interface runs directly on your computer, without a browser or an intermediary web server.",
  "desc2": "Spend your time on your servers: no Docker installation, containers to maintain, or web ports, volumes and encryption keys to configure for deploying the tool.",
  "tableHeaderFeature": "Feature",
  "tableHeaderApp": "BanchoXterm",
  "tableHeaderOther": "Docker-based web solution",
  "rows": [
    [
      "Interface",
      "Native: C++17 + Qt 6",
      "Browser + backend + connection engine"
    ],
    [
      "Additional infrastructure",
      "No Docker or web server",
      "Docker and a running web service"
    ],
    [
      "Initial setup",
      "Open the app and create your session",
      "Container, networking, volume and encryption key"
    ],
    [
      "Your network and VPN",
      "Directly from your computer",
      "From the server or container network"
    ],
    [
      "Availability",
      "Open the app when you need it",
      "Depends on the web service and its engine"
    ],
    [
      "Tool maintenance",
      "Update the application",
      "Update images and manage the deployment"
    ]
  ]
},
    install: {
      tag: "Download & Install",
      title: "Start using BanchoXterm today",
      desc: "Download pre-built binary installers for Windows or build directly on your favorite Linux distribution in just a few commands.",
      windowsTitle: "Windows 10 / 11",
      windowsMeta: "x64 & ARM64 • Pre-built binaries",
      windowsDesc: "Features ConPTY terminal bridging, X11 companion server integration (VcXsrv), and DPAPI-encrypted password storage.",
      btnInstaller: "Installer (.exe)",
      btnPortable: "Portable (.zip)",
      linuxTitle: "Linux (Ubuntu / Fedora / Arch)",
      linuxMeta: "Native Build with Ninja & CMake",
      linuxDesc: "Integrates with Linux Secret Service (GNOME Keyring / KWallet), native QTermWidget rendering, and system shells.",
      btnViewLinux: "View Build Instructions",
      btnGithubGuide: "GitHub Guide",
      copyText: "Copy",
      stepDeps: "1. Install required dependencies",
      stepClone: "2. Clone repository & submodules",
      stepBuild: "3. Configure, build & run",
      msvcStep: "Build with MSVC in Developer Command Prompt"
    },
    footer: {
      desc: "Native multi-protocol terminal emulator and remote session manager for Linux and Windows. Built with C++17 and Qt 6.",
      license: "Licensed under GNU GPL v3.0",
      projectTitle: "Project",
      repo: "GitHub Repository",
      releases: "Releases & Binaries",
      issues: "Report an issue",
      licenseLink: "GPL-3.0 License",
      protocolsTitle: "Protocols",
      featuresTitle: "Features",
      copyright: "BanchoXterm. Open source project maintained by",
      madeWith: "Built with ❤️ and Astro"
    }
  },

  pt: {
    site: {
      title: "BanchoXterm — Emulador de Terminal Multiprotocolo & Gerenciador de Sessões",
      description: "Emulador de terminal multiprotocolo de alto desempenho em C++17 e Qt 6 para Linux e Windows. Suporta SSH, SFTP, FTP/FTPS, Serial com ZMODEM, túneis SSH e multi-input sem Electron."
    },
    nav: {
      features: "Recursos",
      protocols: "Protocolos",
      preview: "Pré-visualização",
      install: "Instalação",
      download: "Baixar",
      langSelect: "Idioma"
    },
    hero: {
      badgeOpenSource: "Código Aberto • GPL v3",
      badgeNoElectron: "Sem Electron: 100% Nativo em C++17 & Qt 6",
      titleLine1: "O emulador de terminal",
      titleLine2: "multiprotocolo definitivo",
      subtitle: "Conecte-se aos seus servidores remotos com máxima velocidade e controle. SSH com X11, explorador SFTP integrado, FTPS seguro, console Serial com ZMODEM, túneis SSH e multi-input simultâneo em uma interface moderna para Linux e Windows.",
      ctaDownload: "Baixar BanchoXterm",
      ctaGitHub: "Ver no GitHub",
      quickCloneLabel: "Clonar e compilar:",
      copied: "✓ Copiado",
      mockup: {
        tabSsh: "prod-app-01 (SSH)",
        tabSftp: "SFTP: /var/www",
        tabSerial: "Serial: /dev/ttyUSB0",
        tabMulti: "Multi-Input (3 servidores)",
        sidebarTitle: "SESSÕES SALVAS",
        cloudCluster: "Cloud Cluster",
        networking: "Networking & Lab",
        statusOnline: "SSH: CONECTADO",
        cpu: "CPU",
        ram: "RAM",
        disk: "DISCO",
        uptime: "UPTIME"
      }
    },
    features: {
      tag: "Principais Recursos",
      title: "Tudo o que um SysAdmin e Desenvolvedor precisa",
      desc: "Projetado para substituir ferramentas pesadas e fragmentadas por uma solução nativa integrada, rápida e confiável.",
      items: [
        {
          icon: "/icons/terminal.svg",
          title: "Terminal Multi-Abas e Split",
          description: "Organize várias sessões locais e remotas ao mesmo tempo. Abra abas independentes ou divida a tela lado a lado para comparar logs e shells.",
          tag: "Produtividade"
        },
        {
          icon: "/icons/multiinput.svg",
          title: "Multi-Input Broadcast",
          description: "Envie comandos de teclado para todos os terminais abertos simultaneamente. Ideal para atualizar pacotes, reiniciar clusters ou checar status em lote.",
          tag: "Orquestração"
        },
        {
          icon: "/icons/file.svg",
          title: "Explorador SFTP Integrado",
          description: "Navegue pela árvore de arquivos remota no painel lateral, envie/baixe via drag & drop, gerencie permissões e edite arquivos de texto sem sair do app.",
          tag: "Arquivos"
        },
        {
          icon: "/icons/tunnel.svg",
          title: "Túneis SSH (Local, Remote, SOCKS5)",
          description: "Configure port forwardings locais, remotos e proxies dinâmicos SOCKS5 facilmente. Supere firewalls e acesse bancos de dados privados com um clique.",
          tag: "Segurança"
        },
        {
          icon: "/icons/cpu.svg",
          title: "Monitoramento Remoto em Tempo Real",
          description: "Acompanhe em tempo real o uso de CPU, memória RAM, capacidade de disco e uptime diretamente na barra de status de cada sessão SSH conectada.",
          tag: "Telemetria"
        },
        {
          icon: "/icons/serial.svg",
          title: "Console Serial & ZMODEM",
          description: "Suporte para hardware UART/RS-232, switches de rede, roteadores e placas IoT com baudrates ajustáveis e transferências de arquivos via XMODEM/ZMODEM.",
          tag: "Hardware"
        },
        {
          icon: "/icons/server.svg",
          title: "FTP / FTPS com TLS 1.3",
          description: "Conexões FTP seguras com validação rigorosa de certificados, modo passivo, suporte a pacotes CA customizados e reconexão automática resiliente.",
          tag: "Redes"
        },
        {
          icon: "/icons/gear.svg",
          title: "100% Nativo em C++17 e Qt 6",
          description: "Zero peso de Chromium ou Electron. Inicialização instantânea, latência mínima ao digitar e consumo ínfimo de memória graças ao QTermWidget.",
          tag: "Desempenho"
        },
        {
          icon: "/icons/theme.svg",
          title: "Temas, Paletas e i18n",
          description: "Modo escuro e claro automático ou manual, esquemas de cores de terminal personalizáveis e suporte nativo a Português, Espanhol e Inglês.",
          tag: "Personalização"
        }
      ]
    },
    protocols: {
      tag: "Multiprotocolo Real",
      title: "Todos os seus canais de conexão em uma única ferramenta",
      desc: "Da nuvem ao cabo serial do seu rack: administre infraestrutura moderna e dispositivos de rede sem precisar alternar entre vários programas.",
      items: [
        {
          id: "ssh",
          title: "SSH & X11",
          badge: "Acesso Remoto",
          desc: "Conexões SSH robustas com libssh2. Compatível com chaves públicas/privadas (Ed25519, RSA), agentes SSH, senhas e encaminhamento gráfico X11.",
          highlights: [
            "Autenticação com chaves protegidas e agente SSH",
            "Encaminhamento de aplicações gráficas X11 (VcXsrv integrado no Windows)",
            "Gerenciador de túneis SSH: Local, Remote e Dynamic SOCKS5",
            "Integração com o Secret Service do Linux (GNOME Keyring / KWallet)"
          ]
        },
        {
          id: "sftp",
          title: "SFTP & Transferência",
          badge: "Explorador de Arquivos",
          desc: "Um cliente de arquivos remoto sempre disponível na barra lateral. Chega de abrir FileZilla ou WinSCP apenas para ajustar um arquivo de configuração.",
          highlights: [
            "Árvore remota exibindo permissões, proprietários e tamanhos",
            "Upload e download com suporte a arrastar e soltar (drag & drop)",
            "Editor integrado para modificar arquivos no servidor diretamente",
            "Operações em segundo plano sem interromper seu terminal ativo"
          ]
        },
        {
          id: "serial",
          title: "Console Serial & ZMODEM",
          badge: "Hardware & IoT",
          desc: "Feito sob medida para engenheiros de rede, sysadmins e makers que lidam com switches Cisco, roteadores Mikrotik, Raspberry Pi ou placas embarcadas.",
          highlights: [
            "Compatível com comandos de picocom, screen e minicom",
            "Detecção e ajuste de portas COM e /dev/ttyUSB*",
            "Transferência de firmwares e arquivos via XMODEM e YMODEM",
            "Envio nativo ZMODEM através da integração com 'sz' (lrzsz)"
          ]
        },
        {
          id: "ftp",
          title: "FTP / FTPS Seguro",
          badge: "Protocolos Clássicos",
          desc: "Conecte-se a servidores de arquivos tradicionais com proteção moderna de criptografia TLS 1.2 ou TLS 1.3.",
          highlights: [
            "Criptografia FTPS moderna com opção para TLS 1.2 ou TLS 1.3 exclusivo",
            "Validação de certificados CA personalizados e certificados autoassinados",
            "Transferência recursiva de pastas e arquivos com pausa e cancelamento",
            "Reconexão automática tolerante a oscilações de link"
          ]
        }
      ]
    },
    comparison: {
  "tag": "A vantagem do nativo",
  "title": "Abra o app. Conecte. Trabalhe.",
  "desc1": "O BanchoXterm traz suas conexões para o desktop com C++17 e Qt 6. A interface funciona diretamente no seu computador, sem navegador ou servidor web intermediário.",
  "desc2": "Dedique seu tempo aos seus servidores: sem instalar Docker, manter contêineres ou configurar portas web, volumes e chaves de criptografia para implantar a ferramenta.",
  "tableHeaderFeature": "Recurso",
  "tableHeaderApp": "BanchoXterm",
  "tableHeaderOther": "Solução web em Docker",
  "rows": [
    [
      "Interface",
      "Nativa: C++17 + Qt 6",
      "Navegador + backend + motor de conexões"
    ],
    [
      "Infraestrutura adicional",
      "Sem Docker ou servidor web",
      "Docker e um serviço web ativo"
    ],
    [
      "Configuração inicial",
      "Abra o app e crie sua sessão",
      "Contêiner, rede, volume e chave de criptografia"
    ],
    [
      "Sua rede e VPN",
      "Diretamente do seu computador",
      "Pela rede do servidor ou contêiner"
    ],
    [
      "Disponibilidade",
      "Abra o app quando precisar",
      "Depende do serviço web e do motor"
    ],
    [
      "Manutenção da ferramenta",
      "Atualize o aplicativo",
      "Atualize imagens e administre a implantação"
    ]
  ]
},
    install: {
      tag: "Download e Instalação",
      title: "Comece a usar o BanchoXterm hoje",
      desc: "Baixe instaladores binários para Windows ou compile diretamente na sua distribuição Linux preferida com poucos comandos.",
      windowsTitle: "Windows 10 / 11",
      windowsMeta: "x64 e ARM64 • Binários prontos",
      windowsDesc: "Inclui suporte ConPTY, servidor X11 integrado (VcXsrv) e armazenamento de senhas protegido por DPAPI.",
      btnInstaller: "Instalador (.exe)",
      btnPortable: "Versão Portátil (.zip)",
      linuxTitle: "Linux (Ubuntu / Fedora / Arch)",
      linuxMeta: "Compilação Nativa com Ninja & CMake",
      linuxDesc: "Integrado ao Secret Service (GNOME Keyring / KWallet), renderização QTermWidget nativa e suporte completo a shells do sistema.",
      btnViewLinux: "Ver Instruções de Build",
      btnGithubGuide: "Guia no GitHub",
      copyText: "Copiar",
      stepDeps: "1. Instalar dependências necessárias",
      stepClone: "2. Clonar repositório e submódulos",
      stepBuild: "3. Compilar e executar",
      msvcStep: "Compilar com MSVC no Developer Command Prompt"
    },
    footer: {
      desc: "Emulador de terminal multiprotocolo e gerenciador de sessões remotas nativo para Linux e Windows. Feito com C++17 e Qt 6.",
      license: "Licenciado sob GNU GPL v3.0",
      projectTitle: "Projeto",
      repo: "Repositório GitHub",
      releases: "Lançamentos e Binários",
      issues: "Reportar um problema",
      licenseLink: "Licença GPL-3.0",
      protocolsTitle: "Protocolos",
      featuresTitle: "Recursos",
      copyright: "BanchoXterm. Projeto de código aberto mantido por",
      madeWith: "Feito com ❤️ e Astro"
    }
  }
} as const;

export function useTranslations(locale: Locale = 'es') {
  return translations[locale] || translations.es;
}

export function getRelativeLocaleUrl(locale: Locale, path: string = ''): string {
  const cleanPath = path.startsWith('/') ? path.slice(1) : path;
  if (locale === 'es') {
    return cleanPath ? `/${cleanPath}` : '/';
  }
  return cleanPath ? `/${locale}/${cleanPath}` : `/${locale}`;
}
