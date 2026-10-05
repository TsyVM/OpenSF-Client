// The game's line to Team Vanilla's account service (TVAS, Docs/UniversalServerDeploy.md §4.1, §5):
// signing in, the server list, tickets, and everything universal about a soldier (friends, mail,
// clans, TV's own shop services). HTTPS only (ID-1), asked on worker threads and answered on the
// game's own frame, so a slow TVAS never stalls a frame. Nothing is pushed and nothing waits for
// news (TV-7, TV-10): the game asks.
//
// The session token is kept between runs in the system's protected store (ID-9): DPAPI on Windows;
// elsewhere a file only the user can read until the platform's own store is wired in.
#pragma once

#include "Engine/Core/Json.hpp"
#include "Engine/Net/Https.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace lsf {

struct TvReply {
    int status = 0;              // 0: no answer
    eng::json::Value body;
    std::string raw;             // the body as it came (a signed reply is checked against it)
    std::string signature;       // X-TVAS-Sig, when TVAS signed the reply (SL-4)
    std::string error;           // no answer: why; not 2xx: TVAS's own words
    std::string code;            // TVAS's short reason ("banned", "unnamed", "throttled", ...)
    bool ok() const { return status >= 200 && status < 300; }
    bool down() const { return status == 0 || status >= 500; }
    // What to tell the player when it failed.
    std::string text() const;
};

class TvasClient {
public:
    using Done = std::function<void(const TvReply&)>;
    TvasClient();
    ~TvasClient();

    // TV-15: the address is a setting; nothing else about TVAS is known here (TV-11).
    void configure(std::string address);
    const std::string& address() const { return address_; }
    void set_token(std::string token) { token_ = std::move(token); }
    const std::string& token() const { return token_; }

    void get(const std::string& path, Done done, double timeout = 15);
    void post(const std::string& path, const eng::json::Value& body, Done done, double timeout = 15);
    // Runs the callbacks of what has been answered (the game's frame).
    void poll();
    size_t pending() const;
    // Tests and shutdown: waits for what is in flight.
    void drain(double seconds);

private:
    void send(const char* method, const std::string& path, std::string body, Done done, double timeout);
    std::string address_;
    std::string token_;
    std::unique_ptr<eng::net::HttpWorker> worker_;
};

// The saved sign-in (ID-9). Empty when there is none or it cannot be read.
bool save_session_token(const std::filesystem::path& file, std::string_view token);
std::string load_session_token(const std::filesystem::path& file);
void forget_session_token(const std::filesystem::path& file);

}  // namespace lsf
