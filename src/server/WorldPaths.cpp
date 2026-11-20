/**
 * @file WorldPaths.cpp
 * @brief World pathfinding: A* over walkable tiles and unit orders.
 */
#include "common/Progression.hpp"
#include "server/World.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <queue>

namespace
{
// Octile-distance A* over walkable tiles; keeps the closest node seen so a blocked goal still yields a partial path.
struct AStar
{
    struct Node
    {
        float g;
        int64_t par;
        bool closed;
    };
    using QueueEntry = std::pair<float, int64_t>;

    World &w;
    IV goal;
    bool water;
    uint32_t ignore;
    std::unordered_map<int64_t, Node> nodes;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> q;
    int64_t best = 0;
    float bestH = 0;

    float h(int x, int y) const
    {
        float dx = float(std::abs(x - goal.x)), dy = float(std::abs(y - goal.y));
        return dx + dy - 0.586f * std::min(dx, dy);
    }

    void expand(int64_t k, float cg)
    {
        int x = keyCx(k), y = keyCy(k);
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
            {
                if (!dx && !dy)
                    continue;
                int nx = x + dx, ny = y + dy;
                if (!w.walkable(nx, ny, water, ignore))
                    continue;
                if (dx && dy && (!w.walkable(x + dx, y, water, ignore) || !w.walkable(x, y + dy, water, ignore)))
                    continue;

                int64_t nk = tkey(nx, ny);
                float ng = cg + (dx && dy ? 1.414f : 1.f);
                auto it = nodes.find(nk);
                if (it == nodes.end() || ng < it->second.g)
                {
                    nodes[nk] = {ng, k, false};
                    q.push({ng + h(nx, ny), nk});
                }
            }
    }

    // Returns true when the goal itself was reached.
    bool run(int64_t sk, int64_t gk, int maxNodes)
    {
        best = sk;
        bestH = h(keyCx(sk), keyCy(sk));
        nodes[sk] = {0, -1, false};
        q.push({bestH, sk});

        int expanded = 0;
        while (!q.empty() && expanded < maxNodes)
        {
            int64_t k = q.top().second;
            q.pop();
            Node &cur = nodes[k];
            if (cur.closed)
                continue;
            cur.closed = true;
            expanded++;
            if (k == gk)
                return true;

            float cg = cur.g;
            float ch = h(keyCx(k), keyCy(k));
            if (ch < bestH)
            {
                bestH = ch;
                best = k;
            }
            expand(k, cg);
        }
        return false;
    }
};
} // namespace

// An unwalkable goal moves to the nearest walkable tile within 8 rings.
bool World::findPath(IV s, IV g, bool water, uint32_t ignore, std::vector<Pos> &out, int maxNodes)
{
    out.clear();
    if (!walkable(g.x, g.y, water, ignore))
    {
        bool f = false;
        for (int r = 1; r <= 8 && !f; r++)
            for (int dy = -r; dy <= r && !f; dy++)
                for (int dx = -r; dx <= r && !f; dx++)
                    if (std::max(std::abs(dx), std::abs(dy)) == r && walkable(g.x + dx, g.y + dy, water, ignore))
                    {
                        g = {g.x + dx, g.y + dy};
                        f = true;
                    }
        if (!f)
            return false;
    }
    if (s.x == g.x && s.y == g.y)
        return true;

    AStar a{*this, g, water, ignore, {}, {}};
    int64_t sk = tkey(s.x, s.y), gk = tkey(g.x, g.y);
    bool found = a.run(sk, gk, maxNodes);

    int64_t end = found ? gk : a.best;
    if (end == sk)
        return false;
    for (int64_t k = end; k != sk && k != -1; k = a.nodes[k].par)
        out.push_back({float(keyCx(k)) + .5f, float(keyCy(k)) + .5f});
    std::reverse(out.begin(), out.end());
    return !out.empty();
}

bool World::order(entt::entity e, Pos goal, uint32_t ignore, int maxNodes)
{
    auto &u = reg.get<Unit>(e);
    Pos p = reg.get<Pos>(e);
    bool water = hasTag(reg.get<Info>(e).kind, KT_Naval);
    std::vector<Pos> path;
    if (!findPath({int(std::floor(p.x)), int(std::floor(p.y))}, {int(std::floor(goal.x)), int(std::floor(goal.y))},
                  water, ignore, path, maxNodes))
        return false;
    u.path = std::move(path);
    u.i = 0;
    return true;
}
