cmake_minimum_required(VERSION 3.16)
project(banchoxterm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network Test)

# Find QTermWidget6
find_package(qtermwidget6 REQUIRED)

# Find libssh2 using PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED IMPORTED_TARGET libssh2)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Source files
set(SOURCES
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
    src/theme.cpp
    src/theme.h
    resources.qrc
)

add_executable(banchoxterm ${SOURCES})

target_link_libraries(banchoxterm
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Network
        qtermwidget6
        PkgConfig::LIBSSH2
)

# ── Tests ──────────────────────────────────────────────────────────

enable_testing()

add_executable(banchoxterm_tests
    tests/test_session.cpp
    src/session.cpp
    src/session.h
)

target_link_libraries(banchoxterm_tests
    PRIVATE
        Qt6::Core
        Qt6::Test
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
