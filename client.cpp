// Raylib client: fog-of-war rendering, culling, selection, context menus, HUD, minimap.
#include "shared.hpp"

#include <entt/entt.hpp>
#include <raylib.h>

#include <cstdio>
#include <fstream>
#include <set>
#include <unordered_map>

namespace {

constexpr float TILE = 32.f;

Color rgb(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)std::clamp(r, 0, 255), (unsigned char)std::clamp(g, 0, 255), (unsigned char)std::clamp(b, 0, 255), (unsigned char)std::clamp(a, 0, 255)};
}
Color pcol(int o) {
    static const Color pal[MAX_PLAYERS + 1] = {rgb(150, 150, 150), rgb(220, 60, 60), rgb(60, 110, 230), rgb(230, 200, 50), rgb(160, 80, 200),
                                               rgb(240, 140, 40), rgb(50, 200, 200), rgb(240, 110, 170), rgb(235, 235, 235)};
    return pal[std::clamp(o, 0, MAX_PLAYERS)];
}
Color terrainColor(int t) {
    static const Color c[4] = {rgb(124, 190, 84), rgb(52, 120, 58), rgb(122, 120, 128), rgb(46, 102, 172)};
    return c[t & 3];
}

// client-side ECS components
struct NetId { uint32_t id; };
struct Info { Kind kind; uint8_t owner; float hp, maxhp; };
struct RPos { float x, y; };   // smoothed render position (tiles)
struct Goal { float x, y; };   // last server position
struct Seen { uint32_t stamp; };
struct LogEvent { std::string text; };
struct LogLine { std::string text; double t; };

std::vector<LogLine> logs;
void onLog(const LogEvent& e) {
    logs.push_back({e.text, GetTime()});
    if (logs.size() > 8) logs.erase(logs.begin());
}

struct App {
    struct Btn { const char* name; Kind k; };
    struct MenuItem { std::string label; int act; bool enabled; };
    struct Marker { float x, y; double t; };
    const Btn btns[5] = {{"Gold Mine", K_GoldMine}, {"Lumber Camp", K_Lumber}, {"Scout", K_Scout}, {"Soldier", K_Soldier}, {"Knight", K_Knight}};

    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;
    bool connected = false, welcomed = false, lost = false;
    double startT = 0;
    std::string ip;
    int port = DEFAULT_PORT;

    int me = 0;
    float gold = 0, wood = 0;
    uint16_t allyMask = 0, pendMask = 0;
    std::vector<uint8_t> terrain, vis;
    std::vector<Color> mmPix;
    Texture2D mmTex{};
    entt::registry reg;
    entt::dispatcher disp;
    std::unordered_map<uint32_t, entt::entity> ents;
    std::set<uint32_t> sel;
    uint32_t stamp = 0;
    Camera2D cam{};
    bool dragging = false;
    Vector2 dragStart{};
    int buildKind = -1;
    bool menuOpen = false;
    Vector2 menuPos{};
    std::vector<MenuItem> menu;
    std::string menuTitle;
    int mTx = 0, mTy = 0, mOwner = -1;
    uint32_t mTarget = 0;
    std::vector<Marker> markers;

    void send(const Writer& w) { if (peer && connected) sendPacket(peer, w); }
    void msg(const std::string& s) { disp.enqueue(LogEvent{s}); }

    // ---------- networking ----------
    void rebuildMinimap() {
        for (int i = 0; i < MAP_W * MAP_H; i++) mmPix[i] = vis[i] ? terrainColor(terrain[i]) : rgb(18, 22, 30);
        UpdateTexture(mmTex, mmPix.data());
    }

