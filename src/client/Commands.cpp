#include "client/Commands.hpp"

#include "client/App.hpp"

#include <cmath>

void Commands::move(Vector2 w) {
    auto ids = app.input.selUnits();
    if (ids.empty()) { app.msg("Select units first (left-click or drag)."); return; }
    Writer wr;
    wr.put<uint8_t>(C_Move).put<uint8_t>(uint8_t(ids.size()));
    for (auto i : ids) wr.put<uint32_t>(i);
    wr.put<float>(w.x / TILE).put<float>(w.y / TILE);
    app.session.send(wr);
    app.world.markers.push_back({w.x, w.y, GetTime()});
    if (app.world.biomeAt(int(std::floor(w.x / TILE)), int(std::floor(w.y / TILE))) == B_Ocean) app.world.ripples.push_back({w.x, w.y, GetTime()});
}
void Commands::attackMove(Vector2 w) {
    auto ids = app.input.selUnits();
    if (ids.empty()) { app.msg("Select units first."); return; }
    Writer wr;
    wr.put<uint8_t>(C_AttackMove).put<uint8_t>(uint8_t(ids.size()));
    for (auto i : ids) wr.put<uint32_t>(i);
    wr.put<float>(w.x / TILE).put<float>(w.y / TILE);
    app.session.send(wr);
    app.world.markers.push_back({w.x, w.y, GetTime()});
}
void Commands::attack(uint32_t target) {
    auto ids = app.input.selUnits();
    if (ids.empty()) { app.msg("Select army units first."); return; }
    Writer wr;
    wr.put<uint8_t>(C_Attack).put<uint8_t>(uint8_t(ids.size()));
    for (auto i : ids) wr.put<uint32_t>(i);
    wr.put<uint32_t>(target);
    app.session.send(wr);
}
void Commands::ally(int to, int act) { Writer w; w.put<uint8_t>(C_Ally).put<uint8_t>(uint8_t(to)).put<uint8_t>(uint8_t(act)); app.session.send(w); }
void Commands::train(uint32_t b, Kind k) { Writer w; w.put<uint8_t>(C_Train).put<uint32_t>(b).put<uint8_t>(k); app.session.send(w); }
void Commands::upgrade(uint32_t b) { Writer w; w.put<uint8_t>(C_Upgrade).put<uint32_t>(b); app.session.send(w); }
void Commands::clanCreate(const std::string& n) { Writer w; w.put<uint8_t>(C_ClanCreate); w.str(n); app.session.send(w); }
void Commands::clanJoin(int c) { Writer w; w.put<uint8_t>(C_ClanJoin).put<uint8_t>(uint8_t(c)); app.session.send(w); }
void Commands::clanLeave() { Writer w; w.put<uint8_t>(C_ClanLeave); app.session.send(w); }
void Commands::transfer(int to, int res, float amount) {
    Writer w;
    w.put<uint8_t>(C_Transfer).put<uint8_t>(uint8_t(to));
    for (int i = 0; i < R_Count; i++) w.put<float>(i == res ? amount : 0.f);
    app.session.send(w);
}
void Commands::donate(int faction, int res, float amount) { Writer w; w.put<uint8_t>(C_Donate).put<uint8_t>(uint8_t(faction)).put<uint8_t>(uint8_t(res)).put<float>(amount); app.session.send(w); }
void Commands::setHaul(uint32_t b, int pct) { Writer w; w.put<uint8_t>(C_SetHaul).put<uint32_t>(b).put<uint8_t>(uint8_t(pct)); app.session.send(w); }
void Commands::upgradeUnit(Kind k) { Writer w; w.put<uint8_t>(C_UpgradeUnit).put<uint8_t>(k); app.session.send(w); }
void Commands::chat(int ch, const std::string& t) { Writer w; w.put<uint8_t>(C_Chat).put<uint8_t>(uint8_t(ch)); w.str(t); app.session.send(w); }
void Commands::clanLeader(int id) { Writer w; w.put<uint8_t>(C_ClanLeader).put<uint8_t>(uint8_t(id)); app.session.send(w); }
void Commands::build(Kind k, IV t) { Writer w; w.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(t.x).put<int32_t>(t.y); app.session.send(w); }
void Commands::clear(const std::vector<IV>& tiles, const std::vector<uint32_t>& pawns) {
    for (size_t i = 0; i < tiles.size(); i += MAX_CLEAR_TILES) {
        size_t n = std::min<size_t>(MAX_CLEAR_TILES, tiles.size() - i);
        Writer w;
        w.put<uint8_t>(C_Clear).put<uint8_t>(uint8_t(n));
        for (size_t k = 0; k < n; k++) w.put<int32_t>(tiles[i + k].x).put<int32_t>(tiles[i + k].y);
        if (!pawns.empty()) {                                       // optional tail: the pawns to send
            size_t m = std::min<size_t>(pawns.size(), 255);
            w.put<uint8_t>(uint8_t(m));
            for (size_t k = 0; k < m; k++) w.put<uint32_t>(pawns[k]);
        }
        app.session.send(w);
    }
}
void Commands::board(uint32_t ship, const std::vector<uint32_t>& us) {
    Writer w;
    w.put<uint8_t>(C_Board).put<uint8_t>(uint8_t(us.size()));
    for (auto u : us) w.put<uint32_t>(u);
    w.put<uint32_t>(ship);
    app.session.send(w);
}
void Commands::disembark(uint32_t ship) { Writer w; w.put<uint8_t>(C_Disembark).put<uint32_t>(ship); app.session.send(w); }
void Commands::quota(int q) { Writer w; w.put<uint8_t>(C_SetQuota).put<uint8_t>(uint8_t(q)); app.session.send(w); }
void Commands::support(float x, float y, int n) { Writer w; w.put<uint8_t>(C_Support).put<float>(x).put<float>(y).put<uint8_t>(uint8_t(n)); app.session.send(w); }
void Commands::moveTown(IV t) { Writer w; w.put<uint8_t>(C_MoveTown).put<int32_t>(t.x).put<int32_t>(t.y); app.session.send(w); }
void Commands::relocate(uint32_t b, IV t) { Writer w; w.put<uint8_t>(C_Relocate).put<uint32_t>(b).put<int32_t>(t.x).put<int32_t>(t.y); app.session.send(w); }
void Commands::dequeue(uint32_t b, int idx) { Writer w; w.put<uint8_t>(C_Dequeue).put<uint32_t>(b).put<uint8_t>(uint8_t(idx)); app.session.send(w); }
void Commands::repair(uint32_t b, int mode) { Writer w; w.put<uint8_t>(C_Repair).put<uint32_t>(b).put<uint8_t>(uint8_t(mode)); app.session.send(w); }
void Commands::camera(Vector2 t) { Writer w; w.put<uint8_t>(C_Cam).put<float>(t.x / TILE).put<float>(t.y / TILE); app.session.send(w); }
