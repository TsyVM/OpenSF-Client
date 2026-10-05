// The server's line to Team Vanilla's account service (Docs/UniversalServerDeploy.md §4, §6.6, §8.2):
// every request signed with the server's own key (§8.1), made on worker threads and answered on the
// server's own tick, so a slow TVAS never stalls a match (TV-6).
//
// Match reports a TVAS that is down cannot take are kept on disk and sent again until it answers
// (PR-7): reports/pending/<match>.json, removed once TVAS has taken or refused one.
#pragma once

#include "ServerOnly.hpp"

#include "Engine/Core/Json.hpp"
#include "Engine/Net/Https.hpp"
#include "Game/Tvas.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace lsfs {

using eng::u32;
using eng::u64;

struct TvasReply {
    int status = 0;              // 0: no answer
    eng::json::Value body;
    std::string error;           // no answer: why; an answer that is not 2xx: TVAS's own words
    bool ok() const { return status >= 200 && status < 300; }
    bool down() const { return status == 0 || status >= 500; }
};

class TvasLink {
public:
    using Done = std::function<void(const TvasReply&)>;
    TvasLink();
    ~TvasLink();

    void configure(std::string address, u64 server_id, lsf::tvas::ServerKey key, std::filesystem::path pending_dir);
    bool enabled() const { return server_id_ != 0 && key_.valid; }
    const std::string& address() const { return address_; }
    u64 server_id() const { return server_id_; }

    void post(const std::string& path, const eng::json::Value& body, Done done, double timeout = 15);
    void get(const std::string& path, Done done, double timeout = 15);
    // One request waited for (the server's start: the test keys, the first heartbeat).
    TvasReply post_now(const std::string& path, const eng::json::Value& body, double timeout = 10);
    // Runs the callbacks of what has been answered.
    void poll();
    size_t pending() const { return worker_ ? worker_->pending() : 0; }
    void drain(double seconds) {
        if (worker_) worker_->drain(seconds);
    }

    // PR-7: a match report, kept on disk until TVAS answers it. `done` hears TVAS's answer (or that
    // it is queued: a down() reply) the first time; a report sent again later is answered silently.
    void report(u64 match_id, const eng::json::Value& report, Done done);
    // Sends again the reports still waiting (every minute or so, from the tick).
    void retry_reports(double now);
    size_t reports_waiting() const;

private:
    eng::net::HttpRequest request(const std::string& method, const std::string& path, std::string body, double timeout) const;
    static TvasReply reply_of(const eng::net::HttpResponse& r);
    std::string address_;
    u64 server_id_ = 0;
    lsf::tvas::ServerKey key_;
    std::filesystem::path pending_dir_;
    std::unique_ptr<eng::net::HttpWorker> worker_;
    double last_retry_ = 0;
    bool retrying_ = false;
};

}  // namespace lsfs
