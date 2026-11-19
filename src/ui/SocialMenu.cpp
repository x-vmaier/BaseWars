#include "ui/SocialMenu.hpp"

#include "client/App.hpp"
#include "rendering/Palette.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cctype>
#include <cmath>

namespace {
bool containsNoCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    std::string h = hay, n = needle;
    for (auto& c : h) c = char(std::tolower(uint8_t(c)));
    for (auto& c : n) c = char(std::tolower(uint8_t(c)));
    return h.find(n) != std::string::npos;
}
}  // namespace

void SocialMenu::load() { mSocial = loadTMap("social_menu"); }

// ---------------------------------------------------------------- rows per tab: players / friends / clans / my clan / factions
std::vector<SocialMenu::SRow> SocialMenu::rows() {
    const ClientWorld& W = app.world;
    std::vector<SRow> rows;
    int mc = W.myClan();
    auto friendBtn = [&](const Peer& p) {
        if (p.flags & 2) return SBtn{"Unfriend", 3, 0, 0, p.id, true, true, 70};
        if (p.flags & 4) return SBtn{"Accept", 2, 0, 0, p.id, true, false, 70};
        if (p.flags & 8) return SBtn{"Sent", 0, 0, 0, p.id, false, false, 70};
        return SBtn{"Friend", 2, 0, 0, p.id, true, false, 70};
    };
    auto peerRow = [&](const Peer& p) {
        std::string cn = p.clan ? W.clanName(p.clan) : "";
        SRow r{p.name + (cn.empty() ? "" : "  [" + cn + "]"), std::string(p.flags & 1 ? "online" : "offline") + (p.hasLoc ? TextFormat("  (%d, %d)", int(p.lx), int(p.ly)) : "  location unknown"), teamColor(p.id), {}};
        r.btns.push_back({"Travel", 1, p.lx, p.ly, 0, p.hasLoc, false, 60});
        r.btns.push_back(friendBtn(p));
        return r;
    };
    switch (tab) {
    case 0: for (auto& p : W.roster) if (p.id != W.me && containsNoCase(p.name, searchText)) rows.push_back(peerRow(p)); break;
    case 1: for (auto& p : W.roster) if (p.id != W.me && (p.flags & (2 | 4 | 8)) && containsNoCase(p.name, searchText)) rows.push_back(peerRow(p)); break;
    case 2:
        for (auto& c : W.clansList) {
            if (!containsNoCase(c.name, searchText)) continue;
            SRow r{c.name, TextFormat("%d member%s", c.members, c.members == 1 ? "" : "s"), clanColor(c.id), {}};
            r.btns.push_back({c.id == mc ? "Yours" : "Join", 4, 0, 0, c.id, mc == 0, false, 60});
            rows.push_back(r);
        }
        break;
    case 3:
        if (!mc) rows.push_back({"You are not in a clan.", "Create one or join one in the Clans tab.", rgb(90, 60, 40), {}});
        else
            for (auto& p : W.roster) {
                if (p.clan != mc) continue;
                int leader = 0;
                for (auto& c : W.clansList) if (c.id == mc) leader = c.leader;
                SRow r{p.name + (p.id == leader ? "  (leader, clan hall)" : ""), std::string(p.flags & 1 ? "online" : "offline") + (p.hasLoc ? TextFormat("  (%d, %d)", int(p.lx), int(p.ly)) : ""), teamColor(p.id), {}};
                r.btns.push_back({"Travel", 1, p.lx, p.ly, 0, p.hasLoc, false, 56});
                if (p.id != W.me) {
                    if (leader == W.me) r.btns.push_back({"Lead", 8, 0, 0, p.id, true, false, 38});
                    static const char* ab[R_Count] = {"W", "G", "S", "M"};
                    for (int k = 0; k < R_Count; k++) r.btns.push_back({std::string("50") + ab[k], 5, float(k), 50.f, p.id, W.res[k] >= 50.f, false, 34});
                }
                rows.push_back(r);
            }
        break;
    default:
        for (int f = 0; f < FACTION_COUNT; f++) {
            int rp = W.facRep[f];
            SRow r{FACTION_NAME[f], TextFormat("%s (%d)", repTier(rp), rp), teamColor(FACTION_BASE + f), {}};
            const FacTown* best = nullptr;
            float bd = 1e9f;
            for (auto& t : W.facTowns) if (t.faction == f) { float d = std::hypot(t.x - app.cam.target.x / TILE, t.y - app.cam.target.y / TILE); if (d < bd) { bd = d; best = &t; } }
            r.btns.push_back({best ? "Nearest town" : "None found", 1, best ? best->x : 0.f, best ? best->y : 0.f, 0, best != nullptr, false, 104});
            rows.push_back(r);
        }
        break;
    }
    return rows;
}

