// Raylib client: Tiny Swords art, Tiled-driven UI panels, biome terrain with autotiled cliffs, shader effects,
// building placement, context panels, alliances and RTS controls.
#include "shared.hpp"

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

namespace fs = std::filesystem;
constexpr float TILE = 64.f;

Color rgb(int r, int g, int b, int a = 255) {
    return Color{(unsigned char)std::clamp(r, 0, 255), (unsigned char)std::clamp(g, 0, 255), (unsigned char)std::clamp(b, 0, 255), (unsigned char)std::clamp(a, 0, 255)};
}
const char* COLOR_NAME[TEAM_COLORS] = {"Blue", "Red", "Yellow", "Purple", "Black"};
Color teamColor(int owner) {
    static const Color c[TEAM_COLORS] = {rgb(70, 130, 235), rgb(225, 70, 70), rgb(240, 205, 60), rgb(165, 95, 205), rgb(95, 95, 110)};
    return owner < 1 ? rgb(235, 235, 235) : c[(owner - 1) % TEAM_COLORS];
}
int colorIdx(int owner) { return owner < 1 ? 0 : (owner - 1) % TEAM_COLORS; }
Color biomeColor(int b) {
    static const Color c[B_Count] = {rgb(46, 102, 172), rgb(140, 190, 80), rgb(50, 120, 58), rgb(122, 120, 128), rgb(214, 190, 110), rgb(205, 150, 205), rgb(235, 240, 248)};
    return b >= 0 && b < B_Count ? c[b] : rgb(20, 24, 34);
}
float smooth01(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3 - 2 * x); }

// ---------------------------------------------------------------- assets
std::string assetRoot;
void findAssets() {
    std::vector<fs::path> bases{fs::path(GetApplicationDirectory()), fs::current_path()};
    for (auto b : bases) {
        for (int up = 0; up < 6; up++) {
            fs::path c = b / "assets";
            if (fs::exists(c / "Units") && fs::exists(c / "Terrain")) { assetRoot = c.lexically_normal().string(); return; }
            if (!b.has_parent_path() || b.parent_path() == b) break;
            b = b.parent_path();
        }
    }
    assetRoot = "assets";
    TraceLog(LOG_ERROR, "assets directory not found next to the executable or in a parent folder");
}
std::string asset(const std::string& rel) { return assetRoot + "/" + rel; }

std::unordered_map<std::string, Texture2D> texCache;
Texture2D tex(const std::string& rel) {
    auto it = texCache.find(rel);
    if (it != texCache.end()) return it->second;
    Texture2D t = LoadTexture(asset(rel).c_str());
    if (t.id == 0) TraceLog(LOG_WARNING, "missing texture %s", rel.c_str());
    else SetTextureFilter(t, TEXTURE_FILTER_POINT);
    texCache[rel] = t;
    return t;
}
struct Anim { Texture2D t; int fw, fh, n; float fps, footX, footY; };
Anim mkAnim(const std::string& rel, int fw, int fh, float fps, float fx, float fy) {
    Texture2D t = tex(rel);
    return {t, fw, fh, std::max(1, t.width / fw), fps, fx, fy};
}
Rectangle frameRect(const Anim& a, int f) { return {float(f % a.n * a.fw), 0, float(a.fw), float(a.fh)}; }

// ---------------------------------------------------------------- tiny JSON (for Tiled .tmj)
struct J {
    enum T { Null, Num, Str, Arr, Obj } t = Null;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
    const J* get(const std::string& k) const { for (auto& p : o) if (p.first == k) return &p.second; return nullptr; }
};
struct JParser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;
    void ws() { while (i < s.size() && std::isspace(uint8_t(s[i]))) i++; }
    std::string str() {
        std::string r;
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) { i++; r += s[i] == 'n' ? '\n' : (s[i] == 't' ? '\t' : s[i]); }
            else r += s[i];
            i++;
        }
        i++;
        return r;
    }
    J parse() {
        J v;
        ws();
        if (i >= s.size()) { ok = false; return v; }
        char c = s[i];
        if (c == '{') {
            v.t = J::Obj; i++; ws();
            while (i < s.size() && s[i] != '}') {
                ws(); std::string k = str(); ws(); if (s[i] == ':') i++;
                v.o.push_back({k, parse()}); ws(); if (s[i] == ',') i++;
                ws();
            }
            i++;
        } else if (c == '[') {
            v.t = J::Arr; i++; ws();
            while (i < s.size() && s[i] != ']') { v.a.push_back(parse()); ws(); if (s[i] == ',') i++; ws(); }
            i++;
        } else if (c == '"') { v.t = J::Str; v.s = str(); }
        else if (s.compare(i, 4, "true") == 0) { v.t = J::Num; v.n = 1; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.t = J::Num; v.n = 0; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else { size_t e = i; while (e < s.size() && (std::isdigit(uint8_t(s[e])) || s[e] == '-' || s[e] == '+' || s[e] == '.' || s[e] == 'e' || s[e] == 'E')) e++; if (e == i) { ok = false; i++; return v; } v.t = J::Num; v.n = std::atof(s.substr(i, e - i).c_str()); i = e; }
        return v;
    }
};

