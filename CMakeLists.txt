cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(banchoxterm VERSION 1.0.0 LANGUAGES C CXX)

option(BANCHO_ENABLE_ASAN "Enable AddressSanitizer for memory debugging" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network SerialPort Test LinguistTools)

include(FetchContent)

# Fetch libssh2
if(WIN32)
    set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
endif()
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libssh2
    GIT_REPOSITORY https://github.com/libssh2/libssh2.git
    GIT_TAG        libssh2-1.11.1
)
FetchContent_MakeAvailable(libssh2)

# Standard password-based encryption primitives used by the credential vault.
# The version is pinned so clean builds remain reproducible.
set(SODIUM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    libsodium
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG        1.0.20-RELEASE
)
FetchContent_MakeAvailable(libsodium)

# libsodium's CMake build generates version.h in a platform-specific location,
# while sodium.h includes it as sodium/version.h. Normalize that layout for all
# consumers of the library.
set(BANCHO_SODIUM_GENERATED_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/generated-sodium")
file(MAKE_DIRECTORY "${BANCHO_SODIUM_GENERATED_INCLUDE}/sodium")
if(WIN32)
configure_file(
        "${libsodium_SOURCE_DIR}/builds/msvc/version.h"
        "${BANCHO_SODIUM_GENERATED_INCLUDE}/sodium/version.h"
        COPYONLY
    )
else()
    # libsodium is an Autotools project and does not generate this header when
    # consumed through FetchContent.  Generate it from the pinned release's
    # template so sodium.h can be included by the sources built below.
    set(BANCHO_SODIUM_VERSION "1.0.20")
    set(BANCHO_PROJECT_VERSION "${PROJECT_VERSION}")
    set(VERSION "${BANCHO_SODIUM_VERSION}")
    set(SODIUM_LIBRARY_VERSION_MAJOR 26)
    set(SODIUM_LIBRARY_VERSION_MINOR 2)
    set(SODIUM_LIBRARY_MINIMAL_DEF "")
    configure_file(
        "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h.in"
        "${BANCHO_SODIUM_GENERATED_INCLUDE}/sodium/version.h"
        COPYONLY
    )
    file(READ
        "${BANCHO_SODIUM_GENERATED_INCLUDE}/sodium/version.h"
        BANCHO_SODIUM_VERSION_HEADER
    )
    string(CONFIGURE "${BANCHO_SODIUM_VERSION_HEADER}"
        BANCHO_SODIUM_VERSION_HEADER @ONLY
    )
    file(WRITE
        "${BANCHO_SODIUM_GENERATED_INCLUDE}/sodium/version.h"
        "${BANCHO_SODIUM_VERSION_HEADER}"
    )
    set(VERSION "${BANCHO_PROJECT_VERSION}")
    set(PROJECT_VERSION "${BANCHO_PROJECT_VERSION}")
    unset(BANCHO_PROJECT_VERSION)
endif()
configure_file(
    "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/export.h"
    "${BANCHO_SODIUM_GENERATED_INCLUDE}/sodium/export.h"
    COPYONLY
)

file(GLOB_RECURSE BANCHO_SODIUM_SOURCES CONFIGURE_DEPENDS
    "${libsodium_SOURCE_DIR}/src/libsodium/*.c"
)
add_library(sodium STATIC ${BANCHO_SODIUM_SOURCES})
target_include_directories(sodium PUBLIC
    ${libsodium_SOURCE_DIR}/src/libsodium/include
    ${BANCHO_SODIUM_GENERATED_INCLUDE}
)
target_include_directories(sodium PRIVATE ${libsodium_SOURCE_DIR}/src/libsodium)
target_include_directories(sodium PRIVATE ${libsodium_SOURCE_DIR}/src/libsodium/include/sodium)
target_include_directories(sodium PRIVATE ${BANCHO_SODIUM_GENERATED_INCLUDE}/sodium)
if(WIN32)
    target_include_directories(sodium PRIVATE ${libsodium_SOURCE_DIR}/builds/msvc)
endif()
if(WIN32)
    target_compile_definitions(sodium PRIVATE SODIUM_STATIC)
endif()

# Include directories (target-scoped so third-party targets don't see src/)
# include_directories removed: use target_include_directories below.

# Optional: embedded VNC client via libvncclient (libvncserver project).
option(BANCHO_ENABLE_VNC "Enable the embedded VNC client" ON)

# Source files
set(SOURCES
    src/apppaths.cpp
    src/apppaths.h
    src/keyring.cpp
    src/keyring.h
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/session.cpp
    src/session.h
    src/sessiondialog.cpp
    src/sessiondialog.h
    src/sessionssidebar.cpp
    src/sessionssidebar.h
    src/sftpsidebar.cpp
    src/sftpsidebar.h
    src/terminaltab.cpp
    src/terminaltab.h
    src/remoteeditordialog.cpp
    src/remoteeditordialog.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/sshconnection.cpp
    src/sshconnection.h
    src/ftpclient.cpp
    src/ftpclient.h
    src/sshtunnel.cpp
    src/sshtunnel.h
    src/masterpasswordmanager.cpp
    src/masterpasswordmanager.h
    src/updater.cpp
    src/updater.h
    src/vtterminalwidget.cpp
    src/vtterminalwidget.h
    resources.qrc
)

if(BANCHO_ENABLE_VNC)
    list(APPEND SOURCES
        src/vncclientwidget.cpp
        src/vncclientwidget.h
    )
endif()

add_executable(banchoxterm ${SOURCES})

target_include_directories(banchoxterm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(banchoxterm PRIVATE
    ${libsodium_SOURCE_DIR}/src/libsodium/include
    ${BANCHO_SODIUM_GENERATED_INCLUDE}
)
target_compile_definitions(banchoxterm PRIVATE SODIUM_STATIC)

set(BANCHO_VERSION "${PROJECT_VERSION}")
target_compile_definitions(banchoxterm PRIVATE BANCHO_VERSION="${BANCHO_VERSION}")

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set_property(TARGET banchoxterm PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(MSVC)
    target_compile_options(banchoxterm PRIVATE $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot>)
else()
    target_compile_options(banchoxterm PRIVATE $<$<CONFIG:Release>:-O3>)
endif()

# Generate the version file consumed by the NSIS installer so the installer,
# the About dialog and the updater all derive the version from project(VERSION).
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/version.nsh.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.nsh
    @ONLY
)

target_link_libraries(banchoxterm
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Network
        Qt6::SerialPort
        libssh2
        sodium
)

# Vendored QTermWidget fork (Gargadon/qtermwidget), built directly from the
# submodule on every platform so Linux and Windows run the exact same
# emulator code.
#
# The fork is used instead of upstream lxqt/qtermwidget because it adds
# startExternal()/feedData() (used to bridge SSH and ConPTY bytes) and keeps
# the app-dir color-schemes fallback enabled on Linux (upstream guards it
# with #ifdef Q_OS_MAC).
#
# The whole app is GPL-2.0-or-later, so QTermWidget (also GPL-2.0-or-later)
# is linked directly into the application binary. On Windows the emulator has
# no PTY: local shells run through ConPTY (conpty.cpp) which feeds the
# emulator via QTermWidget::startExternal()/feedData(). On Unix KPty backs
# the local shells via startShellProgram().
set(QTQ_LIB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qtermwidget/lib)

set(QTQ_SOURCES
    ${QTQ_LIB}/BlockArray.cpp
    ${QTQ_LIB}/ColorScheme.cpp
    ${QTQ_LIB}/Emulation.cpp
    ${QTQ_LIB}/Filter.cpp
    ${QTQ_LIB}/History.cpp
    ${QTQ_LIB}/HistorySearch.cpp
    ${QTQ_LIB}/KeyboardTranslator.cpp
    ${QTQ_LIB}/konsole_wcwidth.cpp
    ${QTQ_LIB}/Pty.cpp
    ${QTQ_LIB}/qtermwidget.cpp
    ${QTQ_LIB}/Screen.cpp
    ${QTQ_LIB}/ScreenWindow.cpp
    ${QTQ_LIB}/SearchBar.cpp
    ${QTQ_LIB}/SearchBar.ui
    ${QTQ_LIB}/Session.cpp
    ${QTQ_LIB}/ShellCommand.cpp
    ${QTQ_LIB}/TerminalCharacterDecoder.cpp
    ${QTQ_LIB}/TerminalDisplay.cpp
    ${QTQ_LIB}/tools.cpp
    ${QTQ_LIB}/Vt102Emulation.cpp
)

if(NOT WIN32)
    # KPty (vendored in the fork) provides the pseudo-terminal used by
    # startShellProgram() for local/telnet/serial sessions on Unix. It is not
    # part of the Windows build: Pty.cpp is a stub there and local shells use
    # ConPTY.
    list(APPEND QTQ_SOURCES
        ${QTQ_LIB}/kprocess.cpp
        ${QTQ_LIB}/kpty.cpp
        ${QTQ_LIB}/kptydevice.cpp
        ${QTQ_LIB}/kptyprocess.cpp
    )
endif()

add_library(qtermwidget6 STATIC ${QTQ_SOURCES})
target_include_directories(qtermwidget6 PUBLIC ${QTQ_LIB})
target_link_libraries(qtermwidget6 PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

# MSVC's AddressSanitizer reports the instruction that performs an invalid
# access. This is intentionally opt-in because it changes the runtime and is
# not suitable for normal release builds.
if(BANCHO_ENABLE_ASAN AND MSVC)
    foreach(bancho_sanitized_target banchoxterm qtermwidget6 libssh2_static sodium)
        if(TARGET ${bancho_sanitized_target})
            target_compile_options(${bancho_sanitized_target} PRIVATE /fsanitize=address /Zi)
            target_link_options(${bancho_sanitized_target} PRIVATE /fsanitize=address /INCREMENTAL:NO)
        endif()
    endforeach()
endif()
target_compile_definitions(qtermwidget6 PRIVATE
    KB_LAYOUT_DIR="${QTQ_LIB}/kb-layouts"
    COLORSCHEMES_DIR="${QTQ_LIB}/color-schemes"
    TRANSLATIONS_DIR="${QTQ_LIB}/translations"
)

if(NOT WIN32)
    target_compile_definitions(qtermwidget6 PRIVATE HAVE_POSIX_OPENPT HAVE_SYS_TIME_H)
else()
    target_compile_definitions(qtermwidget6 PRIVATE _CRT_NONSTDC_NO_DEPRECATE)
endif()

target_link_libraries(banchoxterm PRIVATE qtermwidget6)

# Ship the emulator data folders next to the app binary on every platform so
# the color-schemes list and keyboard layouts are available at runtime.
file(COPY "${QTQ_LIB}/color-schemes" DESTINATION "${CMAKE_BINARY_DIR}")
file(COPY "${QTQ_LIB}/kb-layouts" DESTINATION "${CMAKE_BINARY_DIR}")
file(COPY "${QTQ_LIB}/translations" DESTINATION "${CMAKE_BINARY_DIR}")

if(WIN32)
    target_compile_definitions(banchoxterm PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
    target_link_libraries(banchoxterm PRIVATE advapi32)
endif()

if(WIN32)
    target_sources(banchoxterm PRIVATE
        src/conpty.cpp
        src/conpty.h
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/banchoxterm.rc
    )
endif()

# Optional: embed RDP on Windows via the native Remote Desktop ActiveX control
# (ActiveQt). When ActiveQt is not available we fall back to launching mstsc.exe.
if(WIN32)
    find_package(Qt6 QUIET COMPONENTS AxContainer)
    if(TARGET Qt6::AxContainer)
        target_link_libraries(banchoxterm PRIVATE Qt6::AxContainer)
        target_compile_definitions(banchoxterm PRIVATE BANCHO_HAVE_RDP_AX)
    endif()
endif()

# Optional: embedded VNC client via libvncclient (libvncserver project).
if(BANCHO_ENABLE_VNC)
    set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
    set(WITH_LZO OFF CACHE BOOL "" FORCE)
    set(WITH_JPEG OFF CACHE BOOL "" FORCE)
    set(WITH_PNG OFF CACHE BOOL "" FORCE)
    set(WITH_SDL OFF CACHE BOOL "" FORCE)
    set(WITH_GTK OFF CACHE BOOL "" FORCE)
    set(WITH_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(WITH_THREADS OFF CACHE BOOL "" FORCE)
    set(WITH_GNUTLS OFF CACHE BOOL "" FORCE)
    set(WITH_OPENSSL OFF CACHE BOOL "" FORCE)
    set(WITH_SYSTEMD OFF CACHE BOOL "" FORCE)
    set(WITH_GCRYPT OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
    set(WITH_TIGHTVNC_FILETRANSFER OFF CACHE BOOL "" FORCE)
    set(WITH_SASL OFF CACHE BOOL "" FORCE)
    set(WITH_WEBSOCKETS OFF CACHE BOOL "" FORCE)
    set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBVNCSERVER_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        libvncserver
        GIT_REPOSITORY https://github.com/LibVNC/libvncserver.git
        GIT_TAG LibVNCServer-0.9.14
    )
    FetchContent_MakeAvailable(libvncserver)

    # libvncserver 0.9.14 is not MSVC-clean: with threading disabled the
    # MUTEX()/COND() macros expand to an empty declaration, which MSVC rejects
    # inside a struct (GCC accepts it as a null declaration). Give them a dummy
    # 'int' field instead so the client headers compile on MSVC.
    file(READ "${libvncserver_SOURCE_DIR}/rfb/threading.h" _vnc_threading_h)
    string(REPLACE "#define MUTEX(mutex)\n" "#define MUTEX(mutex) int (mutex)\n" _vnc_threading_h "${_vnc_threading_h}")
    string(REPLACE "#define COND(cond)\n" "#define COND(cond) int (cond)\n" _vnc_threading_h "${_vnc_threading_h}")
    file(WRITE "${libvncserver_SOURCE_DIR}/rfb/threading.h" "${_vnc_threading_h}")

    target_link_libraries(banchoxterm PRIVATE vncclient)
    target_include_directories(banchoxterm PRIVATE
        ${libvncserver_SOURCE_DIR}
        ${libvncserver_BINARY_DIR}
    )
    target_compile_definitions(banchoxterm PRIVATE BANCHO_HAVE_VNC)
endif()

# ── Translations ────────────────────────────────────────────────────
# Generate the .qm files from the .ts sources so that a clean checkout can
# build without pre-generating them. The .qm files are embedded via
# resources.qrc.

set(TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/banchoxterm_es.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/banchoxterm_pt.ts
)
set(QM_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/banchoxterm_es.qm
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/banchoxterm_pt.qm
)

add_custom_command(
    OUTPUT ${QM_FILES}
    COMMAND Qt6::lrelease ${TS_FILES}
    DEPENDS ${TS_FILES}
    COMMENT "Generating translation files (.qm)"
)

add_custom_target(translations DEPENDS ${QM_FILES})
add_dependencies(banchoxterm translations)

# ── Tests ──────────────────────────────────────────────────────────

enable_testing()

add_executable(banchoxterm_tests
    tests/test_session.cpp
    src/apppaths.cpp
    src/apppaths.h
    src/session.cpp
    src/session.h
    src/masterpasswordmanager.cpp
    src/masterpasswordmanager.h
    src/vtterminalwidget.cpp
    src/vtterminalwidget.h
)

target_link_libraries(banchoxterm_tests
    PRIVATE
        Qt6::Core
        Qt6::Test
        Qt6::Widgets
)

target_include_directories(banchoxterm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(banchoxterm_tests PRIVATE
    ${libsodium_SOURCE_DIR}/src/libsodium/include
    ${BANCHO_SODIUM_GENERATED_INCLUDE}
)
target_compile_definitions(banchoxterm_tests PRIVATE SODIUM_STATIC)
target_link_libraries(banchoxterm_tests PRIVATE sodium)

add_test(NAME SessionTests COMMAND banchoxterm_tests)
# The test binary creates QWidget-based terminal components.  Use Qt's
# headless platform plugin so ctest is reproducible on CI and servers without
# an X11/Wayland display.
set_tests_properties(SessionTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# ── Formatting ──────────────────────────────────────────────────────

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        src/*.cpp src/*.h
        tests/*.cpp tests/*.h
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Running clang-format"
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FORMAT_SOURCES}
        COMMENT "Checking formatting with clang-format"
    )
endif()
