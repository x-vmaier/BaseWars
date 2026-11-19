cmake_minimum_required(VERSION 3.26)
project(BaseWars LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(Compiler)       # C++23, compile_commands.json, warnings, build type
include(Dependencies)   # raylib, EnTT
include(Steamworks)     # mandatory Steamworks SDK (fails the configure when it is missing)
include(Targets)        # bw_common, server, client, asset/config/steam-library copy steps
