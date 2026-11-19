# unpack the SDK into third_party/steamworks (the folder that contains public/steam/steam_api.h),
# or pass -DSTEAMWORKS_SDK_DIR=<path> / set the STEAMWORKS_SDK environment variable.
set(STEAMWORKS_SDK_DIR "" CACHE PATH "Steamworks SDK root (contains public/steam/steam_api.h)")
set(_steam_candidates "${STEAMWORKS_SDK_DIR}" "$ENV{STEAMWORKS_SDK}" "${CMAKE_SOURCE_DIR}/third_party/steamworks" "${CMAKE_SOURCE_DIR}/third_party/steamworks/sdk")
set(STEAM_ROOT "")
foreach(cand IN LISTS _steam_candidates)
    if(cand AND EXISTS "${cand}/public/steam/steam_api.h")
        set(STEAM_ROOT "${cand}")
        break()
    endif()
endforeach()
if(NOT STEAM_ROOT)
    message(FATAL_ERROR
        "Steamworks SDK not found. Download it from https://partner.steamgames.com/downloads/list and unpack it so that "
        "third_party/steamworks/public/steam/steam_api.h exists, or configure with -DSTEAMWORKS_SDK_DIR=<path>.")
endif()

if(WIN32)
    set(STEAM_BIN_DIR "${STEAM_ROOT}/redistributable_bin/win64")
    set(STEAM_LIB_NAME steam_api64)
    set(STEAM_RUNTIME_FILE "${STEAM_BIN_DIR}/steam_api64.dll")
elseif(APPLE)
    set(STEAM_BIN_DIR "${STEAM_ROOT}/redistributable_bin/osx")
    set(STEAM_LIB_NAME steam_api)
    set(STEAM_RUNTIME_FILE "${STEAM_BIN_DIR}/libsteam_api.dylib")
else()
    set(STEAM_BIN_DIR "${STEAM_ROOT}/redistributable_bin/linux64")
    set(STEAM_LIB_NAME steam_api)
    set(STEAM_RUNTIME_FILE "${STEAM_BIN_DIR}/libsteam_api.so")
endif()

# On Windows the import library lives in the same folder as the DLL
find_library(STEAM_API_LIB NAMES ${STEAM_LIB_NAME} PATHS "${STEAM_BIN_DIR}" NO_DEFAULT_PATH)
if(NOT STEAM_API_LIB)
    message(FATAL_ERROR "Steamworks SDK found in ${STEAM_ROOT}, but ${STEAM_LIB_NAME} is missing in ${STEAM_BIN_DIR}.")
endif()
message(STATUS "Steamworks SDK: ${STEAM_ROOT}")

add_library(steamworks INTERFACE)
target_include_directories(steamworks SYSTEM INTERFACE "${STEAM_ROOT}/public")
target_link_libraries(steamworks INTERFACE "${STEAM_API_LIB}")

# Copies the Steam runtime library (and a test app id; 480 = Spacewar) next to an executable
function(bw_copy_steam_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAM_RUNTIME_FILE}" "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E echo 480 > "$<TARGET_FILE_DIR:${target}>/steam_appid.txt"
        VERBATIM)
endfunction()
