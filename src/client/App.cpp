// Raylib client: Tiny Swords art, Tiled-driven UI panels, biome terrain with autotiled cliffs, shader effects,
// building placement, context panels, alliances and RTS controls.
#include "client/Settings.hpp"
#include "common/Shared.hpp"
#include "networking/ServerBrowser.hpp"
#include "networking/SteamClient.hpp"
#include "networking/SteamClientNet.hpp"
#include "rendering/Assets.hpp"

#include <entt/entt.hpp>
#include <raylib.h>
#include <rlgl.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef near
#undef near
#endif

namespace {

constexpr float TILE = 64.f;

const char* COLOR_NAME[TEAM_COLORS] = {"Blue", "Red", "Yellow", "Purple", "Black"};
Color teamColor(int owner) {
    static const Color c[TEAM_COLORS] = {rgb(70, 130, 235), rgb(225, 70, 70), rgb(240, 205, 60), rgb(165, 95, 205), rgb(95, 95, 110)};
    return owner < 1 ? rgb(235, 235, 235) : c[(isFactionOwner(owner) ? FACTION_COLOR[owner - FACTION_BASE] : (owner - 1)) % TEAM_COLORS];
}
int colorIdx(int owner) { return isFactionOwner(owner) ? FACTION_COLOR[owner - FACTION_BASE] : (owner < 1 ? 0 : (owner - 1) % TEAM_COLORS); }
Color clanColor(int clan) {
    static const Color c[8] = {rgb(255, 120, 60), rgb(70, 220, 190), rgb(240, 90, 170), rgb(150, 220, 70), rgb(120, 140, 255), rgb(255, 210, 60), rgb(200, 120, 255), rgb(90, 200, 255)};
    return c[(clan + 7) % 8];
}
Color biomeColor(int b) {
    static const Color c[B_Count] = {rgb(46, 102, 172), rgb(140, 190, 80), rgb(50, 120, 58), rgb(122, 120, 128), rgb(214, 190, 110), rgb(205, 150, 205), rgb(235, 240, 248)};
    return b >= 0 && b < B_Count ? c[b] : rgb(20, 24, 34);
}
float smooth01(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3 - 2 * x); }

// ---------------------------------------------------------------- client state types
struct NetId { uint32_t id; };
struct Info { Kind kind; uint8_t owner, state, aux, aim, prog; float hp, maxhp; uint16_t stock; };
struct RPos { float x, y; };
struct Goal { float x, y; };
struct Face { bool left = false; };
struct Seen { uint32_t stamp; };
struct LogEvent { std::string text; };
struct LogLine { std::string text; double t; };
struct Peer { int id; uint8_t flags, clan; std::string name; bool hasLoc; float lx, ly; };
struct ClanInfo { int id; std::string name; int members, leader; };
struct FacTown { int faction; float x, y; };
struct Chunk { uint8_t biome[CH * CH]; uint8_t fog[32]; bool hasTerr = false, hasFog = false; };
struct Fx { uint8_t type; float x, y, x2, y2; double t; };
struct Ghost { Kind kind; uint8_t owner, level; float x, y; bool left; double t; };
struct DrawItem { float y; std::function<void()> fn; };

std::vector<LogLine> logs;
void onLog(const LogEvent& e) {
    logs.push_back({e.text, GetTime()});
    if (logs.size() > 7) logs.erase(logs.begin());
}


// ---------------------------------------------------------------- the application
struct App {
    enum Mode { M_None, M_Move, M_Attack };
    struct MenuAct { std::string label; int act; uint32_t a, b; bool enabled; bool red; };
    struct BuildEntry { Kind k; };
    const BuildEntry buildList[7] = {{K_Outpost}, {K_Harbour}, {K_Storage}, {K_Archery}, {K_Mine}, {K_Lumber}, {K_Campfire}};

    // network
    Settings settings;
    bool connected = false, welcomed = false, lost = false, helloSent = false;
    double startT = 0, lastCam = 0;
    std::string ip, uuid, myName;
    int port = DEFAULT_PORT;

    // world
    int me = 0;
    uint32_t seed = 0;
    float res[R_Count]{};
    std::unordered_map<int64_t, Chunk> chunks;
    std::unordered_set<int64_t> occ;
    std::vector<Peer> roster;
    entt::registry reg;
    entt::dispatcher disp;
    std::unordered_map<uint32_t, entt::entity> ents;
    std::set<uint32_t> sel;
    uint32_t stamp = 0, hovered = 0, ctxTarget = 0;
    std::vector<Fx> fxs;
    std::vector<Ghost> ghosts;
    struct Ripple { float x, y; double t; };
    std::vector<Ripple> ripples;
    struct Marker { float x, y; double t; };
    std::vector<Marker> markers;

    // view / input
    Camera2D cam{};
    bool dragging = false, buildOpen = false, ctxOpen = false;
    // social menu / big map
    std::vector<ClanInfo> clansList;
    std::vector<FacTown> facTowns;
    int facRep[FACTION_COUNT]{}, unitLvl[5]{1, 1, 1, 1, 1};
    struct ChatLine { uint8_t ch; std::string name, text; double t; };
    std::vector<ChatLine> chatLines;
    bool chatOpen = false, trainOpen = false, ctxSlider = false, sliderDrag = false;
    int chatCh = 0, trainSel = 0, ctxPct = 75;
    uint32_t trainTarget = 0;
    std::string chatInput;
    std::vector<std::pair<std::string, Color>> ctxLines;
    std::vector<std::string> ctxNext;
    std::vector<Kind> ctxNextUnits;
    int locOut[5]{-1, -1, -1, -1, -1}, locDis[4]{-1, -1, -1, -1};   // cached uniform locations (outline: uvRect/texelSize/outlineColor/thickness/time; dissolve: uvRect/texSize/progress/seed)
    TMap mTrain;
#include "client_pause.inc"
#include "client_menu.inc"
#include "client_world.inc"                           // [world] clearing, drops, gold sparkle, land clouds
#include "client_ships.inc"
#include "client_base.inc"      // base mechanics: fire, queues, clan alerts, relocation (client side)
    bool socialOpen = false, bigMapOpen = false, textFocus = false, bmDragging = false;
    int socialTab = 0, socialScroll = 0, bmZoom = 4, bmW = 0, bmH = 0;
    std::string searchText;
    Vector2 bmCenter{}, bmDragMouse{};
    float bmDragMoved = 0;
    double bmLast = 0;
    std::vector<Color> bmPix;
    Texture2D bmTex{};
    TMap mSocial, mMapFrame;
    Vector2 dragStart{}, ctxPos{};
    float buildSlide = 0;
    int buildKind = -1;
    Mode mode = M_None;
    std::vector<MenuAct> ctx;
    float shakeAmt = 0, damageAmt = 0, coldAmt = 0, warmAmt = 0;
    double lastMM = 0;
    std::vector<Color> mmPix;
    Texture2D mmTex{};
    static constexpr int MM = 100;

    // gfx resources
    RenderTexture2D rt{}, bloomA{}, bloomB{};
    Shader shOutline{}, shWater{}, shDissolve{}, shPost{}, shBright{}, shBlur{};
    bool okOutline = false, okWater = false, okDissolve = false, okPost = false, okBloom = false;
    TMap mHud, mTitle, mBuild, mMini, mCtx, mRoster, mBtn, mBtnP, mBtnR, mBtnRP;

    void send(const Writer& w) { cnet.send(w); }
    void msg(const std::string& s) { disp.enqueue(LogEvent{s}); }
    bool hasMe() const { return me > 0; }

    // ---------------------------------------------------------------- profile / config
    void loadProfile() {
        std::string path = std::string(GetApplicationDirectory()) + "profile.txt";
        std::ifstream f(path);
        std::string ln;
        while (f && std::getline(f, ln)) {
            auto p = ln.find('=');
            if (p == std::string::npos) continue;
            std::string k = ln.substr(0, p), v = ln.substr(p + 1);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            if (k == "uuid") uuid = v;
            else if (k == "name" && myName.empty()) myName = v;
        }
        std::random_device rd;
        if (uuid.size() < 16) {
            char buf[40];
            uint32_t a = rd(), b = rd(), c = rd(), d = rd();
            std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%04x%08x", a, b >> 16, b & 0xfff, 0x8000 | (c & 0x3fff), c >> 16, d);
            uuid = buf;
        }
        if (myName.empty()) myName = "Player" + std::to_string(rd() % 9000 + 1000);
        std::ofstream o(path);
        if (o) o << "uuid=" << uuid << "\nname=" << myName << "\n";
    }

    // ---------------------------------------------------------------- terrain access
    int biomeAt(int x, int y) const {
        auto it = chunks.find(chunkKey(chunkOf(x), chunkOf(y)));
        return it == chunks.end() || !it->second.hasTerr ? INVALID_BIOME : it->second.biome[localIdx(x, y)];
    }
    bool exploredAt(int x, int y) const {
        auto it = chunks.find(chunkKey(chunkOf(x), chunkOf(y)));
        if (it == chunks.end() || !it->second.hasFog) return false;
        int i = localIdx(x, y);
        return it->second.fog[i >> 3] >> (i & 7) & 1;
    }
    const Peer* peerOf(int id) const { for (auto& p : roster) if (p.id == id) return &p; return nullptr; }
    int myClan() const { auto p = peerOf(me); return p ? p->clan : 0; }
    bool alliedWith(int owner) const {
        if (owner == me) return true;
        if (isFactionOwner(owner)) return false;
        auto p = peerOf(owner);
        return p && ((p->flags & 2) || (p->clan && p->clan == myClan()));
    }
    bool hostileTo(int owner) const {
        if (isFactionOwner(owner)) return facRep[owner - FACTION_BASE] <= HOSTILE_REP;
        return owner != me && !alliedWith(owner);
    }
    std::string nameOf(int owner) const {
        if (owner == me) return myName;
        if (isFactionOwner(owner)) return FACTION_NAME[owner - FACTION_BASE];
        auto p = peerOf(owner);
        return p ? p->name : (owner ? "Player " + std::to_string(owner) : "Wildlife");
    }
    std::string clanName(int c) const { for (auto& k : clansList) if (k.id == c) return k.name; return ""; }

