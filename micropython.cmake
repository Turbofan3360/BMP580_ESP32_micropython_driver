add_library(usermod_bmp580 INTERFACE)

target_sources(usermod_bmp580 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/bmp580.c
)

target_include_directories(usermod_bmp580 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(usermod INTERFACE usermod_bmp580)