    void onPacket(const uint8_t* d, size_t n) {
        Reader r{d, d + n};
        switch (r.get<uint8_t>()) {
        case S_Welcome: {
            me = r.get<uint8_t>();
            int w = r.get<uint16_t>(), h = r.get<uint16_t>();
            int sx = r.get<int16_t>(), sy = r.get<int16_t>();
            auto t = r.bytes(size_t(w) * h);
            if (!t || w != MAP_W || h != MAP_H) break;
            terrain.assign(t, t + w * h);
            vis.assign(w * h, 0);
            mmPix.assign(w * h, rgb(18, 22, 30));
            Image im = GenImageColor(w, h, BLACK);
            mmTex = LoadTextureFromImage(im);
            UnloadImage(im);
            cam.target = {(sx + .5f) * TILE, (sy + .5f) * TILE};
            cam.zoom = 1.3f;
            welcomed = true;
            break;
        }
        case S_State: {
            if (!welcomed) break;
            gold = r.get<float>();
            wood = r.get<float>();
            allyMask = r.get<uint16_t>();
            pendMask = r.get<uint16_t>();
            int cnt = r.get<uint16_t>();
            ++stamp;
            for (int i = 0; i < cnt; i++) {
                uint32_t id = r.get<uint32_t>();
                Kind k = Kind(r.get<uint8_t>());
                uint8_t ow = r.get<uint8_t>();
                float x = r.get<float>(), y = r.get<float>();
                float hp = r.get<uint16_t>(), mhp = r.get<uint16_t>();
                if (!r.ok || k >= K_Count) return;
                entt::entity e;
                auto it = ents.find(id);
                if (it == ents.end()) {
                    e = reg.create();
                    ents[id] = e;
                    reg.emplace<NetId>(e, NetId{id});
                    reg.emplace<RPos>(e, RPos{x, y});
                    reg.emplace<Goal>(e, Goal{x, y});
                    reg.emplace<Info>(e, Info{k, ow, hp, mhp});
                    reg.emplace<Seen>(e, Seen{stamp});
                } else {
                    e = it->second;
                    reg.get<Info>(e) = Info{k, ow, hp, mhp};
                    reg.get<Goal>(e) = Goal{x, y};
                    reg.get<Seen>(e).stamp = stamp;
                }
            }
            std::vector<entt::entity> dead;
            reg.view<Seen, NetId>().each([&](entt::entity e, const Seen& s, const NetId& nid) {
                if (s.stamp != stamp) { dead.push_back(e); ents.erase(nid.id); sel.erase(nid.id); }
            });
            for (auto e : dead) reg.destroy(e);
            break;
        }
        case S_Fog: {
            auto b = r.bytes((MAP_W * MAP_H + 7) / 8);
            if (!b || !welcomed) break;
            for (int i = 0; i < MAP_W * MAP_H; i++) vis[i] = (b[i >> 3] >> (i & 7)) & 1;
            rebuildMinimap();
            break;
        }
        case S_Text: msg(r.str()); break;
        default: break;
        }
    }