    // ---------------------------------------------------------------- networking
    void rebuildOcc() {
        occ.clear();
        reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {
            if (!isBuilding(i.kind)) return;
            int tx = int(std::lround(g.x - float(KINFO[i.kind].fw) * .5f)), ty = int(std::lround(g.y - float(KINFO[i.kind].fh) * .5f));
            for (int y = ty; y < ty + KINFO[i.kind].fh; y++) for (int x = tx; x < tx + KINFO[i.kind].fw; x++) occ.insert(chunkKey(x, y));
        });
    }
    void addFx(uint8_t type, float x, float y, float x2 = 0, float y2 = 0) {
        fxs.push_back({type, x, y, x2, y2, GetTime()});
        if (type == E_Splash) ripples.push_back({x * TILE, y * TILE, GetTime()});
        if (type == E_Explosion) {
            float d = std::hypot(x * TILE - cam.target.x, y * TILE - cam.target.y) / TILE;
            shakeAmt = std::max(shakeAmt, std::clamp(14.f - d * .6f, 0.f, 14.f));
        }
    }
    bool visibleSource(float x, float y) {
        bool v = false;
        reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {
            if (v || !alliedWith(i.owner) || (isBuilding(i.kind) && i.prog < 100)) return;
            float r = float(KINFO[i.kind].vision) - 1.5f;
            if (r > 0 && std::hypot(g.x - x, g.y - y) < r) v = true;
        });
        return v;
    }

    void onPacket(const uint8_t* d, size_t n) {
        Reader r{d, d + n};
        uint8_t ptype = r.get<uint8_t>();
        switch (ptype) {
        case S_Welcome: {
            me = r.get<uint8_t>();
            seed = r.get<uint32_t>();
            float cx = r.get<float>(), cy = r.get<float>();
            std::string nm = r.str();
            if (!r.ok) break;
            myName = nm;
            cam.target = {cx * TILE, cy * TILE};
            cam.zoom = 1.f;
            if (const char* c = std::getenv("BW_CAM")) { float a = 0, b = 0; if (std::sscanf(c, "%f,%f", &a, &b) == 2) cam.target = {a * TILE, b * TILE}; }
            if (const char* z = std::getenv("BW_ZOOM")) cam.zoom = float(std::atof(z));
            welcomed = true;
            screen = Screen::Playing;
            SetWindowTitle("Base Wars - connected");
            break;
        }
        case S_State: {
            if (!welcomed) break;
            for (float& v : res) v = r.get<float>();
            for (int& f : facRep) f = r.get<int8_t>();
            for (int& u : unitLvl) u = r.get<uint8_t>();
            int cnt = r.get<uint16_t>();
            ++stamp;
            for (int i = 0; i < cnt; i++) {
                uint32_t id = r.get<uint32_t>();
                Kind k = Kind(r.get<uint8_t>());
                uint8_t ow = r.get<uint8_t>();
                float x = r.get<float>(), y = r.get<float>();
                float hp = r.get<uint16_t>(), mhp = r.get<uint16_t>();
                uint8_t st = r.get<uint8_t>(), aux = r.get<uint8_t>(), aim = r.get<uint8_t>(), prog = r.get<uint8_t>();
                uint16_t stock = r.get<uint16_t>();
                if (!r.ok || k >= K_Count) return;
                Info inf{k, ow, st, aux, aim, prog, hp, mhp, stock};
                if (aboardUnit(inf)) { sel.erase(id); if (hovered == id) hovered = 0; }   // embarked: hidden, cannot be selected
                auto it = ents.find(id);
                if (it == ents.end()) {
                    auto e = reg.create();
                    ents[id] = e;
                    reg.emplace<NetId>(e, NetId{id});
                    reg.emplace<RPos>(e, RPos{x, y});
                    reg.emplace<Goal>(e, Goal{x, y});
                    reg.emplace<Face>(e);
                    reg.emplace<Info>(e, inf);
                    reg.emplace<Seen>(e, Seen{stamp});
                } else {
                    auto e = it->second;
                    auto& old = reg.get<Info>(e);
                    if (ow == me && hp < old.hp - .5f) { damageAmt = std::min(.7f, damageAmt + .12f); shakeAmt = std::max(shakeAmt, 3.f); }
                    old = inf;
                    reg.get<Goal>(e) = Goal{x, y};
                    reg.get<Seen>(e).stamp = stamp;
                }
            }
            std::vector<entt::entity> dead;
            reg.view<Seen, NetId>().each([&](entt::entity e, const Seen& s, const NetId& nid) {
                if (s.stamp != stamp) dead.push_back(e);
                (void)nid;
            });
            for (auto e : dead) {
                uint32_t id = reg.get<NetId>(e).id;
                const auto& in = reg.get<Info>(e);
                const auto& rp = reg.get<RPos>(e);
                if (visibleSource(rp.x, rp.y)) ghosts.push_back({in.kind, in.owner, 1, rp.x, rp.y, reg.get<Face>(e).left, GetTime()});
                ents.erase(id);
                sel.erase(id);
                if (hovered == id) hovered = 0;
                if (ctxTarget == id) ctxOpen = false;
                reg.destroy(e);
            }
            int ne = r.get<uint16_t>();
            for (int i = 0; i < ne; i++) {
                uint8_t t = r.get<uint8_t>();
                float x = r.get<float>(), y = r.get<float>(), x2 = r.get<float>(), y2 = r.get<float>();
                if (!r.ok) break;
                addFx(t, x, y, x2, y2);
            }
            rebuildOcc();
            break;
        }
        case S_Fog: {
            if (!welcomed) break;
            int nt = r.get<uint16_t>();
            for (int i = 0; i < nt; i++) {
                int cx = r.get<int32_t>(), cy = r.get<int32_t>();
                auto b = r.bytes(CH * CH);
                if (!b) return;
                Chunk& c = chunks[chunkKey(cx, cy)];
                std::memcpy(c.biome, b, CH * CH);
                c.hasTerr = true;
            }
            int nf = r.get<uint16_t>();
            for (int i = 0; i < nf; i++) {
                int cx = r.get<int32_t>(), cy = r.get<int32_t>();
                auto b = r.bytes(32);
                if (!b) return;
                Chunk& c = chunks[chunkKey(cx, cy)];
                std::memcpy(c.fog, b, 32);
                c.hasFog = true;
            }
            lastMM = 0;
            break;
        }
        case S_Text: msg(r.str()); break;
        case S_Roster: {
            int n = r.get<uint8_t>();
            std::vector<Peer> v;
            for (int i = 0; i < n; i++) {
                Peer p{};
                p.id = r.get<uint8_t>();
                p.flags = r.get<uint8_t>();
                p.clan = r.get<uint8_t>();
                p.name = r.str();
                p.hasLoc = r.get<uint8_t>() != 0;
                p.lx = r.get<float>();
                p.ly = r.get<float>();
                if (!r.ok) return;
                v.push_back(p);
            }
            int nc = r.get<uint8_t>();
            std::vector<ClanInfo> cl;
            for (int i = 0; i < nc; i++) {
                ClanInfo c{};
                c.id = r.get<uint8_t>();
                c.name = r.str();
                c.members = r.get<uint8_t>();
                c.leader = r.get<uint8_t>();
                if (!r.ok) return;
                cl.push_back(c);
            }
            roster = v;
            clansList = cl;
            break;
        }
        case S_Chat: {
            ChatLine c{};
            c.ch = r.get<uint8_t>();
            r.get<uint8_t>();
            c.name = r.str();
            c.text = r.str();
            c.t = GetTime();
            if (!r.ok) return;
            chatLines.push_back(c);
            if (chatLines.size() > 80) chatLines.erase(chatLines.begin());
            break;
        }
        case S_Map: {
            int n = r.get<uint8_t>();
            std::vector<FacTown> v;
            for (int i = 0; i < n; i++) {
                FacTown t{};
                t.faction = r.get<uint8_t>();
                t.x = r.get<float>();
                t.y = r.get<float>();
                if (!r.ok) return;
                v.push_back(t);
            }
            facTowns = v;
            break;
        }
        case S_Paths: if (welcomed) onShipsPaths(r); break;
        case S_ShipInfo: if (welcomed) onShipsInfo(r); break;
        default: worldPacket(d, n); baseOnPacket(ptype, r); break;     // [world] / [base] packet sets (each ignores the other's types)
        }
    }
    void pump() {
        NetEvent ev;
        while (cnet.poll(ev)) {
            switch (ev.type) {
            case NetEvent::Connect:
                connected = true;
                if (!helloSent) { sendHello(); helloSent = true; }
                break;
            case NetEvent::Receive: onPacket(ev.data.data(), ev.data.size()); break;
            case NetEvent::Disconnect:
                connected = false;
                if (screen != Screen::Menu) backToMenu(welcomed ? "Disconnected from the server." : "The server closed the connection.");
                break;
            }
        }
    }

    // ---------------------------------------------------------------- queries
    Pos myTH() {
        Pos out{cam.target.x / TILE, cam.target.y / TILE};
        reg.view<Goal, Info>().each([&](const Goal& g, const Info& i) { if (i.kind == K_TownHall && i.owner == me) out = {g.x, g.y}; });
        return out;
    }
    struct PW {
        App& a;
        int biome(int x, int y) { return a.biomeAt(x, y); }
        bool explored(int x, int y) { return a.exploredAt(x, y); }
        bool occupied(int x, int y) { return a.occ.count(chunkKey(x, y)) > 0; }
        template <class F> void anchors(F f) {
            a.reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {
                if ((i.kind == K_TownHall || i.kind == K_Outpost) && !(i.state & BLD_DISABLED)) f(Pos{g.x, g.y}, i.kind, i.owner == a.me, i.owner != a.me && a.alliedWith(i.owner));
            });
        }
        bool campfireNear(Pos c, float r) {
            bool hit = false;
            a.reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) { if (i.kind == K_Campfire && std::hypot(g.x - c.x, g.y - c.y) < r) hit = true; });
            return hit;
        }
        bool inClanRadius(Pos c) {                                  // clan towns we know of (roster locations of clan mates)
            for (auto& p : a.roster)
                if (p.id != a.me && p.hasLoc && p.clan && p.clan == a.myClan() && std::max(std::abs(p.lx - c.x), std::abs(p.ly - c.y)) <= CLAN_TOWN_RADIUS) return true;
            return false;
        }
    };
    IV placeTile(Kind k, Vector2 world) {
        return {int(std::floor(world.x / TILE - float(KINFO[k].fw) * .5f + .5f)), int(std::floor(world.y / TILE - float(KINFO[k].fh) * .5f + .5f))};
    }
    bool affordable(const float* cost) const { for (int r = 0; r < R_Count; r++) if (res[r] + 1e-3f < cost[r]) return false; return true; }
    int armyCount() { int n = 0; for (auto id : sel) { auto it = ents.find(id); if (it != ents.end() && !isBuilding(reg.get<Info>(it->second).kind)) n++; } return n; }

    // ---------------------------------------------------------------- layout
    Rectangle hudRect() { Vector2 s = mHud.size(); return {10, 10, s.x, s.y}; }
    Rectangle nameRect() { Vector2 s = mTitle.size(); return {10, 78, s.x, s.y}; }
    Rectangle miniFrame() { Vector2 s = mMini.size(); return {float(GetScreenWidth()) - s.x - 8, 8, s.x, s.y}; }
    Rectangle miniRect() { Rectangle f = miniFrame(); float in = 46.f * mMini.scale / .4f * 1.f; return {f.x + in, f.y + in, f.width - 2 * in, f.height - 2 * in}; }
    Rectangle buildRect() { Vector2 s = mBuild.size(); return {10 - (1 - buildSlide) * (s.x + 20), 150, s.x, s.y}; }
    Rectangle buildTab() { return {std::max(0.f, buildRect().x + buildRect().width - 6), 150, 40, 56}; }
    Rectangle buildItem(int i) { Rectangle b = buildRect(); float pad = float(mBuild.padding); return {b.x + pad + 2, b.y + pad + 34 + float(i) * 47.f, b.width - 2 * pad - 4, 44}; }
    Rectangle coordRect() { Rectangle f = miniFrame(); return {f.x + f.width / 2 - 78, f.y + f.height + 2, 156, 24}; }
    Rectangle socialTabBtn() { return {0, 214, 40, 56}; }
    Rectangle socialRect() {                                    // natural size, shrunk to fit small windows
        Vector2 s = mSocial.size();
        float w = std::min(s.x, float(GetScreenWidth()) - 16), h = std::min(s.y, float(GetScreenHeight()) - 16);
        return {(float(GetScreenWidth()) - w) / 2, (float(GetScreenHeight()) - h) / 2, w, h};
    }
    Rectangle mapFrameRect() {
        Vector2 s = mMapFrame.size();
        float f = std::min({1.f, (float(GetScreenWidth()) - 30) / s.x, (float(GetScreenHeight()) - 30) / s.y});
        return {(float(GetScreenWidth()) - s.x * f) / 2, (float(GetScreenHeight()) - s.y * f) / 2, s.x * f, s.y * f};
    }
    Rectangle mapInner() {
        Rectangle r = mapFrameRect();
        float f = r.width / mMapFrame.size().x, in = 46.f * (mMapFrame.scale / .4f) * f;
        return {r.x + in, r.y + in, r.width - 2 * in, r.height - 2 * in};
    }
    Rectangle ctxRect() {
        Vector2 s = mCtx.size();
        return {std::clamp(ctxPos.x, 8.f, float(GetScreenWidth()) - s.x - 8), std::clamp(ctxPos.y, 56.f, float(GetScreenHeight()) - s.y - 8), s.x, s.y};
    }
    bool overUI(Vector2 m) {
        if (CheckCollisionPointRec(m, hudRect()) || CheckCollisionPointRec(m, nameRect()) || CheckCollisionPointRec(m, miniFrame())) return true;
        if (CheckCollisionPointRec(m, buildTab()) || (buildSlide > .02f && CheckCollisionPointRec(m, buildRect()))) return true;
        if (CheckCollisionPointRec(m, coordRect()) || CheckCollisionPointRec(m, socialTabBtn())) return true;
        if (socialOpen || bigMapOpen || trainOpen || chatOpen) return true;
        if (overBaseUI(m)) return true;
        if (ctxOpen && CheckCollisionPointRec(m, ctxRect())) return true;
        if (shipPanelOver(m)) return true;
        return false;
    }

    // ---------------------------------------------------------------- commands
    std::vector<uint32_t> selUnits() {
        std::vector<uint32_t> v;
        for (auto id : sel) { auto it = ents.find(id); if (it != ents.end() && !isBuilding(reg.get<Info>(it->second).kind) && v.size() < 255) v.push_back(id); }
        return v;
    }
    void cmdMove(Vector2 w) {
        auto ids = selUnits();
        if (ids.empty()) { msg("Select units first (left-click or drag)."); return; }
        Writer wr;
        wr.put<uint8_t>(C_Move).put<uint8_t>(uint8_t(ids.size()));
        for (auto i : ids) wr.put<uint32_t>(i);
        wr.put<float>(w.x / TILE).put<float>(w.y / TILE);
        send(wr);
        markers.push_back({w.x, w.y, GetTime()});
        if (biomeAt(int(std::floor(w.x / TILE)), int(std::floor(w.y / TILE))) == B_Ocean) ripples.push_back({w.x, w.y, GetTime()});
    }
    void cmdAttackMove(Vector2 w) {
        auto ids = selUnits();
        if (ids.empty()) { msg("Select units first."); return; }
        Writer wr;
        wr.put<uint8_t>(C_AttackMove).put<uint8_t>(uint8_t(ids.size()));
        for (auto i : ids) wr.put<uint32_t>(i);
        wr.put<float>(w.x / TILE).put<float>(w.y / TILE);
        send(wr);
        markers.push_back({w.x, w.y, GetTime()});
    }
    void cmdAttack(uint32_t target) {
        auto ids = selUnits();
        if (ids.empty()) { msg("Select army units first."); return; }
        Writer wr;
        wr.put<uint8_t>(C_Attack).put<uint8_t>(uint8_t(ids.size()));
        for (auto i : ids) wr.put<uint32_t>(i);
        wr.put<uint32_t>(target);
        send(wr);
    }
    void cmdAlly(int to, int act) { Writer w; w.put<uint8_t>(C_Ally).put<uint8_t>(uint8_t(to)).put<uint8_t>(uint8_t(act)); send(w); }
    void cmdTrain(uint32_t b, Kind k) { Writer w; w.put<uint8_t>(C_Train).put<uint32_t>(b).put<uint8_t>(k); send(w); }
    void cmdUpgrade(uint32_t b) { Writer w; w.put<uint8_t>(C_Upgrade).put<uint32_t>(b); send(w); }
    void cmdClanCreate(const std::string& n) { Writer w; w.put<uint8_t>(C_ClanCreate); w.str(n); send(w); }
    void cmdClanJoin(int c) { Writer w; w.put<uint8_t>(C_ClanJoin).put<uint8_t>(uint8_t(c)); send(w); }
    void cmdClanLeave() { Writer w; w.put<uint8_t>(C_ClanLeave); send(w); }
    void cmdTransfer(int to, int res, float amount) {
        Writer w;
        w.put<uint8_t>(C_Transfer).put<uint8_t>(uint8_t(to));
        for (int i = 0; i < R_Count; i++) w.put<float>(i == res ? amount : 0.f);
        send(w);
    }
    void cmdDonate(int faction, int res, float amount) { Writer w; w.put<uint8_t>(C_Donate).put<uint8_t>(uint8_t(faction)).put<uint8_t>(uint8_t(res)).put<float>(amount); send(w); }
    void cmdSetHaul(uint32_t b, int pct) { Writer w; w.put<uint8_t>(C_SetHaul).put<uint32_t>(b).put<uint8_t>(uint8_t(pct)); send(w); }
    void cmdUpgradeUnit(Kind k) { Writer w; w.put<uint8_t>(C_UpgradeUnit).put<uint8_t>(k); send(w); }
    void cmdChat(int ch, const std::string& t) { Writer w; w.put<uint8_t>(C_Chat).put<uint8_t>(uint8_t(ch)); w.str(t); send(w); }
    void cmdClanLeader(int id) { Writer w; w.put<uint8_t>(C_ClanLeader).put<uint8_t>(uint8_t(id)); send(w); }
    void cmdBuild(Kind k, IV t) { Writer w; w.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(t.x).put<int32_t>(t.y); send(w); }

    void selectBuildKind(int i) {
        Kind k = buildList[i].k;
        placeMode = 0;
        buildKind = buildKind == int(k) ? -1 : int(k);
        mode = M_None;
        if (buildKind >= 0) msg(std::string("Placing ") + KINFO[k].name + ": click to build, Shift keeps building, Esc/right-click cancels.");
    }

    // ---------------------------------------------------------------- picking
    uint32_t pick(Vector2 mouse, bool includeBuildings = true) {
        Vector2 w = GetScreenToWorld2D(mouse, cam);
        uint32_t best = 0;
        float bd = 1e9f;
        reg.view<RPos, Info, NetId>().each([&](const RPos& p, const Info& i, const NetId& n) {
            if (isBuilding(i.kind) || aboardUnit(i)) return;
            float cy = p.y * TILE - (i.kind == K_Sheep ? 14.f : (i.kind == K_Ship ? 20.f : 28.f)), cx = p.x * TILE;
            float d = std::hypot(w.x - cx, w.y - cy);
            if (d < (i.kind == K_Ship ? 54.f : 34.f) && d < bd) { bd = d; best = n.id; }
        });
        if (best || !includeBuildings) return best;
        reg.view<RPos, Info, NetId>().each([&](const RPos& p, const Info& i, const NetId& n) {
            if (!isBuilding(i.kind)) return;
            float fw = float(KINFO[i.kind].fw) * TILE, fh = float(KINFO[i.kind].fh) * TILE;
            Rectangle r{p.x * TILE - fw / 2, p.y * TILE - fh / 2 - TILE * .9f, fw, fh + TILE * .9f};
            if (CheckCollisionPointRec(w, r)) best = n.id;
        });
        return best;
    }
    void doSelect(Vector2 a, Vector2 b, bool add) {
        if (!add) sel.clear();
        if (std::abs(b.x - a.x) > 6 || std::abs(b.y - a.y) > 6) {
            Rectangle R{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
            reg.view<RPos, Info, NetId>().each([&](const RPos& p, const Info& i, const NetId& n) {
                if (i.owner != me || isBuilding(i.kind) || aboardUnit(i)) return;
                if (CheckCollisionPointRec(GetWorldToScreen2D({p.x * TILE, p.y * TILE - 24}, cam), R)) sel.insert(n.id);
            });
        } else {
            uint32_t id = pick(b);
            auto it = ents.find(id);
            if (id && it != ents.end() && reg.get<Info>(it->second).owner == me) sel.insert(id);
        }
    }

    // ---------------------------------------------------------------- context panel
    int mapUpgradeCost(const Info& in, int r) { return int(std::ceil(KINFO[in.kind].cost[r] * upgradeCostMul(in.aux) * .7f)); }
    std::string costText(const float* c) {
        std::string s;
        static const char* ab[R_Count] = {"W", "G", "S", "M"};
        for (int r = 0; r < R_Count; r++) if (c[r] > 0) s += std::string(ab[r]) + std::to_string(int(c[r])) + " ";
        return s.empty() ? "free" : s;
    }
    void openContext(uint32_t id, Vector2 at) {
        ctxTarget = id;
        ctxOpen = true;
        ctxPos = {at.x + 12, at.y - 20};
    }
    static std::string unlockText(int level) {
        std::string t;
        for (int u = K_Pawn; u <= K_Monk; u++) if (unitUnlockLevel(Kind(u)) == level) t += (t.empty() ? "" : ", ") + std::string(KINFO[u].name);
        return t;
    }
    // what the next building level changes (plus animated previews of newly unlocked troops)
    void nextLevelInfo(const Info& in, std::vector<std::string>& out, std::vector<Kind>& units) {
        int L = in.aux, N = L + 1;
        if (L >= MAX_LEVEL || in.kind == K_Campfire) return;
        out.push_back(TextFormat("Health +%d%%", int(std::lround((levelHp(N) / levelHp(L) - 1.f) * 100.f))));
        if (isStorage(in.kind)) out.push_back(TextFormat("Capacity %d -> %d", int(storageCap(in.kind, L)), int(storageCap(in.kind, N))));
        if (KINFO[in.kind].dps > 0) out.push_back(TextFormat("Defence %.0f -> %.0f dps", KINFO[in.kind].dps * levelMul(L), KINFO[in.kind].dps * levelMul(N)));
        if (isResourceBuilding(in.kind)) out.push_back(TextFormat("Output x%.1f -> x%.1f", levelMul(L), levelMul(N)));
        if (in.kind == K_Archery) {
            std::string u = unlockText(N);
            if (!u.empty()) out.push_back("Unlocks: " + u);
            for (int k = K_Pawn; k <= K_Monk; k++) if (unitUnlockLevel(Kind(k)) == N) units.push_back(Kind(k));
            int maxLv = 1;
            for (int t = 2; t <= MAX_UNIT_LEVEL; t++) if (troopUpgradeNeedsArchery(t) <= N) maxLv = t;
            out.push_back(TextFormat("Troops upgradable to Lv%d", maxLv));
        }
    }
    void rebuildContext() {
        ctx.clear();
        ctxLines.clear();
        ctxNext.clear();
        ctxNextUnits.clear();
        ctxSlider = false;
        ctxQuota = false;
        auto it = ents.find(ctxTarget);
        if (it == ents.end()) { ctxOpen = false; return; }
        const Info& in = reg.get<Info>(it->second);
        bool mine = in.owner == me, done = in.prog >= 100;
        Color ink = rgb(70, 45, 30);
        auto line = [&](const std::string& t, Color c) { ctxLines.push_back({t, c}); };
        line(std::string("Owner: ") + nameOf(in.owner), mine ? rgb(30, 90, 40) : (isFactionOwner(in.owner) ? rgb(60, 50, 110) : rgb(150, 40, 30)));
        if (isFactionOwner(in.owner)) {
            int rp = facRep[in.owner - FACTION_BASE];
            line(TextFormat("Reputation: %s (%d)", repTier(rp), rp), rp <= HOSTILE_REP ? rgb(170, 40, 30) : (rp >= FRIENDLY_REP ? rgb(30, 110, 40) : ink));
            line(rp <= HOSTILE_REP ? "They will fight you on sight." : (rp >= FRIENDLY_REP ? "They will aid you if attacked." : "Gifts earn their trust."), ink);
        }
        if (in.prog < 100) line("Under construction " + std::to_string(in.prog) + "%", ink);
        line(TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), ink);
        if (mine || alliedWith(in.owner)) {
            line(TextFormat("Level %d / %d", in.aux, MAX_LEVEL), ink);
            Pos c = {reg.get<Goal>(it->second).x, reg.get<Goal>(it->second).y};
            int b = biomeAt(int(std::floor(c.x)), int(std::floor(c.y)));
            line(std::string("Biome: ") + (b == INVALID_BIOME ? "?" : BIOME_NAME[b]), ink);
            if (isResourceBuilding(in.kind)) {
                Yield yv = yieldOf(in.kind, b, in.kind == K_Harbour);
                std::string pr;
                int goldN = in.kind == K_Mine ? worldGoldNear(c) : 0;                       // [world] nearby gold deposits boost mines
                float gb = 1.f + goldBonusOf(goldN);
                for (int r = 0; r < R_Count; r++) if (yv.r[r] > 0) pr += TextFormat("%s %.1f/s  ", RES_NAME[r], yv.r[r] * levelMul(in.aux) * gb);
                line("Output: " + pr, ink);
                if (in.kind == K_Mine) line(goldN ? TextFormat("Gold deposits nearby: %d (+%d%% output)", goldN, int(std::lround((gb - 1.f) * 100.f))) : "No gold deposits nearby (+20% each, max +100%)", goldN ? rgb(150, 105, 0) : rgb(130, 90, 30));
                bool pref = preferredBiome(in.kind, b);
                line(pref ? "Preferred biome: BONUS ACTIVE" : (in.kind == K_Lumber ? "Best in: Forest" : (in.kind == K_Mine ? "Best in: Highlands, Desert" : "Coastal fishing")), pref ? rgb(30, 110, 40) : rgb(130, 90, 30));
                line(TextFormat("Stock waiting: %d", in.stock), ink);
            } else if (isStorage(in.kind)) {
                float cap = storageCap(in.kind, in.aux);
                if (in.kind == K_TownHall) line(TextFormat("Storage: %d / %d", int(res[0] + res[1] + res[2] + res[3]), int(cap)), ink);
                else line(TextFormat("Stored: %d / %d (%d%%)", int(in.stock), int(cap), int(in.stock * 100.f / cap)), ink);
                if (KINFO[in.kind].dps > 0) line(TextFormat("Defence %.0f dps, territory %d", KINFO[in.kind].dps * levelMul(in.aux), anchorRadius(in.kind)), ink);
                if (mine && done && (in.kind == K_Outpost || in.kind == K_Storage)) { ctxSlider = true; if (!sliderDrag) ctxPct = in.aim; }
            } else if (in.kind == K_Campfire) line("Warms units in the snow", ink);
        }
        if (mine && done) {
            Kind k = in.kind;
            for (int u = K_Pawn; u < K_Count; u++)
                if (canTrain(k, Kind(u))) ctx.push_back({std::string("Build ") + KINFO[u].name + "  " + costText(KINFO[u].cost), 1, ctxTarget, uint32_t(u), affordable(KINFO[u].cost), false});
            if (k != K_Campfire) {
                float c[R_Count];
                for (int r = 0; r < R_Count; r++) c[r] = float(mapUpgradeCost(in, r));
                ctx.push_back({in.aux >= MAX_LEVEL ? "Max level reached" : "Upgrade to Lv" + std::to_string(in.aux + 1) + "  " + costText(c), 2, ctxTarget, 0, in.aux < MAX_LEVEL && affordable(c), false});
                nextLevelInfo(in, ctxNext, ctxNextUnits);
            }
        } else if (!mine) {
            if (isFactionOwner(in.owner)) {
                static const char* nm[R_Count] = {"Wood", "Gold", "Stone", "Meat"};
                for (int r = 0; r < R_Count; r++) ctx.push_back({std::string("Help: give 50 ") + nm[r], 6, uint32_t(in.owner - FACTION_BASE), uint32_t(r), res[r] >= 50.f, false});
            }
            ctx.push_back({"Attack this target", 3, ctxTarget, 0, true, true});
            if (in.owner > 0 && !isFactionOwner(in.owner)) {
                auto p = peerOf(in.owner);
                uint8_t fl = p ? p->flags : 0;
                if (fl & 2) ctx.push_back({"Break alliance", 5, uint32_t(in.owner), 0, true, true});
                else if (fl & 4) ctx.push_back({"Accept alliance", 4, uint32_t(in.owner), 0, true, false});
                else if (fl & 8) ctx.push_back({"Alliance invite pending", 4, uint32_t(in.owner), 0, false, false});
                else ctx.push_back({"Invite to alliance", 4, uint32_t(in.owner), 0, true, false});
            }
        }
        baseContext(in, mine, done);
    }
    void runContext(const MenuAct& a) {
        switch (a.act) {
        case 1: cmdTrain(a.a, Kind(a.b)); break;
        case 2: cmdUpgrade(a.a); break;
        case 3: cmdAttack(a.a); break;
        case 4: cmdAlly(int(a.a), AA_Invite); break;
        case 5: cmdAlly(int(a.a), AA_Break); break;
        case 6: cmdDonate(int(a.a), int(a.b), 50.f); break;
        default: if (!baseRunContext(a)) ctxOpen = false; break;
        }
    }
    float ctxTopY() { Rectangle r = ctxRect(); return r.y + float(mCtx.padding) + 20; }
    Rectangle ctxSliderRect() { Rectangle r = ctxRect(); float pad = float(mCtx.padding); return {r.x + pad + 12, ctxTopY() + 19.f * float(ctxLines.size()) + 22, r.width - 2 * pad - 24, 14}; }
    Rectangle ctxButton(int i) {
        Rectangle r = ctxRect();
        float pad = float(mCtx.padding);
        return {r.x + pad + 6, ctxTopY() + 19.f * float(ctxLines.size()) + (ctxSlider ? 50.f : 4.f) + 34.f * float(i), r.width - 2 * pad - 12, 30};
    }

    // ---------------------------------------------------------------- input
    void input(float dt) {
        Vector2 rm = ::GetMousePosition();                              // real pixels: camera, picking, dragging
        Vector2 m = GetMousePosition();                                 // UI space (divided by the UI scale): panels and buttons
        float sw = float(::GetScreenWidth()), sh = float(::GetScreenHeight());
        cam.offset = {sw * .5f, sh * .5f};
        bool ui = overUI(m);

        if (pauseOpen) { pauseInput(); return; }
        if (chatOpen) { chatUpdate(m); return; }
        if (baseInput(m)) return;
        if (bigMapOpen) { bigMapInput(m); return; }
        if (trainOpen) { trainInput(m); return; }
        if (socialOpen) { socialInput(m); return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) { chatOpen = true; chatInput.clear(); return; }
        if (IsKeyPressed(KEY_N)) { openBigMap(); return; }
        if (IsKeyPressed(KEY_P)) { socialOpen = true; textFocus = false; return; }
        float sp = 800.f * dt / cam.zoom;
        if (IsKeyDown(KEY_UP)) cam.target.y -= sp;
        if (IsKeyDown(KEY_DOWN)) cam.target.y += sp;
        if (IsKeyDown(KEY_LEFT)) cam.target.x -= sp;
        if (IsKeyDown(KEY_RIGHT)) cam.target.x += sp;
        if (IsCursorOnScreen() && !dragging && !ui) {                       // edge scrolling
            if (rm.x < 3) cam.target.x -= sp;
            if (rm.x > sw - 4) cam.target.x += sp;
            if (rm.y < 3) cam.target.y -= sp;
            if (rm.y > sh - 4) cam.target.y += sp;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) { Vector2 d = GetMouseDelta(); cam.target.x -= d.x / cam.zoom; cam.target.y -= d.y / cam.zoom; }
        float wh = GetMouseWheelMove();
        if (wh != 0 && !ui) {
            Vector2 b = GetScreenToWorld2D(rm, cam);
            cam.zoom = std::clamp(cam.zoom * (1 + wh * .1f), .2f, 2.2f);
            Vector2 a = GetScreenToWorld2D(rm, cam);
            cam.target.x += b.x - a.x;
            cam.target.y += b.y - a.y;
        }
        if (IsKeyPressed(KEY_H)) { Pos t = myTH(); cam.target = {t.x * TILE, t.y * TILE}; }
        if (IsKeyPressed(KEY_B)) buildOpen = !buildOpen;
        if (IsKeyPressed(KEY_M)) { mode = mode == M_Move ? M_None : M_Move; buildKind = -1; }
        if (IsKeyPressed(KEY_A)) { mode = mode == M_Attack ? M_None : M_Attack; buildKind = -1; }
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (buildKind >= 0 || mode != M_None || ctxOpen || !sel.empty()) { buildKind = -1; mode = M_None; ctxOpen = false; sel.clear(); }
            else { pauseOpen = true; return; }
        }
        if (buildOpen) for (int i = 0; i < 7; i++) if (IsKeyPressed(KEY_ONE + i)) { selectBuildKind(i); }
        if (GetTime() - lastCam > 1.5 && connected) { lastCam = GetTime(); Writer w; w.put<uint8_t>(C_Cam).put<float>(cam.target.x / TILE).put<float>(cam.target.y / TILE); send(w); }

        hovered = ui || dragging ? 0 : pick(rm);
        bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), lr = IsMouseButtonReleased(MOUSE_BUTTON_LEFT), rp = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (shipUiInput(m, lp, rp)) return;

        if (dragging && lr) { dragging = false; doSelect(dragStart, rm, shift); return; }
        if (ctxOpen) rebuildContext();
        if (ctxOpen && ctxSlider) {
            Rectangle sr = ctxSliderRect();
            Rectangle hit{sr.x - 12, sr.y - 10, sr.width + 24, sr.height + 20};
            if (lp && CheckCollisionPointRec(m, hit)) sliderDrag = true;
            if (sliderDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                int pct = ctxQuota ? quotaFromMouse(m.x, sr) : std::clamp(int(std::lround((10.f + 90.f * (m.x - sr.x) / sr.width) / 5.f)) * 5, 10, 100);
                if (pct != ctxPct) { ctxPct = pct; if (ctxQuota) { myQuota = pct; cmdQuota(pct); } else cmdSetHaul(ctxTarget, pct); }
            }
            if (sliderDrag && lr) sliderDrag = false;
            if (sliderDrag || (lp && CheckCollisionPointRec(m, hit))) return;
        }
        if (lp) {
            if (ctxOpen) {
                bool hit = false;
                for (size_t i = 0; i < ctx.size(); i++)
                    if (CheckCollisionPointRec(m, ctxButton(int(i)))) { hit = true; if (ctx[i].enabled) { MenuAct a = ctx[i]; runContext(a); } break; }
                if (hit || CheckCollisionPointRec(m, ctxRect())) return;
                ctxOpen = false;
            }
            if (CheckCollisionPointRec(m, buildTab())) { buildOpen = !buildOpen; return; }
            if (buildSlide > .9f && CheckCollisionPointRec(m, buildRect())) {
                for (int i = 0; i < 7; i++) if (CheckCollisionPointRec(m, buildItem(i))) { selectBuildKind(i); return; }
                return;
            }
            if (CheckCollisionPointRec(m, socialTabBtn())) { socialOpen = true; textFocus = false; return; }
            if (CheckCollisionPointRec(m, miniRect())) { miniClick(m); return; }
            if (ui) return;
        }
        if (ui) { if (rp) ctxOpen = false; return; }

        Vector2 w = GetScreenToWorld2D(rm, cam);
        if (lp) {
            if (buildKind >= 0) {
                Kind k = Kind(buildKind);
                IV t = placeTile(k, w);
                int err = placeCheck(k, t);
                if (err) msg(std::string("Cannot build here: ") + PLACE_TEXT[err] + ".");
                else if (!affordable(placeCost(k))) msg("Not enough resources" + std::string(roster.empty() ? "." : " (allies may cover the rest)."));
                bool clearing = !err && worldPlaceClick(k, t);                // [world] only features block: start clearing exactly those tiles
                if (!err && !clearing) cmdPlace(k, t);
                if (effMode() != 0) { if (!err && !clearing) buildKind = -1; }   // special placements: stay in the mode until a valid site was chosen
                else if (!shift && !clearing) buildKind = -1;
            } else if (mode == M_Move) {
                cmdMove(w);
                if (!shift) mode = M_None;
            } else if (mode == M_Attack) {
                if (hovered && attackable(hovered)) cmdAttack(hovered); else cmdAttackMove(w);
                if (!shift) mode = M_None;
            } else if (hovered && hostileTargetId(hovered) && armyCountSel() > 0) {
                cmdAttack(hovered);
            } else { dragging = true; dragStart = rm; if (!shift) { sel.clear(); } }
        }
        if (rp) {
            if (buildKind >= 0 || mode != M_None) { buildKind = -1; mode = M_None; return; }
            uint32_t id = pick(rm);
            auto it = id ? ents.find(id) : ents.end();
            if (shipRightClick(id)) return;
            if (it != ents.end() && isBuilding(reg.get<Info>(it->second).kind)) {
                const Info& bi = reg.get<Info>(it->second);
                if (bi.kind == K_Archery && bi.owner == me && bi.prog >= 100 && !(bi.state & (BLD_DISABLED | BLD_MOVING))) { trainOpen = true; trainTarget = id; trainSel = 0; ctxOpen = false; }
                else openContext(id, m);
                return;
            }
            if (!selUnits().empty()) {
                if (id && hostileTargetId(id)) cmdAttack(id); else cmdMove(w);
            }
        }
    }
    bool hostileTargetId(uint32_t id) {
        auto it = ents.find(id);
        if (it == ents.end()) return false;
        const Info& i = reg.get<Info>(it->second);
        return i.owner == 0 ? i.kind == K_Sheep : hostileTo(i.owner);
    }
    int armyCountSel() {
        int n = 0;
        for (auto id : sel) { auto it = ents.find(id); if (it != ents.end()) { Kind k = reg.get<Info>(it->second).kind; if (!isBuilding(k) && k != K_Ship && k != K_Monk) n++; } }
        return n;
    }
    void miniClick(Vector2) { openBigMap(); }
    bool attackable(uint32_t id) {
        auto it = ents.find(id);
        if (it == ents.end()) return false;
        const Info& i = reg.get<Info>(it->second);
        return i.owner != me && !alliedWith(i.owner);
    }

    // ---------------------------------------------------------------- sprite helpers
    void drawSprite(Texture2D t, Rectangle src, Rectangle dst, Color tint, int mode, Color oc = WHITE, float prog = 0, float sd = 0) {
        if (t.id == 0) return;
        bool shaded = (mode == 1 && okOutline) || (mode == 2 && okDissolve);
        if (!shaded) { DrawTexturePro(t, src, dst, {0, 0}, 0, tint); return; }
        Shader& sh = mode == 1 ? shOutline : shDissolve;
        float u0 = std::min(src.x, src.x + src.width) / float(t.width), u1 = std::max(src.x, src.x + src.width) / float(t.width);
        float v0 = std::min(src.y, src.y + src.height) / float(t.height), v1 = std::max(src.y, src.y + src.height) / float(t.height);
        float uv[4] = {u0, v0, u1, v1}, ts[2] = {1.f / float(t.width), 1.f / float(t.height)}, tsz[2] = {float(t.width), float(t.height)};
        float col[4] = {oc.r / 255.f, oc.g / 255.f, oc.b / 255.f, oc.a / 255.f}, th = 2.5f, tm = float(GetTime());
        if (mode == 1) {
            SetShaderValue(sh, locOut[0], uv, SHADER_UNIFORM_VEC4);
            SetShaderValue(sh, locOut[1], ts, SHADER_UNIFORM_VEC2);
            SetShaderValue(sh, locOut[2], col, SHADER_UNIFORM_VEC4);
            SetShaderValue(sh, locOut[3], &th, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, locOut[4], &tm, SHADER_UNIFORM_FLOAT);
        } else {
            SetShaderValue(sh, locDis[0], uv, SHADER_UNIFORM_VEC4);
            SetShaderValue(sh, locDis[1], tsz, SHADER_UNIFORM_VEC2);
            SetShaderValue(sh, locDis[2], &prog, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, locDis[3], &sd, SHADER_UNIFORM_FLOAT);
        }
        BeginShaderMode(sh);
        DrawTexturePro(t, src, dst, {0, 0}, 0, tint);
        EndShaderMode();
    }
    void drawAnim(const Anim& a, int frame, float footWX, float footWY, bool flip, float scale, Color tint = WHITE, int mode = 0, Color oc = WHITE, float prog = 0, float sd = 0) {
        Rectangle src = frameRect(a, frame);
        if (flip) src.width = -src.width;
        float fx = flip ? float(a.fw) - a.footX : a.footX;
        Rectangle dst{footWX - fx * scale, footWY - a.footY * scale, float(a.fw) * scale, float(a.fh) * scale};
        drawSprite(a.t, src, dst, tint, mode, oc, prog, sd);
    }
    int frameFor(const Anim& a, double t, float phase = 0) { return int(std::fmod(t * double(a.fps) + double(phase), double(a.n))) % a.n; }

    Anim unitAnim(Kind k, int owner, int state, int aux, int aim, bool moving, int biome, double t) {
        std::string base = std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/";
        float ang = float(aim) / 255.f * 6.28319f - 3.14159f;
        if (state == US_Extinguish) return extinguishAnim(k, owner);
        switch (k) {
        case K_Pawn: {
            if (state == US_Work) return mkAnim(base + (biome == B_Forest ? "Pawn/Pawn_Interact Axe.png" : "Pawn/Pawn_Interact Pickaxe.png"), 192, 192, 9, 96, 135);
            if (state == US_Attack) return mkAnim(base + "Pawn/Pawn_Interact Knife.png", 192, 192, 10, 96, 135);
            static const char* carry[] = {"", " Wood", " Gold", " Gold", " Meat"};
            std::string sfx = aux >= 1 && aux <= 4 ? carry[aux] : "";
            return mkAnim(base + (moving ? "Pawn/Pawn_Run" : "Pawn/Pawn_Idle") + sfx + ".png", 192, 192, moving ? 10 : 7, 96, 135);
        }
        case K_Warrior:
            if (state == US_Attack) return mkAnim(base + (int(t * 2.5) % 2 ? "Warrior/Warrior_Attack2.png" : "Warrior/Warrior_Attack1.png"), 192, 192, 10, 96, 135);
            return mkAnim(base + (moving ? "Warrior/Warrior_Run.png" : "Warrior/Warrior_Idle.png"), 192, 192, moving ? 10 : 7, 96, 135);
        case K_Archer:
            if (state == US_Attack) return mkAnim(base + "Archer/Archer_Shoot.png", 192, 192, 10, 96, 135);
            return mkAnim(base + (moving ? "Archer/Archer_Run.png" : "Archer/Archer_Idle.png"), 192, 192, moving ? 10 : 7, 96, 135);
        case K_Lancer: {
            if (state == US_Attack) {
                float dx = std::cos(ang), dy = std::sin(ang);
                const char* d = std::abs(dy) < .38f ? "Right" : (dy > 0 ? (std::abs(dx) < .38f ? "Down" : "DownRight") : (std::abs(dx) < .38f ? "Up" : "UpRight"));
                return mkAnim(base + "Lancer/Lancer_" + d + "_Attack.png", 320, 320, 8, 160, 198);
            }
            return mkAnim(base + (moving ? "Lancer/Lancer_Run.png" : "Lancer/Lancer_Idle.png"), 320, 320, moving ? 10 : 9, 160, 198);
        }
        case K_Monk:
            if (state == US_Heal) return mkAnim(base + "Monk/Heal.png", 192, 192, 10, 96, 135);
            return mkAnim(base + (moving ? "Monk/Run.png" : "Monk/Idle.png"), 192, 192, moving ? 9 : 7, 96, 135);
        case K_Sheep:
            return mkAnim(std::string("Terrain/Resources/Meat/Sheep/") + (moving ? "Sheep_Move.png" : "Sheep_Idle.png"), 128, 128, 7, 64, 84);
        default: return mkAnim(base + "Warrior/Warrior_Idle.png", 192, 192, 7, 96, 135);
        }
    }

    struct BSpr { const char* file; float scale, artBottom, artH, artW; };
    BSpr buildingSprite(Kind k, int level) {
        switch (k) {
        case K_TownHall: return {"Castle", .8f, 249, 256, 320};
        case K_Outpost: return {"Tower", .8f, 230, 256, 128};
        case K_Storage: return {"Barracks", .66f, 245, 256, 192};
        case K_Archery: return {"Archery", 1.f, 240, 256, 192};
        case K_Harbour: return {"House3", 1.f, 172, 192, 128};
        case K_Lumber: return {"House1", .9f, 173, 192, 128};      // always House1: House2 is a rotated variant and is never used
        default: return {"", 1, 0, 0, 0};
        }
    }

    void drawBar(Vector2 c, float w, float frac, Color fill) {
        Texture2D base = tex("UI Elements/UI Elements/Bars/SmallBar_Base.png"), fl = tex("UI Elements/UI Elements/Bars/SmallBar_Fill.png");
        if (base.id == 0) { DrawRectangle(int(c.x - w / 2), int(c.y), int(w), 5, BLACK); DrawRectangle(int(c.x - w / 2), int(c.y), int(w * frac), 5, fill); return; }
        float s = w / 222.f, h = 19.f * s, x = c.x - w / 2, y = c.y;
        float cap = 15.f * s;
        DrawTexturePro(base, {49, 22, 15, 19}, {x, y, cap, h}, {0, 0}, 0, WHITE);
        DrawTexturePro(base, {128, 22, 64, 19}, {x + cap, y, w - 2 * cap, h}, {0, 0}, 0, WHITE);
        DrawTexturePro(base, {256, 22, 15, 19}, {x + w - cap, y, cap, h}, {0, 0}, 0, WHITE);
        float ix = x + 4 * s * 1.6f, iw = (w - 8 * s * 1.6f) * std::clamp(frac, 0.f, 1.f);
        if (iw > 0) DrawTexturePro(fl, {0, 30, 64, 3}, {ix, y + 5 * s, iw, h - 10 * s}, {0, 0}, 0, fill);
    }
    void brackets(Rectangle r, Color c) {
        Texture2D t = tex("UI Elements/UI Elements/Cursors/Cursor_04.png");
        float s = .5f;
        auto q = [&](float sx, float sy, float dx, float dy) { DrawTexturePro(t, {sx, sy, 64, 64}, {dx, dy, 64 * s, 64 * s}, {0, 0}, 0, c); };
        q(0, 0, r.x - 2, r.y - 2);
        q(64, 0, r.x + r.width - 64 * s + 2, r.y - 2);
        q(0, 64, r.x - 2, r.y + r.height - 64 * s + 2);
        q(64, 64, r.x + r.width - 64 * s + 2, r.y + r.height - 64 * s + 2);
    }
    void textShadow(const std::string& s, int x, int y, int sz, Color c) {
        DrawText(s.c_str(), x + 1, y + 1, sz, rgb(0, 0, 0, c.a / 2 + 40));
        DrawText(s.c_str(), x, y, sz, c);
    }

    // ---------------------------------------------------------------- world drawing
    void drawBuilding(const RPos& p, const Info& i, uint32_t id, bool selected, bool hover, std::vector<DrawItem>& items, double t) {
        const KindInfo& ki = KINFO[i.kind];
        int tx = int(std::lround(p.x - float(ki.fw) * .5f)), ty = int(std::lround(p.y - float(ki.fh) * .5f));
        float cx = (float(tx) + float(ki.fw) * .5f) * TILE, bottom = float(ty + ki.fh) * TILE;
        float prog = std::clamp(float(i.prog) / 100.f, 0.f, 1.f);
        bool mine = i.owner == me;
        Color oc = !mine && hostileTo(i.owner) ? rgb(255, 90, 90) : rgb(255, 255, 255);
        int col = colorIdx(i.owner);
        float sortY = bottom;

        items.push_back({sortY - 6.f, [=, this]() {
            DrawEllipse(int(cx), int(bottom - 8), float(ki.fw) * 28.f, float(ki.fh) * 14.f, rgb(0, 0, 0, 55));
            DrawEllipseLines(int(cx), int(bottom - 8), float(ki.fw) * 28.f, float(ki.fh) * 14.f, teamColor(i.owner));
            bool moving = (i.state & BLD_MOVING) != 0;
            Color tint = moving ? rgb(255, 255, 255, 62) : (prog < 1.f ? rgb(255, 255, 255, 190) : WHITE);
            auto A = [&](Color c) { if (moving) c.a = 62; return c; };
            int md = hover ? 1 : 0;
            if (i.kind == K_Harbour) {                                    // wooden pier over adjacent ocean tiles
                for (int y = ty - 1; y <= ty + ki.fh; y++)
                    for (int x = tx - 1; x <= tx + ki.fw; x++) {
                        bool inside = x >= tx && x < tx + ki.fw && y >= ty && y < ty + ki.fh, corner = (x < tx || x >= tx + ki.fw) && (y < ty || y >= ty + ki.fh);
                        if (inside || corner || biomeAt(x, y) != B_Ocean) continue;
                        Rectangle pl{float(x) * TILE + 6, float(y) * TILE + 6, TILE - 12, TILE - 12};
                        DrawRectangleRec(pl, rgb(150, 105, 62));
                        for (int k = 0; k < 4; k++) DrawLineEx({pl.x, pl.y + 8.f + 16.f * float(k)}, {pl.x + pl.width, pl.y + 8.f + 16.f * float(k)}, 2, rgb(96, 62, 36));
                        DrawRectangleLinesEx(pl, 2, rgb(70, 44, 26));
                    }
            }
            if (i.kind == K_Campfire) {
                Anim f = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62);
                for (int k = 0; k < 6; k++) { float a = float(k) * 1.047f; DrawCircle(int(cx + std::cos(a) * 15), int(bottom - 12 + std::sin(a) * 7), 5, rgb(110, 108, 118)); }
                if (prog >= 1.f) drawAnim(f, frameFor(f, t, float(id % 5)), cx, bottom - 12, false, 1.5f, WHITE, md, oc);
                else DrawRectangle(int(cx - 10), int(bottom - 20), 20, 8, rgb(120, 80, 50));
                return;
            }
            if (i.kind == K_Mine) {                                       // cave mouth, timber frame, ore pile
                float w = float(ki.fw) * TILE - 12;
                DrawEllipse(int(cx), int(bottom - 34), w * .42f, 40, A(rgb(36, 30, 30)));
                DrawRectangle(int(cx - w * .42f - 4), int(bottom - 70), 8, 70, A(rgb(120, 82, 46)));
                DrawRectangle(int(cx + w * .42f - 4), int(bottom - 70), 8, 70, A(rgb(120, 82, 46)));
                DrawRectangle(int(cx - w * .42f - 8), int(bottom - 78), int(w * .84f + 16), 10, A(rgb(146, 100, 58)));
                Texture2D r1 = tex("Terrain/Decorations/Rocks/Rock2.png"), r2 = tex("Terrain/Decorations/Rocks/Rock3.png"), g1 = tex("Terrain/Resources/Gold/Gold Stones/Gold Stone 2.png");
                drawSprite(r1, {0, 0, 64, 64}, {cx - 62, bottom - 62, 64, 64}, tint, md, oc);
                drawSprite(r2, {0, 0, 64, 64}, {cx + 14, bottom - 58, 64, 64}, tint, md, oc);
                drawSprite(g1, {0, 0, 128, 128}, {cx - 4, bottom - 84, 96, 96}, tint, md, oc);
                return;
            }
            BSpr s = buildingSprite(i.kind, i.aux);
            Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[col] + " Buildings/" + s.file + ".png");
            float dh = s.artH * s.scale * prog, sy = s.artH * (1.f - prog);
            Rectangle src{0, sy, s.artW, s.artH - sy};
            float dw = s.artW * s.scale;
            Rectangle dst{cx - dw / 2, bottom - 6 - s.artBottom * s.scale + (s.artH * s.scale - dh), dw, dh};
            drawSprite(bt, src, dst, tint, md, oc);
        }});
        if (prog < 1.f)
            items.push_back({sortY + 1.f, [=, this]() {
                Anim h = mkAnim(std::string("Units/") + COLOR_NAME[col] + " Units/Pawn/Pawn_Interact Hammer.png", 192, 192, 6, 96, 135);
                drawAnim(h, frameFor(h, t, float(id % 3)), cx + float(ki.fw) * 28.f, bottom - 4, false, .9f);
                Anim d = mkAnim("Particle FX/Dust_01.png", 64, 64, 8, 32, 60);
                drawAnim(d, frameFor(d, t, float(id % 4)), cx - float(ki.fw) * 20.f, bottom - 6, false, 1.2f, rgb(255, 255, 255, 180));
            }});
        baseBuildingFx(i, id, cx, bottom, ki.fw, ki.fh, sortY, items, t);       // fire, red cross, relocation marker
        float hpFrac = i.hp / std::max(1.f, i.maxhp);
        if (prog >= 1.f && isBuilding(i.kind) && i.kind != K_Campfire) {                // damage: smoke and flames
            if (hpFrac < .6f) items.push_back({sortY + 2.f, [=, this]() { Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60); drawAnim(d, frameFor(d, t, float(id % 7)), cx - 20, bottom - 70, false, 1.8f, rgb(120, 120, 130, 210)); }});
            if (hpFrac < .3f) items.push_back({sortY + 3.f, [=, this]() { Anim f = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62); drawAnim(f, frameFor(f, t, float(id % 5)), cx + 14, bottom - 46, false, 1.4f); drawAnim(f, frameFor(f, t, float(id % 3) + 4), cx - 22, bottom - 40, false, 1.1f); }});
        }
        if (i.kind == K_Campfire && prog >= 1.f)
            items.push_back({sortY + 2.f, [=, this]() { Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 6, 32, 60); drawAnim(d, frameFor(d, t, float(id % 3)), cx + 6, bottom - 60, false, 1.2f, rgb(200, 200, 205, 120)); }});
        if (i.kind == K_TownHall)
            items.push_back({sortY + 4.f, [=, this]() {
                std::string nm = nameOf(i.owner);
                int hall = leaderClanOf(i.owner);
                if (hall) nm += "  [Clan Hall]";
                textShadow(nm, int(cx) - MeasureText(nm.c_str(), 16) / 2, int(bottom) - 150, 16, hall ? clanColor(hall) : teamColor(i.owner));
            }});
        items.push_back({sortY + 5.f, [=, this]() {
            Rectangle fr{float(tx) * TILE, float(ty) * TILE - 40, float(ki.fw) * TILE, float(ki.fh) * TILE + 40};
            if (selected) brackets(fr, rgb(255, 235, 90));
            if (prog < 1.f) drawBar({cx, bottom + 4}, 70, prog, rgb(90, 160, 255));
            else if (hpFrac < .995f || selected || hover) drawBar({cx, bottom + 4}, 76, hpFrac, hpFrac > .5f ? rgb(90, 220, 90) : (hpFrac > .25f ? rgb(240, 190, 50) : rgb(235, 70, 60)));
            if (i.kind == K_Campfire && (hover || selected)) DrawCircleLinesV({cx, bottom - 16}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));
        }});
    }

    void drawUnit(const RPos& p, Info& i, Face& fc, uint32_t id, bool selected, bool hover, std::vector<DrawItem>& items, double t, int biome) {
        float px = p.x * TILE, py = p.y * TILE;
        bool moving = (i.state & 7) == US_Move, convoy = (i.state & UNIT_CONVOY_FLAG) != 0;
        int carry = convoy && !i.aux ? 2 : i.aux;
        Color oc = i.owner == me || alliedWith(i.owner) ? rgb(255, 255, 255) : (hostileTo(i.owner) ? rgb(255, 90, 90) : rgb(255, 225, 130));
        if (i.kind == K_Sheep) oc = rgb(255, 235, 140);
        items.push_back({py, [=, this]() {
            if (i.kind == K_Ship) { drawShip(px, py, i.owner, fc.left, hover, t, id, i.aux, i.prog); drawShipLabel(px, py, i, selected, hover); if (selected) DrawEllipseLines(int(px), int(py + 4), 56, 18, rgb(255, 235, 90)); return; }
            Anim a = unitAnim(i.kind, i.owner, i.state & 7, carry, i.aim, moving, biome, t);
            DrawEllipse(int(px), int(py + 2), 16, 6, rgb(0, 0, 0, 60));
            if (selected) DrawEllipseLines(int(px), int(py + 2), 24, 10, rgb(255, 235, 90));
            drawAnim(a, frameFor(a, t, float(id % 7)), px, py + 6, fc.left, 1.f, WHITE, hover ? 1 : 0, oc);
            if (convoy) textShadow("Convoy", int(px) - 22, int(py) - 70, 14, rgb(120, 230, 255));
            if ((i.state & UNIT_SUPPORT_FLAG) && !convoy) textShadow("Patrol", int(px) - 20, int(py) - 70, 14, alliedWith(i.owner) ? rgb(255, 190, 80) : rgb(255, 110, 100));
            if ((i.state & 7) == US_Extinguish) drawExtinguishSplash(px, py, fc.left, t, id);
            if (i.kind == K_Monk && (i.state & 7) == US_Heal) { Anim h = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(i.owner)] + " Units/Monk/Heal_Effect.png", 192, 192, 10, 96, 135); drawAnim(h, frameFor(h, t), px, py + 6, false, 1.f); }
            float fr = i.hp / std::max(1.f, i.maxhp);
            if ((fr < .995f || selected || hover) && i.kind != K_Sheep) drawBar({px, py - 56}, 48, fr, fr > .5f ? rgb(90, 220, 90) : (fr > .25f ? rgb(240, 190, 50) : rgb(235, 70, 60)));
            if (i.kind == K_Pawn && i.owner != 0 && i.aux) { /* carrying: sprite variant already shows the cargo */ }
        }});
        if (i.kind == K_Ship && moving)
            items.push_back({py - 1, [=, this]() { Anim s = mkAnim("Particle FX/Water Splash.png", 192, 192, 8, 96, 150); drawAnim(s, frameFor(s, t, float(id % 3)), px - (fc.left ? -30.f : 30.f), py + 6, false, .45f, rgb(255, 255, 255, 140)); }});
    }

    void drawFeatures(int x0, int y0, int x1, int y1, const std::vector<uint8_t>& bio, int ox, int oy, int bw, std::vector<DrawItem>& items, double t) {
        auto B = [&](int x, int y) { int ix = x - ox, iy = y - oy; return ix < 0 || iy < 0 || ix >= bw ? int(INVALID_BIOME) : (size_t(iy * bw + ix) < bio.size() ? int(bio[size_t(iy * bw + ix)]) : int(INVALID_BIOME)); };
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                int b = B(x, y);
                if (!isLand(b) || !exploredAt(x, y) || isCliffTile(b, B(x, y - 1))) continue;
                uint8_t f = featureAt(x, y, b, seed);
                if (!f || clearedT.count(chunkKey(x, y))) continue;           // [world] cleared features are gone
                bool blocked = false;
                if (occ.count(chunkKey(x, y))) blocked = true;               // [world] only the building's own tiles hide features (neighbours must stay clickable/visible)
                if (blocked) continue;
                uint32_t h = hash2(x, y, seed + 31);
                float cx = (float(x) + .5f) * TILE + float(int(h & 15) - 8), by = (float(y) + 1.f) * TILE - 6 - float((h >> 4) & 7);
                float jp = -1.f;                                              // [world] clearing in progress: shake + progress bar
                if (auto jit = cjobs.find(chunkKey(x, y)); jit != cjobs.end()) { jp = jit->second.pct; cx += std::sin(float(t) * 46.f + float(h % 7)) * 3.f; }
                bool nearMine = f == F_Gold && goldNearMine(x, y);
                items.push_back({by, [=, this]() {
                    switch (f) {
                    case F_Pine: { Anim a = mkAnim(std::string("Terrain/Resources/Wood/Trees/Tree") + (h & 1 ? "1" : "2") + ".png", 192, 256, 6, 96, 238); drawAnim(a, frameFor(a, t, float(h % 8)), cx, by, false, 1.f); break; }
                    case F_Oak: { Anim a = mkAnim(std::string("Terrain/Resources/Wood/Trees/Tree") + (h & 1 ? "3" : "4") + ".png", 192, 192, 6, 96, 168); drawAnim(a, frameFor(a, t, float(h % 8)), cx, by, false, 1.f); break; }
                    case F_Bush: { Anim a = mkAnim(std::string("Terrain/Decorations/Bushes/Bushe") + char('1' + h % 4) + ".png", 128, 128, 5, 64, 78); drawAnim(a, frameFor(a, t, float(h % 8)), cx, by, false, 1.f); break; }
                    case F_Rock: { Texture2D r = tex(std::string("Terrain/Decorations/Rocks/Rock") + char('1' + h % 4) + ".png"); DrawTexturePro(r, {0, 0, 64, 64}, {cx - 32, by - 50, 64, 64}, {0, 0}, 0, WHITE); break; }
                    case F_Gold: { Anim a = mkAnim(std::string("Terrain/Resources/Gold/Gold Stones/Gold Stone ") + char('1' + h % 6) + "_Highlight.png", 128, 128, 6, 64, 78); drawAnim(a, frameFor(a, t, float(h % 6)), cx, by, false, 1.f); break; }
                    default: break;
                    }
                    if (f == F_Gold) goldGlints(cx, by, h, t, nearMine);       // [world] sparkling deposits (stronger near our mines)
                    if (jp >= 0.f) clearBar(cx, by, jp);
                }});
            }
    }

    void drawFx(std::vector<DrawItem>& items, double t) {
        for (auto& f : fxs) {
            double age = t - f.t;
            Fx fx = f;
            switch (f.type) {
            case E_Explosion: items.push_back({f.y * TILE + 30, [=, this]() { Anim a = mkAnim("Particle FX/Explosion_02.png", 192, 192, 14, 96, 120); int fr = int(age * 14); if (fr < a.n) drawAnim(a, fr, fx.x * TILE, fx.y * TILE + 40, false, 1.6f); Anim s = mkAnim("Particle FX/Dust_02.png", 64, 64, 8, 32, 60); int sf = int(age * 8); if (sf < s.n) drawAnim(s, sf, fx.x * TILE - 30, fx.y * TILE, false, 3.f, rgb(110, 110, 110, 200)); }}); break;
            case E_Dust: items.push_back({f.y * TILE + 30, [=, this]() { Anim a = mkAnim("Particle FX/Dust_01.png", 64, 64, 14, 32, 60); int fr = int(age * 14); if (fr < a.n) drawAnim(a, fr, fx.x * TILE, fx.y * TILE + 20, false, 2.f); }}); break;
            case E_Splash: items.push_back({f.y * TILE + 30, [=, this]() { Anim a = mkAnim("Particle FX/Water Splash.png", 192, 192, 14, 96, 130); int fr = int(age * 14); if (fr < a.n) drawAnim(a, fr, fx.x * TILE, fx.y * TILE + 20, false, 1.f); }}); break;
            case E_Heal: items.push_back({f.y * TILE + 40, [=, this]() { Anim a = mkAnim("Units/Blue Units/Monk/Heal_Effect.png", 192, 192, 14, 96, 135); int fr = int(age * 14); if (fr < a.n) drawAnim(a, fr, fx.x * TILE, fx.y * TILE + 6, false, .8f); }}); break;
            case E_Arrow: {
                float dur = std::max(.12f, std::hypot(f.x2 - f.x, f.y2 - f.y) / 16.f);
                if (age < dur) items.push_back({f.y2 * TILE + 50, [=, this]() {
                    float k = float(age) / dur;
                    Vector2 a{fx.x * TILE, fx.y * TILE - 30}, b{fx.x2 * TILE, fx.y2 * TILE - 24};
                    Vector2 p{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k - std::sin(k * 3.14159f) * 22.f};
                    float rot = std::atan2(b.y - a.y, b.x - a.x) * 57.2958f;
                    Texture2D ar = tex("Units/Blue Units/Archer/Arrow.png");
                    DrawTexturePro(ar, {0, 0, 64, 64}, {p.x, p.y, 48, 48}, {24, 24}, rot, WHITE);
                }});
                break;
            }
            default: baseDrawFx(f, items, t); break;
            }
        }
        std::erase_if(fxs, [&](const Fx& f) { return t - f.t > (f.type == E_Explosion ? 1.6 : 1.0); });
    }

    void drawWorld(float t) {
        Vector2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(::GetScreenWidth()), float(::GetScreenHeight())}, cam);
        int x0 = int(std::floor(tl.x / TILE)) - 1, y0 = int(std::floor(tl.y / TILE)) - 1, x1 = int(std::floor(br.x / TILE)) + 1, y1 = int(std::floor(br.y / TILE)) + 1;
        int wx0 = x0 - 2, wy0 = y0 - 3, wx1 = x1 + 2, wy1 = y1 + 5, bw = wx1 - wx0 + 1, bh = wy1 - wy0 + 1;
        std::vector<uint8_t> bio(size_t(bw) * size_t(bh), INVALID_BIOME), exp(size_t(bw) * size_t(bh), 0), vnow(size_t(bw) * size_t(bh), 0);
        for (int y = wy0; y <= wy1; y++) for (int x = wx0; x <= wx1; x++) { size_t k = size_t((y - wy0) * bw + (x - wx0)); bio[k] = uint8_t(biomeAt(x, y)); exp[k] = exploredAt(x, y); }
        auto B = [&](int x, int y) { return x < wx0 || y < wy0 || x > wx1 || y > wy1 ? int(INVALID_BIOME) : int(bio[size_t((y - wy0) * bw + (x - wx0))]); };
        auto E = [&](int x, int y) { return x < wx0 || y < wy0 || x > wx1 || y > wy1 ? false : exp[size_t((y - wy0) * bw + (x - wx0))] != 0; };
        reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {         // currently visible tiles
            if (!alliedWith(i.owner) || (isBuilding(i.kind) && i.prog < 100)) return;
            int r = KINFO[i.kind].vision, cx = int(std::floor(g.x)), cy = int(std::floor(g.y));
            for (int y = std::max(cy - r, wy0); y <= std::min(cy + r, wy1); y++) for (int x = std::max(cx - r, wx0); x <= std::min(cx + r, wx1); x++) if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r + r) vnow[size_t((y - wy0) * bw + (x - wx0))] = 1;
        });
        static const char* tileset[B_Count] = {"", "1", "3", "2", "4", "2", "5"};
        auto tint = [](int b) { return b == B_Desert ? rgb(255, 232, 170) : (b == B_Snow ? rgb(236, 246, 255) : WHITE); };
        auto autoTile = [&](Texture2D ts, int x, int y, int colOff, auto member, Color tn) {
            bool U = member(x, y - 1), D = member(x, y + 1), L = member(x - 1, y), R = member(x + 1, y);
            int c, r;
            if (!U && !D) { r = 3; c = (!L && !R) ? 3 : (!L ? 0 : (!R ? 2 : 1)); }
            else if (!L && !R) { c = 3; r = !U ? 0 : (!D ? 2 : 1); }
            else { c = !L ? 0 : (!R ? 2 : 1); r = !U ? 0 : (!D ? 2 : 1); }
            DrawTexturePro(ts, {float((c + colOff) * 64), float(r * 64), 64, 64}, {float(x) * TILE, float(y) * TILE, TILE, TILE}, {0, 0}, 0, tn);
        };
        auto land = [&](int x, int y) { int b = B(x, y); return b != B_Ocean && b != INVALID_BIOME; };
        auto high = [&](int x, int y) { return B(x, y) == B_Highlands; };
        Texture2D foam = tex("Terrain/Tileset/Water Foam.png");
        // foam under the shore
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            if (!land(x, y) || !E(x, y)) continue;
            bool shore = false;
            for (int dy = -1; dy <= 1 && !shore; dy++) for (int dx = -1; dx <= 1; dx++) if (B(x + dx, y + dy) == B_Ocean) { shore = true; break; }
            if (!shore) continue;
            int fr = int(t * 10.f + float(hash2(x, y, 4) % 16)) % 16;
            DrawTexturePro(foam, {float(fr * 192), 0, 192, 192}, {float(x) * TILE - 64, float(y) * TILE - 64, 192, 192}, {0, 0}, 0, WHITE);
        }
        // flat grass (every land tile), then plateau tops, then cliff faces
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            int b = B(x, y);
            if (!land(x, y) || !E(x, y)) continue;
            autoTile(tex(std::string("Terrain/Tileset/Tilemap_color") + tileset[b] + ".png"), x, y, 0, land, tint(b));
            if (b == B_Snow) DrawRectangle(x * 64, y * 64, 64, 64, rgb(240, 248, 255, 215));      // snow cover
            else if (b == B_Desert) DrawRectangle(x * 64, y * 64, 64, 64, rgb(232, 200, 120, 120)); // sand
        }
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            if (!E(x, y)) continue;
            if (high(x, y)) autoTile(tex("Terrain/Tileset/Tilemap_color2.png"), x, y, 5, high, WHITE);
            int b = B(x, y), above = B(x, y - 1);
            if (b != B_Highlands && b != INVALID_BIOME && above == B_Highlands) {
                auto cl = [&](int cx2) { int bb = B(cx2, y); return bb != B_Highlands && bb != INVALID_BIOME && B(cx2, y - 1) == B_Highlands; };
                bool l = cl(x - 1), r = cl(x + 1);
                int c = (!l && !r) ? 8 : (!l ? 5 : (!r ? 7 : 6)), row = b == B_Ocean ? 5 : 4;
                if (b != B_Ocean) autoTile(tex("Terrain/Tileset/Tilemap_color1.png"), x, y, 0, land, WHITE);
                DrawTexturePro(tex("Terrain/Tileset/Tilemap_color2.png"), {float(c * 64), float(row * 64), 64, 64}, {float(x) * TILE, float(y) * TILE, TILE, TILE}, {0, 0}, 0, WHITE);
            }
        }
        // unexplored tiles
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (!E(x, y)) DrawRectangle(x * 64, y * 64, 64, 64, rgb(124, 154, 182));

        std::vector<DrawItem> items;
        items.reserve(512);
        worldFrame();
        drawFeatures(x0, y0 - 0, x1, y1 + 4, bio, wx0, wy0, bw, items, t);
        queuePathLines(items);
        for (auto& mk : markers) { Marker m = mk; items.push_back({-1e9f, [=]() { float a = float(GetTime() - m.t); DrawCircleLines(int(m.x), int(m.y), 6 + a * 40, rgb(255, 255, 255, int(255 * std::max(0.f, 1 - a * 2)))); }}); }
        std::erase_if(markers, [](const Marker& m) { return GetTime() - m.t > .5; });
        Rectangle vr{tl.x - 400, tl.y - 400, br.x - tl.x + 800, br.y - tl.y + 800};
        reg.view<RPos, Info, NetId, Face>().each([&](const RPos& p, Info& i, const NetId& n, Face& fc) {
            if (!CheckCollisionPointRec({p.x * TILE, p.y * TILE}, vr)) return;
            if (aboardUnit(i)) return;
            bool selected = sel.count(n.id) > 0, hover = hovered == n.id;
            if (isBuilding(i.kind)) drawBuilding(p, i, n.id, selected, hover, items, t);
            else drawUnit(p, i, fc, n.id, selected, hover, items, t, B(int(std::floor(p.x)), int(std::floor(p.y))));
        });
        for (auto& g : ghosts) {                                           // destroyed things burn away into ash
            float prog = float((t - g.t) / 1.5);
            if (prog >= 1.f) continue;
            Ghost gh = g;
            items.push_back({gh.y * TILE + 2, [=, this]() {
                float seedv = gh.x * 3.1f + gh.y;
                if (isBuilding(gh.kind)) {
                    const KindInfo& ki = KINFO[gh.kind];
                    BSpr s = buildingSprite(gh.kind, 1);
                    if (!s.file[0]) return;
                    float cx = gh.x * TILE, bottom = (gh.y + float(ki.fh) * .5f) * TILE;
                    Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[colorIdx(gh.owner)] + " Buildings/" + s.file + ".png");
                    float dw = s.artW * s.scale, dh = s.artH * s.scale;
                    drawSprite(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, WHITE, 2, WHITE, prog, seedv);
                } else if (gh.kind != K_Ship) {
                    Anim a = unitAnim(gh.kind, gh.owner, US_Idle, 0, 0, false, 1, 0);
                    drawAnim(a, 0, gh.x * TILE, gh.y * TILE + 6, gh.left, 1.f, WHITE, 2, WHITE, prog, seedv);
                }
                Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 8, 32, 60);
                drawAnim(d, int(prog * 9.f) % d.n, gh.x * TILE, gh.y * TILE - prog * 50.f, false, 2.2f, rgb(70, 70, 76, int(200 * (1 - prog))));
            }});
        }
        std::erase_if(ghosts, [&](const Ghost& g) { return t - g.t > 1.6; });
        drawFx(items, t);
        worldDrawFx(items, t);                                              // [world] bouncing resource drops

        // building ghost (placement preview)
        if (buildKind >= 0 && !overUI(GetMousePosition())) {
            Kind k = Kind(buildKind);
            Vector2 w = GetScreenToWorld2D(::GetMousePosition(), cam);
            IV tt = placeTile(k, w);
            int err = placeCheck(k, tt);
            bool ok = err == P_OK && affordable(placeCost(k));
            items.push_back({1e9f, [=, this]() {
                drawPlaceExtras();
                reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {          // territory squares of own bases
                    if (i.owner != me || (i.kind != K_TownHall && i.kind != K_Outpost)) return;
                    float r = float(anchorRadius(i.kind));
                    DrawRectangleRec({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, rgb(90, 200, 255, 22));
                    DrawRectangleLinesEx({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, 2, rgb(120, 220, 255, 150));
                });
                for (int y = tt.y; y < tt.y + KINFO[k].fh; y++) for (int x = tt.x; x < tt.x + KINFO[k].fw; x++) {
                    PW p2{*this};
                    bool tileOk = p2.explored(x, y) && isLand(p2.biome(x, y)) && !isCliffTile(p2.biome(x, y), p2.biome(x, y - 1)) && !p2.occupied(x, y);
                    bool feat = tileOk && featBlocking(x, y);                  // [world] uncleared tree / rock / gold vein: orange
                    DrawRectangle(x * 64, y * 64, 64, 64, (ok && tileOk && !feat) ? rgb(80, 255, 90, 110) : (tileOk && (err != P_OK || feat) ? rgb(255, 150, 40, 130) : rgb(255, 60, 60, 120)));
                    DrawRectangleLines(x * 64, y * 64, 64, 64, rgb(255, 255, 255, 140));
                }
                float cx = (float(tt.x) + float(KINFO[k].fw) * .5f) * TILE, bottom = float(tt.y + KINFO[k].fh) * TILE;
                BSpr s = buildingSprite(k, 1);
                if (s.file[0]) {
                    Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[colorIdx(me)] + " Buildings/" + s.file + ".png");
                    float dw = s.artW * s.scale, dh = s.artH * s.scale;
                    DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, {0, 0}, 0, rgb(255, 255, 255, 150));
                }
                std::string label = err ? PLACE_TEXT[err] : (ok ? costText(placeCost(k)) : "Not enough resources");
                if (!err) { std::string wl = worldGhostLabel(k, tt); if (!wl.empty()) label = wl; }          // [world]
                textShadow(label, int(cx) - MeasureText(label.c_str(), 18) / 2, int(bottom) + 10, 18, ok ? rgb(160, 255, 160) : rgb(255, 150, 130));
                if (k == K_Campfire) DrawCircleLinesV({cx, bottom - 30}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));
                if (k == K_Mine) worldMineGhost(tt, cx, bottom);              // [world]
            }});
        }
        std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) { return a.y < b.y; });
        for (auto& it : items) it.fn();

        // fog: dim remembered land, then drifting clouds over the unknown
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            size_t k = size_t((y - wy0) * bw + (x - wx0));
            if (exp[k] && !vnow[k]) DrawRectangle(x * 64, y * 64, 64, 64, rgb(10, 14, 30, 85));
        }
        for (int gy = int(std::floor(float(y0) / 5.f)) - 1; gy <= y1 / 5 + 1; gy++)
            for (int gx = int(std::floor(float(x0) / 5.f)) - 2; gx <= x1 / 5 + 2; gx++) {
                uint32_t h = hash2(gx, gy, 4242);
                int cx = gx * 5 + int(h % 5), cy = gy * 5 + int((h >> 3) % 5);
                if (E(cx, cy) && E(cx + 1, cy + 1)) continue;
                if (cx >= wx0 && cx <= wx1 && cy >= wy0 && cy <= wy1 && E(cx, cy)) continue;
                if (E(cx, cy)) continue;
                Texture2D ct = tex(std::string("Terrain/Decorations/Clouds/Clouds_0") + char('1' + (h >> 8) % 8) + ".png");
                float drift = std::sin(t * .12f + float(h % 100)) * 48.f;
                DrawTexturePro(ct, {0, 0, 576, 256}, {float(cx) * TILE - 220 + drift, float(cy) * TILE - 110, 576, 256}, {0, 0}, 0, rgb(255, 255, 255, 235));
            }
        worldDrawClouds(x0, y0, x1, y1, t, E, B);                           // [world] translucent clouds over discovered land
    }

    // ---------------------------------------------------------------- social menu (players / friends / clans / factions)
    struct SBtn { std::string label; int act; float a, b; int c; bool enabled, red; float w; };
    struct SRow { std::string text, sub; Color col; std::vector<SBtn> btns; };
    static bool containsNoCase(const std::string& hay, const std::string& needle) {
        if (needle.empty()) return true;
        std::string h = hay, n = needle;
        for (auto& c : h) c = char(std::tolower(uint8_t(c)));
        for (auto& c : n) c = char(std::tolower(uint8_t(c)));
        return h.find(n) != std::string::npos;
    }
    std::vector<SRow> socialRows() {
        std::vector<SRow> rows;
        int mc = myClan();
        auto friendBtn = [&](const Peer& p) {
            if (p.flags & 2) return SBtn{"Unfriend", 3, 0, 0, p.id, true, true, 70};
            if (p.flags & 4) return SBtn{"Accept", 2, 0, 0, p.id, true, false, 70};
            if (p.flags & 8) return SBtn{"Sent", 0, 0, 0, p.id, false, false, 70};
            return SBtn{"Friend", 2, 0, 0, p.id, true, false, 70};
        };
        auto peerRow = [&](const Peer& p) {
            std::string cn = p.clan ? clanName(p.clan) : "";
            SRow r{p.name + (cn.empty() ? "" : "  [" + cn + "]"), std::string(p.flags & 1 ? "online" : "offline") + (p.hasLoc ? TextFormat("  (%d, %d)", int(p.lx), int(p.ly)) : "  location unknown"), teamColor(p.id), {}};
            r.btns.push_back({"Travel", 1, p.lx, p.ly, 0, p.hasLoc, false, 60});
            r.btns.push_back(friendBtn(p));
            return r;
        };
        switch (socialTab) {
        case 0: for (auto& p : roster) if (p.id != me && containsNoCase(p.name, searchText)) rows.push_back(peerRow(p)); break;
        case 1: for (auto& p : roster) if (p.id != me && (p.flags & (2 | 4 | 8)) && containsNoCase(p.name, searchText)) rows.push_back(peerRow(p)); break;
        case 2:
            for (auto& c : clansList) {
                if (!containsNoCase(c.name, searchText)) continue;
                SRow r{c.name, TextFormat("%d member%s", c.members, c.members == 1 ? "" : "s"), clanColor(c.id), {}};
                r.btns.push_back({c.id == mc ? "Yours" : "Join", 4, 0, 0, c.id, mc == 0, false, 60});
                rows.push_back(r);
            }
            break;
        case 3:
            if (!mc) rows.push_back({"You are not in a clan.", "Create one or join one in the Clans tab.", rgb(90, 60, 40), {}});
            else
                for (auto& p : roster) {
                    if (p.clan != mc) continue;
                    int leader = 0;
                    for (auto& c : clansList) if (c.id == mc) leader = c.leader;
                    SRow r{p.name + (p.id == leader ? "  (leader, clan hall)" : ""), std::string(p.flags & 1 ? "online" : "offline") + (p.hasLoc ? TextFormat("  (%d, %d)", int(p.lx), int(p.ly)) : ""), teamColor(p.id), {}};
                    r.btns.push_back({"Travel", 1, p.lx, p.ly, 0, p.hasLoc, false, 56});
                    if (p.id != me) {
                        if (leader == me) r.btns.push_back({"Lead", 8, 0, 0, p.id, true, false, 38});
                        static const char* ab[R_Count] = {"W", "G", "S", "M"};
                        for (int k = 0; k < R_Count; k++) r.btns.push_back({std::string("50") + ab[k], 5, float(k), 50.f, p.id, res[k] >= 50.f, false, 34});
                    }
                    rows.push_back(r);
                }
            break;
        default:
            for (int f = 0; f < FACTION_COUNT; f++) {
                int rp = facRep[f];
                SRow r{FACTION_NAME[f], TextFormat("%s (%d)", repTier(rp), rp), teamColor(FACTION_BASE + f), {}};
                const FacTown* best = nullptr;
                float bd = 1e9f;
                for (auto& t : facTowns) if (t.faction == f) { float d = std::hypot(t.x - cam.target.x / TILE, t.y - cam.target.y / TILE); if (d < bd) { bd = d; best = &t; } }
                r.btns.push_back({best ? "Nearest town" : "None found", 1, best ? best->x : 0.f, best ? best->y : 0.f, 0, best != nullptr, false, 104});
                rows.push_back(r);
            }
            break;
        }
        return rows;
    }
    void travelTo(float tx, float ty) { cam.target = {tx * TILE, ty * TILE}; socialOpen = false; bigMapOpen = false; textFocus = false; }
    void runSocial(const SBtn& b) {
        switch (b.act) {
        case 1: travelTo(b.a, b.b); break;
        case 2: cmdAlly(b.c, AA_Invite); break;
        case 3: cmdAlly(b.c, AA_Break); break;
        case 4: cmdClanJoin(b.c); break;
        case 5: cmdTransfer(b.c, int(b.a), b.b); break;
        case 6: cmdClanLeave(); break;
        case 7: cmdClanCreate(searchText); searchText.clear(); break;
        case 8: cmdClanLeader(b.c); break;
        default: break;
        }
    }
    static constexpr float SROW = 30.f;
    float socialScale() { Rectangle r = socialRect(); return std::min(r.width / mSocial.size().x, r.height / mSocial.size().y); }
    Rectangle socialTabRect(int i) {
        Rectangle r = socialRect();
        float pad = float(mSocial.padding), w = (r.width - 2 * pad - 8) / 5.f;
        return {r.x + pad + 4 + float(i) * w, r.y + pad + 4, w - 3, 26};
    }
    Rectangle socialSearch() { Rectangle r = socialRect(); float pad = float(mSocial.padding) * socialScale(); return {r.x + pad + 6, r.y + pad + 36, r.width - 2 * pad - 12, 24}; }
    Rectangle socialList() { Rectangle r = socialRect(); float pad = float(mSocial.padding) * socialScale(); return {r.x + pad + 6, r.y + pad + 68, r.width - 2 * pad - 12, r.height - 2 * pad - 68 - 42}; }
    Rectangle socialBottomBtn() { Rectangle r = socialRect(); float pad = float(mSocial.padding) * socialScale(); return {r.x + r.width - pad - 150, r.y + r.height - pad - 34, 144, 28}; }
    Rectangle socialClose() { Rectangle r = socialRect(); float pad = float(mSocial.padding) * socialScale(); return {r.x + r.width - pad - 20, r.y + std::max(2.f, pad - 22), 22, 18}; }
    Rectangle rowBtnRect(const Rectangle& row, const std::vector<SBtn>& btns, size_t i) {
        float x = row.x + row.width - 4;
        for (size_t k = btns.size(); k-- > i;) x -= btns[k].w + 4;
        return {x, row.y + 3, btns[i].w, SROW - 8};
    }
    bool bottomBtn(SBtn& out) {
        if (socialTab == 2 && myClan() == 0) { out = {"Create clan", 7, 0, 0, 0, searchText.size() >= 3, false, 144}; return true; }
        if (socialTab == 3 && myClan() != 0) { out = {"Leave clan", 6, 0, 0, 0, true, true, 144}; return true; }
        return false;
    }
    void socialInput(Vector2 m) {
        if (textFocus) {
            for (int ch = GetCharPressed(); ch > 0; ch = GetCharPressed()) if (ch >= 32 && ch < 127 && searchText.size() < 24) { searchText.push_back(char(ch)); socialScroll = 0; }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !searchText.empty()) searchText.pop_back();
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) textFocus = false;
        } else if (IsKeyPressed(KEY_P)) { socialOpen = false; return; }
        if (IsKeyPressed(KEY_ESCAPE)) { if (textFocus) textFocus = false; else socialOpen = false; return; }
        Rectangle sr = socialRect(), list = socialList();
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && CheckCollisionPointRec(m, sr)) socialScroll = std::max(0, socialScroll - int(wheel));
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        if (!CheckCollisionPointRec(m, sr)) { socialOpen = false; textFocus = false; return; }
        if (CheckCollisionPointRec(m, socialClose())) { socialOpen = false; textFocus = false; return; }
        for (int i = 0; i < 5; i++) if (CheckCollisionPointRec(m, socialTabRect(i))) { socialTab = i; socialScroll = 0; return; }
        textFocus = CheckCollisionPointRec(m, socialSearch());
        SBtn bb;
        if (bottomBtn(bb) && CheckCollisionPointRec(m, socialBottomBtn()) && bb.enabled) { runSocial(bb); return; }
        auto rows = socialRows();
        int vis = int(list.height / SROW);
        socialScroll = std::clamp(socialScroll, 0, std::max(0, int(rows.size()) - vis));
        for (int i = 0; i < vis && i + socialScroll < int(rows.size()); i++) {
            Rectangle rr{list.x, list.y + float(i) * SROW, list.width, SROW};
            auto& row = rows[size_t(i + socialScroll)];
            for (size_t k = 0; k < row.btns.size(); k++)
                if (CheckCollisionPointRec(m, rowBtnRect(rr, row.btns, k)) && row.btns[k].enabled) { runSocial(row.btns[k]); return; }
        }
    }
    void drawSocial() {
        Vector2 m = GetMousePosition();
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), rgb(0, 0, 0, 110));
        Rectangle sr = socialRect();
        drawTMap(mSocial, sr);
        float pad = float(mSocial.padding) * socialScale();
        Color ink = rgb(70, 45, 30);
        static const char* tabs[5] = {"Players", "Friends", "Clans", "My Clan", "Factions"};
        for (int i = 0; i < 5; i++) drawButton(socialTabRect(i), tabs[i], true, false, CheckCollisionPointRec(m, socialTabRect(i)), socialTab == i, 12);
        drawButton(socialClose(), "X", true, true, CheckCollisionPointRec(m, socialClose()), false, 12);
        Rectangle sb = socialSearch();
        DrawRectangleRounded(sb, .3f, 6, rgb(255, 250, 235, 235));
        DrawRectangleRoundedLinesEx(sb, .3f, 6, 2, textFocus ? rgb(60, 120, 200) : rgb(120, 90, 60));
        std::string shown = searchText.empty() && !textFocus ? (socialTab == 2 ? "Search clans or type a new clan name..." : "Search by name...") : searchText + (textFocus && int(GetTime() * 2) % 2 == 0 ? "|" : "");
        DrawText(shown.c_str(), int(sb.x) + 8, int(sb.y) + 5, 15, searchText.empty() && !textFocus ? rgb(150, 130, 110) : ink);
        auto rows = socialRows();
        Rectangle list = socialList();
        int vis = int(list.height / SROW);
        socialScroll = std::clamp(socialScroll, 0, std::max(0, int(rows.size()) - vis));
        if (rows.empty()) DrawText(socialTab == 1 ? "No friends yet. Add players from the Players tab." : "Nothing to show.", int(list.x) + 6, int(list.y) + 8, 14, rgb(120, 90, 60));
        for (int i = 0; i < vis && i + socialScroll < int(rows.size()); i++) {
            Rectangle rr{list.x, list.y + float(i) * SROW, list.width, SROW};
            auto& row = rows[size_t(i + socialScroll)];
            DrawRectangleRec(rr, i % 2 ? rgb(0, 0, 0, 14) : rgb(255, 255, 255, 36));
            DrawText(row.text.c_str(), int(rr.x) + 6, int(rr.y) + 2, 15, row.col.r > 200 && row.col.g > 200 ? ink : row.col);
            DrawText(row.sub.c_str(), int(rr.x) + 6, int(rr.y) + 17, 10, rgb(110, 85, 60));
            for (size_t k = 0; k < row.btns.size(); k++) {
                Rectangle b = rowBtnRect(rr, row.btns, k);
                drawButton(b, row.btns[k].label, row.btns[k].enabled, row.btns[k].red, CheckCollisionPointRec(m, b), false, 11);
            }
        }
        if (int(rows.size()) > vis) DrawText(TextFormat("%d-%d of %d  (mouse wheel)", socialScroll + 1, std::min(int(rows.size()), socialScroll + vis), int(rows.size())), int(list.x) + 6, int(list.y + list.height) + 2, 10, ink);
        SBtn bb;
        if (bottomBtn(bb)) drawButton(socialBottomBtn(), bb.label, bb.enabled, bb.red, CheckCollisionPointRec(m, socialBottomBtn()), false, 13);
        if (socialTab == 3 && myClan()) DrawText("Resources travel by convoy and can be raided.", int(sr.x + pad) + 6, int(sr.y + sr.height - pad - 28), 11, ink);
        if (socialTab == 2 && myClan() == 0) DrawText("Type a name above, then press Create.", int(sr.x + pad) + 6, int(sr.y + sr.height - pad - 28), 11, ink);
    }

    // ---------------------------------------------------------------- big map
    float bmTpp() const { static const float z[7] = {.125f, .25f, .5f, 1.f, 2.f, 4.f, 8.f}; return z[std::clamp(bmZoom, 0, 6)]; }
    void openBigMap() { bigMapOpen = true; bmCenter = {cam.target.x / TILE, cam.target.y / TILE}; bmLast = 0; bmDragging = false; }
    Vector2 mapToTile(Vector2 p) {
        Rectangle in = mapInner();
        return {bmCenter.x + (p.x - in.x - in.width / 2) * bmTpp(), bmCenter.y + (p.y - in.y - in.height / 2) * bmTpp()};
    }
    Vector2 tileToMap(float tx, float ty) {
        Rectangle in = mapInner();
        return {in.x + in.width / 2 + (tx - bmCenter.x) / bmTpp(), in.y + in.height / 2 + (ty - bmCenter.y) / bmTpp()};
    }
    void bigMapInput(Vector2 m) {
        Rectangle in = mapInner(), fr = mapFrameRect();
        if (bmMenuInput(m)) return;
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_N)) { bigMapOpen = false; return; }
        if (IsKeyPressed(KEY_P)) { bigMapOpen = false; socialOpen = true; return; }
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && CheckCollisionPointRec(m, in)) {
            Vector2 before = mapToTile(m);
            bmZoom = std::clamp(bmZoom - (wheel > 0 ? 1 : -1), 0, 6);
            Vector2 after = mapToTile(m);
            bmCenter.x += before.x - after.x;
            bmCenter.y += before.y - after.y;
            bmLast = 0;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (!CheckCollisionPointRec(m, fr)) { bigMapOpen = false; return; }
            if (CheckCollisionPointRec(m, in)) { bmDragging = true; bmDragMouse = m; bmDragMoved = 0; }
        }
        if (bmDragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 d = {m.x - bmDragMouse.x, m.y - bmDragMouse.y};
            bmDragMoved += std::abs(d.x) + std::abs(d.y);
            bmCenter.x -= d.x * bmTpp();
            bmCenter.y -= d.y * bmTpp();
            bmDragMouse = m;
            bmLast = 0;
        }
        if (bmDragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            bmDragging = false;
            if (bmDragMoved < 5) { Vector2 t = mapToTile(m); travelTo(t.x, t.y); }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !bmRightClick(m)) bigMapOpen = false;
    }
    void refreshBigMap() {
        Rectangle in = mapInner();
        int w = std::max(8, int(in.width / 2)), h = std::max(8, int(in.height / 2));
        if (!bmTex.id || w != bmW || h != bmH) {
            if (bmTex.id) UnloadTexture(bmTex);
            Image im = GenImageColor(w, h, BLACK);
            bmTex = LoadTextureFromImage(im);
            UnloadImage(im);
            bmW = w; bmH = h;
            bmPix.assign(size_t(w) * size_t(h), BLACK);
            bmLast = 0;
        }
        if (GetTime() - bmLast < .35) return;
        bmLast = GetTime();
        float tpp = bmTpp();
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int tx = int(std::floor(bmCenter.x + (float(i * 2 + 1) - in.width / 2) * tpp)), ty = int(std::floor(bmCenter.y + (float(j * 2 + 1) - in.height / 2) * tpp));
                bool ex = exploredAt(tx, ty);
                Color c = ex ? biomeColor(biomeAt(tx, ty)) : rgb(20, 26, 38);
                if (!ex && ((i / 4 + j / 4) & 1)) c = rgb(24, 30, 44);
                bmPix[size_t(j * w + i)] = c;
            }
        UpdateTexture(bmTex, bmPix.data());
    }
    void mapIcon(const std::string& file, Vector2 p, float width, float artW, float artH, Color tint = WHITE) {
        Texture2D t = tex(file);
        float sc = width / artW;
        DrawTexturePro(t, {0, 0, artW, artH}, {p.x - width / 2, p.y - artH * sc * .8f, width, artH * sc}, {0, 0}, 0, tint);
    }
    // Icons shared by the world map and the minimap: territory, towns, faction towns and travelling convoys.
    int leaderClanOf(int owner) const {                                  // the Town Hall of a clan's leader is its clan hall
        for (auto& c : clansList) if (c.leader == owner) return c.id;
        return 0;
    }
    void drawMapOverlay(Rectangle in, Vector2 c, float tpp, bool mini) {
        auto T = [&](float tx, float ty) { return Vector2{in.x + in.width / 2 + (tx - c.x) / tpp, in.y + in.height / 2 + (ty - c.y) / tpp}; };
        auto inside = [&](Vector2 p, float pad) { return p.x > in.x + pad && p.y > in.y + pad && p.x < in.x + in.width - pad && p.y < in.y + in.height - pad; };
        float castle = mini ? 15.f : 28.f, mycastle = mini ? 19.f : 34.f;
        reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {        // territory: clans share one colour
            if (!isBuilding(i.kind) || i.prog < 100 || (i.state & BLD_DISABLED) || ((i.state & BLD_MOVING) && i.kind != K_TownHall) || isFactionOwner(i.owner) || (i.kind != K_TownHall && i.kind != K_Outpost)) return;
            auto p = peerOf(i.owner);
            int clan = i.owner == me ? myClan() : (p ? p->clan : 0);
            Color col = clan ? clanColor(clan) : teamColor(i.owner);
            float r = float(anchorRadius(i.kind)) / tpp;
            Vector2 sp = T(g.x, g.y);
            float x0 = std::max(sp.x - r, in.x), y0 = std::max(sp.y - r, in.y), x1 = std::min(sp.x + r, in.x + in.width), y1 = std::min(sp.y + r, in.y + in.height);
            if (x1 <= x0 || y1 <= y0) return;
            DrawRectangleRec({x0, y0, x1 - x0, y1 - y0}, Color{col.r, col.g, col.b, 55});
            DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, mini ? 1.f : 2.f, Color{col.r, col.g, col.b, 190});
        });
        for (auto& t : facTowns) {                                           // discovered faction towns
            Vector2 sp = T(t.x, t.y);
            if (!inside(sp, mini ? 6.f : 10.f)) continue;
            int rp = facRep[t.faction];
            Color ring = rp <= HOSTILE_REP ? rgb(240, 70, 60) : (rp >= FRIENDLY_REP ? rgb(90, 220, 110) : rgb(235, 220, 120));
            DrawCircleV({sp.x, sp.y - castle * .25f}, castle * .52f, Color{ring.r, ring.g, ring.b, 90});
            mapIcon(std::string("Buildings/") + COLOR_NAME[FACTION_COLOR[t.faction]] + " Buildings/Castle.png", sp, castle - 2, 320, 256);
            if (!mini) { std::string nm = FACTION_NAME[t.faction]; DrawText(nm.c_str(), int(sp.x) - MeasureText(nm.c_str(), 10) / 2, int(sp.y) + 6, 10, WHITE); }
        }
        std::set<int> drawnTH;
        reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {         // player towns and outposts we know about
            if (!isBuilding(i.kind) || isFactionOwner(i.owner) || (i.kind != K_TownHall && i.kind != K_Outpost)) return;
            Vector2 sp = T(g.x, g.y);
            if (i.kind == K_TownHall) drawnTH.insert(i.owner);
            if (!inside(sp, mini ? 4.f : 8.f)) return;
            bool mine = i.owner == me;
            if (i.kind == K_TownHall) {
                if (mine) DrawCircleV({sp.x, sp.y - mycastle * .25f}, mycastle * .5f, rgb(255, 215, 80, 110));
                mapIcon(std::string("Buildings/") + COLOR_NAME[colorIdx(i.owner)] + " Buildings/Castle.png", sp, mine ? mycastle : castle, 320, 256);
                int hallOf = leaderClanOf(i.owner);
                if (hallOf) {                                                // clan hall: a banner in the clan colour
                    Color cc = clanColor(hallOf);
                    float bx = sp.x, by = sp.y - (mine ? mycastle : castle) * .85f;
                    DrawRectangle(int(bx) - 1, int(by) - (mini ? 6 : 10), 2, mini ? 8 : 12, BLACK);
                    DrawTriangle({bx + 1, by - (mini ? 6.f : 10.f)}, {bx + 1, by - (mini ? 1.f : 3.f)}, {bx + (mini ? 6.f : 10.f), by - (mini ? 3.5f : 6.5f)}, cc);
                }
                if (!mini) {
                    std::string nm = nameOf(i.owner);
                    DrawText(nm.c_str(), int(sp.x) - MeasureText(nm.c_str(), 11) / 2, int(sp.y) + 6, 11, mine ? rgb(255, 235, 140) : (alliedWith(i.owner) ? rgb(150, 255, 170) : WHITE));
                }
            } else mapIcon(std::string("Buildings/") + COLOR_NAME[colorIdx(i.owner)] + " Buildings/Tower.png", sp, mini ? 6.f : 12.f, 128, 256);
        });
        for (auto& p : roster) {                                             // towns whose location we know but cannot currently see
            if (p.id == me || !p.hasLoc || drawnTH.count(p.id)) continue;
            Vector2 sp = T(p.lx, p.ly);
            if (!inside(sp, mini ? 4.f : 8.f)) continue;
            mapIcon(std::string("Buildings/") + COLOR_NAME[colorIdx(p.id)] + " Buildings/Castle.png", sp, castle - 2, 320, 256, rgb(255, 255, 255, 200));
            if (!mini) DrawText(p.name.c_str(), int(sp.x) - MeasureText(p.name.c_str(), 11) / 2, int(sp.y) + 6, 11, WHITE);
        }
        double now = GetTime();
        reg.view<Info, RPos>().each([&](const Info& i, const RPos& p) {          // travelling convoys and hauling parties
            if (!(i.state & UNIT_CONVOY_FLAG)) return;
            Vector2 sp = T(p.x, p.y);
            if (!inside(sp, mini ? 3.f : 6.f)) return;
            bool friendly = alliedWith(i.owner);
            Color col = friendly ? rgb(100, 230, 255) : rgb(255, 70, 60);
            float pulse = friendly ? 0.f : 3.f * float(std::sin(now * 6) * .5 + .5), rad = (mini ? 4.f : 8.f) + pulse;
            DrawCircleV(sp, rad, Color{col.r, col.g, col.b, 110});
            DrawCircleLines(int(sp.x), int(sp.y), rad, col);
            if (mini) { DrawCircleV(sp, 2, col); return; }
            Anim a = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(i.owner)] + " Units/Pawn/Pawn_Run Gold.png", 192, 192, 10, 96, 135);
            DrawTexturePro(a.t, frameRect(a, 0), {sp.x - 12, sp.y - 16, 24, 24}, {0, 0}, 0, WHITE);
            if (!friendly) { DrawText("!", int(sp.x) + 9, int(sp.y) - 16, 14, rgb(255, 90, 70)); DrawText((nameOf(i.owner) + " convoy").c_str(), int(sp.x) + 14, int(sp.y) - 4, 10, rgb(255, 170, 150)); }
        });
        drawSupportMapIcons(in, c, tpp, mini);
    }
    void drawBigMap() {
        Vector2 m = GetMousePosition();
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), rgb(0, 0, 0, 150));
        Rectangle fr = mapFrameRect(), in = mapInner();
        drawTMap(mMapFrame, fr);
        refreshBigMap();
        DrawRectangleRec(in, rgb(16, 20, 30));
        if (bmTex.id) DrawTexturePro(bmTex, {0, 0, float(bmW), float(bmH)}, {in.x, in.y, float(bmW) * 2, float(bmH) * 2}, {0, 0}, 0, WHITE);
        auto inside = [&](Vector2 p, float pad) { return p.x > in.x + pad && p.y > in.y + pad && p.x < in.x + in.width - pad && p.y < in.y + in.height - pad; };
        float tpp = bmTpp();
        drawMapOverlay(in, bmCenter, tpp, false);
        // far-away friends: an arrow on the map border pointing towards their town
        for (auto& p : roster) {
            if (p.id == me || !p.hasLoc || !(p.flags & 2 || (p.clan && p.clan == myClan()))) continue;
            Vector2 sp = tileToMap(p.lx, p.ly);
            if (inside(sp, 0)) continue;
            Vector2 c{in.x + in.width / 2, in.y + in.height / 2}, d{sp.x - c.x, sp.y - c.y};
            float k = std::min((in.width / 2 - 20) / std::max(1.f, std::abs(d.x)), (in.height / 2 - 20) / std::max(1.f, std::abs(d.y)));
            Vector2 e{c.x + d.x * k, c.y + d.y * k};
            float ang = std::atan2(d.y, d.x);
            Color col = p.clan && p.clan == myClan() ? clanColor(p.clan) : rgb(150, 255, 170);
            Vector2 a1{e.x + std::cos(ang) * 12, e.y + std::sin(ang) * 12}, a2{e.x + std::cos(ang + 2.5f) * 10, e.y + std::sin(ang + 2.5f) * 10}, a3{e.x + std::cos(ang - 2.5f) * 10, e.y + std::sin(ang - 2.5f) * 10};
            DrawTriangle(a1, a3, a2, col);
            DrawTriangleLines(a1, a3, a2, BLACK);
            float dist = std::hypot(p.lx - bmCenter.x, p.ly - bmCenter.y);
            std::string lb = p.name + " " + std::to_string(int(dist)) + "m";
            float tx = std::clamp(e.x - float(MeasureText(lb.c_str(), 11)) / 2 - std::cos(ang) * 22, in.x + 4, in.x + in.width - float(MeasureText(lb.c_str(), 11)) - 4);
            float ty = std::clamp(e.y - 6 - std::sin(ang) * 18, in.y + 4, in.y + in.height - 16);
            textShadow(lb, int(tx), int(ty), 11, col);
        }
        // where the game camera currently looks
        {
            Vector2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(::GetScreenWidth()), float(::GetScreenHeight())}, cam);
            Vector2 a = tileToMap(tl.x / TILE, tl.y / TILE), b = tileToMap(br.x / TILE, br.y / TILE);
            float x0 = std::max(a.x, in.x), y0 = std::max(a.y, in.y), x1 = std::min(b.x, in.x + in.width), y1 = std::min(b.y, in.y + in.height);
            if (x1 > x0 && y1 > y0) DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, 1, rgb(255, 255, 255, 220));
        }
        DrawRectangleLinesEx(in, 2, rgb(60, 40, 26));
        textShadow("WORLD MAP", int(fr.x + fr.width / 2) - 50, int(fr.y) + 10, 18, rgb(255, 240, 200));
        std::string hint = "Click: travel   Right-click ally: support patrol   Drag: pan   Wheel: zoom   N/Esc: close";
        textShadow(hint, int(fr.x + fr.width / 2) - MeasureText(hint.c_str(), 12) / 2, int(fr.y + fr.height) - 24, 12, rgb(255, 240, 200));
        if (CheckCollisionPointRec(m, in)) {
            Vector2 t = mapToTile(m);
            std::string cs = TextFormat("%d, %d", int(std::floor(t.x)), int(std::floor(t.y)));
            DrawRectangleRounded({m.x + 14, m.y + 16, float(MeasureText(cs.c_str(), 14)) + 12, 22}, .3f, 4, rgb(30, 22, 16, 225));
            DrawText(cs.c_str(), int(m.x) + 20, int(m.y) + 20, 14, WHITE);
        }
        drawBmMenu();
    }
    void drawSocialAndMap() {
        if (bigMapOpen) drawBigMap();
        if (socialOpen) drawSocial();
    }

    // ---------------------------------------------------------------- Archery: troop training panel
    struct TroopText { const char* role; const char* desc; };
    static TroopText troopText(Kind k) {
        switch (k) {
        case K_Pawn: return {"Worker", "Carries farmed resources to the nearest storage and hauls stock between outposts, storage houses and the Town Hall. Weak in a fight, and hauling parties can be raided."};
        case K_Warrior: return {"Frontline infantry", "Sturdy sword and shield fighter. Takes 20% less damage from every attack. The backbone of any army against both units and buildings."};
        case K_Archer: return {"Ranged support", "Shoots arrows from a distance. Deals only 70% damage to buildings and dies quickly in melee, so keep them behind your warriors."};
        case K_Lancer: return {"Fast spear cavalry", "The fastest troop. Deals 30% extra damage to units but only 60% to buildings. Ideal for hunting convoys and raiders."};
        case K_Monk: return {"Healer", "Automatically heals the most wounded friendly unit nearby. Cannot attack, so escort them. Higher levels heal faster."};
        default: return {"", ""};
        }
    }
    static std::vector<std::string> wrapText(const std::string& s, int maxW, int fs) {
        std::vector<std::string> out;
        std::string cur, word;
        std::istringstream ss(s);
        while (ss >> word) {
            std::string t = cur.empty() ? word : cur + " " + word;
            if (MeasureText(t.c_str(), fs) > maxW && !cur.empty()) { out.push_back(cur); cur = word; }
            else cur = t;
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }
    int troopLevel(Kind k) const { return unitLvl[std::clamp(int(k) - int(K_Pawn), 0, 4)]; }
    int trainBuildingLevel() { auto it = ents.find(trainTarget); return it == ents.end() ? 1 : reg.get<Info>(it->second).aux; }
    struct PanelXf { float ox, oy, sc; };
    PanelXf trainXf() {
        Vector2 s = mTrain.size();
        float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
        float sc = std::min(1.f, std::min((sw - 16) / s.x, (sh - 30) / s.y));
        return {(sw - s.x * sc) / 2, (sh - s.y * sc) / 2 + 12 * sc, sc};
    }
    Vector2 toPanel(Vector2 m) { PanelXf x = trainXf(); return {(m.x - x.ox) / x.sc, (m.y - x.oy) / x.sc}; }
    Rectangle trainCard(int i) { return {30.f + float(i % 3) * 108.f, 66.f + float(i / 3) * 128.f, 100, 118}; }
    Rectangle trainDetail() { return {30.f + 3 * 108.f + 8, 66, mTrain.size().x - (30.f + 3 * 108.f + 8) - 30, 246}; }
    Rectangle trainBtnTrain() { Rectangle d = trainDetail(); return {d.x + 8, d.y + d.height - 66, d.width - 16, 28}; }
    Rectangle trainBtnTroopUp() { Rectangle d = trainDetail(); return {d.x + 8, d.y + d.height - 34, d.width - 16, 28}; }
    Rectangle trainBlock() { return {30, 66 + 2 * 128 + 6, mTrain.size().x - 60, mTrain.size().y - (66 + 2 * 128 + 6) - 30}; }
    Rectangle trainBtnBuildingUp() { Rectangle b = trainBlock(); return {b.x + 8, b.y + 26, 220, 28}; }
    Rectangle trainClose() { return {mTrain.size().x - 52, 20, 26, 20}; }
    void troopCost(Kind k, int toLevel, float* cost) {
        for (int i = 0; i < R_Count; i++) cost[i] = std::ceil(KINFO[k].cost[i] * 2.5f * float(toLevel - 1) + (i == R_Gold ? 30.f * float(toLevel - 1) : 0.f));
    }
    void trainInput(Vector2 mouse) {
        if (IsKeyPressed(KEY_ESCAPE)) { trainOpen = false; return; }
        auto it = ents.find(trainTarget);
        if (it == ents.end()) { trainOpen = false; return; }
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        Vector2 m = toPanel(mouse);
        Vector2 sz = mTrain.size();
        if (m.x < 0 || m.y < 0 || m.x > sz.x || m.y > sz.y || CheckCollisionPointRec(m, trainClose())) { trainOpen = false; return; }
        if (queueClick(m)) return;
        for (int i = 0; i < 5; i++) if (CheckCollisionPointRec(m, trainCard(i))) { trainSel = i; return; }
        Kind k = Kind(K_Pawn + trainSel);
        int bl = trainBuildingLevel();
        if (CheckCollisionPointRec(m, trainBtnTrain()) && bl >= unitUnlockLevel(k) && affordable(KINFO[k].cost)) cmdTrain(trainTarget, k);
        int lv = troopLevel(k);
        float uc[R_Count];
        troopCost(k, lv + 1, uc);
        if (CheckCollisionPointRec(m, trainBtnTroopUp()) && lv < MAX_UNIT_LEVEL && bl >= troopUpgradeNeedsArchery(lv + 1) && affordable(uc)) cmdUpgradeUnit(k);
        float bc[R_Count];
        const Info& in = reg.get<Info>(it->second);
        for (int r = 0; r < R_Count; r++) bc[r] = float(mapUpgradeCost(in, r));
        if (CheckCollisionPointRec(m, trainBtnBuildingUp()) && in.aux < MAX_LEVEL && affordable(bc)) cmdUpgrade(trainTarget);
    }
    void statBar(float x, float y, float w, float frac, Color c, const std::string& label, const std::string& value, const std::string& bonus) {
        DrawText(label.c_str(), int(x), int(y), 12, rgb(70, 45, 30));
        DrawRectangleRounded({x + 56, y + 2, w, 10}, .5f, 4, rgb(60, 40, 28, 200));
        DrawRectangleRounded({x + 56, y + 2, std::max(4.f, w * std::clamp(frac, 0.f, 1.f)), 10}, .5f, 4, c);
        DrawText(value.c_str(), int(x + 62 + w), int(y), 12, rgb(70, 45, 30));
        if (!bonus.empty()) DrawText(bonus.c_str(), int(x + 62 + w + MeasureText(value.c_str(), 12) + 4), int(y), 11, rgb(30, 130, 50));
    }
    void drawTrain() {
        auto it = ents.find(trainTarget);
        if (it == ents.end()) return;
        const Info& bin = reg.get<Info>(it->second);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), rgb(0, 0, 0, 120));
        PanelXf xf = trainXf();
        Vector2 m = toPanel(GetMousePosition());
        rlPushMatrix();
        rlTranslatef(xf.ox, xf.oy, 0);
        rlScalef(xf.sc, xf.sc, 1);
        Vector2 sz = mTrain.size();
        Color ink = rgb(70, 45, 30);
        drawTMap(mTrain, {0, 0, sz.x, sz.y});
        Rectangle tr{sz.x / 2 - mTitle.size().x / 2, -26, mTitle.size().x, mTitle.size().y};
        drawTMap(mTitle, tr);
        std::string title = TextFormat("Archery - Level %d", bin.aux);
        textShadow(title, int(tr.x + tr.width / 2) - MeasureText(title.c_str(), 20) / 2, int(tr.y + tr.height / 2) - 12, 20, WHITE);
        drawButton(trainClose(), "X", true, true, CheckCollisionPointRec(m, trainClose()), false, 12);
        double now = GetTime();
        for (int i = 0; i < 5; i++) {                                   // grid of animated troop cards
            Kind k = Kind(K_Pawn + i);
            Rectangle c = trainCard(i);
            bool locked = bin.aux < unitUnlockLevel(k), sel = trainSel == i, hov = CheckCollisionPointRec(m, c);
            DrawRectangleRounded(c, .12f, 6, sel ? rgb(255, 245, 200) : (hov ? rgb(250, 235, 205) : rgb(238, 222, 190)));
            DrawRectangleRoundedLinesEx(c, .12f, 6, sel ? 3.f : 2.f, sel ? rgb(60, 140, 220) : rgb(120, 90, 60));
            Anim a = unitAnim(k, me, US_Idle, 0, 0, false, 1, now);
            float sc = a.fw > 200 ? .36f : .62f;
            drawAnim(a, frameFor(a, now, float(i) * .7f), c.x + c.width / 2, c.y + 84, false, sc, locked ? rgb(70, 70, 90, 255) : WHITE);
            DrawText(KINFO[k].name, int(c.x + c.width / 2) - MeasureText(KINFO[k].name, 14) / 2, int(c.y + c.height - 20), 14, ink);
            DrawRectangleRounded({c.x + 4, c.y + 4, 34, 18}, .4f, 4, rgb(60, 40, 28, 220));
            DrawText(TextFormat("Lv%d", troopLevel(k)), int(c.x) + 9, int(c.y) + 7, 12, rgb(255, 235, 160));
            if (locked) {
                DrawRectangleRounded(c, .12f, 6, rgb(10, 10, 25, 140));
                DrawRectangle(int(c.x + c.width / 2) - 9, int(c.y + 40), 18, 14, rgb(230, 190, 70));
                DrawRectangleLines(int(c.x + c.width / 2) - 9, int(c.y + 40), 18, 14, BLACK);
                DrawCircleLines(int(c.x + c.width / 2), int(c.y + 40), 7, rgb(230, 230, 240));
                std::string need = TextFormat("Archery Lv%d", unitUnlockLevel(k));
                DrawText(need.c_str(), int(c.x + c.width / 2) - MeasureText(need.c_str(), 12) / 2, int(c.y + 60), 12, WHITE);
            }
        }
        // detail panel for the selected troop
        Kind k = Kind(K_Pawn + trainSel);
        Rectangle d = trainDetail();
        DrawRectangleRounded(d, .05f, 6, rgb(255, 250, 235, 200));
        DrawRectangleRoundedLinesEx(d, .05f, 6, 2, rgb(120, 90, 60));
        int lv = troopLevel(k);
        Anim big = unitAnim(k, me, US_Idle, 0, 0, false, 1, now);
        drawAnim(big, frameFor(big, now), d.x + 52, d.y + 106, false, big.fw > 200 ? .5f : .9f);
        TroopText tt = troopText(k);
        DrawText(TextFormat("%s  Lv%d", KINFO[k].name, lv), int(d.x) + 104, int(d.y) + 8, 17, ink);
        DrawText(tt.role, int(d.x) + 104, int(d.y) + 28, 12, rgb(110, 80, 50));
        float w = 52, x0 = d.x + 100;
        auto nxt = [&](float cur, float next) { return lv < MAX_UNIT_LEVEL ? TextFormat("+%.0f", next - cur) : ""; };
        float hp = KINFO[k].hp * unitHpMul(lv), hpN = KINFO[k].hp * unitHpMul(lv + 1);
        float dps = KINFO[k].dps * unitDmgMul(lv), dpsN = KINFO[k].dps * unitDmgMul(lv + 1);
        float spd = KINFO[k].speed * unitSpeedMul(lv);
        statBar(x0, d.y + 48, w, hp / 300.f, rgb(220, 70, 60), "Health", TextFormat("%.0f", hp), nxt(hp, hpN));
        if (k == K_Monk) statBar(x0, d.y + 66, w, dps / 30.f, rgb(90, 200, 110), "Healing", TextFormat("%.0f/s", dps), "");
        else statBar(x0, d.y + 66, w, dps / 30.f, rgb(240, 160, 50), "Damage", TextFormat("%.0f/s", dps), nxt(dps, dpsN));
        statBar(x0, d.y + 84, w, KINFO[k].range / 8.f, rgb(110, 160, 230), "Range", TextFormat("%.1f", KINFO[k].range), "");
        statBar(x0, d.y + 102, w, spd / 5.f, rgb(170, 130, 230), "Speed", TextFormat("%.1f", spd), "");
        float ty = d.y + 126;
        for (auto& ln : wrapText(tt.desc, int(d.width) - 18, 12)) { DrawText(ln.c_str(), int(d.x) + 9, int(ty), 12, ink); ty += 14; }
        int bl = bin.aux;
        bool unlocked = bl >= unitUnlockLevel(k);
        Rectangle bt = trainBtnTrain();
        drawButton(bt, unlocked ? std::string("Train  ") + costText(KINFO[k].cost) : TextFormat("Locked: Archery Lv%d", unitUnlockLevel(k)), unlocked && affordable(KINFO[k].cost), false, CheckCollisionPointRec(m, bt), false, 13);
        Rectangle bu = trainBtnTroopUp();
        float uc[R_Count];
        troopCost(k, lv + 1, uc);
        bool maxed = lv >= MAX_UNIT_LEVEL, needB = !maxed && bl < troopUpgradeNeedsArchery(lv + 1);
        drawButton(bu, maxed ? "Max troop level" : (needB ? TextFormat("Upgrade: Archery Lv%d needed", troopUpgradeNeedsArchery(lv + 1)) : std::string("Upgrade to Lv") + std::to_string(lv + 1) + "  " + costText(uc)),
                   !maxed && !needB && affordable(uc), false, CheckCollisionPointRec(m, bu), false, 12);
        // building block: level, upgrade and a preview of the next level
        Rectangle bb = trainBlock();
        DrawText(TextFormat("Building health %d / %d", int(bin.hp), int(bin.maxhp)), int(bb.x) + 4, int(bb.y) + 4, 14, ink);
        float bc[R_Count];
        for (int r = 0; r < R_Count; r++) bc[r] = float(mapUpgradeCost(bin, r));
        Rectangle bub = trainBtnBuildingUp();
        drawButton(bub, bin.aux >= MAX_LEVEL ? "Max level reached" : std::string("Upgrade Archery to Lv") + std::to_string(bin.aux + 1) + "  " + costText(bc), bin.aux < MAX_LEVEL && affordable(bc), false, CheckCollisionPointRec(m, bub), false, 12);
        std::vector<std::string> info;
        std::vector<Kind> units;
        nextLevelInfo(bin, info, units);
        float iy = bub.y + bub.height + 6;
        if (!info.empty()) {
            DrawText("Next level:", int(bb.x) + 6, int(iy), 14, rgb(30, 90, 40));
            iy += 17;
            for (auto& t : info) { DrawText(t.c_str(), int(bb.x) + 12, int(iy), 13, ink); iy += 15; }
        }
        for (size_t u = 0; u < units.size(); u++) {                      // animated previews of troops the next level unlocks
            Anim a = unitAnim(units[u], me, US_Idle, 0, 0, false, 1, now);
            drawAnim(a, frameFor(a, now, float(u)), bb.x + bb.width - 60 - 70 * float(units.size() - 1 - u), bb.y + bb.height - 8, false, a.fw > 200 ? .4f : .65f);
        }
        drawQueueStrip(m);
        rlPopMatrix();
    }

    // ---------------------------------------------------------------- chat (global / clan / friends)
    static const char* chatName(int ch) { return ch == 0 ? "Global" : (ch == 1 ? "Clan" : "Friends"); }
    static Color chatColor(int ch) { return ch == 0 ? rgb(255, 255, 255) : (ch == 1 ? rgb(110, 230, 255) : rgb(150, 255, 160)); }
    Rectangle chatBar() { return {float(GetScreenWidth()) - 396, float(GetScreenHeight()) - 36, 388, 26}; }
    Rectangle chatTab(int i) { Rectangle b = chatBar(); return {b.x + float(i) * 64, b.y - 22, 60, 20}; }
    void chatUpdate(Vector2 m) {
        for (int ch = GetCharPressed(); ch > 0; ch = GetCharPressed()) if (ch >= 32 && ch < 127 && chatInput.size() < 150) chatInput.push_back(char(ch));
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !chatInput.empty()) chatInput.pop_back();
        if (IsKeyPressed(KEY_TAB)) chatCh = (chatCh + 1) % 3;
        if (IsKeyPressed(KEY_ESCAPE)) { chatOpen = false; return; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            if (!chatInput.empty()) cmdChat(chatCh, chatInput);
            chatInput.clear();
            chatOpen = false;
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) for (int i = 0; i < 3; i++) if (CheckCollisionPointRec(m, chatTab(i))) chatCh = i;
    }
    void drawChat() {
        double now = GetTime();
        Rectangle bar = chatBar();
        std::vector<const ChatLine*> vis;
        for (auto it = chatLines.rbegin(); it != chatLines.rend() && vis.size() < 8; ++it) if (chatOpen || now - it->t < 14) vis.push_back(&*it);
        float y = bar.y - 28 - 17.f * float(vis.size());
        if (!vis.empty()) DrawRectangleRounded({bar.x, y - 4, bar.width, 17.f * float(vis.size()) + 8}, .1f, 4, rgb(20, 14, 10, chatOpen ? 190 : 120));
        for (auto it = vis.rbegin(); it != vis.rend(); ++it) {
            const ChatLine& c = **it;
            std::string t = std::string("[") + chatName(c.ch)[0] + "] " + c.name + ": " + c.text;
            while (MeasureText(t.c_str(), 13) > int(bar.width) - 12 && t.size() > 4) t.pop_back();
            float a = chatOpen ? 1.f : std::clamp(float(14 - (now - c.t)) / 3.f, 0.f, 1.f);
            DrawText(t.c_str(), int(bar.x) + 6, int(y), 13, Color{chatColor(c.ch).r, chatColor(c.ch).g, chatColor(c.ch).b, (unsigned char)(255 * a)});
            y += 17;
        }
        if (!chatOpen) { DrawText("Enter: chat", int(bar.x) + 300, int(bar.y) + 8, 11, rgb(210, 214, 225, 170)); return; }
        Vector2 m = GetMousePosition();
        for (int i = 0; i < 3; i++) drawButton(chatTab(i), chatName(i), true, false, CheckCollisionPointRec(m, chatTab(i)), chatCh == i, 11);
        DrawRectangleRounded(bar, .3f, 6, rgb(255, 250, 235, 235));
        DrawRectangleRoundedLinesEx(bar, .3f, 6, 2, chatColor(chatCh).r > 200 ? rgb(60, 120, 200) : rgb(60, 140, 100));
        std::string shown = chatInput + (int(now * 2) % 2 == 0 ? "|" : "");
        while (MeasureText(shown.c_str(), 14) > int(bar.width) - 14 && shown.size() > 2) shown.erase(shown.begin());
        DrawText(shown.c_str(), int(bar.x) + 8, int(bar.y) + 6, 14, rgb(70, 45, 30));
        DrawText("Tab: channel   Enter: send   Esc: cancel", int(bar.x), int(bar.y) + 30, 10, rgb(210, 214, 225, 200));
    }

    // ---------------------------------------------------------------- UI drawing
    void drawResourceIcon(int r, float x, float y, float s) {
        if (r == R_Stone) { Texture2D t = tex("Terrain/Decorations/Rocks/Rock2.png"); DrawTexturePro(t, {0, 0, 64, 64}, {x - 4, y - 10, 64 * s, 64 * s}, {0, 0}, 0, WHITE); return; }
        static const char* icon[R_Count] = {"Icon_02", "Icon_03", "", "Icon_04"};
        Texture2D t = tex(std::string("UI Elements/UI Elements/Icons/") + icon[r] + ".png");
        DrawTexturePro(t, {0, 0, 64, 64}, {x, y, 64 * s, 64 * s}, {0, 0}, 0, WHITE);
    }
    void drawButton(Rectangle r, const std::string& label, bool enabled, bool red, bool hover, bool pressed, int fs = 14) {
        const TMap& m = red ? (pressed ? mBtnRP : mBtnR) : (pressed ? mBtnP : mBtn);
        drawTMap(m, r, enabled ? (hover ? rgb(255, 255, 255) : rgb(238, 238, 238)) : rgb(140, 140, 140, 220));
        int tw = MeasureText(label.c_str(), fs);
        while (tw > int(r.width) - 12 && fs > 9) { fs--; tw = MeasureText(label.c_str(), fs); }      // shrink long labels to fit
        DrawText(label.c_str(), int(r.x + r.width / 2) - tw / 2 + 1, int(r.y + r.height / 2) - fs / 2 + 1, fs, rgb(20, 20, 40, 180));
        DrawText(label.c_str(), int(r.x + r.width / 2) - tw / 2, int(r.y + r.height / 2) - fs / 2, fs, enabled ? WHITE : rgb(200, 200, 200));
    }

    void drawUI(float dt) {
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        Vector2 m = GetMousePosition();
        // resource ribbon
        Rectangle h = hudRect();
        drawTMap(mHud, h);
        float slot = (h.width - 2 * float(mHud.padding) - 60) / 4.f;
        for (int r = 0; r < R_Count; r++) {
            float x = h.x + float(mHud.padding) + 22 + float(r) * slot;
            drawResourceIcon(r, x, h.y + h.height * .5f - 14, .45f);
            textShadow(std::to_string(int(res[r])), int(x) + 30, int(h.y + h.height * .5f) - 8, 18, WHITE);
        }
        // name banner
        Rectangle nr = nameRect();
        drawTMap(mTitle, nr);
        int units = 0, bld = 0;
        reg.view<Info>().each([&](const Info& i) { if (i.owner == me) (isBuilding(i.kind) ? bld : units)++; });
        Texture2D av = tex(std::string("UI Elements/UI Elements/Human Avatars/Avatars_") + (me % 25 + 1 < 10 ? "0" : "") + std::to_string(me % 25 + 1) + ".png");
        DrawTexturePro(av, {0, 0, 256, 256}, {nr.x + 22, nr.y + 8, 44, 44}, {0, 0}, 0, WHITE);
        textShadow(myName, int(nr.x) + 76, int(nr.y) + 12, 18, teamColor(me));
        textShadow(TextFormat("Units %d/%d  Buildings %d  FPS %d", units, UNIT_CAP, bld, GetFPS()), int(nr.x) + 76, int(nr.y) + 34, 12, rgb(235, 235, 235));

        // build menu
        buildSlide += ((buildOpen ? 1.f : 0.f) - buildSlide) * std::min(1.f, 12.f * dt);
        Rectangle br = buildRect();
        Rectangle tab = buildTab();
        DrawRectangleRounded(tab, .3f, 6, rgb(70, 50, 34, 235));
        DrawRectangleRoundedLinesEx(tab, .3f, 6, 2, rgb(30, 20, 14));
        Texture2D hm = tex("UI Elements/UI Elements/Icons/Icon_01.png");
        DrawTexturePro(hm, {0, 0, 64, 64}, {tab.x + 4, tab.y + 8, 40, 40}, {0, 0}, 0, WHITE);
        if (buildSlide > .01f) {
            drawTMap(mBuild, br);
            textShadow("BUILD", int(br.x + br.width / 2) - 28, int(br.y) + mBuild.padding + 8, 20, rgb(255, 240, 200));
            for (int i = 0; i < 7; i++) {
                Kind k = buildList[i].k;
                Rectangle it = buildItem(i);
                bool aff = affordable(KINFO[k].cost), act = buildKind == int(k), hov = CheckCollisionPointRec(m, it) && buildSlide > .9f;
                DrawRectangleRounded(it, .2f, 5, act ? rgb(80, 150, 80, 210) : (hov ? rgb(255, 255, 255, 70) : rgb(0, 0, 0, 60)));
                BSpr s = buildingSprite(k, 1);
                if (s.file[0]) {
                    Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[colorIdx(me)] + " Buildings/" + s.file + ".png");
                    float sc = 38.f / s.artH * 1.f;
                    DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {it.x + 4, it.y + 3, s.artW * sc, s.artH * sc}, {0, 0}, 0, WHITE);
                } else if (k == K_Campfire) { Anim f = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62); drawAnim(f, frameFor(f, GetTime()), it.x + 24, it.y + 40, false, .8f); }
                else { Texture2D g = tex("Terrain/Resources/Gold/Gold Stones/Gold Stone 2.png"); DrawTexturePro(g, {0, 0, 128, 128}, {it.x + 2, it.y - 2, 48, 48}, {0, 0}, 0, WHITE); }
                textShadow(TextFormat("[%d] %s", i + 1, KINFO[k].name), int(it.x) + 54, int(it.y) + 6, 16, WHITE);
                std::string c = costText(KINFO[k].cost);
                DrawText(c.c_str(), int(it.x) + 54, int(it.y) + 26, 14, aff ? rgb(255, 225, 120) : rgb(255, 120, 110));
            }
            DrawText("Shift: keep placing", int(br.x) + mBuild.padding + 4, int(br.y + br.height) - mBuild.padding - 14, 12, rgb(255, 235, 200));
        }

        // log
        double now = GetTime();
        int ly = sh - 30;
        for (int i = int(logs.size()) - 1; i >= 0; i--) {
            double age = now - logs[i].t;
            if (age > 9) continue;
            textShadow(logs[i].text, 14, ly, 16, rgb(255, 255, 255, int(255 * std::min(1.0, (9 - age) / 2))));
            ly -= 20;
        }
        textShadow("LMB select/attack  RMB command/menu  M move  A attack  B build  P players  H home  Arrows/MMB pan  Wheel zoom", 14, sh - 14, 11, rgb(210, 214, 225, 200));
        if (mode != M_None) textShadow(mode == M_Move ? "MOVE: click a destination" : "ATTACK: click a target or ground (attack-move)", sw / 2 - 150, 16, 20, rgb(255, 230, 120));

        // minimap
        Rectangle mf = miniFrame(), mr = miniRect();
        drawTMap(mMini, mf);
        DrawRectangleRec(mr, rgb(18, 22, 30));
        if (mmTex.id) DrawTexturePro(mmTex, {0, 0, float(MM), float(MM)}, mr, {0, 0}, 0, WHITE);
        drawMapOverlay(mr, {std::floor(cam.target.x / TILE) + .5f, std::floor(cam.target.y / TILE) + .5f}, float(MM) / mr.width, true);
        DrawRectangleLinesEx(mr, 2, rgb(60, 40, 26));
        {
            Vector2 tl = GetScreenToWorld2D({0, 0}, cam), brr = GetScreenToWorld2D({float(::GetScreenWidth()), float(::GetScreenHeight())}, cam);
            float k = mr.width / float(MM) / TILE;
            Rectangle vr{mr.x + mr.width / 2 + (tl.x - cam.target.x) * k, mr.y + mr.height / 2 + (tl.y - cam.target.y) * k, (brr.x - tl.x) * k, (brr.y - tl.y) * k};
            float x0 = std::max(vr.x, mr.x), y0 = std::max(vr.y, mr.y), x1 = std::min(vr.x + vr.width, mr.x + mr.width), y1 = std::min(vr.y + vr.height, mr.y + mr.height);
            if (x1 > x0 && y1 > y0) DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, 1, WHITE);
        }

        // camera coordinates (so players can tell each other where they are)
        {
            Rectangle cr = coordRect();
            DrawRectangleRounded(cr, .3f, 6, rgb(40, 28, 20, 225));
            DrawRectangleRoundedLinesEx(cr, .3f, 6, 2, rgb(20, 12, 8));
            std::string cs = TextFormat("X %d    Y %d", int(std::floor(cam.target.x / TILE)), int(std::floor(cam.target.y / TILE)));
            DrawText(cs.c_str(), int(cr.x + cr.width / 2) - MeasureText(cs.c_str(), 16) / 2, int(cr.y) + 4, 16, rgb(255, 240, 200));
        }
        {   // social menu button under the build tab
            Rectangle t = socialTabBtn();
            DrawRectangleRounded(t, .3f, 6, rgb(70, 50, 34, 235));
            DrawRectangleRoundedLinesEx(t, .3f, 6, 2, rgb(30, 20, 14));
            DrawTexturePro(tex("UI Elements/UI Elements/Icons/Icon_06.png"), {0, 0, 64, 64}, {t.x + 4, t.y + 8, 40, 40}, {0, 0}, 0, WHITE);
        }

        drawSocialAndMap();
        if (trainOpen) drawTrain();
        drawChat();
        drawBaseUI();
        // context panel
        if (ctxOpen) {
            auto it = ents.find(ctxTarget);
            if (it != ents.end()) {
                const Info& in = reg.get<Info>(it->second);
                Rectangle cr = ctxRect();
                drawTMap(mCtx, cr);
                Rectangle tr{cr.x + cr.width / 2 - mTitle.size().x / 2, cr.y - mTitle.size().y * .55f, mTitle.size().x, mTitle.size().y};
                drawTMap(mTitle, tr);
                std::string title = KINFO[in.kind].name;
                textShadow(title, int(tr.x + tr.width / 2) - MeasureText(title.c_str(), 20) / 2, int(tr.y + tr.height / 2) - 12, 20, WHITE);
                float pad = float(mCtx.padding);
                float x = cr.x + pad + 8, y = ctxTopY();
                Color ink = rgb(70, 45, 30);
                for (auto& l : ctxLines) { DrawText(l.first.c_str(), int(x), int(y), 15, l.second); y += 19; }
                if (ctxSlider) {                                                   // auto-haul threshold
                    if (ctxQuota) DrawText(TextFormat("Defense quota: %d troops per attacked clan mate", ctxPct), int(x), int(y) + 2, 13, ink);
                    else DrawText(TextFormat("Auto-haul to Town Hall at %d%% full", ctxPct), int(x), int(y) + 2, 13, ink);
                    Rectangle sr = ctxSliderRect();
                    DrawRectangleRounded(sr, .5f, 6, rgb(60, 40, 28, 220));
                    float k = ctxQuota ? float(ctxPct) / float(MAX_QUOTA_UI) : (float(ctxPct) - 10.f) / 90.f;
                    DrawRectangleRounded({sr.x, sr.y, std::max(8.f, sr.width * k), sr.height}, .5f, 6, rgb(80, 170, 220));
                    DrawCircleV({sr.x + sr.width * k, sr.y + sr.height / 2}, 10, rgb(250, 235, 200));
                    DrawCircleLines(int(sr.x + sr.width * k), int(sr.y + sr.height / 2), 10, rgb(60, 40, 28));
                }
                for (size_t i = 0; i < ctx.size(); i++) {
                    Rectangle b = ctxButton(int(i));
                    drawButton(b, ctx[i].label, ctx[i].enabled, ctx[i].red, CheckCollisionPointRec(m, b), IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, b));
                }
                if (!ctxNext.empty()) {                                            // what the next level will change
                    float ny = ctxButton(int(ctx.size())).y + 4;
                    DrawText("Next level:", int(x), int(ny), 14, rgb(30, 90, 40));
                    ny += 17;
                    for (auto& t : ctxNext) { DrawText(t.c_str(), int(x) + 6, int(ny), 13, ink); ny += 16; }
                    float px = cr.x + cr.width - pad - 46 * float(ctxNextUnits.size());
                    for (size_t u = 0; u < ctxNextUnits.size(); u++) {
                        Anim a = unitAnim(ctxNextUnits[u], me, US_Idle, 0, 0, false, 1, GetTime());
                        float sc = a.fw > 200 ? .26f : .42f;
                        drawAnim(a, frameFor(a, GetTime(), float(u)), px + 46 * float(u) + 22, ny - 6, false, sc);
                    }
                }
            }
        }
        if (dragging) {
            Vector2 ds{dragStart.x / uiScale(), dragStart.y / uiScale()};
            Rectangle R{std::min(ds.x, m.x), std::min(ds.y, m.y), std::abs(m.x - ds.x), std::abs(m.y - ds.y)};
            if (R.width > 6 || R.height > 6) { DrawRectangleRec(R, rgb(255, 235, 90, 36)); DrawRectangleLinesEx(R, 1, rgb(255, 235, 90)); }
        }
        drawShipPanel();
        // hover label
        if (hovered && ents.count(hovered)) {
            const Info& in = reg.get<Info>(ents[hovered]);
            std::string s = std::string(KINFO[in.kind].name) + (in.owner ? " - " + nameOf(in.owner) : "") + shipTooltip(in);
            int w = MeasureText(s.c_str(), 14);
            DrawRectangleRounded({m.x + 16, m.y + 18, float(w) + 12, 22}, .3f, 4, rgb(30, 22, 16, 220));
            DrawText(s.c_str(), int(m.x) + 22, int(m.y) + 22, 14, WHITE);
        }
    }

    void drawCursor() {
        Vector2 m = GetMousePosition();
        const char* file = "Cursor_01.png";
        bool ui = overUI(m);
        if (hovered || ui) file = "Cursor_02.png";
        if (buildKind >= 0 && !ui) {
            IV t = placeTile(Kind(buildKind), GetScreenToWorld2D(::GetMousePosition(), cam));
            if (placeCheck(Kind(buildKind), t) != P_OK) file = "Cursor_03.png";
        }
        Texture2D c = tex(std::string("UI Elements/UI Elements/Cursors/") + file);
        DrawTexturePro(c, {0, 0, 64, 64}, {m.x - 22, m.y - 17, 64, 64}, {0, 0}, 0, WHITE);
    }

    void updateMinimap() {
        if (GetTime() - lastMM < .3) return;
        lastMM = GetTime();
        int cx = int(std::floor(cam.target.x / TILE)) - MM / 2, cy = int(std::floor(cam.target.y / TILE)) - MM / 2;
        for (int j = 0; j < MM; j++)
            for (int i = 0; i < MM; i++) {
                int x = cx + i, y = cy + j;
                mmPix[size_t(j * MM + i)] = exploredAt(x, y) ? biomeColor(biomeAt(x, y)) : rgb(16, 20, 30);
            }
        reg.view<Info, Goal>().each([&](const Info& in, const Goal& g) {
            int i = int(std::floor(g.x)) - cx, j = int(std::floor(g.y)) - cy;
            if (i < 1 || j < 1 || i >= MM - 1 || j >= MM - 1 || in.kind == K_Sheep) return;
            int sz = isBuilding(in.kind) ? 2 : 1;
            for (int a = 0; a < sz + 1; a++) for (int b = 0; b < sz + 1; b++) if (i + a < MM && j + b < MM) mmPix[size_t((j + b) * MM + i + a)] = teamColor(in.owner);
        });
        UpdateTexture(mmTex, mmPix.data());
    }

    // ---------------------------------------------------------------- shaders
    Shader loadShader(const char* name, bool& ok) {
        Shader s = LoadShader(nullptr, asset(std::string("shaders/") + name).c_str());
        ok = s.id > 0 && IsShaderValid(s);
        if (!ok) TraceLog(LOG_WARNING, "shader %s unavailable; falling back to plain rendering", name);
        return s;
    }
    void setU(Shader& s, const char* n, const void* v, int type, int count = 1) { int l = GetShaderLocation(s, n); if (l >= 0) SetShaderValueV(s, l, v, type, count); }

    void drawWater(float t) {
        float sw = float(::GetScreenWidth()), sh = float(::GetScreenHeight());
        Texture2D wt = tex("Terrain/Tileset/Water Background color.png");
        if (okWater) {
            float res2[2] = {sw, sh}, tgt[2] = {cam.target.x, cam.target.y}, z = cam.zoom;
            float rp[8][3];
            for (int i = 0; i < 8; i++) { rp[i][0] = rp[i][1] = 0; rp[i][2] = -1; }
            std::erase_if(ripples, [&](const Ripple& r) { return GetTime() - r.t > 2.2; });
            for (size_t i = 0; i < ripples.size() && i < 8; i++) { auto& r = ripples[ripples.size() - 1 - i]; rp[i][0] = r.x; rp[i][1] = r.y; rp[i][2] = float(GetTime() - r.t); }
            setU(shWater, "resolution", res2, SHADER_UNIFORM_VEC2);
            setU(shWater, "camTarget", tgt, SHADER_UNIFORM_VEC2);
            setU(shWater, "zoom", &z, SHADER_UNIFORM_FLOAT);
            setU(shWater, "time", &t, SHADER_UNIFORM_FLOAT);
            setU(shWater, "ripples", rp, SHADER_UNIFORM_VEC3, 8);
            BeginShaderMode(shWater);
        }
        DrawTexturePro(wt, {0, 0, 64, 64}, {0, 0, sw, sh}, {0, 0}, 0, WHITE);
        if (okWater) EndShaderMode();
    }

    // Bloom: bright-pass into a half-res target, then a separable gaussian (H then V).
    void runBloom() {
        if (!okBloom) return;
        float bw = float(bloomA.texture.width), bh = float(bloomA.texture.height);
        float th = .78f;
        setU(shBright, "threshold", &th, SHADER_UNIFORM_FLOAT);
        BeginTextureMode(bloomA);
        ClearBackground(BLACK);
        BeginShaderMode(shBright);
        DrawTexturePro(rt.texture, {0, 0, float(rt.texture.width), -float(rt.texture.height)}, {0, 0, bw, bh}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
        float dh[2] = {1.f / bw, 0}, dv[2] = {0, 1.f / bh};
        setU(shBlur, "direction", dh, SHADER_UNIFORM_VEC2);
        BeginTextureMode(bloomB);
        ClearBackground(BLACK);
        BeginShaderMode(shBlur);
        DrawTexturePro(bloomA.texture, {0, 0, bw, -bh}, {0, 0, bw, bh}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
        setU(shBlur, "direction", dv, SHADER_UNIFORM_VEC2);
        BeginTextureMode(bloomA);
        ClearBackground(BLACK);
        BeginShaderMode(shBlur);
        DrawTexturePro(bloomB.texture, {0, 0, bw, -bh}, {0, 0, bw, bh}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
    }

    void postUniforms(float t) {
        float sw = float(::GetScreenWidth()), sh = float(::GetScreenHeight());
        int ctx2 = int(std::floor(cam.target.x / TILE)), cty = int(std::floor(cam.target.y / TILE));
        bool snow = biomeAt(ctx2, cty) == B_Snow;
        bool nearFire = false;
        std::vector<float> lights;
        int lc = 0;
        reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {
            if (!isBuilding(i.kind) || i.prog < 100 || !alliedWith(i.owner)) return;
            bool fire = i.kind == K_Campfire;
            if (!fire && i.kind != K_TownHall && i.kind != K_Outpost) return;
            if (fire && std::hypot(g.x - cam.target.x / TILE, g.y - cam.target.y / TILE) < WARMTH_RADIUS) nearFire = true;
            Vector2 s = GetWorldToScreen2D({g.x * TILE, g.y * TILE - (fire ? 20.f : 60.f)}, cam);
            if (lc < 16 && s.x > -300 && s.y > -300 && s.x < sw + 300 && s.y < sh + 300) { lights.push_back(s.x); lights.push_back(s.y); lights.push_back((fire ? 190.f : 150.f) * cam.zoom); lc++; }
        });
        float dt = GetFrameTime();
        float targetCold = snow && !nearFire ? 1.f : 0.f, targetWarm = nearFire && snow ? 1.f : (nearFire ? .45f : 0.f);
        coldAmt += (targetCold - coldAmt) * std::min(1.f, 1.5f * dt);
        warmAmt += (targetWarm - warmAmt) * std::min(1.f, 2.f * dt);
        shakeAmt = std::max(0.f, shakeAmt - 28.f * dt);
        damageAmt = std::max(0.f, damageAmt - .6f * dt);
        double ph = std::fmod(double(std::time(nullptr)), 480.0) / 480.0;
        float daylight = std::clamp(.5f + .75f * std::sin(float(ph * 6.28319 - 1.5708)), 0.f, 1.f);
        float blizz = coldAmt * (.75f + .25f * std::sin(t * .7f));
        float res2[2] = {sw, sh};
        float lt[48] = {};
        for (size_t i = 0; i < lights.size(); i++) lt[i] = lights[i];
        setU(shPost, "resolution", res2, SHADER_UNIFORM_VEC2);
        setU(shPost, "time", &t, SHADER_UNIFORM_FLOAT);
        setU(shPost, "shake", &shakeAmt, SHADER_UNIFORM_FLOAT);
        setU(shPost, "damage", &damageAmt, SHADER_UNIFORM_FLOAT);
        setU(shPost, "daylight", &daylight, SHADER_UNIFORM_FLOAT);
        setU(shPost, "cold", &coldAmt, SHADER_UNIFORM_FLOAT);
        setU(shPost, "warmth", &warmAmt, SHADER_UNIFORM_FLOAT);
        setU(shPost, "blizzard", &blizz, SHADER_UNIFORM_FLOAT);
        setU(shPost, "lights", lt, SHADER_UNIFORM_VEC3, 16);
        setU(shPost, "lightCount", &lc, SHADER_UNIFORM_INT);
        float bloomK = okBloom ? .55f : 0.f, expo = 1.05f;
        setU(shPost, "bloomStrength", &bloomK, SHADER_UNIFORM_FLOAT);
        setU(shPost, "exposure", &expo, SHADER_UNIFORM_FLOAT);
        int bl = GetShaderLocation(shPost, "bloomTex");
        if (bl >= 0 && okBloom) SetShaderValueTexture(shPost, bl, bloomA.texture);
    }

    // ---------------------------------------------------------------- main loop
    void ensureTargets() {
        int w = ::GetScreenWidth(), h = ::GetScreenHeight();
        if (rt.id == 0 || rt.texture.width != w || rt.texture.height != h) {
            if (rt.id) UnloadRenderTexture(rt);
            rt = LoadRenderTexture(w, h);
            SetTextureFilter(rt.texture, TEXTURE_FILTER_POINT);
        }
        int bw = std::max(2, w / 2), bh = std::max(2, h / 2);
        if (bloomA.id == 0 || bloomA.texture.width != bw || bloomA.texture.height != bh) {
            if (bloomA.id) { UnloadRenderTexture(bloomA); UnloadRenderTexture(bloomB); }
            bloomA = LoadRenderTexture(bw, bh);
            bloomB = LoadRenderTexture(bw, bh);
            SetTextureFilter(bloomA.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(bloomB.texture, TEXTURE_FILTER_BILINEAR);
        }
    }
    void loadGfx() {
        shOutline = loadShader("outline.fs", okOutline);
        shWater = loadShader("water.fs", okWater);
        shDissolve = loadShader("dissolve.fs", okDissolve);
        if (okOutline) { const char* n[5] = {"uvRect", "texelSize", "outlineColor", "thickness", "time"}; for (int i = 0; i < 5; i++) locOut[i] = GetShaderLocation(shOutline, n[i]); }
        if (okDissolve) { const char* n[4] = {"uvRect", "texSize", "progress", "seed"}; for (int i = 0; i < 4; i++) locDis[i] = GetShaderLocation(shDissolve, n[i]); }
        shPost = loadShader("post.fs", okPost);
        bool b1 = false, b2 = false;
        shBright = loadShader("bloom_bright.fs", b1);
        shBlur = loadShader("bloom_blur.fs", b2);
        okBloom = b1 && b2 && okPost;
        mHud = loadTMap("hud_resources"); mTitle = loadTMap("title_ribbon"); mBuild = loadTMap("build_menu"); mMini = loadTMap("minimap_frame");
        mCtx = loadTMap("context_panel"); mRoster = loadTMap("roster_panel"); mSocial = loadTMap("social_menu"); mMapFrame = loadTMap("map_frame"); mTrain = loadTMap("training_panel"); mMenu = loadTMap("main_menu"); mPause = loadTMap("pause_menu");
        mBtn = loadTMap("button_blue"); mBtnP = loadTMap("button_blue_pressed"); mBtnR = loadTMap("button_red"); mBtnRP = loadTMap("button_red_pressed");
        Image im = GenImageColor(MM, MM, BLACK);
        mmTex = LoadTextureFromImage(im);
        UnloadImage(im);
        mmPix.assign(size_t(MM * MM), BLACK);
    }

    const char* shotPath = std::getenv("BW_SHOT");
    int shotFrames = 0, shotAfter = std::getenv("BW_SHOT_FRAMES") ? std::atoi(std::getenv("BW_SHOT_FRAMES")) : 400;

    void run(const std::string& autoJoin) {
        disp.sink<LogEvent>().connect<&onLog>();
        loadGfx();
        menuRefresh();
        if (!autoJoin.empty()) { ServerEntry e; e.name = autoJoin; e.address = autoJoin; e.local = true; joinServer(e); }
        while (!WindowShouldClose() && !wantQuit) {
            float dt = GetFrameTime();
            steam.run();
            pump();
            disp.update();
            ensureTargets();
            bool playing = screen == Screen::Playing && welcomed && !lost;
            if (playing) {
                input(dt);
                reg.view<RPos, Goal, Face, Info>().each([&](RPos& p, const Goal& g, Face& f, const Info& in) {
                    float k = std::min(1.f, 12.f * dt);
                    float dx = g.x - p.x;
                    p.x += dx * k;
                    p.y += (g.y - p.y) * k;
                    if ((in.state & 7) == US_Attack || (in.state & 7) == US_Heal) f.left = std::cos(float(in.aim) / 255.f * 6.28319f - 3.14159f) < 0;
                    else if (std::abs(dx) > .004f) f.left = dx < 0;
                });
                updateMinimap();
            } else menuUpdate();
            if (shotPath && welcomed && shotFrames == 25) if (const char* c = std::getenv("BW_CMD")) { std::string cm = c; if (cm.rfind("chat:", 0) == 0) { chatLines.push_back({0, "Tester", cm.substr(5), GetTime()}); chatLines.push_back({1, "Friend", "clan chat line", GetTime()}); }
                if (cm.rfind("clan:", 0) == 0) cmdClanCreate(cm.substr(5, cm.find(';') == std::string::npos ? std::string::npos : cm.find(';') - 5)); }
            if (shotPath && welcomed && (shotFrames == 300 || shotFrames == 360) && shotPath && welcomed) if (const char* c = std::getenv("BW_CMD")) { std::string cm = c; auto p = cm.find("xfer:"); if (p != std::string::npos) cmdTransfer(std::atoi(cm.c_str() + p + 5), R_Wood, 60.f); }
            if (shotPath && welcomed && shotFrames >= 20) {                 // test hooks (screenshots only)
                if (const char* mp = std::getenv("BW_MOUSE")) { float dx = 0, dy = 0; if (std::sscanf(mp, "%f,%f", &dx, &dy) == 2) SetMousePosition(::GetScreenWidth() / 2 + int(dx), ::GetScreenHeight() / 2 + int(dy)); }
                if (const char* u = std::getenv("BW_UI")) {
                    std::string ui = u;
                    if (ui.find("build") != std::string::npos) buildOpen = true;
                    if (ui.find("place") != std::string::npos) buildKind = K_Lumber;
                    if (ui.find("social") != std::string::npos && shotFrames == 20) { socialOpen = true; if (const char* tb = std::getenv("BW_TAB")) socialTab = std::atoi(tb); if (const char* se = std::getenv("BW_SEARCH")) searchText = se; }
                    if (ui.find("map") != std::string::npos && shotFrames == 20) { openBigMap(); if (const char* z = std::getenv("BW_MAPZ")) bmZoom = std::atoi(z); }
                    if (ui.find("pause") != std::string::npos) pauseOpen = true;
                    if (ui.find("train") != std::string::npos && !trainOpen)
                        reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (!trainOpen && i.owner == me && i.kind == K_Archery) { trainOpen = true; trainTarget = n.id; if (const char* ts = std::getenv("BW_TSEL")) trainSel = std::atoi(ts); } });
                    if (ui.find("ctxop") != std::string::npos && !ctxOpen)
                        reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (!ctxOpen && i.owner == me && i.kind == K_Outpost) openContext(n.id, {float(GetScreenWidth()) * .5f, float(GetScreenHeight()) * .15f}); });
                    if (ui.find("ctxfac") != std::string::npos && !ctxOpen)
                        reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (!ctxOpen && isFactionOwner(i.owner) && i.kind == K_TownHall) openContext(n.id, {float(GetScreenWidth()) * .55f, float(GetScreenHeight()) * .2f}); });
                    else if (ui.find("ctx") != std::string::npos && ui.find("ctxfac") == std::string::npos && !ctxOpen)
                        reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (i.owner == me && i.kind == K_TownHall) openContext(n.id, {float(GetScreenWidth()) * .6f, float(GetScreenHeight()) * .25f}); });
                }
            }
            float t = float(GetTime());
            if (playing) {
                BeginTextureMode(rt);
                ClearBackground(rgb(20, 22, 30));
                drawWater(t);
                BeginMode2D(cam);
                drawWorld(t);
                EndMode2D();
                EndTextureMode();
                runBloom();
            }
            BeginDrawing();
            ClearBackground(rgb(20, 22, 30));
            if (playing) {
                HideCursor();
                if (okPost) { postUniforms(t); BeginShaderMode(shPost); }
                DrawTextureRec(rt.texture, {0, 0, float(rt.texture.width), -float(rt.texture.height)}, {0, 0}, WHITE);
                if (okPost) EndShaderMode();
                beginUi();
                if (ctxOpen) rebuildContext();
                drawUI(dt);
                if (pauseOpen) drawPause();
                drawCursor();
                endUi();
                if (shotPath && ++shotFrames == shotAfter) {                 // test hook: BW_SHOT=<file.png> saves a frame and quits
                    rlDrawRenderBatchActive();                                 // flush batched UI draws before reading pixels
                    Image img = LoadImageFromScreen();
                    ExportImage(img, shotPath);
                    UnloadImage(img);
                    EndDrawing();
                    break;
                }
            } else {
                ShowCursor();
                drawMenuBackdrop(t);
                beginUi();
                drawMenu(t);
                endUi();
                if (shotPath && ++shotFrames == shotAfter) {
                    rlDrawRenderBatchActive();
                    Image img = LoadImageFromScreen();
                    ExportImage(img, shotPath);
                    UnloadImage(img);
                    EndDrawing();
                    break;
                }
            }
            EndDrawing();
        }
        settings.save(std::string(GetApplicationDirectory()));
        cnet.close();
        steam.shutdown();
    }
};

