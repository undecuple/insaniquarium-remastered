// Steam for co-op (steam.cpp): lobbies to find games, and Steam's relayed messages to play them. Uses Valve's
// steam_api.dll (32-bit) through its flat C API, loaded only when a player picks Steam: mods\coop\steam_api.dll, else
// a copy another Steam game already has on this computer (SDK 1.49 or newer; found once, then remembered by the caller). App ID 480
// (Spacewar), like many small projects: lobbies are tagged so only this mod's games are listed.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace steam {
struct Lobby { uint64_t id; std::string name, code; int members; };
struct Events
{
    void (*created)(uint64_t lobby);        // our lobby is up (host)
    void (*entered)(uint64_t owner);        // we're in a lobby, its owner is the host (guest)
    void (*failed)(const char* why);
    void (*left)(uint64_t user);            // someone left the lobby
    void (*listed)();                       // the lobby list is ready
};
bool Start(const Events& events, std::string& error, const std::string& hint);   // loads and starts Steam (once);
                                                        // hint = the steam_api.dll found last time, or ""
std::string LoadedFrom();                    // the steam_api.dll in use
void SetLog(void (*log)(const char* line));
bool Running();
void Frame();                                // callbacks; call every frame
void Host(bool listed, const std::string& name, const std::string& tag);
void Refresh(const std::string& tag);
const std::vector<Lobby>& Lobbies();
void Join(uint64_t lobby);
void Leave();
uint64_t CurrentLobby();
void SetJoinable(bool on);
std::string Code(uint64_t lobby);
uint64_t FromCode(const std::string& code);  // 0 = not a code
uint64_t Me();
std::string MyName();
bool Send(uint64_t to, const std::string& data);
bool Receive(uint64_t& from, std::string& data);
}
