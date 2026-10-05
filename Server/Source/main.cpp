// LegacySFServer: the Soldier Front Legacy game server (Docs/UniversalServerDeploy.md §12).
//
//   LegacySFServer.exe                     runs the server from server.cfg beside it
//   LegacySFServer.exe --register CODE     the one-time code from the UCP: registers this server's
//                                          key with Team Vanilla and writes its id in server.cfg
//   LegacySFServer.exe --key               prints this server's public key (made on the first run)
//   LegacySFServer.exe --check             reads server.cfg, the data and the packs, then stops
//   LegacySFServer.exe --firewall          (Windows) allows the server through the Windows firewall
//
// Everything is in the server's own folder: server.cfg, server.key, channels.cfg, staff.cfg,
// shop.cfg, accounts/, packs/, recordings/, logs/ and data/ (the Soldier Front game data). Players
// find the server in the game's server list once it is registered and reachable (§8.1, SL-2).
// Ctrl+C (SIGTERM on Linux) saves and stops it.
#include "Server.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Net/Https.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <io.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
using eng::u16;
using eng::u32;
using eng::u64;

std::atomic<bool> g_run{true};
void on_signal(int) { g_run = false; }
// The log mirrored to the console once the server runs; before that, only what say() says.
std::atomic<bool> g_echo{false};

fs::path g_dir;

const char* exe_name() {
#ifdef _WIN32
    return "LegacySFServer.exe";
#else
    return "./legacysf-server";
#endif
}

void say(const std::string& text) {
    if (!g_echo) std::fprintf(stderr, "%s\n", text.c_str());
    LOG_INFO("%s", text.c_str());
}

int refuse(const std::string& text) {
    std::fprintf(stderr, "\nThe server did not start: %s\n", text.c_str());
    LOG_ERROR("Did not start: %s", text.c_str());
    eng::log::shutdown();
    return 1;
}

bool read_text(const fs::path& p, std::string& out) {
    auto bytes = eng::fs::read_file(p);
    if (!bytes) return false;
    out.assign(bytes->begin(), bytes->end());
    return true;
}

bool interactive() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

// ── server.key (§8.1): made on the first run, never sent anywhere ───────────────

bool load_or_make_key(lsf::tvas::ServerKey& key, bool* made, std::string* why) {
    const fs::path file = g_dir / "server.key";
    *made = false;
    std::string text;
    if (read_text(file, text)) {
        if (!lsf::tvas::read_server_key(eng::str::trim(text), key)) {
            *why = "server.key is damaged. If this server's key is lost, revoke it on the UCP and delete server.key to make a new one.";
            return false;
        }
        return true;
    }
    key = lsf::tvas::make_server_key();
    const std::string seed = lsf::tvas::server_key_text(key) + "\n";
    if (!eng::fs::write_file(file, seed.data(), seed.size())) {
        *why = "server.key cannot be written in " + eng::str::narrow(g_dir.wstring());
        return false;
    }
#ifndef _WIN32
    ::chmod(file.c_str(), 0600);
#endif
    *made = true;
    return true;
}

std::string public_hex(const lsf::tvas::ServerKey& k) { return eng::crypto::to_hex(k.pub); }

// ── server.cfg (§12.3, DS-1, DS-4) ────────────────────────────────────────────

const char* kDefaultConfig =
    "# Soldier Front Legacy server (Docs: README.txt). Every value is checked when the server starts;\n"
    "# one out of range stops it with the reason, never a guess.\n"
    "\n"
    "# From the registration (README: Registering). Run the server once with --register CODE.\n"
    "server_id =\n"
    "\n"
    "name = My Soldier Front Server\n"
    "motd = Welcome!\n"
    "region =\n"
    "\n"
    "# Network. bind_address: one of this machine's own addresses (empty: all of them).\n"
    "# public_address / public_port: where players reach it, when that is not what Team Vanilla sees.\n"
    "bind_address =\n"
    "port = 27240\n"
    "public_address =\n"
    "public_port =\n"
    "\n"
    "max_players = 128\n"
    "reserved_slots = 4\n"
    "\n"
    "# The gate (ranks 0..74; the real rank, never a Fake Rank Mark's).\n"
    "min_rank = 0\n"
    "max_rank = 74\n"
    "min_kd = 0\n"
    "\n"
    "# A private server: a password, and/or listed = no to keep it off the list.\n"
    "password =\n"
    "listed = yes\n"
    "\n"
    "# The Soldier Front game data (empty: data/ beside the server).\n"
    "client_data =\n"
    "\n"
    "# Pack downloads: the whole upload they may use, and how many players download at once.\n"
    "upload_kbps = 4000\n"
    "max_downloaders = 4\n"
    "\n"
    "log_days = 14\n";

