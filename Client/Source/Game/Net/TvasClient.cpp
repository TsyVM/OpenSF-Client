#include "Game/Net/TvasClient.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Game/Tvas.hpp"

#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <wincrypt.h>
#else
#include <sys/stat.h>
#endif

namespace lsf {

std::string TvReply::text() const {
    if (status == 0) return "Team Vanilla's services cannot be reached; try again shortly.";
    if (status >= 500) return "Team Vanilla's services are down; try again shortly.";
    return error.empty() ? "Team Vanilla refused that." : error;
}

TvasClient::TvasClient() = default;
TvasClient::~TvasClient() = default;

void TvasClient::configure(std::string address) {
    address_ = std::move(address);
    if (!worker_) worker_ = std::make_unique<eng::net::HttpWorker>(3);
}

namespace {

TvReply reply_of(const eng::net::HttpResponse& r) {
    TvReply out;
    out.status = r.error.empty() ? r.status : 0;
    if (!r.error.empty()) {
        out.error = r.error;
        return out;
    }
    out.raw = r.body;
    out.signature = r.header("x-tvas-sig");
    if (!r.body.empty() && !eng::json::parse(r.body, out.body)) {
        out.error = "Team Vanilla answered something that could not be read.";
        out.status = 502;
        return out;
    }
    if (!out.ok()) {
        out.error = out.body["error"].str("Team Vanilla refused that (HTTP " + std::to_string(r.status) + ").");
        out.code = out.body["code"].str();
    }
    return out;
}

}  // namespace

void TvasClient::send(const char* method, const std::string& path, std::string body, Done done, double timeout) {
    if (!worker_ || address_.empty()) {
        if (done) done(TvReply{0, {}, {}, {}, "Team Vanilla's address is not set.", {}});
        return;
    }
    eng::net::HttpRequest r;
    r.method = method;
    r.url = tvas::url(address_, path);
    r.body = std::move(body);
    r.timeout = timeout;
    if (!r.body.empty()) r.headers.emplace_back("Content-Type", "application/json");
    if (!token_.empty()) r.headers.emplace_back("Authorization", "Bearer " + token_);
    worker_->submit(std::move(r), [done = std::move(done)](eng::net::HttpResponse res) {
        if (done) done(reply_of(res));
    });
}

void TvasClient::get(const std::string& path, Done done, double timeout) { send("GET", path, {}, std::move(done), timeout); }

void TvasClient::post(const std::string& path, const eng::json::Value& body, Done done, double timeout) {
    send("POST", path, body.is_null() ? std::string("{}") : body.dump(), std::move(done), timeout);
}

void TvasClient::poll() {
    if (worker_) worker_->poll();
}

size_t TvasClient::pending() const { return worker_ ? worker_->pending() : 0; }

void TvasClient::drain(double seconds) {
    if (worker_) worker_->drain(seconds);
}

// ── The saved sign-in (ID-9) ───────────────────────────────────────────────────

bool save_session_token(const std::filesystem::path& file, std::string_view token) {
    if (token.empty()) {
        forget_session_token(file);
        return true;
    }
#ifdef _WIN32
    // DPAPI: only this Windows user, on this PC, can read it back.
    DATA_BLOB in{DWORD(token.size()), reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Soldier Front Legacy sign-in", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    const bool ok = eng::fs::write_file(file, out.pbData, out.cbData);
    LocalFree(out.pbData);
    return ok;
#else
    if (!eng::fs::write_file(file, token.data(), token.size())) return false;
    ::chmod(file.c_str(), S_IRUSR | S_IWUSR);
    return true;
#endif
}

std::string load_session_token(const std::filesystem::path& file) {
    auto bytes = eng::fs::read_file(file);
    if (!bytes || bytes->empty() || bytes->size() > 8192) return {};
#ifdef _WIN32
    DATA_BLOB in{DWORD(bytes->size()), bytes->data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string token(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return token;
#else
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
#endif
}

void forget_session_token(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

}  // namespace lsf
