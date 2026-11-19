#include "ui/Chat.hpp"

#include "client/App.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

const char *Chat::channelName(int c)
{
    return c == 0 ? "Global" : (c == 1 ? "Clan" : "Friends");
}
Color Chat::channelColor(int c)
{
    return c == 0 ? rgb(255, 255, 255) : (c == 1 ? rgb(110, 230, 255) : rgb(150, 255, 160));
}
// The bar sits high enough that the helper line below it stays inside the window.
Rectangle Chat::bar() const
{
    float w = std::min(388.f, float(ui::screenW()) - 16);
    return {float(ui::screenW()) - w - 8, float(ui::screenH()) - 54, w, 26};
}
bool Chat::channelOk(int c) const
{
    return c != 1 || app.world.myClan() != 0;
} // clan chat needs a clan
Rectangle Chat::tab(int i) const
{
    Rectangle b = bar();
    return {b.x + float(i) * 64, b.y - 22, 60, 20};
}

void Chat::push(const ChatLine &c)
{
    lines.push_back(c);
    if (lines.size() > 80)
        lines.erase(lines.begin());
}

void Chat::update(Vector2 m)
{
    for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
        if (c >= 32 && c < 127 && text.size() < 150)
            text.push_back(char(c));
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty())
        text.pop_back();
    if (!channelOk(ch))
        ch = 0; // left the clan: back to Global
    if (IsKeyPressed(KEY_TAB))
        do
            ch = (ch + 1) % 3;
        while (!channelOk(ch));
    if (IsKeyPressed(KEY_ESCAPE))
    {
        isOpen = false;
        return;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        if (!text.empty())
            app.cmd.chat(ch, text);
        text.clear();
        isOpen = false;
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        for (int i = 0; i < 3; i++)
            if (CheckCollisionPointRec(m, tab(i)) && channelOk(i))
                ch = i;
}

void Chat::draw()
{
    double now = GetTime();
    Rectangle b = bar();
    std::vector<const ChatLine *> vis;
    for (auto it = lines.rbegin(); it != lines.rend() && vis.size() < 8; ++it)
        if (isOpen || now - it->t < 14)
            vis.push_back(&*it);
    float y = b.y - 28 - 17.f * float(vis.size());
    if (!vis.empty())
        DrawRectangleRounded({b.x, y - 4, b.width, 17.f * float(vis.size()) + 8}, .1f, 4,
                             rgb(20, 14, 10, isOpen ? 190 : 120));
    for (auto it = vis.rbegin(); it != vis.rend(); ++it)
    {
        const ChatLine &c = **it;
        std::string t = std::string("[") + channelName(c.ch)[0] + "] " + c.name + ": " + c.text;
        while (MeasureText(t.c_str(), 13) > int(b.width) - 12 && t.size() > 4)
            t.pop_back();
        float a = isOpen ? 1.f : std::clamp(float(14 - (now - c.t)) / 3.f, 0.f, 1.f);
        DrawText(t.c_str(), int(b.x) + 6, int(y), 13,
                 Color{channelColor(c.ch).r, channelColor(c.ch).g, channelColor(c.ch).b, (unsigned char)(255 * a)});
        y += 17;
    }
    if (!isOpen)
    {
        DrawText("Enter: chat", int(b.x + b.width) - MeasureText("Enter: chat", 11) - 4, int(b.y) + 8, 11,
                 rgb(210, 214, 225, 170));
        return;
    }
    Vector2 m = ui::mouse();
    if (!channelOk(ch))
        ch = 0;
    std::string tip;
    for (int i = 0; i < 3; i++)
    {
        bool ok = channelOk(i), hov = CheckCollisionPointRec(m, tab(i));
        ui::drawButton(tab(i), channelName(i), ok, false, hov && ok, ch == i, 11);
        if (hov && !ok)
            tip = "Join a clan first";
    }
    if (!tip.empty())
    {
        int w = MeasureText(tip.c_str(), 12);
        Rectangle t1 = tab(1);
        DrawRectangleRounded({t1.x, t1.y - 24, float(w) + 12, 20}, .3f, 4, rgb(30, 22, 16, 235));
        DrawText(tip.c_str(), int(t1.x) + 6, int(t1.y) - 20, 12, rgb(255, 200, 160));
    }
    DrawRectangleRounded(b, .3f, 6, rgb(255, 250, 235, 235));
    DrawRectangleRoundedLinesEx(b, .3f, 6, 2, channelColor(ch).r > 200 ? rgb(60, 120, 200) : rgb(60, 140, 100));
    std::string shown = text + (int(now * 2) % 2 == 0 ? "|" : "");
    while (MeasureText(shown.c_str(), 14) > int(b.width) - 14 && shown.size() > 2)
        shown.erase(shown.begin());
    DrawText(shown.c_str(), int(b.x) + 8, int(b.y) + 6, 14, rgb(70, 45, 30));
    DrawText("Tab: channel   Enter: send   Esc: cancel", int(b.x), int(b.y) + 30, 10, rgb(210, 214, 225, 200));
}