// ---------------------------------------------------------------- Tiled maps used as UI backgrounds
struct TMap {
    bool ok = false;
    int w = 0, h = 0, tw = 64, th = 64, padding = 0;
    float scale = 1;
    std::vector<uint32_t> data;
    struct TS { int first, cols, tw, th; Texture2D t; };
    std::vector<TS> sets;
    Vector2 size() const { return {float(w * tw) * scale, float(h * th) * scale}; }
};
TMap loadTMap(const std::string& name) {
    TMap m;
    std::string path = asset("ui/" + name + ".tmj");
    std::ifstream f(path);
    if (!f) { TraceLog(LOG_WARNING, "missing UI map %s", path.c_str()); return m; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string txt = ss.str();
    JParser p{txt};
    J root = p.parse();
    if (!p.ok || root.t != J::Obj) { TraceLog(LOG_WARNING, "bad UI map %s", path.c_str()); return m; }
    auto num = [&](const J& j, const char* k, double d) { auto v = j.get(k); return v ? v->n : d; };
    m.w = int(num(root, "width", 0)); m.h = int(num(root, "height", 0));
    m.tw = int(num(root, "tilewidth", 64)); m.th = int(num(root, "tileheight", 64));
    if (auto props = root.get("properties"))
        for (auto& pr : props->a) {
            auto nm = pr.get("name"); auto v = pr.get("value");
            if (!nm || !v) continue;
            if (nm->s == "scale") m.scale = float(v->n);
            if (nm->s == "padding") m.padding = int(v->n);
        }
    if (auto ts = root.get("tilesets"))
        for (auto& t : ts->a) {
            auto img = t.get("image");
            if (!img) { TraceLog(LOG_WARNING, "%s: external tilesets are not supported; use 'Embed tileset' in Tiled", name.c_str()); continue; }
            std::string full = (fs::path(asset("ui")) / img->s).lexically_normal().string();
            Texture2D tx = LoadTexture(full.c_str());
            if (tx.id == 0) { TraceLog(LOG_WARNING, "missing tileset image %s", full.c_str()); continue; }
            SetTextureFilter(tx, TEXTURE_FILTER_POINT);
            m.sets.push_back({int(num(t, "firstgid", 1)), int(num(t, "columns", 1)), int(num(t, "tilewidth", 64)), int(num(t, "tileheight", 64)), tx});
        }
    if (auto ls = root.get("layers"))
        for (auto& l : ls->a) {
            auto ty = l.get("type"), d = l.get("data");
            if (!ty || ty->s != "tilelayer" || !d) continue;
            if (m.data.empty()) m.data.assign(size_t(m.w) * size_t(m.h), 0);
            for (size_t i = 0; i < d->a.size() && i < m.data.size(); i++) { uint32_t g = uint32_t(d->a[i].n) & 0x1FFFFFFFu; if (g) m.data[i] = g; }
        }
    std::sort(m.sets.begin(), m.sets.end(), [](const TMap::TS& a, const TMap::TS& b) { return a.first > b.first; });
    m.ok = m.w > 0 && m.h > 0 && !m.sets.empty() && !m.data.empty();
    TraceLog(LOG_INFO, "UI map %s: %dx%d tiles, scale %.2f, ok=%d", name.c_str(), m.w, m.h, m.scale, int(m.ok));
    return m;
}
// Draws the whole map stretched into `dst` (natural size when dst matches size()).
void drawTMap(const TMap& m, Rectangle dst, Color tint = WHITE) {
    if (!m.ok) { DrawRectangleRec(dst, rgb(60, 50, 40, 230)); return; }
    float cw = dst.width / float(m.w), ch = dst.height / float(m.h);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) {
            uint32_t g = m.data[size_t(y * m.w + x)];
            if (!g) continue;
            const TMap::TS* ts = nullptr;
            for (auto& s : m.sets) if (int(g) >= s.first) { ts = &s; break; }
            if (!ts) continue;
            int id = int(g) - ts->first;
            Rectangle src{float(id % ts->cols * ts->tw), float(id / ts->cols * ts->th), float(ts->tw), float(ts->th)};
            float x0 = std::floor(dst.x + float(x) * cw), x1 = std::floor(dst.x + float(x + 1) * cw);
            float y0 = std::floor(dst.y + float(y) * ch), y1 = std::floor(dst.y + float(y + 1) * ch);
            DrawTexturePro(ts->t, src, {x0, y0, x1 - x0, y1 - y0}, {0, 0}, 0, tint);
        }
}

// ---------------------------------------------------------------- client state types
struct NetId { uint32_t id; };
struct Info { Kind kind; uint8_t owner, state, aux, aim, prog; float hp, maxhp; uint16_t stock; };
struct RPos { float x, y; };
struct Goal { float x, y; };
struct Face { bool left = false; };
struct Seen { uint32_t stamp; };
struct LogEvent { std::string text; };
struct LogLine { std::string text; double t; };
struct Peer { int id; uint8_t flags; std::string name; };
struct Chunk { uint8_t biome[CH * CH]; uint8_t fog[32]; bool hasTerr = false, hasFog = false; };
struct Fx { uint8_t type; float x, y, x2, y2; double t; };
struct Ghost { Kind kind; uint8_t owner, level; float x, y; bool left; double t; };
struct DrawItem { float y; std::function<void()> fn; };

std::vector<LogLine> logs;
void onLog(const LogEvent& e) {
    logs.push_back({e.text, GetTime()});
    if (logs.size() > 7) logs.erase(logs.begin());
}

enum Feat : uint8_t { F_None, F_Pine, F_Oak, F_Bush, F_Rock, F_Gold };
uint8_t featureAt(int x, int y, int b, uint32_t seed) {
    uint32_t h = hash2(x, y, seed + 777) % 1000;
    switch (b) {
    case B_Forest: return h < 250 ? (hash2(x, y, 3) % 100 < 30 ? F_Oak : F_Pine) : (h < 290 ? F_Bush : (h < 315 ? F_Rock : F_None));
    case B_Plains: return h < 35 ? F_Oak : (h < 80 ? F_Bush : (h < 92 ? F_Rock : F_None));
    case B_Flowers: return h < 110 ? F_Bush : (h < 125 ? F_Oak : F_None);
    case B_Desert: return h < 30 ? F_Gold : (h < 65 ? F_Rock : F_None);
    case B_Highlands: return h < 110 ? F_Rock : (h < 122 ? F_Gold : F_None);
    case B_Snow: return h < 140 ? F_Pine : (h < 158 ? F_Rock : F_None);
    default: return F_None;
    }
}

