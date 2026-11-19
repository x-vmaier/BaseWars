file(GLOB_RECURSE BW_COMMON_HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/common/*.hpp" "${CMAKE_SOURCE_DIR}/src/components/*.hpp")

add_library(bw_common INTERFACE)
target_include_directories(bw_common INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(bw_common INTERFACE EnTT::EnTT steamworks)
if(WIN32)
    target_link_libraries(bw_common INTERFACE ws2_32 winmm)
endif()

# ---- authoritative server ----
file(GLOB_RECURSE BW_SERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/server/*.cpp" "${CMAKE_SOURCE_DIR}/src/systems/*.cpp")
set(BW_SERVER_SOURCES ${BW_SERVER_SOURCES}
    "${CMAKE_SOURCE_DIR}/src/networking/SteamServerNet.cpp")
add_executable(server ${BW_SERVER_SOURCES})
target_link_libraries(server PRIVATE bw_common)
bw_copy_steam_runtime(server)
add_custom_command(TARGET server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:server>"
    VERBATIM)

# ---- client ----
file(GLOB_RECURSE BW_CLIENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/client/*.cpp" "${CMAKE_SOURCE_DIR}/src/rendering/*.cpp" "${CMAKE_SOURCE_DIR}/src/ui/*.cpp")
set(BW_CLIENT_SOURCES ${BW_CLIENT_SOURCES}
    "${CMAKE_SOURCE_DIR}/src/networking/SteamClientNet.cpp"
    "${CMAKE_SOURCE_DIR}/src/networking/SteamClient.cpp"
    "${CMAKE_SOURCE_DIR}/src/networking/ServerBrowser.cpp")
add_executable(client ${BW_CLIENT_SOURCES})
target_link_libraries(client PRIVATE bw_common raylib)
bw_copy_steam_runtime(client)
# default client config next to the executable (server address for the direct-connect tab)
add_custom_command(TARGET client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DOUT="$<TARGET_FILE_DIR:client>/config.txt" -P "${CMAKE_SOURCE_DIR}/cmake/WriteClientConfig.cmake"
    VERBATIM)
