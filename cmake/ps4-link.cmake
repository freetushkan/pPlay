# PS4: link with cmake/ps4-link.x instead of the toolchain's link.x (see the comment there).
# Include after project(): the toolchain's flags are in CMAKE_EXE_LINKER_FLAGS by then.
set(PS4_LINK_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ps4-link.x")
string(REGEX REPLACE "--script[ ]+[^ ]*link\\.x" "--script ${PS4_LINK_SCRIPT}"
        CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
if (NOT CMAKE_EXE_LINKER_FLAGS MATCHES "ps4-link\\.x")
    message(FATAL_ERROR "PS4: the toolchain's --script link.x not found in CMAKE_EXE_LINKER_FLAGS")
endif ()