// ---------------------------------------------------------------- the application
struct App {
    enum Mode { M_None, M_Move, M_Attack };
    struct MenuAct { std::string label; int act; uint32_t a, b; bool enabled; bool red; };
    struct BuildEntry { Kind k; };
    const BuildEntry buildList[7] = {{K_TownHall}, {K_Outpost}, {K_Harbour}, {K_Barracks}, {K_Mine}, {K_Lumber}, {K_Campfire}};

    // network
    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;
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
    bool dragging = false, buildOpen = false, rosterOpen = true, ctxOpen = false;
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

    void send(const Writer& w) { if (peer && connected) sendPacket(peer, w); }
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
    bool alliedWith(int owner) const { if (owner == me) return true; auto p = peerOf(owner); return p && (p->flags & 2); }
    bool hostileTo(int owner) const { return owner != me && !alliedWith(owner); }
    std::string nameOf(int owner) const { if (owner == me) return myName; auto p = peerOf(owner); return p ? p->name : (owner ? "Player " + std::to_string(owner) : "Wildlife"); }

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
        switch (r.get<uint8_t>()) {
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
            break;
        }
        case S_State: {
            if (!welcomed) break;
            for (float& v : res) v = r.get<float>();
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
                Peer p;
                p.id = r.get<uint8_t>();
                p.flags = r.get<uint8_t>();
                p.name = r.str();
                if (!r.ok) return;
                v.push_back(p);
            }
            roster = v;
            break;
        }
        default: break;
        }
    }
    void pump() {
        ENetEvent ev;
        while (enet_host_service(host, &ev, 0) > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                connected = true;
                if (!helloSent) { Writer w; w.put<uint8_t>(C_Hello); w.str(uuid); w.str(myName); send(w); helloSent = true; }
                break;
            case ENET_EVENT_TYPE_RECEIVE: onPacket(ev.packet->data, ev.packet->dataLength); enet_packet_destroy(ev.packet); break;
            case ENET_EVENT_TYPE_DISCONNECT: connected = false; lost = true; break;
            default: break;
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
                if (i.kind == K_TownHall || i.kind == K_Outpost) f(Pos{g.x, g.y}, i.kind, i.owner == a.me, i.owner != a.me && a.alliedWith(i.owner));
            });
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
    Rectangle rosterRect() { Vector2 s = mRoster.size(); return {float(GetScreenWidth()) - s.x - 8, float(GetScreenHeight()) - s.y - 8, s.x, s.y}; }
    Rectangle ctxRect() {
        Vector2 s = mCtx.size();
        return {std::clamp(ctxPos.x, 8.f, float(GetScreenWidth()) - s.x - 8), std::clamp(ctxPos.y, 56.f, float(GetScreenHeight()) - s.y - 8), s.x, s.y};
    }
    bool overUI(Vector2 m) {
        if (CheckCollisionPointRec(m, hudRect()) || CheckCollisionPointRec(m, nameRect()) || CheckCollisionPointRec(m, miniFrame())) return true;
        if (CheckCollisionPointRec(m, buildTab()) || (buildSlide > .02f && CheckCollisionPointRec(m, buildRect()))) return true;
        if (rosterOpen && CheckCollisionPointRec(m, rosterRect())) return true;
        if (ctxOpen && CheckCollisionPointRec(m, ctxRect())) return true;
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
    void cmdBuild(Kind k, IV t) { Writer w; w.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(t.x).put<int32_t>(t.y); send(w); }

    void selectBuildKind(int i) {
        Kind k = buildList[i].k;
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
            if (isBuilding(i.kind)) return;
            float cy = p.y * TILE - (i.kind == K_Sheep ? 14.f : 28.f), cx = p.x * TILE;
            float d = std::hypot(w.x - cx, w.y - cy);
            if (d < 34.f && d < bd) { bd = d; best = n.id; }
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
                if (i.owner != me || isBuilding(i.kind)) return;
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
    void rebuildContext() {
        ctx.clear();
        auto it = ents.find(ctxTarget);
        if (it == ents.end()) { ctxOpen = false; return; }
        const Info& in = reg.get<Info>(it->second);
        bool mine = in.owner == me, done = in.prog >= 100;
        if (mine && done) {
            Kind k = in.kind;
            for (int u = K_Pawn; u < K_Count; u++)
                if (canTrain(k, Kind(u))) ctx.push_back({std::string(k == K_Harbour ? "Build " : "Train ") + KINFO[u].name + "  " + costText(KINFO[u].cost), 1, ctxTarget, uint32_t(u), affordable(KINFO[u].cost), false});
            if (k != K_Campfire) {
                float c[R_Count];
                for (int r = 0; r < R_Count; r++) c[r] = float(mapUpgradeCost(in, r));
                ctx.push_back({in.aux >= MAX_LEVEL ? "Max level reached" : "Upgrade to Lv" + std::to_string(in.aux + 1) + "  " + costText(c), 2, ctxTarget, 0, in.aux < MAX_LEVEL && affordable(c), false});
            }
        } else if (!mine) {
            ctx.push_back({"Attack this target", 3, ctxTarget, 0, true, true});
            if (in.owner > 0) {
                auto p = peerOf(in.owner);
                uint8_t fl = p ? p->flags : 0;
                if (fl & 2) ctx.push_back({"Break alliance", 5, uint32_t(in.owner), 0, true, true});
                else if (fl & 4) ctx.push_back({"Accept alliance", 4, uint32_t(in.owner), 0, true, false});
                else if (fl & 8) ctx.push_back({"Alliance invite pending", 4, uint32_t(in.owner), 0, false, false});
                else ctx.push_back({"Invite to alliance", 4, uint32_t(in.owner), 0, true, false});
            }
        }
        ctx.push_back({"Close", 0, 0, 0, true, false});
    }
    void runContext(const MenuAct& a) {
        switch (a.act) {
        case 1: cmdTrain(a.a, Kind(a.b)); break;
        case 2: cmdUpgrade(a.a); break;
        case 3: cmdAttack(a.a); break;
        case 4: cmdAlly(int(a.a), AA_Invite); break;
        case 5: cmdAlly(int(a.a), AA_Break); break;
        default: ctxOpen = false; break;
        }
    }
    Rectangle ctxButton(int i) {
        Rectangle r = ctxRect();
        float pad = float(mCtx.padding);
        return {r.x + pad + 6, r.y + r.height - pad - 8 - 34.f * float(int(ctx.size()) - i), r.width - 2 * pad - 12, 30};
    }

    // ---------------------------------------------------------------- input
    void input(float dt) {
        Vector2 m = GetMousePosition();
        float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
        cam.offset = {sw * .5f, sh * .5f};
        bool ui = overUI(m);

        float sp = 800.f * dt / cam.zoom;
        if (IsKeyDown(KEY_UP)) cam.target.y -= sp;
        if (IsKeyDown(KEY_DOWN)) cam.target.y += sp;
        if (IsKeyDown(KEY_LEFT)) cam.target.x -= sp;
        if (IsKeyDown(KEY_RIGHT)) cam.target.x += sp;
        if (IsCursorOnScreen() && !dragging && !ui) {                       // edge scrolling
            if (m.x < 3) cam.target.x -= sp;
            if (m.x > sw - 4) cam.target.x += sp;
            if (m.y < 3) cam.target.y -= sp;
            if (m.y > sh - 4) cam.target.y += sp;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) { Vector2 d = GetMouseDelta(); cam.target.x -= d.x / cam.zoom; cam.target.y -= d.y / cam.zoom; }
        float wh = GetMouseWheelMove();
        if (wh != 0 && !ui) {
            Vector2 b = GetScreenToWorld2D(m, cam);
            cam.zoom = std::clamp(cam.zoom * (1 + wh * .1f), .2f, 2.2f);
            Vector2 a = GetScreenToWorld2D(m, cam);
            cam.target.x += b.x - a.x;
            cam.target.y += b.y - a.y;
        }
        if (IsKeyPressed(KEY_H)) { Pos t = myTH(); cam.target = {t.x * TILE, t.y * TILE}; }
        if (IsKeyPressed(KEY_B)) buildOpen = !buildOpen;
        if (IsKeyPressed(KEY_P)) rosterOpen = !rosterOpen;
        if (IsKeyPressed(KEY_M)) { mode = mode == M_Move ? M_None : M_Move; buildKind = -1; }
        if (IsKeyPressed(KEY_A)) { mode = mode == M_Attack ? M_None : M_Attack; buildKind = -1; }
        if (IsKeyPressed(KEY_ESCAPE)) { buildKind = -1; mode = M_None; ctxOpen = false; sel.clear(); }
        if (buildOpen) for (int i = 0; i < 7; i++) if (IsKeyPressed(KEY_ONE + i)) { selectBuildKind(i); }
        if (GetTime() - lastCam > 1.5 && connected) { lastCam = GetTime(); Writer w; w.put<uint8_t>(C_Cam).put<float>(cam.target.x / TILE).put<float>(cam.target.y / TILE); send(w); }

        hovered = ui || dragging ? 0 : pick(m);
        bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), lr = IsMouseButtonReleased(MOUSE_BUTTON_LEFT), rp = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        if (dragging && lr) { dragging = false; doSelect(dragStart, m, shift); return; }
        if (ctxOpen) rebuildContext();
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
            if (rosterOpen && CheckCollisionPointRec(m, rosterRect())) {
                Rectangle rr = rosterRect();
                float pad = float(mRoster.padding);
                int row = 0;
                for (auto& p : roster) {
                    if (p.id == me) continue;
                    Rectangle b{rr.x + rr.width - pad - 66, rr.y + pad + 26 + float(row) * 30.f, 62, 24};
                    if (CheckCollisionPointRec(m, b)) {
                        if (p.flags & 2) cmdAlly(p.id, AA_Break); else if (!(p.flags & 8)) cmdAlly(p.id, AA_Invite);
                    }
                    row++;
                }
                return;
            }
            if (CheckCollisionPointRec(m, miniRect())) { miniClick(m); return; }
            if (ui) return;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, miniRect()) && !dragging) { miniClick(m); return; }
        if (ui) { if (rp) ctxOpen = false; return; }

        Vector2 w = GetScreenToWorld2D(m, cam);
        if (lp) {
            if (buildKind >= 0) {
                Kind k = Kind(buildKind);
                IV t = placeTile(k, w);
                PW pw{*this};
                int err = checkPlace(pw, k, t.x, t.y);
                if (err) msg(std::string("Cannot build here: ") + PLACE_TEXT[err] + ".");
                else if (!affordable(KINFO[k].cost)) msg("Not enough resources" + std::string(roster.empty() ? "." : " (allies may cover the rest)."));
                if (!err) cmdBuild(k, t);
                if (!shift) buildKind = -1;
            } else if (mode == M_Move) {
                cmdMove(w);
                if (!shift) mode = M_None;
            } else if (mode == M_Attack) {
                if (hovered && hostileTargetId(hovered)) cmdAttack(hovered); else cmdAttackMove(w);
                if (!shift) mode = M_None;
            } else if (hovered && hostileTargetId(hovered) && armyCountSel() > 0) {
                cmdAttack(hovered);
            } else { dragging = true; dragStart = m; if (!shift) { sel.clear(); } }
        }
        if (rp) {
            if (buildKind >= 0 || mode != M_None) { buildKind = -1; mode = M_None; return; }
            uint32_t id = pick(m);
            auto it = id ? ents.find(id) : ents.end();
            if (it != ents.end() && isBuilding(reg.get<Info>(it->second).kind)) { openContext(id, m); return; }
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
    void miniClick(Vector2 m) {
        Rectangle r = miniRect();
        float fx = (m.x - r.x) / r.width, fy = (m.y - r.y) / r.height;
        cam.target = {cam.target.x + (fx - .5f) * float(MM) * TILE, cam.target.y + (fy - .5f) * float(MM) * TILE};
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
        SetShaderValue(sh, GetShaderLocation(sh, "uvRect"), uv, SHADER_UNIFORM_VEC4);
        if (mode == 1) {
            SetShaderValue(sh, GetShaderLocation(sh, "texelSize"), ts, SHADER_UNIFORM_VEC2);
            SetShaderValue(sh, GetShaderLocation(sh, "outlineColor"), col, SHADER_UNIFORM_VEC4);
            SetShaderValue(sh, GetShaderLocation(sh, "thickness"), &th, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, GetShaderLocation(sh, "time"), &tm, SHADER_UNIFORM_FLOAT);
        } else {
            SetShaderValue(sh, GetShaderLocation(sh, "texSize"), tsz, SHADER_UNIFORM_VEC2);
            SetShaderValue(sh, GetShaderLocation(sh, "progress"), &prog, SHADER_UNIFORM_FLOAT);
            SetShaderValue(sh, GetShaderLocation(sh, "seed"), &sd, SHADER_UNIFORM_FLOAT);
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
        case K_Barracks: return {"Barracks", .66f, 245, 256, 192};
        case K_Harbour: return {"House3", 1.f, 172, 192, 128};
        case K_Lumber: return level >= 3 ? BSpr{"House3", .9f, 172, 192, 128} : (level == 2 ? BSpr{"House2", .9f, 178, 192, 128} : BSpr{"House1", .9f, 173, 192, 128});
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
            Color tint = prog < 1.f ? rgb(255, 255, 255, 190) : WHITE;
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
                DrawEllipse(int(cx), int(bottom - 34), w * .42f, 40, rgb(36, 30, 30));
                DrawRectangle(int(cx - w * .42f - 4), int(bottom - 70), 8, 70, rgb(120, 82, 46));
                DrawRectangle(int(cx + w * .42f - 4), int(bottom - 70), 8, 70, rgb(120, 82, 46));
                DrawRectangle(int(cx - w * .42f - 8), int(bottom - 78), int(w * .84f + 16), 10, rgb(146, 100, 58));
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
        float hpFrac = i.hp / std::max(1.f, i.maxhp);
        if (prog >= 1.f && isBuilding(i.kind) && i.kind != K_Campfire) {                // damage: smoke and flames
            if (hpFrac < .6f) items.push_back({sortY + 2.f, [=, this]() { Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60); drawAnim(d, frameFor(d, t, float(id % 7)), cx - 20, bottom - 70, false, 1.8f, rgb(120, 120, 130, 210)); }});
            if (hpFrac < .3f) items.push_back({sortY + 3.f, [=, this]() { Anim f = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62); drawAnim(f, frameFor(f, t, float(id % 5)), cx + 14, bottom - 46, false, 1.4f); drawAnim(f, frameFor(f, t, float(id % 3) + 4), cx - 22, bottom - 40, false, 1.1f); }});
        }
        if (i.kind == K_Campfire && prog >= 1.f)
            items.push_back({sortY + 2.f, [=, this]() { Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 6, 32, 60); drawAnim(d, frameFor(d, t, float(id % 3)), cx + 6, bottom - 60, false, 1.2f, rgb(200, 200, 205, 120)); }});
        if (i.kind == K_TownHall)
            items.push_back({sortY + 4.f, [=, this]() { textShadow(nameOf(i.owner), int(cx) - MeasureText(nameOf(i.owner).c_str(), 16) / 2, int(bottom) - 150, 16, teamColor(i.owner)); }});
        items.push_back({sortY + 5.f, [=, this]() {
            Rectangle fr{float(tx) * TILE, float(ty) * TILE - 40, float(ki.fw) * TILE, float(ki.fh) * TILE + 40};
            if (selected) brackets(fr, rgb(255, 235, 90));
            if (prog < 1.f) drawBar({cx, bottom + 4}, 70, prog, rgb(90, 160, 255));
            else if (hpFrac < .995f || selected || hover) drawBar({cx, bottom + 4}, 76, hpFrac, hpFrac > .5f ? rgb(90, 220, 90) : (hpFrac > .25f ? rgb(240, 190, 50) : rgb(235, 70, 60)));
            if (i.kind == K_Campfire && (hover || selected)) DrawCircleLinesV({cx, bottom - 16}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));
        }});
    }

    void drawShip(float px, float py, int owner, bool flip, bool hover, double t, uint32_t id) {
        float bob = std::sin(float(t) * 2.f + float(id)) * 2.5f;
        py += bob;
        float d = flip ? -1.f : 1.f;
        DrawEllipse(int(px), int(py + 6), 46, 12, rgb(0, 40, 80, 70));
        Vector2 a{px - 44 * d, py - 10}, b{px + 44 * d, py - 10}, c2{px + 30 * d, py + 8}, e{px - 30 * d, py + 8};
        DrawTriangle(a, e, c2, rgb(122, 84, 48));
        DrawTriangle(a, c2, b, rgb(138, 96, 56));
        DrawLineEx(a, b, 3, rgb(88, 58, 32));
        DrawLineEx(a, e, 2, rgb(70, 44, 26));
        DrawLineEx(e, c2, 2, rgb(70, 44, 26));
        DrawLineEx(c2, b, 2, rgb(70, 44, 26));
        DrawRectangle(int(px - 2), int(py - 64), 4, 56, rgb(96, 66, 38));
        Vector2 s1{px + 2, py - 62}, s2{px + 2 + 34 * d, py - 30}, s3{px + 2, py - 20};
        if (d > 0) DrawTriangle(s1, s3, s2, rgb(240, 236, 220)); else DrawTriangle(s1, s2, s3, rgb(240, 236, 220));
        Vector2 f1{px - 2, py - 62}, f2{px - 2 - 18 * d, py - 56}, f3{px - 2, py - 50};
        if (d > 0) DrawTriangle(f1, f3, f2, teamColor(owner)); else DrawTriangle(f1, f2, f3, teamColor(owner));
        Anim w = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/Warrior/Warrior_Idle.png", 192, 192, 7, 96, 135);
        drawAnim(w, frameFor(w, t, float(id % 5)), px - 14 * d, py - 8, flip, .55f, WHITE, hover ? 1 : 0, rgb(255, 255, 255));
        if (hover) DrawEllipseLines(int(px), int(py - 4), 50, 16, WHITE);
    }

    void drawUnit(const RPos& p, Info& i, Face& fc, uint32_t id, bool selected, bool hover, std::vector<DrawItem>& items, double t, int biome) {
        float px = p.x * TILE, py = p.y * TILE;
        bool moving = i.state == US_Move;
        Color oc = i.owner == me || alliedWith(i.owner) ? rgb(255, 255, 255) : rgb(255, 90, 90);
        if (i.kind == K_Sheep) oc = rgb(255, 235, 140);
        items.push_back({py, [=, this]() {
            if (i.kind == K_Ship) { drawShip(px, py, i.owner, fc.left, hover, t, id); return; }
            Anim a = unitAnim(i.kind, i.owner, i.state, i.aux, i.aim, moving, biome, t);
            DrawEllipse(int(px), int(py + 2), 16, 6, rgb(0, 0, 0, 60));
            if (selected) DrawEllipseLines(int(px), int(py + 2), 24, 10, rgb(255, 235, 90));
            drawAnim(a, frameFor(a, t, float(id % 7)), px, py + 6, fc.left, 1.f, WHITE, hover ? 1 : 0, oc);
            if (i.kind == K_Monk && i.state == US_Heal) { Anim h = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(i.owner)] + " Units/Monk/Heal_Effect.png", 192, 192, 10, 96, 135); drawAnim(h, frameFor(h, t), px, py + 6, false, 1.f); }
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
                if (!f) continue;
                bool blocked = false;
                for (int dy = -1; dy <= 1 && !blocked; dy++) for (int dx = -1; dx <= 1; dx++) if (occ.count(chunkKey(x + dx, y + dy))) { blocked = true; break; }
                if (blocked) continue;
                uint32_t h = hash2(x, y, seed + 31);
                float cx = (float(x) + .5f) * TILE + float(int(h & 15) - 8), by = (float(y) + 1.f) * TILE - 6 - float((h >> 4) & 7);
                items.push_back({by, [=, this]() {
                    switch (f) {
                    case F_Pine: { Anim a = mkAnim(std::string("Terrain/Resources/Wood/Trees/Tree") + (h & 1 ? "1" : "2") + ".png", 192, 256, 6, 96, 238); drawAnim(a, frameFor(a, t, float(h % 8)), cx, by, false, 1.f); break; }
                    case F_Oak: { Anim a = mkAnim(std::string("Terrain/Resources/Wood/Trees/Tree") + (h & 1 ? "3" : "4") + ".png", 192, 192, 6, 96, 168); drawAnim(a, frameFor(a, t, float(h % 8)), cx, by, false, 1.f); break; }
                    case F_Bush: { Anim a = mkAnim(std::string("Terrain/Decorations/Bushes/Bushe") + char('1' + h % 4) + ".png", 128, 128, 5, 64, 78); drawAnim(a, frameFor(a, t, float(h % 8)), cx, by, false, 1.f); break; }
                    case F_Rock: { Texture2D r = tex(std::string("Terrain/Decorations/Rocks/Rock") + char('1' + h % 4) + ".png"); DrawTexturePro(r, {0, 0, 64, 64}, {cx - 32, by - 50, 64, 64}, {0, 0}, 0, WHITE); break; }
                    case F_Gold: { Anim a = mkAnim(std::string("Terrain/Resources/Gold/Gold Stones/Gold Stone ") + char('1' + h % 6) + "_Highlight.png", 128, 128, 6, 64, 78); drawAnim(a, frameFor(a, t, float(h % 6)), cx, by, false, 1.f); break; }
                    default: break;
                    }
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
            default: break;
            }
        }
        std::erase_if(fxs, [&](const Fx& f) { return t - f.t > (f.type == E_Explosion ? 1.6 : 1.0); });
    }

    void drawWorld(float t) {
        Vector2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(GetScreenWidth()), float(GetScreenHeight())}, cam);
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
        drawFeatures(x0, y0 - 0, x1, y1 + 4, bio, wx0, wy0, bw, items, t);
        for (auto& mk : markers) { Marker m = mk; items.push_back({-1e9f, [=]() { float a = float(GetTime() - m.t); DrawCircleLines(int(m.x), int(m.y), 6 + a * 40, rgb(255, 255, 255, int(255 * std::max(0.f, 1 - a * 2)))); }}); }
        std::erase_if(markers, [](const Marker& m) { return GetTime() - m.t > .5; });
        Rectangle vr{tl.x - 400, tl.y - 400, br.x - tl.x + 800, br.y - tl.y + 800};
        reg.view<RPos, Info, NetId, Face>().each([&](const RPos& p, Info& i, const NetId& n, Face& fc) {
            if (!CheckCollisionPointRec({p.x * TILE, p.y * TILE}, vr)) return;
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

        // building ghost (placement preview)
        if (buildKind >= 0 && !overUI(GetMousePosition())) {
            Kind k = Kind(buildKind);
            Vector2 w = GetScreenToWorld2D(GetMousePosition(), cam);
            IV tt = placeTile(k, w);
            PW pw{*this};
            int err = checkPlace(pw, k, tt.x, tt.y);
            bool ok = err == P_OK && affordable(KINFO[k].cost);
            items.push_back({1e9f, [=, this]() {
                reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {          // territory squares of own bases
                    if (i.owner != me || (i.kind != K_TownHall && i.kind != K_Outpost)) return;
                    float r = float(anchorRadius(i.kind));
                    DrawRectangleRec({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, rgb(90, 200, 255, 22));
                    DrawRectangleLinesEx({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, 2, rgb(120, 220, 255, 150));
                });
                for (int y = tt.y; y < tt.y + KINFO[k].fh; y++) for (int x = tt.x; x < tt.x + KINFO[k].fw; x++) {
                    PW p2{*this};
                    bool tileOk = p2.explored(x, y) && isLand(p2.biome(x, y)) && !isCliffTile(p2.biome(x, y), p2.biome(x, y - 1)) && !p2.occupied(x, y);
                    DrawRectangle(x * 64, y * 64, 64, 64, (ok && tileOk) ? rgb(80, 255, 90, 110) : (tileOk && err != P_OK ? rgb(255, 190, 60, 110) : rgb(255, 60, 60, 120)));
                    DrawRectangleLines(x * 64, y * 64, 64, 64, rgb(255, 255, 255, 140));
                }
                float cx = (float(tt.x) + float(KINFO[k].fw) * .5f) * TILE, bottom = float(tt.y + KINFO[k].fh) * TILE;
                BSpr s = buildingSprite(k, 1);
                if (s.file[0]) {
                    Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[colorIdx(me)] + " Buildings/" + s.file + ".png");
                    float dw = s.artW * s.scale, dh = s.artH * s.scale;
                    DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, {0, 0}, 0, rgb(255, 255, 255, 150));
                }
                std::string label = err ? PLACE_TEXT[err] : (ok ? costText(KINFO[k].cost) : "Not enough resources");
                textShadow(label, int(cx) - MeasureText(label.c_str(), 18) / 2, int(bottom) + 10, 18, ok ? rgb(160, 255, 160) : rgb(255, 150, 130));
                if (k == K_Campfire) DrawCircleLinesV({cx, bottom - 30}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));
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
        DrawRectangleLinesEx(mr, 2, rgb(60, 40, 26));
        {
            Vector2 tl = GetScreenToWorld2D({0, 0}, cam), brr = GetScreenToWorld2D({float(sw), float(sh)}, cam);
            float k = mr.width / float(MM) / TILE;
            Rectangle vr{mr.x + mr.width / 2 + (tl.x - cam.target.x) * k, mr.y + mr.height / 2 + (tl.y - cam.target.y) * k, (brr.x - tl.x) * k, (brr.y - tl.y) * k};
            float x0 = std::max(vr.x, mr.x), y0 = std::max(vr.y, mr.y), x1 = std::min(vr.x + vr.width, mr.x + mr.width), y1 = std::min(vr.y + vr.height, mr.y + mr.height);
            if (x1 > x0 && y1 > y0) DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, 1, WHITE);
        }

        // roster / alliances
        if (rosterOpen) {
            Rectangle rr = rosterRect();
            drawTMap(mRoster, rr);
            float pad = float(mRoster.padding);
            DrawText("PLAYERS [P]", int(rr.x + pad + 6), int(rr.y + pad + 4), 14, rgb(70, 45, 30));
            int row = 0;
            for (auto& p : roster) {
                float y = rr.y + pad + 26 + float(row) * 30.f;
                if (row >= 6) break;
                DrawCircle(int(rr.x + pad + 14), int(y + 12), 6, p.flags & 1 ? rgb(70, 200, 90) : rgb(130, 130, 130));
                DrawCircleLines(int(rr.x + pad + 14), int(y + 12), 6, BLACK);
                DrawText(p.name.c_str(), int(rr.x + pad + 26), int(y + 5), 14, p.id == me ? teamColor(p.id) : rgb(60, 40, 28));
                if (p.id != me) {
                    Rectangle b{rr.x + rr.width - pad - 66, y, 62, 24};
                    std::string lb = p.flags & 2 ? "Break" : ((p.flags & 4) ? "Accept" : ((p.flags & 8) ? "Sent" : "Ally"));
                    drawButton(b, lb, !(p.flags & 8) || (p.flags & 2), p.flags & 2, CheckCollisionPointRec(m, b), false, 12);
                } else DrawText("(you)", int(rr.x + rr.width - pad - 50), int(y + 6), 12, rgb(110, 80, 60));
                row++;
            }
        }

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
                float x = cr.x + pad + 8, y = cr.y + pad + 20;
                Color ink = rgb(70, 45, 30);
                auto line = [&](const std::string& s, Color c) { DrawText(s.c_str(), int(x), int(y), 15, c); y += 19; };
                line(std::string("Owner: ") + nameOf(in.owner), in.owner == me ? rgb(30, 90, 40) : rgb(150, 40, 30));
                if (in.prog < 100) line("Under construction " + std::to_string(in.prog) + "%", ink);
                line(TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), ink);
                if (in.owner == me || alliedWith(in.owner)) {
                    line(TextFormat("Level %d / %d", in.aux, MAX_LEVEL), ink);
                    Pos c = {reg.get<Goal>(it->second).x, reg.get<Goal>(it->second).y};
                    int b = biomeAt(int(std::floor(c.x)), int(std::floor(c.y)));
                    line(std::string("Biome: ") + (b == INVALID_BIOME ? "?" : BIOME_NAME[b]), ink);
                    if (isResourceBuilding(in.kind)) {
                        Yield yv = yieldOf(in.kind, b, in.kind == K_Harbour);
                        std::string pr;
                        for (int r = 0; r < R_Count; r++) if (yv.r[r] > 0) pr += TextFormat("%s %.1f/s  ", RES_NAME[r], yv.r[r] * levelMul(in.aux));
                        line("Output: " + pr, ink);
                        bool pref = preferredBiome(in.kind, b);
                        line(pref ? "Preferred biome: BONUS ACTIVE" : (in.kind == K_Lumber ? "Best in: Forest" : (in.kind == K_Mine ? "Best in: Highlands, Desert" : "Coastal fishing")), pref ? rgb(30, 110, 40) : rgb(130, 90, 30));
                        line(TextFormat("Stock waiting: %d", in.stock), ink);
                    } else if (isStorage(in.kind)) {
                        line(TextFormat("Delivery point, territory %d", anchorRadius(in.kind)), ink);
                        line(TextFormat("Defence %.0f dps", KINFO[in.kind].dps * levelMul(in.aux)), ink);
                    } else if (in.kind == K_Campfire) line("Warms units in the snow", ink);
                    else if (in.kind == K_Barracks) line("Trains the army", ink);
                }
                for (size_t i = 0; i < ctx.size(); i++) {
                    Rectangle b = ctxButton(int(i));
                    drawButton(b, ctx[i].label, ctx[i].enabled, ctx[i].red, CheckCollisionPointRec(m, b), IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, b));
                }
            }
        }
        if (dragging) {
            Rectangle R{std::min(dragStart.x, m.x), std::min(dragStart.y, m.y), std::abs(m.x - dragStart.x), std::abs(m.y - dragStart.y)};
            if (R.width > 6 || R.height > 6) { DrawRectangleRec(R, rgb(255, 235, 90, 36)); DrawRectangleLinesEx(R, 1, rgb(255, 235, 90)); }
        }
        // hover label
        if (hovered && ents.count(hovered)) {
            const Info& in = reg.get<Info>(ents[hovered]);
            std::string s = std::string(KINFO[in.kind].name) + (in.owner ? " - " + nameOf(in.owner) : "");
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
            PW pw{*this};
            IV t = placeTile(Kind(buildKind), GetScreenToWorld2D(m, cam));
            if (checkPlace(pw, Kind(buildKind), t.x, t.y) != P_OK) file = "Cursor_03.png";
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
        float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
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
        float sw = float(GetScreenWidth()), sh = float(GetScreenHeight());
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
        int w = GetScreenWidth(), h = GetScreenHeight();
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
        shPost = loadShader("post.fs", okPost);
        bool b1 = false, b2 = false;
        shBright = loadShader("bloom_bright.fs", b1);
        shBlur = loadShader("bloom_blur.fs", b2);
        okBloom = b1 && b2 && okPost;
        mHud = loadTMap("hud_resources"); mTitle = loadTMap("title_ribbon"); mBuild = loadTMap("build_menu"); mMini = loadTMap("minimap_frame");
        mCtx = loadTMap("context_panel"); mRoster = loadTMap("roster_panel");
        mBtn = loadTMap("button_blue"); mBtnP = loadTMap("button_blue_pressed"); mBtnR = loadTMap("button_red"); mBtnRP = loadTMap("button_red_pressed");
        Image im = GenImageColor(MM, MM, BLACK);
        mmTex = LoadTextureFromImage(im);
        UnloadImage(im);
        mmPix.assign(size_t(MM * MM), BLACK);
    }

    const char* shotPath = std::getenv("BW_SHOT");
    int shotFrames = 0, shotAfter = std::getenv("BW_SHOT_FRAMES") ? std::atoi(std::getenv("BW_SHOT_FRAMES")) : 400;

    void run() {
        host = enet_host_create(nullptr, 1, 2, 0, 0);
        ENetAddress a{};
        if (!host || enet_address_set_host(&a, ip.c_str()) != 0) { TraceLog(LOG_ERROR, "Bad server address '%s'", ip.c_str()); return; }
        a.port = uint16_t(port);
        peer = enet_host_connect(host, &a, 2, 0);
        startT = GetTime();
        disp.sink<LogEvent>().connect<&onLog>();
        loadGfx();
        HideCursor();
        while (!WindowShouldClose()) {
            float dt = GetFrameTime();
            pump();
            disp.update();
            ensureTargets();
            if (welcomed && !lost) {
                input(dt);
                reg.view<RPos, Goal, Face, Info>().each([&](RPos& p, const Goal& g, Face& f, const Info& in) {
                    float k = std::min(1.f, 12.f * dt);
                    float dx = g.x - p.x;
                    p.x += dx * k;
                    p.y += (g.y - p.y) * k;
                    if (in.state == US_Attack || in.state == US_Heal) f.left = std::cos(float(in.aim) / 255.f * 6.28319f - 3.14159f) < 0;
                    else if (std::abs(dx) > .004f) f.left = dx < 0;
                });
                updateMinimap();
            }
            if (shotPath && welcomed && shotFrames >= 20) {                 // test hooks (screenshots only)
                if (const char* mp = std::getenv("BW_MOUSE")) { float dx = 0, dy = 0; if (std::sscanf(mp, "%f,%f", &dx, &dy) == 2) SetMousePosition(GetScreenWidth() / 2 + int(dx), GetScreenHeight() / 2 + int(dy)); }
                if (const char* u = std::getenv("BW_UI")) {
                    std::string ui = u;
                    if (ui.find("build") != std::string::npos) buildOpen = true;
                    if (ui.find("place") != std::string::npos) buildKind = K_Lumber;
                    if (ui.find("ctx") != std::string::npos && !ctxOpen)
                        reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (i.owner == me && i.kind == K_TownHall) openContext(n.id, {float(GetScreenWidth()) * .6f, float(GetScreenHeight()) * .25f}); });
                }
            }
            float t = float(GetTime());
            if (welcomed && !lost) {
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
            if (welcomed && !lost) {
                if (okPost) { postUniforms(t); BeginShaderMode(shPost); }
                DrawTextureRec(rt.texture, {0, 0, float(rt.texture.width), -float(rt.texture.height)}, {0, 0}, WHITE);
                if (okPost) EndShaderMode();
                if (ctxOpen) rebuildContext();
                drawUI(dt);
                drawCursor();
                if (shotPath && ++shotFrames == shotAfter) {                 // test hook: BW_SHOT=<file.png> saves a frame and quits
                    rlDrawRenderBatchActive();                                 // flush batched UI draws before reading pixels
                    Image img = LoadImageFromScreen();
                    ExportImage(img, shotPath);
                    UnloadImage(img);
                    EndDrawing();
                    break;
                }
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

int main() {
    App app;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    loadConfig(app.ip, app.port, app.myName);
    app.loadProfile();
    if (enet_initialize() != 0) { TraceLog(LOG_ERROR, "enet_initialize failed"); return 1; }
    InitWindow(1280, 720, "Base Wars");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    findAssets();
    app.run();
    CloseWindow();
    enet_deinitialize();
}
