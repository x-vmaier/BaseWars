/**
 * @file Ships.cpp
 * @brief Selftest: the Harbour trains every ship kind, a ship sails only with its crew aboard, paid timed upgrades
 * raise the hold, and ships, crews and a running upgrade survive save / reload.
 */
#include "common/Progression.hpp"
#include "server/SelfTests.hpp"

#include <cmath>
#include <filesystem>

namespace selftest
{

namespace
{
constexpr uint32_t SEED = 1; // a world whose Town Hall lies next to the sea
constexpr Kind SHIPS[] = {K_Ship, K_Longship, K_Cog, K_Galleon};

entt::entity shipOf(World &w, int id, Kind k)
{
    entt::entity found = entt::null;
    w.reg.view<Info, Owner>().each([&](entt::entity e, const Info &in, const Owner &o)
    {
        if (o.id == id && in.kind == k)
            found = e;
    });
    return found;
}

void send(Fixture &f, Writer &w)
{
    f.send(1000, w);
}

void train(Fixture &f, entt::entity harbour, Kind k)
{
    Writer w;
    w.put<uint8_t>(C_Train).put<uint32_t>(f.world.reg.get<Uid>(harbour).id).put<uint8_t>(k);
    send(f, w);
}

// Move order for one unit towards the nearest open water within a few tiles; false when there is none.
bool orderToWater(Fixture &f, entt::entity unit)
{
    Pos p = f.world.reg.get<Pos>(unit);
    for (int d = 3; d <= 8; d++)
        for (int a = 0; a < 8; a++)
        {
            float x = std::floor(p.x) + .5f + std::round(std::cos(float(a) * .785f) * float(d));
            float y = std::floor(p.y) + .5f + std::round(std::sin(float(a) * .785f) * float(d));
            if (!f.world.walkable(int(x), int(y), true, 0))
                continue;
            Writer w;
            w.put<uint8_t>(C_Move)
                .put<uint8_t>(1)
                .put<uint32_t>(f.world.reg.get<Uid>(unit).id)
                .put<float>(x)
                .put<float>(y);
            send(f, w);
            return true;
        }
    return false;
}

void upgrade(Fixture &f, entt::entity ship)
{
    Writer w;
    w.put<uint8_t>(C_UpgradeShip).put<uint32_t>(f.world.reg.get<Uid>(ship).id);
    send(f, w);
}

float resSum(World &w, int id)
{
    float s = 0;
    for (float v : w.pl[id].res)
        s += v;
    return s;
}

void fill(World &w, int id)
{
    for (float &v : w.pl[id].res)
        v = 20000;
}

// Harbour trains all four kinds (gated by its level); each keeps the hold of its registry entry.
void training(Ctx &t, Fixture &f, int id, entt::entity harbour)
{
    World &w = f.world;
    w.reg.get<Info>(harbour).level = 1;
    train(f, harbour, K_Galleon);
    train(f, harbour, K_Longship);
    f.run(2);
    t.check(shipOf(w, id, K_Galleon) == entt::null && shipOf(w, id, K_Longship) == entt::null,
            "higher ships need a higher Harbour level");

    w.reg.get<Info>(harbour).level = 3;
    for (Kind k : SHIPS)
        train(f, harbour, k);
    f.run(8);
    for (Kind k : SHIPS)
    {
        auto e = shipOf(w, id, k);
        t.check(e != entt::null, std::string(KINFO[k].name) + " is trained");
        if (e == entt::null)
            continue;
        const Info &in = w.reg.get<Info>(e);
        t.check(in.level == 1 && w.reg.get<Health>(e).maxhp == KINFO[k].hp && w.reg.all_of<Ship>(e),
                std::string(KINFO[k].name) + " starts at level 1 with its registry hull");
        t.check(shipTroopCap(k, 1) == KINDS[k].ship.troops && shipResCap(k, 1) == KINDS[k].ship.cargo,
                std::string(KINFO[k].name) + " hold comes from the registry");
    }
    t.check(shipTroopCap(K_Longship, 1) > shipTroopCap(K_Cog, 1) && shipResCap(K_Cog, 1) > shipResCap(K_Longship, 1),
            "troop ship carries more troops, cargo ship more cargo");
    t.invariants(f, "after training");
}

// Without the minimum crew the ship ignores orders; with it, it sails.
void crew(Ctx &t, Fixture &f, int id)
{
    World &w = f.world;
    auto ship = shipOf(w, id, K_Longship);
    t.check(!f.sys.naval.crewed(ship), "an empty ship is not crewed");

    t.check(orderToWater(f, ship), "open water exists near the ship");
    t.check(w.reg.get<Unit>(ship).path.empty(), "an uncrewed ship does not move");

    entt::entity pawns[2];
    for (auto &p : pawns)
        p = w.makeUnit(K_Pawn, uint8_t(id), w.reg.get<Pos>(ship).x, w.reg.get<Pos>(ship).y);
    f.sys.naval.embark(pawns[0], ship);
    t.check(!f.sys.naval.crewed(ship), "one troop is below the Longship's crew of two");
    f.sys.naval.embark(pawns[1], ship);
    t.check(f.sys.naval.crewed(ship), "two troops crew a Longship");

    orderToWater(f, ship);
    t.check(!w.reg.get<Unit>(ship).path.empty(), "a crewed ship accepts the move order");
    t.invariants(f, "crewed");
}

// Timed paid upgrade: refused without a high enough Harbour, then paid, locked while running, hold grows.
void upgrades(Ctx &t, Fixture &f, int id, entt::entity harbour, std::string &copy)
{
    World &w = f.world;
    auto ship = shipOf(w, id, K_Ship);
    auto crew = w.makeUnit(K_Pawn, uint8_t(id), w.reg.get<Pos>(ship).x, w.reg.get<Pos>(ship).y);
    f.sys.naval.embark(crew, ship);

    w.reg.get<Info>(harbour).level = 1;
    float before = resSum(w, id);
    upgrade(f, ship);
    t.check(!w.reg.all_of<ShipUpgrade>(ship) && resSum(w, id) == before,
            "no upgrade at a level 1 Harbour, nothing paid");

    w.reg.get<Info>(harbour).level = 3;
    cfg().instantTrain = false;
    float cost[R_Count];
    upgradeCost(K_Ship, 1, cost);
    float c = 0;
    for (float v : cost)
        c += v;
    upgrade(f, ship);
    t.check(w.reg.all_of<ShipUpgrade>(ship) && std::abs(before - resSum(w, id) - c) < .5f,
            "upgrade starts and is paid");

    f.run(5);
    orderToWater(f, ship);
    t.check(w.reg.get<Unit>(ship).path.empty() && w.reg.get<Info>(ship).level == 1,
            "an upgrading ship rests and keeps level 1");
    upgrade(f, ship);
    t.check(std::abs(before - resSum(w, id) - c) < .5f, "a second upgrade order is not charged");

    // save mid-upgrade, reload into a fresh world
    f.sys.save.saveGame();
    copy = f.savePath + ".copy";
    std::filesystem::copy_file(f.savePath, copy, std::filesystem::copy_options::overwrite_existing);

    f.run(shipUpgradeSeconds(K_Ship, 1));
    const Info &in = w.reg.get<Info>(ship);
    t.check(!w.reg.all_of<ShipUpgrade>(ship) && in.level == 2, "the upgrade finishes after its time");
    t.check(shipTroopCap(K_Ship, 2) > shipTroopCap(K_Ship, 1) && w.reg.get<Health>(ship).maxhp > KINFO[K_Ship].hp,
            "level 2 has a bigger hold and hull");
    t.invariants(f, "upgraded");
}

void reload(Ctx &t, const std::string &copy, int id)
{
    Fixture b(SEED);
    std::filesystem::copy_file(copy, b.savePath, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(copy);
    t.check(b.sys.save.loadGame(), "saved world loads");

    World &w = b.world;
    auto ship = shipOf(w, id, K_Ship);
    t.check(ship != entt::null && w.reg.all_of<ShipUpgrade>(ship) && w.reg.get<ShipUpgrade>(ship).t > 1.f,
            "the running upgrade survives reload");
    t.check(shipOf(w, id, K_Longship) != entt::null && b.sys.naval.crewed(shipOf(w, id, K_Longship)),
            "ships and their crews survive reload");
    b.run(shipUpgradeSeconds(K_Ship, 1));
    t.check(w.reg.get<Info>(ship).level == 2, "the reloaded upgrade completes");
    t.invariants(b, "after reload");
}
} // namespace

void shipsTest(Ctx &t)
{
    std::string copy;
    int id = 1;
    {
        Fixture f(SEED);
        cfg().instantBuild = cfg().instantTrain = true;
        id = f.join(1, 1000);
        World &w = f.world;
        w.reg.get<Info>(f.hallOf(id)).level = 6;
        fill(w, id);

        int tx = 0, ty = 0;
        if (!f.findSite(id, K_Harbour, tx, ty))
        {
            t.check(false, "a Harbour site exists near the Town Hall");
            return;
        }
        Writer b;
        b.put<uint8_t>(C_Build).put<uint8_t>(K_Harbour).put<int32_t>(tx).put<int32_t>(ty);
        send(f, b);
        f.run(5);
        auto harbour = w.find(w.occ[tkey(tx, ty)]);
        if (harbour == entt::null || !w.complete(harbour))
        {
            t.check(false, "the Harbour is built");
            return;
        }

        training(t, f, id, harbour);
        crew(t, f, id);
        upgrades(t, f, id, harbour, copy);
    }
    reload(t, copy, id);
}

} // namespace selftest