void SocialMenu::run(const SBtn& b) {
    Commands& cmd = app.cmd;
    switch (b.act) {
    case 1: app.travelTo(b.a, b.b); break;
    case 2: cmd.ally(b.c, AA_Invite); break;
    case 3: cmd.ally(b.c, AA_Break); break;
    case 4: cmd.clanJoin(b.c); break;
    case 5: cmd.transfer(b.c, int(b.a), b.b); break;
    case 6: cmd.clanLeave(); break;
    case 7: cmd.clanCreate(searchText); searchText.clear(); break;
    case 8: cmd.clanLeader(b.c); break;
    default: break;
    }
}

bool SocialMenu::bottomBtn(SBtn& out) {
    int mc = app.world.myClan();
    if (tab == 2 && mc == 0) { out = {"Create clan", 7, 0, 0, 0, searchText.size() >= 3, false, 144}; return true; }
    if (tab == 3 && mc != 0) { out = {"Leave clan", 6, 0, 0, 0, true, true, 144}; return true; }
    return false;
}

// ---------------------------------------------------------------- layout: natural size, shrunk to fit small windows
Rectangle SocialMenu::rect() const {
    Vector2 s = mSocial.size();
    float w = std::min(s.x, float(ui::screenW()) - 16), h = std::min(s.y, float(ui::screenH()) - 16);
    return {(float(ui::screenW()) - w) / 2, (float(ui::screenH()) - h) / 2, w, h};
}
float SocialMenu::scale() const { Rectangle r = rect(); return std::min(r.width / mSocial.size().x, r.height / mSocial.size().y); }
Rectangle SocialMenu::tabRect(int i) const {
    Rectangle r = rect();
    float pad = float(mSocial.padding), w = (r.width - 2 * pad - 8) / 5.f;
    return {r.x + pad + 4 + float(i) * w, r.y + pad + 4, w - 3, 26};
}
Rectangle SocialMenu::search() const { Rectangle r = rect(); float pad = float(mSocial.padding) * scale(); return {r.x + pad + 6, r.y + pad + 36, r.width - 2 * pad - 12, 24}; }
Rectangle SocialMenu::list() const { Rectangle r = rect(); float pad = float(mSocial.padding) * scale(); return {r.x + pad + 6, r.y + pad + 68, r.width - 2 * pad - 12, r.height - 2 * pad - 68 - 42}; }
Rectangle SocialMenu::bottomBtnRect() const { Rectangle r = rect(); float pad = float(mSocial.padding) * scale(); return {r.x + r.width - pad - 150, r.y + r.height - pad - 34, 144, 28}; }
Rectangle SocialMenu::closeRect() const { Rectangle r = rect(); float pad = float(mSocial.padding) * scale(); return {r.x + r.width - pad - 20, r.y + std::max(2.f, pad - 22), 22, 18}; }
Rectangle SocialMenu::rowBtnRect(const Rectangle& row, const std::vector<SBtn>& btns, size_t i) {
    float x = row.x + row.width - 4;
    for (size_t k = btns.size(); k-- > i;) x -= btns[k].w + 4;
    return {x, row.y + 3, btns[i].w, SROW - 8};
}

// ---------------------------------------------------------------- input / drawing
void SocialMenu::input(Vector2 m) {
    if (textFocus) {
        for (int ch = GetCharPressed(); ch > 0; ch = GetCharPressed()) if (ch >= 32 && ch < 127 && searchText.size() < 24) { searchText.push_back(char(ch)); scroll = 0; }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !searchText.empty()) searchText.pop_back();
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) textFocus = false;
    } else if (IsKeyPressed(KEY_P)) { isOpen = false; return; }
    if (IsKeyPressed(KEY_ESCAPE)) { if (textFocus) textFocus = false; else isOpen = false; return; }
    Rectangle sr = rect(), ls = list();
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && CheckCollisionPointRec(m, sr)) scroll = std::max(0, scroll - int(wheel));
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    if (!CheckCollisionPointRec(m, sr)) { isOpen = false; textFocus = false; return; }
    if (CheckCollisionPointRec(m, closeRect())) { isOpen = false; textFocus = false; return; }
    for (int i = 0; i < 5; i++) if (CheckCollisionPointRec(m, tabRect(i))) { tab = i; scroll = 0; return; }
    textFocus = CheckCollisionPointRec(m, search());
    SBtn bb;
    if (bottomBtn(bb) && CheckCollisionPointRec(m, bottomBtnRect()) && bb.enabled) { run(bb); return; }
    auto rs = rows();
    int vis = int(ls.height / SROW);
    scroll = std::clamp(scroll, 0, std::max(0, int(rs.size()) - vis));
    for (int i = 0; i < vis && i + scroll < int(rs.size()); i++) {
        Rectangle rr{ls.x, ls.y + float(i) * SROW, ls.width, SROW};
        auto& row = rs[size_t(i + scroll)];
        for (size_t k = 0; k < row.btns.size(); k++)
            if (CheckCollisionPointRec(m, rowBtnRect(rr, row.btns, k)) && row.btns[k].enabled) { run(row.btns[k]); return; }
    }
}

