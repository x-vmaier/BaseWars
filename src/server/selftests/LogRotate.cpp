/**
 * @file LogRotate.cpp
 * @brief Selftest: a new run moves the previous <prefix>_latest.log into logs/ and keeps only the newest archives.
 */
#include "common/Log.hpp"
#include "server/SelfTests.hpp"

#include <fstream>
#include <spdlog/spdlog.h>

namespace selftest
{

namespace
{
namespace fs = std::filesystem;

size_t countLogs(const fs::path &logs)
{
    size_t n = 0;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(logs, ec))
        n += e.path().extension() == ".log";
    return n;
}
} // namespace

void logRotateTest(Ctx &t)
{
    fs::path dir = fs::temp_directory_path() / "bw_logrotate_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    bw::log::init("probe", true, dir);
    spdlog::info("first run");
    bw::log::init("probe", true, dir); // second run archives the first
    spdlog::info("second run");
    spdlog::default_logger()->flush();
    t.check(fs::exists(dir / "probe_latest.log"), "latest log is next to the executable");
    t.check(countLogs(dir / "logs") == 1, "previous run moved to logs/");

    for (int i = 0; i < 30; i++) // many more runs: the archive is capped
    {
        std::ofstream(dir / "probe_latest.log") << "x";
        bw::log::init("probe", true, dir);
    }
    t.check(countLogs(dir / "logs") == 20, "oldest archives are pruned (20 kept)");

    bw::log::init("server", false); // back to plain console output for the remaining scenarios
    spdlog::default_logger()->flush();
    fs::remove_all(dir, ec);
}

} // namespace selftest