struct Range {
    const char* key;
    long long lo, hi;
};

bool to_int(std::string_view v, long long& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const std::string s(v);
    out = std::strtoll(s.c_str(), &end, 10);
    return end && *end == 0;
}

bool to_float(std::string_view v, double& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const std::string s(v);
    out = std::strtod(s.c_str(), &end);
    return end && *end == 0 && std::isfinite(out);
}

bool to_bool(std::string_view v, bool& out) {
    const std::string s = eng::str::lower(v);
    if (s == "yes" || s == "true" || s == "1" || s == "on") return out = true, true;
    if (s == "no" || s == "false" || s == "0" || s == "off") return out = false, true;
    return false;
}

bool ipv4_literal(std::string_view s) {
    int parts = 0, digits = 0, value = 0;
    for (char c : s) {
        if (c == '.') {
            if (!digits) return false;
            ++parts;
            digits = value = 0;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            if (++digits > 3 || value > 255) return false;
        } else {
            return false;
        }
    }
    return parts == 3 && digits > 0;
}

bool host_name_ok(std::string_view s) {
    if (s.empty() || s.size() > 253) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    return s.front() != '.' && s.front() != '-';
}

// Reads server.cfg into `o`. False and `why` (with the line) on the first value it cannot take.
bool read_config(const fs::path& file, lsfs::ServerOptions& o, int& log_days, std::string* why) {
    std::string text;
    if (!read_text(file, text)) {
        *why = "server.cfg is missing";
        return false;
    }
    eng::ConfigFile cfg;
    std::string err;
    if (!eng::ConfigFile::parse(text, cfg, &err)) {
        *why = "server.cfg: " + err;
        return false;
    }
    if (cfg.sections.size() > 1 || (!cfg.sections.empty() && !cfg.sections[0].name.empty())) {
        *why = "server.cfg has a [section]; it takes only keys";
        return false;
    }
    static const Range kRanges[] = {
        {"port", 1, 65535},          {"public_port", 0, 65535},  {"max_players", 1, 512}, {"reserved_slots", 0, 32}, {"min_rank", 0, 74},
        {"max_rank", 0, 74},         {"upload_kbps", 64, 1000000}, {"max_downloaders", 1, 64}, {"log_days", 1, 365},
    };
    for (const auto& [key, value] : cfg.root().values) {
        const std::string k = eng::str::lower(key);
        const std::string v(eng::str::trim(value));
        auto bad = [&](const std::string& what) {
            *why = "server.cfg: " + k + " = " + v + ": " + what;
            return false;
        };
        long long n = 0;
        const Range* range = nullptr;
        for (const Range& r : kRanges)
            if (k == r.key) range = &r;
        if (range) {
            if (v.empty() && (k == "public_port")) continue;
            if (!to_int(v, n) || n < range->lo || n > range->hi) return bad(eng::str::format("a whole number from %lld to %lld", range->lo, range->hi));
        }
        if (k == "server_id") {
            if (v.empty()) continue;
            if (!to_int(v, n) || n <= 0) return bad("the number the registration gave");
            o.server_id = u64(n);
        } else if (k == "name") {
            if (v.size() < 3 || v.size() > 48) return bad("3 to 48 characters");
            o.name = v;
        } else if (k == "motd") {
            if (v.size() > 512) return bad("at most 512 characters (ML-8)");
            o.motd = v;
        } else if (k == "region") {
            if (v.size() > 24) return bad("at most 24 characters");
            o.region = v;
        } else if (k == "bind_address") {
            if (!v.empty() && !ipv4_literal(v)) return bad("an IPv4 address of this machine (DS-7), or empty for all of them");
            o.bind_address = v;
        } else if (k == "port") {
            o.port = u16(n);
        } else if (k == "public_address") {
            if (!v.empty() && !ipv4_literal(v) && !host_name_ok(v)) return bad("an IPv4 address or a host name (sf.example.com)");
            o.public_address = v;
        } else if (k == "public_port") {
            o.public_port = u16(n);
        } else if (k == "max_players") {
            o.max_players = u32(n);
        } else if (k == "reserved_slots") {
            o.reserved_slots = u32(n);
        } else if (k == "min_rank") {
            o.min_rank = int(n);
        } else if (k == "max_rank") {
            o.max_rank = int(n);
        } else if (k == "min_kd") {
            double f = 0;
            if (!to_float(v, f) || f < 0 || f > 100) return bad("a number from 0 to 100");
            o.min_kd = float(f);
        } else if (k == "password") {
            if (v.size() > 32) return bad("at most 32 characters");
            o.password = v;
        } else if (k == "listed") {
            bool b = true;
            if (!to_bool(v, b)) return bad("yes or no");
            o.listed = b;
        } else if (k == "client_data") {
            if (!v.empty()) o.data = fs::path(eng::str::widen(v)).is_absolute() ? fs::path(eng::str::widen(v)) : g_dir / eng::str::widen(v);
        } else if (k == "upload_kbps") {
            o.upload_kbps = u32(n);
        } else if (k == "max_downloaders") {
            o.max_downloaders = u32(n);
        } else if (k == "log_days") {
            log_days = int(n);
        } else {
            *why = "server.cfg: " + k + " is not a setting (a misspelling?)";
            return false;
        }
    }
    if (o.max_rank < o.min_rank) {
        *why = "server.cfg: max_rank is below min_rank";
        return false;
    }
    if (o.reserved_slots >= o.max_players) {
        *why = "server.cfg: reserved_slots must be fewer than max_players";
        return false;
    }
    return true;
}

