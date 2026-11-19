/**
 * @file FerryPlanner.hpp
 * @brief Ferry planner: decides whether one of the owner's idle ships beats walking, and starts the transport.
 */
#pragma once
// Ferry planner: decides whether one of the owner's idle ships beats walking, and starts the transport.
//
//   TransportPlan planTransport(int owner, const std::vector<entt::entity>& group, Pos to, uint32_t destUid = 0, float
//   boatFactor = .75f);
//       Estimates the best way for `group` (troop units of `owner`, all walking together) to reach `to` and decides
//       whether one of the owner's idle ships beats walking. Compares   land time = A* land path length / slowest unit
//       speed  (partial or missing path = infinity)   with   boat time = max(walk to the boarding shore, ship arrives
//       there) + embark + water route + disembark
//       + walk from the landing shore.  `plan.useBoat` is true only when boatTime < boatFactor * landTime (default
//       75%). `plan.deferred` is true when the planner has no A* budget left this tick: call again a little later
//       (budget: 1 plan/tick). Cheap early-outs: straight distance < FERRY_MIN_DIST tiles, or no free idle ship of the
//       owner => returns immediately. `destUid` = uid of a destination building so that the land path may end inside
//       its footprint (0 = none).
//   bool startTransport(const TransportPlan& plan, const std::vector<entt::entity>& group, Pos dest, uint32_t destUid =
//   0);
//       Executes a plan with useBoat == true: group walks to plan.board, the ship sails to plan.boardWater, the units
//       embark (strict capacity), the ship sails to plan.landWater, the units disembark at plan.land and walk on to
//       `dest` (Haul pawns resume their own routine instead).  Returns false when the plan is stale (ship gone/busy).
//   Haul pawns call this automatically from runHaul (see tryFerryHaul).
#include "server/World.hpp"

struct Systems;

inline constexpr float FERRY_MIN_DIST = 14.f, FERRY_IDLE = 6.f, BOARD_REACH = 2.6f, INF_T = 1e9f;

struct TransportPlan
{
    bool useBoat = false, deferred = false;
    entt::entity ship = entt::null;
    uint32_t shipUid = 0;
    IV board{0, 0}, boardWater{0, 0}, land{0, 0}, landWater{0, 0}; // land shore tiles / ocean tiles next to them
    float etaBoat = 1e9f, etaLand = 1e9f;                          // seconds; 1e9 = unreachable
};

class FerryPlanner
{
  public:
    FerryPlanner(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    // Land tiles that touch the ocean (4-neighbourhood), nearest to `around` first, at least 5 tiles apart.
    void shoreTiles(Pos around, int maxR, size_t want, std::vector<IV> &out);
    IV waterNextTo(IV s); // the ocean neighbour with the most ocean around it (open water)
    // Route length in tiles from `a` to `b`; INF when there is no complete route (cached for 45 s).
    float routeLen(Pos a, IV b, bool water, uint32_t ignore, int maxNodes, float tol = 2.6f);
    TransportPlan planTransport(int owner, const std::vector<entt::entity> &group, Pos to, uint32_t destUid = 0,
                                float boatFactor = .75f);
    bool startTransport(const TransportPlan &plan, const std::vector<entt::entity> &group, Pos dest,
                        uint32_t destUid = 0);
    int tryFerryHaul(entt::entity e, Haul &h,
                     entt::entity target); // 0 = walk, 1 = a voyage was started, 2 = budget exhausted
    int planBudget = 0;

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
    struct RouteKey
    {
        int ax, ay, bx, by;
        bool water;
        uint32_t ignore;
        bool operator==(const RouteKey &o) const
        {
            return ax == o.ax && ay == o.ay && bx == o.bx && by == o.by && water == o.water && ignore == o.ignore;
        }
    };
    struct RouteKeyHash
    {
        size_t operator()(const RouteKey &k) const
        {
            return size_t(hash2(k.ax * 31 + k.bx, k.ay * 17 + k.by, k.ignore * 2u + (k.water ? 1u : 0u)));
        }
    };
    std::unordered_map<RouteKey, std::pair<float, double>, RouteKeyHash> routeCache;
};
