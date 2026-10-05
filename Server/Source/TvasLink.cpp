#include "TvasLink.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <ctime>

namespace lsfs {

TvasLink::TvasLink() = default;
TvasLink::~TvasLink() = default;

void TvasLink::configure(std::string address, u64 server_id, lsf::tvas::ServerKey key, std::filesystem::path pending_dir) {
    address_ = std::move(address);
    server_id_ = server_id;
    key_ = key;
    pending_dir_ = std::move(pending_dir);
    if (!worker_) worker_ = std::make_unique<eng::net::HttpWorker>(2);
    std::error_code ec;
    if (!pending_dir_.empty()) std::filesystem::create_directories(pending_dir_, ec);
}

eng::net::HttpRequest TvasLink::request(const std::string& method, const std::string& path, std::string body, double timeout) const {
    eng::net::HttpRequest r;
    r.method = method;
    r.url = lsf::tvas::url(address_, path);
    r.body = std::move(body);
    r.timeout = timeout;
    r.headers = lsf::tvas::sign_request(server_id_, key_, method, lsf::tvas::path_of(r.url), r.body, u64(std::time(nullptr)));
    return r;
}

TvasReply TvasLink::reply_of(const eng::net::HttpResponse& r) {
    TvasReply out;
    out.status = r.error.empty() ? r.status : 0;
    if (!r.error.empty()) {
        out.error = r.error;
        return out;
    }
    if (!r.body.empty() && !eng::json::parse(r.body, out.body)) {
        out.error = "TVAS answered something that is not JSON";
        out.status = out.status ? 502 : 0;
        return out;
    }
    if (!out.ok()) out.error = out.body["error"].str("TVAS refused (HTTP " + std::to_string(r.status) + ")");
    return out;
}

void TvasLink::post(const std::string& path, const eng::json::Value& body, Done done, double timeout) {
    if (!worker_ || !enabled()) {
        if (done) done(TvasReply{0, {}, "this server is not registered with Team Vanilla"});
        return;
    }
    worker_->submit(request("POST", path, body.dump(), timeout), [done = std::move(done)](eng::net::HttpResponse r) {
        if (done) done(reply_of(r));
    });
}

void TvasLink::get(const std::string& path, Done done, double timeout) {
    if (!worker_ || !enabled()) {
        if (done) done(TvasReply{0, {}, "this server is not registered with Team Vanilla"});
        return;
    }
    worker_->submit(request("GET", path, {}, timeout), [done = std::move(done)](eng::net::HttpResponse r) {
        if (done) done(reply_of(r));
    });
}

TvasReply TvasLink::post_now(const std::string& path, const eng::json::Value& body, double timeout) {
    if (!enabled()) return TvasReply{0, {}, "this server is not registered with Team Vanilla"};
    return reply_of(eng::net::http_request(request("POST", path, body.dump(), timeout)));
}

void TvasLink::poll() {
    if (worker_) worker_->poll();
}

void TvasLink::report(u64 match_id, const eng::json::Value& rep, Done done) {
    const std::filesystem::path file = pending_dir_.empty() ? std::filesystem::path() : pending_dir_ / (std::to_string(match_id) + ".json");
    if (!file.empty()) eng::fs::write_text_file(file, rep.dump());
    post("/v1/server/report", rep, [file, done = std::move(done)](const TvasReply& r) {
        // Taken, or refused for good (a 4xx: implausible, a repeat): either way it is answered.
        if (!r.down() && !file.empty()) {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
        if (done) done(r);
    });
}

void TvasLink::retry_reports(double now) {
    if (pending_dir_.empty() || retrying_ || now - last_retry_ < 60.0 || !enabled()) return;
    last_retry_ = now;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(pending_dir_, ec)) {
        if (entry.path().extension() != ".json") continue;
        auto text = eng::fs::read_text_file(entry.path());
        eng::json::Value rep;
        if (!text || !eng::json::parse(*text, rep)) continue;
        const std::filesystem::path file = entry.path();
        post("/v1/server/report", rep, [file](const TvasReply& r) {
            if (r.down()) return;
            std::error_code e2;
            std::filesystem::remove(file, e2);
            LOG_INFO("TVAS: a waiting match report was %s", r.ok() ? "taken" : ("refused: " + r.error).c_str());
        });
    }
}

size_t TvasLink::reports_waiting() const {
    size_t n = 0;
    std::error_code ec;
    if (!pending_dir_.empty())
        for (const auto& entry : std::filesystem::directory_iterator(pending_dir_, ec)) n += entry.path().extension() == ".json";
    return n;
}

}  // namespace lsfs
