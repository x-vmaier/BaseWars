#pragma once
// Chat (global / clan / friends): recent lines fade out; Enter opens the input bar.
#include "client/ClientState.hpp"

#include <string>
#include <vector>

class App;

class Chat {
public:
    explicit Chat(App& app) : app(app) {}

    bool isOpen = false;
    std::vector<ChatLine> lines;

    void open() { isOpen = true; text.clear(); }
    void push(const ChatLine& c);
    void update(Vector2 m);
    void draw();

private:
    static const char* channelName(int ch);
    static Color channelColor(int ch);
    Rectangle bar() const;
    Rectangle tab(int i) const;
    bool channelOk(int ch) const;       // the clan channel is disabled outside a clan

    App& app;
    int ch = 0;
    std::string text;
};
