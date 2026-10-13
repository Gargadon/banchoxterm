# Keep Windows console capabilities independent of the inbox conhost version.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(conpty_runtime
    URL https://api.nuget.org/v3-flatcontainer/microsoft.windows.console.conpty/1.25.260930003/microsoft.windows.console.conpty.1.25.260930003.nupkg
    DOWNLOAD_NAME conpty-runtime.zip
    URL_HASH SHA256=02b07b349af66d801159bdf9e440d4a1ce78bb951f37fc8609731665afdae7ee
)
FetchContent_MakeAvailable(conpty_runtime)

if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(bancho_conpty_arch arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(bancho_conpty_arch x86)
else()
    set(bancho_conpty_arch x64)
endif()
set(bancho_conpty_dll "${conpty_runtime_SOURCE_DIR}/runtimes/win-${bancho_conpty_arch}/native/conpty.dll")
set(bancho_conpty_host "${conpty_runtime_SOURCE_DIR}/build/native/runtimes/${bancho_conpty_arch}/OpenConsole.exe")
set(bancho_conpty_license "${CMAKE_CURRENT_SOURCE_DIR}/cmake/conpty-LICENSE.txt")
foreach(bancho_file IN ITEMS "${bancho_conpty_dll}" "${bancho_conpty_host}")
    if(NOT EXISTS "${bancho_file}")
        message(FATAL_ERROR "ConPTY runtime is missing: ${bancho_file}. Configure once with FETCHCONTENT_FULLY_DISCONNECTED=OFF.")
    endif()
endforeach()
foreach(bancho_target IN ITEMS banchoxterm banchoxterm_tests)
    add_custom_command(TARGET ${bancho_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bancho_conpty_dll}" "$<TARGET_FILE_DIR:${bancho_target}>/conpty.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bancho_conpty_host}" "$<TARGET_FILE_DIR:${bancho_target}>/OpenConsole.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bancho_conpty_license}" "$<TARGET_FILE_DIR:${bancho_target}>/conpty-LICENSE.txt"
        VERBATIM)
endforeach()
install(FILES "${bancho_conpty_dll}" "${bancho_conpty_host}" "${bancho_conpty_license}"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
