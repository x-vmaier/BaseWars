/**
 * @file Log.hpp
 * @brief spdlog setup shared by server and client: console + rolling "<prefix>_latest.log", Steam log hooks.
 */
#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>

namespace bw::log
{

// Sets the default spdlog logger. With toFile the previous <prefix>_latest.log next to the executable is moved to
// logs/<prefix>_<its last write time>.log (oldest are pruned) and a fresh one is started. Honours SPDLOG_LEVEL.
// dir overrides the executable directory (selftests).
void init(const char *prefix, bool toFile = true, const std::filesystem::path &dir = {});

// Forwards Steam warnings and Steam Networking Sockets diagnostics to spdlog. Call after SteamAPI/GameServer init.
void hookSteam(bool gameServer);

// Raylib has no dependency here: this is the TraceLog level -> spdlog level mapping for the client callback.
spdlog::level::level_enum fromRaylibLevel(int raylibLevel);

} // namespace bw::log
