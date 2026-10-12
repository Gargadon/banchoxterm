export const installationPaths = {
  es: '/instalacion/', en: '/en/installation/', pt: '/pt/instalacao/'
};

export const installationGuide = {
  es: {
    macos: 'macOS: funciones sin probar', macosNote: 'Ninguna de las características de BanchoXterm ha sido probada en macOS. Si decides compilarlo o usarlo en este sistema, revisa su funcionamiento bajo tu propia responsabilidad.',
    msvc: 'Compilar en Windows con MSVC', msvcRequirements: ['Git y CMake.', 'Visual Studio 2022 o Build Tools con Desarrollo para el escritorio con C++.', 'Qt para MSVC de 64 bits con SerialPort y ActiveQt.'], msvcSteps: ['Abre x64 Native Tools Command Prompt for VS 2022.', 'Descarga el código y los submódulos.', 'Ajusta la ruta de Qt a tu instalación, compila y ejecuta.'],
    title: 'Instalar BanchoXterm',
    description: 'Instalación en Windows y compilación desde el código fuente en Linux, paso a paso.',
    tag: 'GUÍA DE INSTALACIÓN', back: 'Volver al inicio', download: 'Ver descargas en GitHub',
    windows: 'Windows: instalador o portable',
    windowsIntro: 'Abre la página de versiones y descarga el archivo de Windows correspondiente a tu arquitectura. Puedes elegir entre el instalador y la edición portable.',
    installer: 'Con instalador', installerSteps: ['Descarga el instalador .exe desde GitHub Releases.', 'Ejecuta el instalador y sigue los pasos del asistente.', 'Abre BanchoXterm desde el menú Inicio.'],
    portable: 'Edición portable', portableSteps: ['Descarga el archivo ZIP portable de Windows.', 'Extrae todo el contenido en una carpeta donde tengas permisos de escritura.', 'Ejecuta banchoxterm.exe desde esa carpeta. Conserva los archivos y subcarpetas incluidos.'],
    portableNote: 'La edición portable guarda sesiones, ajustes y hosts conocidos junto al ejecutable. Estos datos no se comparten con la versión instalada.',
    linux: 'Linux: compilar desde el código fuente',
    linuxIntro: 'El proyecto todavía no publica paquetes Linux. Necesitas Git, CMake 3.16 o posterior, un compilador C++17, Qt 6.6 o posterior y las dependencias de FreeRDP 3 y WinPR 3.',
    dependencies: '1. Instala las dependencias', distroNote: 'Usa una distribución cuyos repositorios incluyan las versiones requeridas de Qt y FreeRDP. Si el gestor no encuentra un paquete, comprueba la versión de tu distribución.',
    clone: '2. Descarga el código y los submódulos', cloneNote: 'El submódulo QTermWidget es necesario. El enlace HTTPS permite clonar sin configurar una llave SSH de GitHub.',
    build: '3. Compila y ejecuta', buildNote: 'CMake descarga las bibliotecas incluidas mediante FetchContent, por lo que la primera compilación necesita conexión a Internet. Ejecuta estos comandos dentro de la carpeta banchoxterm.',
    credentials: 'Guardar contraseñas en Linux', credentialsNote: 'Necesitas secret-tool y un proveedor Secret Service activo en tu sesión de escritorio. Usa el gestor de tu escritorio; si no tienes uno, puedes instalar GNOME Keyring o un KWallet compatible. No es necesario instalar ambos.',
    optional: 'Opcional: X11 y consola serie', optionalNote: 'Para X11 en Windows puedes usar VcXsrv, Xming o X410. Para enviar archivos por ZMODEM en sesiones serie, instala lrzsz en Linux.',
    first: 'Primera conexión', firstSteps: ['Crea una sesión SSH e indica el host, puerto y usuario del servidor.', 'Selecciona tu llave privada o introduce la contraseña. Puedes guardarla en el gestor de credenciales.', 'Comprueba la huella del servidor antes de confiar en él y conecta.'],
    help: 'Si la compilación falla', helpNote: 'Un error sobre freerdp3 o winpr3 suele indicar que faltan las dependencias de desarrollo de FreeRDP 3. Si falta QTermWidget, inicializa los submódulos. Para informar de otro error, incluye el sistema operativo, el comando ejecutado y su salida.',
    readme: 'Consultar el README', issues: 'Informar de un problema', guideLink: 'Guía completa de instalación'
  },
  en: {
    macos: 'macOS: untested features', macosNote: 'None of the BanchoXterm features have been tested on macOS. If you choose to build or use it on this system, evaluate its behavior at your own risk.',
    msvc: 'Build on Windows with MSVC', msvcRequirements: ['Git and CMake.', 'Visual Studio 2022 or Build Tools with Desktop development with C++.', '64-bit MSVC Qt with SerialPort and ActiveQt.'], msvcSteps: ['Open x64 Native Tools Command Prompt for VS 2022.', 'Download the source and submodules.', 'Adjust the Qt path to your installation, build and run.'],
    title: 'Install BanchoXterm',
    description: 'Step-by-step Windows installation and building from source on Linux.',
    tag: 'INSTALLATION GUIDE', back: 'Back to home', download: 'View downloads on GitHub',
    windows: 'Windows: installer or portable', windowsIntro: 'Open the releases page and download the Windows file for your architecture. Choose the installer or the portable edition.',
    installer: 'Using the installer', installerSteps: ['Download the .exe installer from GitHub Releases.', 'Run the installer and follow the setup wizard.', 'Open BanchoXterm from the Start menu.'],
    portable: 'Portable edition', portableSteps: ['Download the Windows portable ZIP.', 'Extract all files into a folder where you have write permission.', 'Run banchoxterm.exe from that folder. Keep the bundled files and subfolders.'],
    portableNote: 'The portable edition stores sessions, settings and known hosts next to the executable. These files are separate from the installed edition.',
    linux: 'Linux: build from source', linuxIntro: 'The project does not publish Linux packages yet. You need Git, CMake 3.16 or later, a C++17 compiler, Qt 6.6 or later, and the FreeRDP 3 and WinPR 3 development dependencies.',
    dependencies: '1. Install dependencies', distroNote: 'Use a distribution whose repositories provide the required Qt and FreeRDP versions. If a package is unavailable, check your distribution version.',
    clone: '2. Get the source and submodules', cloneNote: 'The QTermWidget submodule is required. The HTTPS URL lets you clone without setting up a GitHub SSH key.',
    build: '3. Build and run', buildNote: 'CMake downloads bundled libraries through FetchContent, so the first build needs Internet access. Run these commands inside the banchoxterm folder.',
    credentials: 'Save passwords on Linux', credentialsNote: 'You need secret-tool and an active Secret Service provider in your desktop session. Use your desktop’s credential manager; if you do not have one, install GNOME Keyring or a compatible KWallet provider. You do not need both.',
    optional: 'Optional: X11 and serial console', optionalNote: 'For X11 on Windows, you can use VcXsrv, Xming or X410. To send files over ZMODEM in serial sessions, install lrzsz on Linux.',
    first: 'Your first connection', firstSteps: ['Create an SSH session and enter the server host, port and username.', 'Select your private key or enter a password. You can save it in the credential manager.', 'Verify the server fingerprint before trusting it, then connect.'],
    help: 'If the build fails', helpNote: 'An error mentioning freerdp3 or winpr3 usually means the FreeRDP 3 development dependencies are missing. If QTermWidget is missing, initialize the submodules. When reporting another error, include your operating system, the command and its output.',
    readme: 'Read the README', issues: 'Report a problem', guideLink: 'Full installation guide'
  },
  pt: {
    macos: 'macOS: funcionalidades não testadas', macosNote: 'Nenhuma das funcionalidades do BanchoXterm foi testada no macOS. Se decidir compilar ou usar o programa nesse sistema, avalie seu funcionamento por sua conta e risco.',
    msvc: 'Compilar no Windows com MSVC', msvcRequirements: ['Git e CMake.', 'Visual Studio 2022 ou Build Tools com Desenvolvimento para desktop com C++.', 'Qt para MSVC de 64 bits com SerialPort e ActiveQt.'], msvcSteps: ['Abra x64 Native Tools Command Prompt for VS 2022.', 'Baixe o código e os submódulos.', 'Ajuste o caminho do Qt à sua instalação, compile e execute.'],
    title: 'Instalar o BanchoXterm', description: 'Instalação no Windows e compilação a partir do código-fonte no Linux, passo a passo.',
    tag: 'GUIA DE INSTALAÇÃO', back: 'Voltar ao início', download: 'Ver downloads no GitHub',
    windows: 'Windows: instalador ou portátil', windowsIntro: 'Abra a página de versões e baixe o arquivo do Windows correspondente à sua arquitetura. Escolha o instalador ou a edição portátil.',
    installer: 'Com instalador', installerSteps: ['Baixe o instalador .exe no GitHub Releases.', 'Execute o instalador e siga as etapas do assistente.', 'Abra o BanchoXterm pelo menu Iniciar.'],
    portable: 'Edição portátil', portableSteps: ['Baixe o ZIP portátil para Windows.', 'Extraia todos os arquivos em uma pasta com permissão de escrita.', 'Execute banchoxterm.exe nessa pasta. Mantenha os arquivos e subpastas incluídos.'],
    portableNote: 'A edição portátil salva sessões, configurações e hosts conhecidos junto ao executável. Esses dados são separados dos da versão instalada.',
    linux: 'Linux: compilar a partir do código-fonte', linuxIntro: 'O projeto ainda não publica pacotes Linux. Você precisa de Git, CMake 3.16 ou posterior, um compilador C++17, Qt 6.6 ou posterior e das dependências de desenvolvimento FreeRDP 3 e WinPR 3.',
    dependencies: '1. Instale as dependências', distroNote: 'Use uma distribuição cujos repositórios forneçam as versões necessárias do Qt e do FreeRDP. Se um pacote não estiver disponível, verifique a versão da distribuição.',
    clone: '2. Baixe o código e os submódulos', cloneNote: 'O submódulo QTermWidget é necessário. O endereço HTTPS permite clonar sem configurar uma chave SSH do GitHub.',
    build: '3. Compile e execute', buildNote: 'O CMake baixa as bibliotecas incluídas por meio do FetchContent. Execute estes comandos dentro da pasta banchoxterm.',
    credentials: 'Salvar senhas no Linux', credentialsNote: 'Você precisa do secret-tool e de um provedor Secret Service ativo na sessão do desktop. Use o gerenciador do seu desktop; se não tiver um, instale o GNOME Keyring ou um provedor KWallet compatível. Não é necessário instalar ambos.',
    optional: 'Opcional: X11 e console serial', optionalNote: 'Para X11 no Windows, você pode usar VcXsrv, Xming ou X410. Para enviar arquivos por ZMODEM em sessões seriais, instale o lrzsz no Linux.',
    first: 'Primeira conexão', firstSteps: ['Crie uma sessão SSH e informe o host, a porta e o usuário do servidor.', 'Selecione sua chave privada ou informe a senha. Você pode salvá-la no gerenciador de credenciais.', 'Confira a impressão digital do servidor antes de confiar nele e conecte.'],
    help: 'Se a compilação falhar', helpNote: 'Um erro sobre freerdp3 ou winpr3 costuma indicar que faltam as dependências de desenvolvimento do FreeRDP 3. Se faltar o QTermWidget, inicialize os submódulos. Para relatar outro erro, inclua o sistema operacional, o comando e sua saída.',
    readme: 'Consultar o README', issues: 'Relatar um problema', guideLink: 'Guia completo de instalação'
  }
};
