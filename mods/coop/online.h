// Co-op through your own Nakama server (online.cpp): the alternative to Steam for groups that run a server
// (docs/SERVER.md). Each co-op game is a relayed Nakama match found by its room code (a 5-character code: the match is
// created by name, so the host and everyone with the code land in the same one); listed games are public storage
// objects that the "Find games" list reads. Uses Windows' WinHTTP for HTTP(S) and the WebSocket; all network waits run
// on background threads, so the game never stalls. A dropped connection is opened again (growing waits, up to 2
// minutes); players keep their number across it. The server comes from the co-op settings (host, port, TLS, key).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace online {
struct Lobby { std::string code, name; int members; };
struct Events
{
    void (*created)(const std::string& code);   // our game is up (host)
    void (*entered)(uint64_t host);              // the host answered us (guest)
    void (*failed)(const char* why);
    void (*left)(uint64_t player);               // someone left the game
    void (*listed)();                            // the list of games is ready
    void (*reconnecting)(const char* status);    // the connection dropped: trying again (with this to show)
    void (*reconnected)();                       // back in the room (messages sent meanwhile are lost)
};
bool Configured();                               // a server and key are set
void Configure(const std::string& server, const std::string& key, const std::string& device, const Events& events);
void SetLog(void (*log)(const char* line));
void Host(bool listed, const std::string& name, const std::string& tag);
void Join(const std::string& code);
void Refresh(const std::string& tag);           // at most every 3 s
std::string ListError();                         // why the last list failed, or ""
void Test();                                     // checks the server: healthcheck, login, version
bool TestResult(std::string& result);            // the test's message, once it's done
std::string ServerName();                        // host (and port) of the configured server
const std::vector<Lobby>& Lobbies();
void Frame();                                    // events and listing upkeep; call every frame
void Leave();
void SetJoinable(bool on, int members);          // host: new players welcome or not; the listing follows
std::string Code();                              // the room code of the current game, or ""
bool IsCode(const std::string& s);               // looks like a room code (5 characters of the code alphabet)
std::string CleanCode(const std::string& s);     // upper case, only the code alphabet
bool Send(uint64_t to, const std::string& data);
bool Receive(uint64_t& from, std::string& data);
}
