cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(banchoxterm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network Test LinguistTools)

include(FetchContent)

if(NOT WIN32)
    # Fetch QTermWidget6 and its dependency lxqt-build-tools

    FetchContent_Declare(
        lxqt_build_tools
        GIT_REPOSITORY https://github.com/lxqt/lxqt-build-tools.git
        GIT_TAG        master
    )
    FetchContent_GetProperties(lxqt_build_tools)
    if(NOT lxqt_build_tools_POPULATED)
        FetchContent_Populate(lxqt_build_tools)
        set(SAVED_CMAKE_PROJECT_NAME ${CMAKE_PROJECT_NAME})
        set(CMAKE_PROJECT_NAME "lxqt2-build-tools")
        add_subdirectory(${lxqt_build_tools_SOURCE_DIR} ${lxqt_build_tools_BINARY_DIR})
        set(CMAKE_PROJECT_NAME ${SAVED_CMAKE_PROJECT_NAME})
    endif()
    set(lxqt2-build-tools_DIR "${CMAKE_BINARY_DIR}")

    # Use the vendored QTermWidget fork (git submodule) instead of upstream
    # master: the fork keeps the app-dir color-schemes fallback enabled on
    # Linux (upstream guards it with #ifdef Q_OS_MAC), so the color scheme
    # list in the configuration dialog is populated.
    add_subdirectory(third_party/qtermwidget)
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qtermwidget/lib/color-schemes" DESTINATION "${CMAKE_BINARY_DIR}")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qtermwidget/lib/kb-layouts" DESTINATION "${CMAKE_BINARY_DIR}")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qtermwidget/lib/translations" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# Fetch libssh2
if(WIN32)
    set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
endif()
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libssh2
    GIT_REPOSITORY https://github.com/libssh2/libssh2.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(libssh2)

# Include directories (target-scoped so third-party targets don't see src/)
# include_directories removed: use target_include_directories below.

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
    src/theme.cpp
    src/theme.h
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

set(BANCHO_VERSION "${PROJECT_VERSION}" CACHE STRING "App version string (passed from CI for tagged builds)")
target_compile_definitions(banchoxterm PRIVATE BANCHO_VERSION="${BANCHO_VERSION}")

target_link_libraries(banchoxterm
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Network
        libssh2
)

if(NOT WIN32)
    target_link_libraries(banchoxterm PRIVATE qtermwidget6)
else()
    target_compile_definitions(banchoxterm PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
    target_link_libraries(banchoxterm PRIVATE advapi32)
endif()

# Vendored QTermWidget emulator, ported to build on Windows without KPty.
# (Unix continues to use the system/FetchContent qtermwidget6 above.)
#
# QTermWidget is GPLv2+, so on Windows it is ONLY linked into the separate
# GPL process banchoxterm-term.exe. The MIT app talks to that process over
# IPC and embeds its window; it never links QTermWidget itself.
if(WIN32)
    set(QTQ_LIB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qtermwidget/lib)
    add_library(qtermwidget_win STATIC
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
    target_include_directories(qtermwidget_win PUBLIC ${QTQ_LIB})
    target_link_libraries(qtermwidget_win PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
    target_compile_definitions(qtermwidget_win PRIVATE
        _CRT_NONSTDC_NO_DEPRECATE
        KB_LAYOUT_DIR="${QTQ_LIB}/kb-layouts"
        COLORSCHEMES_DIR="${QTQ_LIB}/color-schemes"
        TRANSLATIONS_DIR="${QTQ_LIB}/translations"
    )

    # GPL terminal host process. This is the only binary that links QTermWidget.
    add_executable(banchoxterm-term
        src/termhost/termhost.cpp
    )
    target_include_directories(banchoxterm-term PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(banchoxterm-term PRIVATE
        qtermwidget_win
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
    )
    target_compile_definitions(banchoxterm-term PRIVATE
        _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006
    )

    # The MIT app embeds the term host via SetParent + a local pipe.
    target_sources(banchoxterm PRIVATE
        src/conpty.cpp
        src/conpty.h
        src/terminalhostclient.cpp
        src/terminalhostclient.h
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/banchoxterm.rc
    )

    target_sources(banchoxterm-term PRIVATE
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
option(BANCHO_ENABLE_VNC "Enable the embedded VNC client" ON)
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

add_test(NAME SessionTests COMMAND banchoxterm_tests)

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
