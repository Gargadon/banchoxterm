# Keep the submodule pinned to a published commit while correcting its
# inclusive search range. Generate a build-local copy; never modify the checkout.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${QTQ_LIB}/HistorySearch.cpp")
file(READ "${QTQ_LIB}/HistorySearch.cpp" bancho_history_search)
set(bancho_search_old "m_emulation->lineCount())")
set(bancho_search_new "(m_emulation->lineCount() - 1))")
string(FIND "${bancho_history_search}" "${bancho_search_old}" bancho_search_position)
if(bancho_search_position EQUAL -1)
    message(FATAL_ERROR "QTermWidget search source changed; review its inclusive line bounds before updating this patch")
endif()
string(REPLACE "${bancho_search_old}" "${bancho_search_new}"
    bancho_history_search "${bancho_history_search}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/patched-qtermwidget")
set(BANCHO_HISTORY_SEARCH_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/patched-qtermwidget/HistorySearch.cpp")
file(WRITE "${BANCHO_HISTORY_SEARCH_SOURCE}" "${bancho_history_search}")