    void pump() {
        ENetEvent ev;
        while (enet_host_service(host, &ev, 0) > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: connected = true; break;
            case ENET_EVENT_TYPE_RECEIVE: onPacket(ev.packet->data, ev.packet->dataLength); enet_packet_destroy(ev.packet); break;
            case ENET_EVENT_TYPE_DISCONNECT: connected = false; lost = true; break;
            default: break;
            }
        }
    }

    // ---------- commands ----------
    std::vector<uint32_t> selIds() {
        std::vector<uint32_t> v(sel.begin(), sel.end());
        if (v.size() > 255) v.resize(255);
        return v;
    }
    void cmdMove() {
        auto ids = selIds();
        if (ids.empty()) { msg("Select units first (left-click or drag)."); return; }
        Writer w;
        w.put<uint8_t>(C_Move).put<uint8_t>(uint8_t(ids.size()));
        for (auto i : ids) w.put<uint32_t>(i);
        w.put<int16_t>(int16_t(mTx)).put<int16_t>(int16_t(mTy));
        send(w);
        markers.push_back({mTx + .5f, mTy + .5f, GetTime()});
    }
    void cmdAttack() {
        auto ids = selIds();
        if (ids.empty()) { msg("Select army units first (scouts cannot attack)."); return; }
        Writer w;
        w.put<uint8_t>(C_Attack).put<uint8_t>(uint8_t(ids.size()));
        for (auto i : ids) w.put<uint32_t>(i);
        w.put<uint32_t>(mTarget);
        send(w);
    }
    void cmdAlly() {
        Writer w;
        w.put<uint8_t>(C_Ally).put<uint8_t>(uint8_t(mOwner));
        send(w);
    }
    void press(int i) {
        Kind k = btns[i].k;
        if (k <= K_Lumber) {
            buildKind = buildKind == int(k) ? -1 : int(k);
            if (buildKind >= 0) msg("Click a tile to place. Shift keeps building, Esc cancels.");
        } else {
            Writer w;
            w.put<uint8_t>(C_Train).put<uint8_t>(k);
            send(w);
        }
    }

    // ---------- queries ----------
    bool myTH(Pos& out) {
        bool f = false;
        reg.view<Goal, Info>().each([&](const Goal& g, const Info& i) { if (i.kind == K_TownHall && i.owner == me) { out = {g.x, g.y}; f = true; } });
        return f;
    }
    bool canPlace(int tx, int ty, int k) {
        if (!inb(tx, ty) || !vis[ix(tx, ty)]) return false;
        uint8_t t = terrain[ix(tx, ty)];
        if (!(k == K_GoldMine ? (t == T_Clearing || t == T_Mountain) : (t == T_Clearing || t == T_Forest))) return false;
        bool taken = false;
        reg.view<Goal, Info>().each([&](const Goal& g, const Info& i) { if (KINFO[i.kind].building && int(g.x) == tx && int(g.y) == ty) taken = true; });
        Pos th;
        if (taken || !myTH(th)) return false;
        return std::max(std::abs(tx - int(th.x)), std::abs(ty - int(th.y))) <= BUILD_RADIUS && gold >= float(KINFO[k].gold) && wood >= float(KINFO[k].wood);
    }
    Rectangle btnRect(int i) { return {10.f + float(i) * 178.f, float(GetScreenHeight()) - 64.f, 170.f, 54.f}; }
    Rectangle miniRect() { return {float(GetScreenWidth()) - 202.f, 10.f, 192.f, 192.f}; }
    Rectangle menuRect() {
        float h = 24.f + 26.f * float(menu.size());
        return {std::min(menuPos.x, float(GetScreenWidth()) - 164.f), std::min(menuPos.y, float(GetScreenHeight()) - h - 4.f), 160.f, h};
    }

    void doSelect(Vector2 a, Vector2 b, bool add) {
        if (!add) sel.clear();
        if (std::abs(b.x - a.x) > 6 || std::abs(b.y - a.y) > 6) {
            Rectangle R{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
            reg.view<RPos, Info, NetId>().each([&](const RPos& p, const Info& i, const NetId& n) {
                if (i.owner != me || KINFO[i.kind].building) return;
                if (CheckCollisionPointRec(GetWorldToScreen2D({p.x * TILE, p.y * TILE}, cam), R)) sel.insert(n.id);
            });
        } else {
            Vector2 w = GetScreenToWorld2D(b, cam);
            float bd = TILE * .6f;
            uint32_t best = 0;
            reg.view<RPos, Info, NetId>().each([&](const RPos& p, const Info& i, const NetId& n) {
                if (i.owner != me || KINFO[i.kind].building) return;
                float d = std::hypot(w.x - p.x * TILE, w.y - p.y * TILE);
                if (d < bd) { bd = d; best = n.id; }
            });
            if (best) sel.insert(best);
        }
    }

    void openMenu(Vector2 m) {
        Vector2 w = GetScreenToWorld2D(m, cam);
        int tx = int(std::floor(w.x / TILE)), ty = int(std::floor(w.y / TILE));
        if (!inb(tx, ty)) return;
        menu.clear();
        mTx = tx; mTy = ty; mTarget = 0; mOwner = -1;
        reg.view<RPos, Info, NetId>().each([&](const RPos& p, const Info& i, const NetId& n) {
            if (i.kind == K_TownHall && i.owner != me && std::hypot(w.x - p.x * TILE, w.y - p.y * TILE) < TILE * .8f) { mTarget = n.id; mOwner = i.owner; }
        });
        if (mTarget) {
            bool al = allyMask >> mOwner & 1, pe = pendMask >> mOwner & 1;
            menuTitle = mOwner ? "Player " + std::to_string(mOwner) + " Town Hall" : "Neutral Camp";
            menu = {{"Attack", 1, true}, {al ? "Allied" : (pe ? "Accept Alliance" : "Ally"), 2, !al}};
        } else {
            static const char* names[4] = {"Clearing", "Forest", "Mountain", "Sea"};
            menuTitle = vis[ix(tx, ty)] ? std::string(names[terrain[ix(tx, ty)]]) + " (" + std::to_string(tx) + "," + std::to_string(ty) + ")" : "Unexplored (" + std::to_string(tx) + "," + std::to_string(ty) + ")";
            menu = {{"Move Here", 0, true}};
        }
        menuOpen = true;
        menuPos = m;
    }

    // ---------- input ----------
    void input(float dt) {
        Vector2 m = GetMousePosition();
        float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
        cam.offset = {sw * .5f, sh * .5f};
        bool inMini = CheckCollisionPointRec(m, miniRect());
        bool uiHit = inMini || m.y > sh - 72 || (menuOpen && CheckCollisionPointRec(m, menuRect()));

        float sp = 700.f * dt / cam.zoom;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) cam.target.y -= sp;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) cam.target.y += sp;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) cam.target.x -= sp;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) cam.target.x += sp;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) { Vector2 d = GetMouseDelta(); cam.target.x -= d.x / cam.zoom; cam.target.y -= d.y / cam.zoom; }
        float wh = GetMouseWheelMove();
        if (wh != 0 && !uiHit) {
            Vector2 b = GetScreenToWorld2D(m, cam);
            cam.zoom = std::clamp(cam.zoom * (1 + wh * .1f), .35f, 3.f);
            Vector2 a = GetScreenToWorld2D(m, cam);
            cam.target.x += b.x - a.x;
            cam.target.y += b.y - a.y;
        }
        cam.target.x = std::clamp(cam.target.x, 0.f, MAP_W * TILE);
        cam.target.y = std::clamp(cam.target.y, 0.f, MAP_H * TILE);

        if (IsKeyPressed(KEY_ESCAPE)) { menuOpen = false; buildKind = -1; }
        for (int i = 0; i < 5; i++) if (IsKeyPressed(KEY_ONE + i)) press(i);
        bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), lr = IsMouseButtonReleased(MOUSE_BUTTON_LEFT), rp = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

        if (menuOpen && lp) {
            Rectangle mr = menuRect();
            for (size_t i = 0; i < menu.size(); i++) {
                Rectangle r{mr.x, mr.y + 24.f + 26.f * float(i), mr.width, 26.f};
                if (CheckCollisionPointRec(m, r) && menu[i].enabled) {
                    if (menu[i].act == 0) cmdMove();
                    else if (menu[i].act == 1) cmdAttack();
                    else cmdAlly();
                }
            }
            menuOpen = false;
            return;
        }
        if (dragging && lr) { dragging = false; doSelect(dragStart, m, IsKeyDown(KEY_LEFT_SHIFT)); }
        if (inMini && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Rectangle mr = miniRect();
            cam.target = {(m.x - mr.x) / 2.f * TILE, (m.y - mr.y) / 2.f * TILE};
            return;
        }
        if (lp) for (int i = 0; i < 5; i++) if (CheckCollisionPointRec(m, btnRect(i))) { press(i); return; }
        if (uiHit) return;
        if (lp) {
            if (buildKind >= 0) {
                Vector2 w = GetScreenToWorld2D(m, cam);
                Writer wr;
                wr.put<uint8_t>(C_Build).put<uint8_t>(uint8_t(buildKind)).put<int16_t>(int16_t(std::floor(w.x / TILE))).put<int16_t>(int16_t(std::floor(w.y / TILE)));
                send(wr);
                if (!IsKeyDown(KEY_LEFT_SHIFT)) buildKind = -1;
            } else { dragging = true; dragStart = m; }
        }
        if (rp) { if (buildKind >= 0) buildKind = -1; else openMenu(m); }
    }

    // ---------- drawing ----------
    void drawTile(int x, int y, float t) {
        int i = ix(x, y);
        float px = x * TILE, py = y * TILE;
        if (!vis[i]) {                                          // clouds
            DrawRectangleRec({px, py, TILE, TILE}, rgb(205, 214, 228));
            for (int k = 0; k < 3; k++) {
                uint32_t h = hash2(x, y, 100 + k);
                DrawCircleV({px + float(h & 31) + std::sin(t * .5f + float((h >> 8) & 15)) * 3.f, py + float((h >> 5) & 31)}, 9.f + float((h >> 10) & 7), rgb(240, 244, 250));
            }
            return;
        }
        uint8_t tt = terrain[i];
        int v = int(hash2(x, y, 5) & 15) - 8;
        Color b = terrainColor(tt);
        DrawRectangleRec({px, py, TILE, TILE}, rgb(b.r + v, b.g + v, b.b + v));
        if (cam.zoom < .45f) return;
        uint32_t h = hash2(x, y, 9);
        if (tt == T_Forest) {
            for (int k = 0; k < 2; k++) {
                float tx = px + 9.f + 14.f * float(k), ty = py + 10.f + 8.f * float((h >> k) & 1);
                DrawTriangle({tx, ty - 8}, {tx - 7, ty + 9}, {tx + 7, ty + 9}, rgb(28, 85, 40));
            }
        } else if (tt == T_Mountain) {
            DrawTriangle({px + 16, py + 4}, {px + 2, py + 29}, {px + 30, py + 29}, rgb(92, 90, 98));
            DrawTriangle({px + 16, py + 4}, {px + 11, py + 13}, {px + 21, py + 13}, rgb(240, 240, 245));
        } else if (tt == T_Sea) {
            float o = std::sin(t * 1.5f + float(h & 7)) * 3.f;
            DrawLineEx({px + 4, py + 12 + o}, {px + 28, py + 12 + o}, 2, rgb(110, 160, 220));
            DrawLineEx({px + 8, py + 24 - o}, {px + 24, py + 24 - o}, 2, rgb(110, 160, 220));
        } else if (h % 7 == 0) {
            DrawCircleV({px + float(h & 15) + 8, py + float((h >> 4) & 15) + 8}, 2.f, rgb(250, 230, 120));
        }
    }

    void drawEnt(const RPos& p, const Info& i, bool selected) {
        float px = p.x * TILE, py = p.y * TILE;
        Color oc = pcol(i.owner);
        switch (i.kind) {
        case K_TownHall: {
            float x = px - 14, y = py - 14;
            DrawRectangleRec({x, y + 10, 28, 18}, rgb(205, 185, 150));
            DrawTriangle({x + 14, y - 2}, {x - 2, y + 10}, {x + 30, y + 10}, oc);
            DrawRectangleRec({x + 11, y + 17, 6, 11}, rgb(90, 60, 40));
            DrawRectangleLinesEx({x - 2, y - 2, 32, 32}, 2, oc);
            DrawText(i.owner ? TextFormat("P%d", i.owner) : "Camp", int(x), int(y) - 14, 12, oc);
            break;
        }
        case K_GoldMine:
            DrawRectangleRec({px - 12, py - 8, 24, 18}, rgb(80, 70, 60));
            DrawCircleV({px - 4, py + 2}, 5, rgb(255, 205, 40));
            DrawCircleV({px + 5, py + 4}, 4, rgb(255, 215, 70));
            DrawRectangleLinesEx({px - 13, py - 9, 26, 20}, 2, oc);
            break;
        case K_Lumber:
            DrawRectangleRec({px - 12, py - 8, 24, 18}, rgb(130, 90, 50));
            DrawCircleV({px - 5, py + 4}, 4, rgb(190, 140, 80));
            DrawCircleV({px + 4, py + 4}, 4, rgb(190, 140, 80));
            DrawRectangleLinesEx({px - 13, py - 9, 26, 20}, 2, oc);
            break;
        case K_Scout:
            DrawTriangle({px, py - 8}, {px - 6, py + 6}, {px + 6, py + 6}, oc);
            break;
        case K_Soldier:
            DrawCircleV({px, py}, 8, oc);
            DrawCircleLines(int(px), int(py), 8, BLACK);
            break;
        default:
            DrawCircleV({px, py}, 11, oc);
            DrawCircleV({px, py}, 5, rgb(40, 40, 40));
            DrawCircleLines(int(px), int(py), 11, BLACK);
            break;
        }
        float r = KINFO[i.kind].building ? 18.f : (i.kind == K_Knight ? 14.f : 11.f);
        if (selected) DrawCircleLines(int(px), int(py), r + 3, rgb(255, 255, 80));
        if (i.hp < i.maxhp) {
            DrawRectangleRec({px - 14, py - r - 8, 28, 4}, BLACK);
            DrawRectangleRec({px - 14, py - r - 8, 28 * i.hp / i.maxhp, 4}, rgb(80, 220, 80));
        }
    }

    void drawWorld(float t) {
        Vector2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(GetScreenWidth()), float(GetScreenHeight())}, cam);
        int x0 = std::max(0, int(std::floor(tl.x / TILE))), y0 = std::max(0, int(std::floor(tl.y / TILE)));
        int x1 = std::min(MAP_W - 1, int(std::floor(br.x / TILE))), y1 = std::min(MAP_H - 1, int(std::floor(br.y / TILE)));
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) drawTile(x, y, t);
        for (auto& mk : markers) {
            float a = float(GetTime() - mk.t);
            DrawCircleLines(int(mk.x * TILE), int(mk.y * TILE), 6 + a * 24, rgb(255, 255, 255, int(255 * std::max(0.f, 1 - a * 2))));
        }
        std::erase_if(markers, [](const Marker& m) { return GetTime() - m.t > .5; });
        Rectangle vr{tl.x - TILE, tl.y - TILE, br.x - tl.x + 2 * TILE, br.y - tl.y + 2 * TILE};   // frustum bounds
        for (int pass = 0; pass < 2; pass++)
            reg.view<RPos, Info, NetId>().each([&](const RPos& p, const Info& i, const NetId& n) {
                if (KINFO[i.kind].building != (pass == 0)) return;
                if (!CheckCollisionPointRec({p.x * TILE, p.y * TILE}, vr)) return;
                drawEnt(p, i, sel.count(n.id) > 0);
            });
        if (buildKind >= 0 && !CheckCollisionPointRec(GetMousePosition(), miniRect()) && GetMouseY() < GetScreenHeight() - 72) {
            Vector2 w = GetScreenToWorld2D(GetMousePosition(), cam);
            int tx = int(std::floor(w.x / TILE)), ty = int(std::floor(w.y / TILE));
            bool ok = canPlace(tx, ty, buildKind);
            DrawRectangleRec({tx * TILE, ty * TILE, TILE, TILE}, ok ? rgb(80, 255, 80, 110) : rgb(255, 60, 60, 110));
        }
    }

    void drawUI() {
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        Vector2 m = GetMousePosition();
        DrawRectangle(0, 0, 520, 34, rgb(0, 0, 0, 150));
        int units = 0;
        reg.view<Info>().each([&](const Info& i) { if (i.owner == me && !KINFO[i.kind].building) units++; });
        DrawRectangle(8, 8, 18, 18, pcol(me));
        DrawText(TextFormat("P%d   Gold %d   Wood %d   Army %d/%d   FPS %d", me, int(gold), int(wood), units, UNIT_CAP, GetFPS()), 34, 9, 18, WHITE);
        DrawRectangle(0, sh - 72, sw, 72, rgb(20, 22, 30, 230));
        for (int i = 0; i < 5; i++) {
            Rectangle r = btnRect(i);
            const KindInfo& k = KINFO[btns[i].k];
            bool afford = gold >= float(k.gold) && wood >= float(k.wood);
            bool active = buildKind == int(btns[i].k);
            DrawRectangleRec(r, active ? rgb(70, 110, 70) : (CheckCollisionPointRec(m, r) ? rgb(70, 74, 92) : rgb(48, 52, 68)));
            DrawRectangleLinesEx(r, 2, afford ? rgb(120, 130, 160) : rgb(150, 60, 60));
            DrawText(TextFormat("[%d] %s", i + 1, btns[i].name), int(r.x) + 8, int(r.y) + 8, 18, WHITE);
            DrawText(TextFormat("%dg %dw", k.gold, k.wood), int(r.x) + 8, int(r.y) + 31, 16, afford ? rgb(255, 215, 90) : rgb(255, 110, 110));
        }
        DrawText("LMB select/drag | RMB menu | WASD/MMB pan | wheel zoom | Esc cancel", 10 + 5 * 178, sh - 50, 14, rgb(170, 175, 190));
        double now = GetTime();
        int ly = sh - 90;
        for (int i = int(logs.size()) - 1; i >= 0; i--) {
            double age = now - logs[i].t;
            if (age > 9) continue;
            DrawText(logs[i].text.c_str(), 12, ly, 16, rgb(255, 255, 255, int(255 * std::min(1.0, (9 - age) / 2))));
            ly -= 20;
        }
        // minimap
        Rectangle mr = miniRect();
        DrawRectangleRec({mr.x - 2, mr.y - 2, mr.width + 4, mr.height + 4}, BLACK);
        DrawTexturePro(mmTex, {0, 0, float(MAP_W), float(MAP_H)}, mr, {0, 0}, 0, WHITE);
        reg.view<Goal, Info>().each([&](const Goal& g, const Info& i) {
            DrawRectangle(int(mr.x + g.x * 2) - 1, int(mr.y + g.y * 2) - 1, KINFO[i.kind].building ? 4 : 3, KINFO[i.kind].building ? 4 : 3, pcol(i.owner));
        });
        Vector2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(sw), float(sh)}, cam);
        DrawRectangleLines(int(mr.x + tl.x / TILE * 2), int(mr.y + tl.y / TILE * 2), int((br.x - tl.x) / TILE * 2), int((br.y - tl.y) / TILE * 2), WHITE);
        if (dragging) {
            Rectangle R{std::min(dragStart.x, m.x), std::min(dragStart.y, m.y), std::abs(m.x - dragStart.x), std::abs(m.y - dragStart.y)};
            if (R.width > 6 || R.height > 6) { DrawRectangleRec(R, rgb(80, 255, 80, 40)); DrawRectangleLinesEx(R, 1, rgb(80, 255, 80)); }
        }
        if (menuOpen) {
            Rectangle r = menuRect();
            DrawRectangleRec(r, rgb(28, 30, 40, 245));
            DrawRectangleLinesEx(r, 2, rgb(150, 160, 190));
            DrawText(menuTitle.c_str(), int(r.x) + 6, int(r.y) + 5, 14, rgb(190, 195, 210));
            for (size_t i = 0; i < menu.size(); i++) {
                Rectangle ir{r.x, r.y + 24.f + 26.f * float(i), r.width, 26.f};
                if (menu[i].enabled && CheckCollisionPointRec(m, ir)) DrawRectangleRec(ir, rgb(70, 110, 170));
                DrawText(menu[i].label.c_str(), int(ir.x) + 10, int(ir.y) + 5, 16, menu[i].enabled ? WHITE : GRAY);
            }
        }
    }

    // ---------- main loop ----------
    void run() {
        host = enet_host_create(nullptr, 1, 2, 0, 0);
        ENetAddress a{};
        if (!host || enet_address_set_host(&a, ip.c_str()) != 0) { TraceLog(LOG_ERROR, "Bad server address '%s'", ip.c_str()); return; }
        a.port = uint16_t(port);
        peer = enet_host_connect(host, &a, 2, 0);
        startT = GetTime();
        disp.sink<LogEvent>().connect<&onLog>();
        while (!WindowShouldClose()) {
            float dt = GetFrameTime();
            pump();
            disp.update();
            if (welcomed && !lost) {
                input(dt);
                reg.view<RPos, Goal>().each([&](RPos& p, const Goal& g) {
                    float k = std::min(1.f, 12.f * dt);
                    p.x += (g.x - p.x) * k;
                    p.y += (g.y - p.y) * k;
                });
            }
            BeginDrawing();
            ClearBackground(rgb(20, 22, 30));
            if (welcomed && !lost) {
                BeginMode2D(cam);
                drawWorld(float(GetTime()));
                EndMode2D();
                drawUI();
            } else {
                std::string s = lost ? "Disconnected from server." : (GetTime() - startT > 8 && !connected ? "Could not reach " + ip + ":" + std::to_string(port) + " - check config.txt" : "Connecting to " + ip + ":" + std::to_string(port) + " ...");
                DrawText(s.c_str(), 40, 40, 28, WHITE);
            }
            EndDrawing();
        }
        if (peer && connected) { enet_peer_disconnect(peer, 0); enet_host_flush(host); }
        enet_host_destroy(host);
    }
};

void loadConfig(std::string& ip, int& port) {
    ip = "127.0.0.1";
    port = DEFAULT_PORT;
    std::ifstream f(std::string(GetApplicationDirectory()) + "config.txt");
    if (!f) f.open("config.txt");
    if (!f) { TraceLog(LOG_WARNING, "config.txt not found next to the executable; using %s:%d", ip.c_str(), port); return; }
    auto trim = [](const std::string& s) {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    };
    std::string ln;
    while (std::getline(f, ln)) {
        auto p = ln.find('=');
        if (p == std::string::npos || ln[0] == '#') continue;
        std::string k = trim(ln.substr(0, p)), v = trim(ln.substr(p + 1));
        if (k == "server_ip" || k == "ip") ip = v;
        else if (k == "server_port" || k == "port") port = std::atoi(v.c_str());
    }
}

}  // namespace

int main() {
    App app;
    loadConfig(app.ip, app.port);
    if (enet_initialize() != 0) { TraceLog(LOG_ERROR, "enet_initialize failed"); return 1; }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Base Wars");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    app.run();
    CloseWindow();
    enet_deinitialize();
}
