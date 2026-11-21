/**
 * @file Log.cpp
 * @brief spdlog setup, log rotation into logs/ and Steam log hooks.
 */
#include "common/Log.hpp"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api.h>
#include <steam/steam_gameserver.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace bw::log
{

namespace
{

namespace fs = std::filesystem;
constexpr size_t KEEP_OLD_LOGS = 20;

fs::path exeDir()
{
    std::error_code ec;
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n > 0 && n < std::size(buf))
        return fs::path(buf).parent_path();
#else
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return p.parent_path();
#endif
    return fs::current_path(ec);
}

std::string stamp(fs::file_time_type t)
{
    std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(t));
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char out[32];
    std::strftime(out, sizeof out, "%Y-%m-%d_%H-%M-%S", &tm);
    return out;
}

// Moves the last run's log into logs/ and drops the oldest archives. Failures are ignored: logging must never
// stop the program from starting.
void archivePrevious(const fs::path &dir, const std::string &prefix, const fs::path &latest)
{
    std::error_code ec;
    if (!fs::exists(latest, ec))
        return;
    fs::path logs = dir / "logs";
    fs::create_directories(logs, ec);
    std::string base = prefix + "_" + stamp(fs::last_write_time(latest, ec));
    fs::path dst = logs / (base + ".log");
    for (int i = 2; fs::exists(dst, ec); i++)
        dst = logs / (base + "_" + std::to_string(i) + ".log");
    fs::rename(latest, dst, ec);

    std::vector<fs::path> old;
    for (const auto &e : fs::directory_iterator(logs, ec))
    {
        std::string n = e.path().filename().string();
        if (n.rfind(prefix + "_", 0) == 0 && e.path().extension() == ".log")
            old.push_back(e.path());
    }
    std::sort(old.begin(), old.end()); // timestamped names sort chronologically
    for (size_t i = 0; i + KEEP_OLD_LOGS < old.size(); i++)
        fs::remove(old[i], ec);
}

void onSteamWarning(int severity, const char *text)
{
    spdlog::log(severity >= 1 ? spdlog::level::warn : spdlog::level::info, "[steam] {}", text);
}

void onSteamNet(ESteamNetworkingSocketsDebugOutputType type, const char *text)
{
    spdlog::level::level_enum lvl = spdlog::level::info;
    switch (type)
    {
    case k_ESteamNetworkingSocketsDebugOutputType_Bug:
    case k_ESteamNetworkingSocketsDebugOutputType_Error:
        lvl = spdlog::level::err;
        break;
    case k_ESteamNetworkingSocketsDebugOutputType_Important:
    case k_ESteamNetworkingSocketsDebugOutputType_Warning:
        lvl = spdlog::level::warn;
        break;
    case k_ESteamNetworkingSocketsDebugOutputType_Msg:
        lvl = spdlog::level::info;
        break;
    default:
        lvl = spdlog::level::debug;
        break;
    }
    spdlog::log(lvl, "[steamnet] {}", text);
}

} // namespace

void init(const char *prefix, bool toFile, const fs::path &dirOverride)
{
    spdlog::shutdown(); // release a previous log file: Windows cannot rename an open file
    std::vector<spdlog::sink_ptr> sinks;
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("[%H:%M:%S] [%^%l%$] %v");
    sinks.push_back(console);

    if (toFile)
    {
        fs::path dir = dirOverride.empty() ? exeDir() : dirOverride;
        fs::path latest = dir / (std::string(prefix) + "_latest.log");
        archivePrevious(dir, prefix, latest);
        try
        {
            auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(latest.string(), true);
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(file);
        }
        catch (const spdlog::spdlog_ex &e)
        {
            std::fprintf(stderr, "cannot open %s: %s\n", latest.string().c_str(), e.what());
        }
    }
    else
        console->set_pattern("%v"); // selftest output stays plain

    auto logger = std::make_shared<spdlog::logger>(prefix, sinks.begin(), sinks.end());
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(1));
    spdlog::cfg::load_env_levels();
}

void hookSteam(bool gameServer)
{
    if (gameServer)
        SteamGameServerUtils()->SetWarningMessageHook(&onSteamWarning);
    else
        SteamUtils()->SetWarningMessageHook(&onSteamWarning);
    SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, &onSteamNet);
}

spdlog::level::level_enum fromRaylibLevel(int l)
{
    switch (l)
    {
    case 1: // LOG_TRACE
        return spdlog::level::trace;
    case 2: // LOG_DEBUG
        return spdlog::level::debug;
    case 4: // LOG_WARNING
        return spdlog::level::warn;
    case 5: // LOG_ERROR
        return spdlog::level::err;
    case 6: // LOG_FATAL
        return spdlog::level::critical;
    default:
        return spdlog::level::info;
    }
}

} // namespace bw::log