void loadConfig(std::string& ip, int& port, std::string& name) {
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
        else if (k == "name" || k == "player_name") name = v;
    }
}

}  // namespace

int main(int argc, char** argv) {
    App app;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    std::string dir = std::string(GetApplicationDirectory());
    loadConfig(app.ip, app.port, app.myName);
    app.settings.load(dir);
    if (app.myName.empty()) app.myName = app.settings.name;
    app.steam.init();
    if (app.myName.empty() && app.steam.ok) app.myName = app.steam.name;
    app.loadProfile();
    app.directAddr = app.settings.lastServer;
    if (const char* sc = std::getenv("BW_SCALE")) app.settings.uiScale = std::clamp(float(std::atof(sc)), 0.6f, 2.0f);       // test hook
    InitWindow(1280, 720, "Base Wars");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    app.applyWindowMode(app.settings.windowMode);
    findAssets();
    std::string autoJoin;                                              // "--connect host:port" joins immediately; screenshot runs join the configured server
    for (int i = 1; i + 1 < argc; i++) if (std::string(argv[i]) == "--connect") autoJoin = argv[i + 1];
    if (autoJoin.empty() && std::getenv("BW_SHOT") && !std::getenv("BW_MENU")) autoJoin = app.ip + ":" + std::to_string(app.port);
    app.run(autoJoin);
    CloseWindow();
}
