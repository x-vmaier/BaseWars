/**
 * @file PacketHandler.hpp
 * @brief Decodes server messages (S_*) into the ClientWorld and the UI queues (chat, toasts, log).
 */
#pragma once
#include <cstddef>
#include <cstdint>

class App;
struct Reader;

class PacketHandler
{
  public:
    explicit PacketHandler(App &app) : app(app) {}
    void handle(const uint8_t *d, size_t n);
    void addFx(uint8_t type, float x, float y, float x2 = 0, float y2 = 0); // effect event (+ ripple / screen shake)

  private:
    void onWelcome(Reader &r);
    void onState(Reader &r);
    void onFog(Reader &r);
    void onRoster(Reader &r);
    void onChat(Reader &r);
    void onMap(Reader &r);
    void onShipsInfo(Reader &r);
    void onShipsPaths(Reader &r);
    void worldPacket(const uint8_t *d, size_t n); // S_Cleared / S_ClearJobs / S_Drop
    bool basePacket(uint8_t type, Reader &r);     // S_Queue / S_Alert / S_BaseInfo
    App &app;
};
