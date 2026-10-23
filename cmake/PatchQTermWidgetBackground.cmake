# Keep the vendored revision unchanged while making explicit ANSI cell
# backgrounds honor the same opacity as the terminal's default background.
# Full redraws (including ConPTY resize output) otherwise cover the glass with
# opaque rectangles even though the widget's opacity is still configured.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${QTQ_LIB}/TerminalDisplay.cpp")
file(READ "${QTQ_LIB}/TerminalDisplay.cpp" bancho_terminal_display)
set(bancho_background_old "false /* do not use transparency */")
set(bancho_background_new "true /* honor terminal background opacity */")
string(FIND "${bancho_terminal_display}" "${bancho_background_old}" bancho_background_position)
if(bancho_background_position EQUAL -1)
    message(FATAL_ERROR "QTermWidget background rendering changed; review its cell opacity before updating this patch")
endif()
string(REPLACE "${bancho_background_old}" "${bancho_background_new}"
    bancho_terminal_display "${bancho_terminal_display}")
# Resizing replaces the character cache. Refresh it before the first paint,
# including a complete invalidation for unchanged cells and pixel margins.
set(bancho_resize_old "void TerminalDisplay::resizeEvent(QResizeEvent*)\n{\n  updateImageSize();\n  processFilters();\n}")
set(bancho_resize_new "void TerminalDisplay::resizeEvent(QResizeEvent*)\n{\n  updateImageSize();\n  updateImage();\n  processFilters();\n  update();\n}")
string(FIND "${bancho_terminal_display}" "${bancho_resize_old}" bancho_resize_position)
if(bancho_resize_position EQUAL -1)
    message(FATAL_ERROR "QTermWidget resize rendering changed; review its image refresh before updating this patch")
endif()
string(REPLACE "${bancho_resize_old}" "${bancho_resize_new}"
    bancho_terminal_display "${bancho_terminal_display}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/patched-qtermwidget")
set(BANCHO_TERMINAL_DISPLAY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/patched-qtermwidget/TerminalDisplay.cpp")
file(WRITE "${BANCHO_TERMINAL_DISPLAY_SOURCE}" "${bancho_terminal_display}")
