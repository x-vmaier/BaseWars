include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.2
    GIT_SHALLOW TRUE)

FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
    SYSTEM)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib entt spdlog)

# MSVC ASan: every object linked into the sanitized targets must agree on container annotations (see Compiler.cmake).
if(MSVC AND BW_SANITIZE)
    target_compile_options(spdlog PRIVATE /fsanitize=address)
    target_compile_definitions(spdlog PUBLIC _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
endif()