// Writes `key = value` into server.cfg, keeping every other line as it was (DS-4: atomically).
bool write_setting(const fs::path& file, std::string_view key, const std::string& value) {
    std::string text;
    if (!read_text(file, text)) text = kDefaultConfig;
    std::string out;
    bool done = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        const std::string t = eng::str::lower(eng::str::trim(line));
        if (!done && t.rfind(key, 0) == 0 && t.find('=') != std::string::npos && eng::str::trim(t.substr(0, t.find('='))) == key) {
            line = std::string(key) + " = " + value;
            done = true;
        }
        out += line;
        if (end < text.size()) out += '\n';
        at = end + 1;
    }
    if (!done) out = std::string(key) + " = " + value + "\n" + out;
    return eng::fs::write_file(file, out.data(), out.size());
}

bool write_server_id(const fs::path& file, u64 id) { return write_setting(file, "server_id", std::to_string(id)); }

// ── --register CODE (§8.1 step 2) ──────────────────────────────────────────────

int register_server(const std::string& tvas, const std::string& code, const lsf::tvas::ServerKey& key) {
    eng::json::Value body = eng::json::Value::object();
    body["code"] = code;
    body["pubkey"] = public_hex(key);
    eng::net::HttpRequest r;
    r.method = "POST";
    r.url = lsf::tvas::url(tvas, "/v1/server/redeem");
    r.headers.emplace_back("Content-Type", "application/json");
    r.body = body.dump();
    const auto res = eng::net::http_request(r);
    eng::json::Value reply;
    const bool parsed = eng::json::parse(res.body, reply);
    if (!res.ok()) {
        const eng::json::Value& r2 = reply;
        const std::string why = parsed && r2.is_object() && r2.has("error") ? r2["error"].str() :
                                !res.error.empty()                                ? "Team Vanilla cannot be reached (" + res.error + ")" :
                                                                                    eng::str::format("Team Vanilla answered %d", res.status);
        return refuse("the registration failed: " + why);
    }
    const eng::json::Value& got = reply;
    const u64 id = parsed ? got["server_id"].as_uint() : 0;
    if (!id) return refuse("Team Vanilla's answer to the registration cannot be read.");
    if (!write_server_id(g_dir / "server.cfg", id)) return refuse("server.cfg cannot be written.");
    const std::string owner = got["owner"].str();
    say(eng::str::format("Registered: \"%s\" is server %llu%s. Its id is in server.cfg; start the server without --register now.", got["name"].str().c_str(),
                         (unsigned long long)id, owner.empty() ? "" : (", owned by " + owner).c_str()));
    eng::log::shutdown();
    return 0;
}

// ── The Windows firewall (§12.4): the first run offers to allow the server in ────

#ifdef _WIN32
const wchar_t* kRuleName = L"Soldier Front Legacy server";