void SocialMenu::draw() {
    Vector2 m = ui::mouse();
    DrawRectangle(0, 0, ui::screenW(), ui::screenH(), rgb(0, 0, 0, 110));
    Rectangle sr = rect();
    drawTMap(mSocial, sr);
    float pad = float(mSocial.padding) * scale();
    Color ink = rgb(70, 45, 30);
    static const char* tabs[5] = {"Players", "Friends", "Clans", "My Clan", "Factions"};
    for (int i = 0; i < 5; i++) ui::drawButton(tabRect(i), tabs[i], true, false, CheckCollisionPointRec(m, tabRect(i)), tab == i, 12);
    ui::drawButton(closeRect(), "X", true, true, CheckCollisionPointRec(m, closeRect()), false, 12);
    Rectangle sb = search();
    DrawRectangleRounded(sb, .3f, 6, rgb(255, 250, 235, 235));
    DrawRectangleRoundedLinesEx(sb, .3f, 6, 2, textFocus ? rgb(60, 120, 200) : rgb(120, 90, 60));
    std::string shown = searchText.empty() && !textFocus ? (tab == 2 ? "Search clans or type a new clan name..." : "Search by name...") : searchText + (textFocus && int(GetTime() * 2) % 2 == 0 ? "|" : "");
    DrawText(shown.c_str(), int(sb.x) + 8, int(sb.y) + 5, 15, searchText.empty() && !textFocus ? rgb(150, 130, 110) : ink);
    auto rs = rows();
    Rectangle ls = list();
    int vis = int(ls.height / SROW);
    scroll = std::clamp(scroll, 0, std::max(0, int(rs.size()) - vis));
    if (rs.empty()) DrawText(tab == 1 ? "No friends yet. Add players from the Players tab." : "Nothing to show.", int(ls.x) + 6, int(ls.y) + 8, 14, rgb(120, 90, 60));
    for (int i = 0; i < vis && i + scroll < int(rs.size()); i++) {
        Rectangle rr{ls.x, ls.y + float(i) * SROW, ls.width, SROW};
        auto& row = rs[size_t(i + scroll)];
        DrawRectangleRec(rr, i % 2 ? rgb(0, 0, 0, 14) : rgb(255, 255, 255, 36));
        DrawText(row.text.c_str(), int(rr.x) + 6, int(rr.y) + 2, 15, row.col.r > 200 && row.col.g > 200 ? ink : row.col);
        DrawText(row.sub.c_str(), int(rr.x) + 6, int(rr.y) + 17, 10, rgb(110, 85, 60));
        for (size_t k = 0; k < row.btns.size(); k++) {
            Rectangle b = rowBtnRect(rr, row.btns, k);
            ui::drawButton(b, row.btns[k].label, row.btns[k].enabled, row.btns[k].red, CheckCollisionPointRec(m, b), false, 11);
        }
    }
    if (int(rs.size()) > vis) DrawText(TextFormat("%d-%d of %d  (mouse wheel)", scroll + 1, std::min(int(rs.size()), scroll + vis), int(rs.size())), int(ls.x) + 6, int(ls.y + ls.height) + 2, 10, ink);
    SBtn bb;
    if (bottomBtn(bb)) ui::drawButton(bottomBtnRect(), bb.label, bb.enabled, bb.red, CheckCollisionPointRec(m, bottomBtnRect()), false, 13);
    int mc = app.world.myClan();
    if (tab == 3 && mc) DrawText("Resources travel by convoy and can be raided.", int(sr.x + pad) + 6, int(sr.y + sr.height - pad - 28), 11, ink);
    if (tab == 2 && mc == 0) DrawText("Type a name above, then press Create.", int(sr.x + pad) + 6, int(sr.y + sr.height - pad - 28), 11, ink);
}
