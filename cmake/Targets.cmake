set(BW_VER_TWEAK 0)
if(PROJECT_VERSION_TWEAK)
    set(BW_VER_TWEAK "${PROJECT_VERSION_TWEAK}")
endif()

set(BW_SRC "${CMAKE_SOURCE_DIR}/src")

function(bw_collect_sources out_var)
    set(patterns)
    foreach(dir IN LISTS ARGN)
        list(APPEND patterns "${BW_SRC}/${dir}/*.cpp")
    endforeach()
    file(GLOB_RECURSE found CONFIGURE_DEPENDS ${patterns})
    set(${out_var} ${found} PARENT_SCOPE)
endfunction()

bw_collect_sources(BW_COMMON_SOURCES common)

add_library(bw_common STATIC ${BW_COMMON_SOURCES})
target_include_directories(bw_common PUBLIC "${BW_SRC}")
target_link_libraries(bw_common PUBLIC EnTT::EnTT steamworks)
bw_apply_compiler_policy(bw_common)

if(WIN32)
    target_link_libraries(bw_common PUBLIC ws2_32 winmm)
endif()

# Server
bw_collect_sources(BW_SERVER_SOURCES server systems)

add_executable(server
    ${BW_SERVER_SOURCES}
    "${BW_SRC}/networking/SteamServerNet.cpp"
)
target_link_libraries(server PRIVATE bw_common)
bw_apply_compiler_policy(server)
bw_copy_steam_runtime(server)

add_custom_command(TARGET server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:server>"
    VERBATIM)

# Client
bw_collect_sources(BW_CLIENT_SOURCES client rendering ui)

add_executable(client
    ${BW_CLIENT_SOURCES}
    "${BW_SRC}/networking/SteamClientNet.cpp"
    "${BW_SRC}/networking/SteamClient.cpp"
    "${BW_SRC}/networking/ServerBrowser.cpp"
)
target_link_libraries(client PRIVATE bw_common raylib)
bw_apply_compiler_policy(client)
bw_copy_steam_runtime(client)
bw_link_assets(client)

# Default client config
add_custom_command(TARGET client POST_BUILD
    COMMAND ${CMAKE_COMMAND}
    "-DOUT=$<TARGET_FILE_DIR:client>/config.txt"
    -P "${CMAKE_SOURCE_DIR}/cmake/WriteClientConfig.cmake"
    VERBATIM)

# Windows resources
if(WIN32)
    set(BW_RC_FILE "${CMAKE_SOURCE_DIR}/BaseWars.rc")
    set(BW_ICO_FILE "${CMAKE_SOURCE_DIR}/BaseWars.ico")

    set_source_files_properties("${BW_RC_FILE}" PROPERTIES
        COMPILE_DEFINITIONS
        "VER_MAJOR=${PROJECT_VERSION_MAJOR};VER_MINOR=${PROJECT_VERSION_MINOR};VER_PATCH=${PROJECT_VERSION_PATCH};VER_BUILD=${BW_VER_TWEAK}"
        OBJECT_DEPENDS "${BW_ICO_FILE}"
    )

    target_sources(client PRIVATE "${BW_RC_FILE}")
endif()
