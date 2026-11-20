/**
 * @file SaveLoad.cpp
 * @brief Selftest: a saved world loads into a fresh world and saves back byte-identical.
 */
#include "server/SelfTests.hpp"

#include <filesystem>

namespace selftest
{

namespace
{
// Raiders and patrols are not saved by design.
size_t persisted(World &w)
{
    size_t n = 0;
    w.reg.view<Info>().each([&](entt::entity e, const Info &) { n += w.ephemeral(e) ? 0 : 1; });
    return n;
}
} // namespace

void saveloadTest(Ctx &t)
{
    uint64_t before = 0;
    std::string copy;
    size_t units = 0, buildings = 0;
    float wood = 0;
    {
        Fixture a;
        cfg().instantBuild = true;
        int p1 = a.join(1);
        int p2 = a.join(2);
        int tx = 0, ty = 0;
        if (a.findSite(p1, K_Lumber, tx, ty))
        {
            Writer w;
            w.put<uint8_t>(C_Build).put<uint8_t>(K_Lumber).put<int32_t>(tx).put<int32_t>(ty);
            a.send(1000, w);
        }
        t.check(p1 == 1 && p2 == 2, "two players joined");
        a.run(90);
        units = persisted(a.world);
        buildings = a.world.occ.size();
        wood = a.world.pl[p1].res[R_Wood];
        before = stateHash(a);
        copy = a.savePath + ".copy";
        std::filesystem::copy_file(a.savePath, copy, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(a.savePath, std::filesystem::temp_directory_path() / "bw_saveload_before.bin",
                                   std::filesystem::copy_options::overwrite_existing);
    }

    Fixture b;
    std::filesystem::copy_file(copy, b.savePath, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(copy);
    t.check(b.sys.save.loadGame(), "saved world loads");
    t.check(persisted(b.world) == units, "entity count survives");
    t.check(b.world.occ.size() == buildings, "building footprints survive");
    t.check(std::abs(b.world.pl[1].res[R_Wood] - wood) < .01f, "resources survive");
    t.check(b.world.pl[1].used && b.world.pl[2].used && !b.world.pl[1].online, "profiles load offline");
    t.invariants(b, "after load");
    bool same = stateHash(b) == before;
    t.check(same, "save -> load -> save is byte-identical");
    if (!same) // keep both files for a byte diff
    {
        auto keep = std::filesystem::temp_directory_path();
        std::filesystem::copy_file(b.savePath, keep / "bw_saveload_after.bin",
                                   std::filesystem::copy_options::overwrite_existing);
        t.log("second save kept as " + (keep / "bw_saveload_after.bin").string());
    }

    // Reconnect to the loaded world and keep playing.
    t.check(b.join(1) == 1, "same profile rejoins its slot");
    b.run(30);
    t.invariants(b, "after rejoin");
}

} // namespace selftest