bool firewall_rule_present() {
    std::wstring cmd = L"netsh advfirewall firewall show rule name=\"" + std::wstring(kRuleName) + L"\"";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(0);
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return true;   // cannot tell: never nag
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

// Asks Windows (an administrator prompt) to add the rule: this program, UDP in.
bool add_firewall_rule() {
    wchar_t exe[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    const std::wstring args = L"advfirewall firewall add rule name=\"" + std::wstring(kRuleName) + L"\" dir=in action=allow protocol=UDP program=\"" +
                              std::wstring(exe, n) + L"\"";
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"netsh";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;
    WaitForSingleObject(sei.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code == 0;
}

void offer_firewall_rule(u16 port) {
    const fs::path asked = g_dir / "logs" / "firewall-asked";
    std::error_code ec;
    if (fs::exists(asked, ec) || firewall_rule_present()) return;
    const std::string mark = "asked\n";
    eng::fs::write_file(asked, mark.data(), mark.size());
    if (!interactive()) {
        say("The Windows firewall may keep players out. Run LegacySFServer.exe --firewall once to allow it in (README: The Windows firewall).");
        return;
    }
    std::fprintf(stderr, "\nThe Windows firewall may keep players out of this server (UDP %u).\nAllow LegacySFServer.exe in now? Windows will ask for an administrator. [y/N] ",
                 unsigned(port));
    int c = std::getchar();
    if (c == 'y' || c == 'Y') say(add_firewall_rule() ? "The firewall now lets the server in." : "The firewall rule was not added (README: The Windows firewall).");
    else say("No firewall rule added. LegacySFServer.exe --firewall adds it later.");
}
#endif

// logs/: one file per start, those older than log_days deleted.
void prune_logs(const fs::path& dir, int days) {
    std::error_code ec;
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * days);
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".log") continue;
        const auto t = fs::last_write_time(e.path(), ec);
        if (!ec && t < cutoff) fs::remove(e.path(), ec);
    }
}

std::string log_name() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "server-%Y%m%d-%H%M%S.log", &tm);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    g_dir = eng::fs::executable_directory();
    std::string tvas = lsf::tvas::kTvasAddress, register_code;
    bool check_only = false, print_key = false, firewall = false, loopback = false, defaults = false;
    fs::path config = g_dir / "server.cfg";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--register") register_code = next();
        else if (a == "--key") print_key = true;
        else if (a == "--check") check_only = true;
        else if (a == "--defaults") defaults = true;
        else if (a == "--firewall") firewall = true;
        else if (a == "--config") config = fs::path(eng::str::widen(next()));
        else if (a == "--dir") g_dir = fs::path(eng::str::widen(next())), config = g_dir / "server.cfg";
        else if (a == "--tvas") tvas = next();   // tests: a TVAS on this machine
        else if (a == "--loopback") loopback = true;
        else if (a == "--help" || a == "-h") {
            std::printf("%s [--register CODE] [--key] [--check] [--firewall] [--defaults]\n"
                        "Settings are in server.cfg beside the server; README.txt explains each.\n",
                        exe_name());
            return 0;
        } else {
            std::fprintf(stderr, "Unknown option %s (--help lists them).\n", a.c_str());
            return 1;
        }
    }
    eng::fs::set_executable_directory(g_dir);
    std::error_code ec;
    // A new server's folder (§12.1), as the release ships it: the settings to fill in, the original
    // thirteen channels, an empty staff list and the folders. Never a key: each server makes its own
    // on its first run (server.key is its identity and is never shared).
    if (defaults) {
        if (!fs::exists(config, ec) && !eng::fs::write_file(config, kDefaultConfig, std::strlen(kDefaultConfig))) return refuse("server.cfg cannot be written");
        lsfs::ServerOptions o;
        int days = 14;
        std::string why;
        if (!read_config(config, o, days, &why)) return refuse(why);
        if (!fs::exists(g_dir / "channels.cfg", ec)) {
            auto channels = lsf::default_channels();
            for (lsf::ChannelDef& c : channels) c.capacity = u16(std::min<u32>(c.capacity, o.max_players));
            if (!lsfs::write_channels_file(g_dir / "channels.cfg", channels, 1)) return refuse("channels.cfg cannot be written");
        }
        if (!fs::exists(g_dir / "staff.cfg", ec)) {
            const std::string staff = "# This server's staff by Team Vanilla account id (the Owner is the account that registered the server)\n"
                                      "# role = admin | game_master | moderator. Named in the game (F9, Accounts) or here:\n"
                                      "#   [staff]\n#   account = 1234\n#   code_name = Deputy\n#   role = admin\n";
            if (!eng::fs::write_file(g_dir / "staff.cfg", staff.data(), staff.size())) return refuse("staff.cfg cannot be written");
        }
        if (!fs::exists(g_dir / "shop.cfg", ec)) {
            // The game's own catalog, as the first start would write it (§6.3: the Owner's to change).
            lsf::ShopConfig shop = lsf::default_shop();
            lsf::sanitize(shop);
            const std::string text = lsf::shop_text(shop);
            if (!eng::fs::write_file(g_dir / "shop.cfg", text.data(), text.size())) return refuse("shop.cfg cannot be written");
        }
        for (const char* d : {"accounts", "weapons", "maps", "chars", "packs", "recordings", "logs"}) fs::create_directories(g_dir / d, ec);
        say("The server's folder is laid out: server.cfg, channels.cfg, staff.cfg, shop.cfg and its folders. Its key is made on its first run.");
        return 0;
    }
    fs::create_directories(g_dir / "logs", ec);
    eng::log::init(eng::str::narrow((g_dir / "logs" / log_name()).wstring()), false);
    eng::log::set_listener([](eng::log::Level, const std::string& line) {
        if (g_echo) std::fprintf(stderr, "%s\n", line.c_str());
    });
    eng::install_crash_handler();
    eng::net::set_user_agent(eng::str::format("LegacySFServer/%u", unsigned(lsf::kBuildNumber)));
    LOG_INFO("Soldier Front Legacy server, build %u (protocol %u), %s", unsigned(lsf::kBuildNumber), unsigned(lsf::kProtocolVersion), eng::build_id().c_str());

    // First run: a server.cfg to fill in, and the key pair.
    if (!fs::exists(config, ec)) {
        if (!eng::fs::write_file(config, kDefaultConfig, std::strlen(kDefaultConfig))) return refuse("server.cfg cannot be written in " + eng::str::narrow(g_dir.wstring()));
        say("Made server.cfg with the defaults: give the server its name there.");
    }
    lsf::tvas::ServerKey key;
    bool made = false;
    std::string why;
    if (!load_or_make_key(key, &made, &why)) return refuse(why);
    if (made) say("Made this server's key: server.key. Never share it or send it anywhere; it is this server's identity.");
    if (print_key) {
        std::printf("%s\n", public_hex(key).c_str());
        eng::log::shutdown();
        return 0;
    }
#ifdef _WIN32
    if (firewall) {
        say(add_firewall_rule() ? "The firewall now lets the server in." : "The firewall rule was not added.");
        eng::log::shutdown();
        return 0;
    }
#else
    if (firewall) {
        say("On Linux the firewall is the system's own: allow UDP on the server's port (README: Linux).");
        eng::log::shutdown();
        return 0;
    }
#endif
    if (!register_code.empty()) return register_server(tvas, register_code, key);

    lsfs::ServerOptions opts;
    int log_days = 14;
    if (!read_config(config, opts, log_days, &why)) return refuse(why);
    prune_logs(g_dir / "logs", log_days);
    if (!opts.server_id) {
        return refuse("server.cfg has no server_id. Register the server on the UCP (teamvanilla.dev), then run\n  " + std::string(exe_name()) +
                      " --register CODE\nwith the code it gives, or paste this public key into the registration:\n  " + public_hex(key));
    }
    opts.tvas = tvas;
    opts.key = key;
    opts.loopback_only = loopback;
    if (opts.data.empty()) opts.data = g_dir / "data";
    opts.accounts_dir = g_dir / "accounts";
    opts.staff_file = g_dir / "staff.cfg";
    opts.channels_file = g_dir / "channels.cfg";
    opts.shop = g_dir / "shop.cfg";
    opts.packs_dir = g_dir / "packs";
    opts.recordings = g_dir / "recordings";
    opts.require_data = true;
    // The Owner's message of the day, changed in the game, kept for the next start (quoted: the
    // line's own # and // stay text).
    opts.on_motd = [config](const std::string& motd) {
        if (!write_setting(config, "motd", "\"" + motd + "\"")) LOG_WARN("server.cfg: the new message of the day could not be written");
    };
    if (check_only) {
        // A check talks to nobody: no heartbeat, nothing listed.
        opts.require_identity = false;
        opts.server_id = 0;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    eng::time::TimerResolution timer;
    lsfs::Server server;
    g_echo = true;
    if (!server.start(opts, &why)) {
        g_echo = false;
        return refuse(why);
    }
    if (check_only) {
        say("server.cfg, the data and the packs check out.");
        server.stop();
        eng::log::shutdown();
        return 0;
    }
#ifdef _WIN32
    if (!loopback) offer_firewall_rule(server.port());
#endif
    say(eng::str::format("Running on UDP %u. Ctrl+C stops it.", unsigned(server.port())));
    while (g_run) {
        server.tick(eng::time::now());
        eng::time::sleep_precise(1.0 / 120.0);
    }
    LOG_INFO("Stopping");
    server.stop();
    eng::log::shutdown();
    return 0;
}
