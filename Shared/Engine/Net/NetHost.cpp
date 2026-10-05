#include "Engine/Net/NetHost.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

namespace eng::net {

namespace {

constexpr double kConnectTimeout = 6.0;
constexpr double kConnectResend = 0.25;

u64 entropy() {
    static std::random_device device;
    const u64 clock = u64(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const u64 thread = u64(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return (u64(device()) << 32 ^ u64(device())) ^ clock * 0x9E3779B97F4A7C15ull ^ thread;
}

// SplitMix64: one well-mixed number from a seed.
u64 mix(u64 x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::vector<u8> header(PacketType type) {
    ByteWriter w(64);
    w.u32(kProtocolMagic);
    w.u8(u8(type));
    return w.take();
}

}  // namespace

// ── Server ─────────────────────────────────────────────────────────────────────

NetServer::~NetServer() { stop(); }

bool NetServer::start(u16 port, u32 max_peers, u32 game_version, u32 bind_ip) {
    stop();
    if (!startup()) return false;
    started_ = true;
    if (!socket_.open(port, bind_ip)) {
        stop();
        return false;
    }
    max_peers_ = max_peers;
    version_ = game_version;
    key_seed_ = entropy();
    return true;
}

void NetServer::stop() {
    if (!started_) return;
    for (auto& [id, peer] : peers_) {
        ByteWriter w;
        w.raw(header(PacketType::Disconnect).data(), 5);
        w.u32(peer.connection->session_key());
        w.string("server shutting down");
        for (int i = 0; i < 3; ++i) send_raw(peer.address, w.data());
    }
    peers_.clear();
    by_address_.clear();
    by_ip_.clear();
    socket_.close();
    shutdown();
    started_ = false;
}

void NetServer::send_raw(const Address& to, const std::vector<u8>& datagram) {
    socket_.send(to, datagram.data(), datagram.size());
}

void NetServer::drop(u32 id, std::string reason, bool notify_remote) {
    auto it = peers_.find(id);
    if (it == peers_.end()) return;
    if (notify_remote) {
        ByteWriter w;
        w.u32(kProtocolMagic);
        w.u8(u8(PacketType::Disconnect));
        w.u32(it->second.connection->session_key());
        w.string(reason);
        for (int i = 0; i < 3; ++i) send_raw(it->second.address, w.data());
    }
    if (auto rec = by_ip_.find(it->second.address.ip); rec != by_ip_.end() && rec->second.live > 0)
        --rec->second.live;
    by_address_.erase(it->second.address);
    peers_.erase(it);
    NetEvent e;
    e.type = NetEvent::Type::Disconnected;
    e.peer = id;
    e.reason = std::move(reason);
    deferred_.push_back(std::move(e));
}

void NetServer::disconnect(u32 peer, std::string_view reason) { drop(peer, std::string(reason), true); }

const char* NetServer::refuse_connect(const Address& from, double now) {
    if (is_loopback(from.ip) && !limits_.limit_loopback) return nullptr;
    auto it = by_ip_.find(from.ip);
    if (it == by_ip_.end()) return nullptr;
    AddressRecord& rec = it->second;
    if (limits_.peers_per_address && rec.live >= limits_.peers_per_address)
        return "too many connections from your address";
    if (limits_.connects_per_minute) {
        std::erase_if(rec.connects, [&](double t) { return now - t >= 60.0; });
        if (rec.connects.size() >= limits_.connects_per_minute)
            return "connecting too often: wait a minute and try again";
    }
    return nullptr;
}

bool NetServer::spend(Peer& p, int messages, double now) {
    const float dt = float(std::max(0.0, now - p.tokens_at));
    p.tokens_at = now;
    bool ok = true;
    if (limits_.datagrams_per_second > 0.0f) {
        p.datagram_tokens = std::min(limits_.datagram_burst, p.datagram_tokens + dt * limits_.datagrams_per_second);
        p.datagram_tokens -= 1.0f;
        ok &= p.datagram_tokens >= 0.0f;
    }
    if (limits_.messages_per_second > 0.0f) {
        p.message_tokens = std::min(limits_.message_burst, p.message_tokens + dt * limits_.messages_per_second);
        p.message_tokens -= float(messages);
        ok &= p.message_tokens >= 0.0f;
    }
    return ok;
}

void NetServer::poll(double now, std::vector<NetEvent>& events) {
    for (auto& e : deferred_) events.push_back(std::move(e));
    deferred_.clear();

    u8 buffer[2048];
    Address from;
    int got;
    while ((got = socket_.receive(from, buffer, sizeof(buffer))) >= 0) {
        ByteReader r(buffer, size_t(got));
        if (r.u32() != kProtocolMagic) {
            if (on_foreign && got >= 4) on_foreign(from, std::span<const u8>(buffer, size_t(got)));
            continue;
        }
        PacketType type = PacketType(r.u8());
        if (!r.ok()) continue;

        auto known = by_address_.find(from);
        switch (type) {
            case PacketType::Connect: {
                u32 nonce = r.u32();
                u32 version = r.u32();
                if (!r.ok()) break;
                if (known != by_address_.end()) {
                    Peer& p = peers_[known->second];
                    if (p.nonce == nonce) {
                        // Our Accept was lost; repeat it.
                        ByteWriter w;
                        w.u32(kProtocolMagic);
                        w.u8(u8(PacketType::Accept));
                        w.u32(nonce);
                        w.u32(p.connection->session_key());
                        send_raw(from, w.data());
                        break;
                    }
                    // Same address, new client instance: the old session is gone.
                    drop(p.id, "reconnected", false);
                    for (auto& e : deferred_) events.push_back(std::move(e));
                    deferred_.clear();
                }
                auto deny = [&](const char* why) {
                    ByteWriter w;
                    w.u32(kProtocolMagic);
                    w.u8(u8(PacketType::Deny));
                    w.u32(nonce);
                    w.string(why);
                    send_raw(from, w.data());
                };
                // Two different answers, never one "mismatch" (DS-6): whose copy is behind.
                if (version < version_) {
                    deny("Your game is older than this server's: update the game.");
                    break;
                }
                if (version > version_) {
                    deny("This server is out of date (your game is newer): it cannot be joined until its owner updates it.");
                    break;
                }
                if (peers_.size() >= max_peers_) {
                    deny("server is full");
                    break;
                }
                if (const char* why = refuse_connect(from, now)) {
                    deny(why);
                    break;
                }
                Peer p;
                p.id = next_id_++;
                p.address = from;
                p.nonce = nonce;
                p.message_tokens = limits_.message_burst;
                p.datagram_tokens = limits_.datagram_burst;
                p.tokens_at = now;
                const u32 key = u32(mix(key_seed_ ^ (u64(nonce) << 17) ^ entropy()) >> 32) | 1;
                p.connection = std::make_unique<Connection>(key, now);
                AddressRecord& rec = by_ip_[from.ip];
                ++rec.live;
                rec.connects.push_back(now);
                ByteWriter w;
                w.u32(kProtocolMagic);
                w.u8(u8(PacketType::Accept));
                w.u32(nonce);
                w.u32(key);
                send_raw(from, w.data());
                by_address_[from] = p.id;
                u32 id = p.id;
                peers_.emplace(id, std::move(p));
                NetEvent e;
                e.type = NetEvent::Type::Connected;
                e.peer = id;
                events.push_back(std::move(e));
                LOG_INFO("Peer %u connected", id);
                break;
            }
            case PacketType::Data: {
                if (known == by_address_.end()) break;
                Peer& p = peers_[known->second];
                if (r.u32() != p.connection->session_key() || !r.ok()) break;
                p.connection->receive(r, size_t(got), now);
                // Collected before anything is handed on, so a peer that has just broken a rule
                // is dropped without the server acting on what it sent in the same breath.
                std::vector<std::vector<u8>> arrived;
                std::vector<u8> message;
                while (p.connection->pop(message)) arrived.push_back(std::move(message));
                const char* why = p.connection->violation();
                if (!why && !spend(p, int(arrived.size()), now)) why = "flooding";
                if (why) {
                    LOG_WARN("Peer %u dropped: %s", p.id, why);
                    drop(p.id, why, true);
                    for (auto& e : deferred_) events.push_back(std::move(e));
                    deferred_.clear();
                    break;
                }
                for (auto& m : arrived) {
                    NetEvent e;
                    e.type = NetEvent::Type::Message;
                    e.peer = p.id;
                    e.data = std::move(m);
                    events.push_back(std::move(e));
                }
                break;
            }
            case PacketType::Disconnect: {
                if (known == by_address_.end()) break;
                Peer& p = peers_[known->second];
                if (r.u32() != p.connection->session_key() || !r.ok()) break;
                std::string reason = r.string(256);
                drop(p.id, reason.empty() ? "left" : reason, false);
                break;
            }
            default:
                break;
        }
    }

    std::vector<u32> timed_out;
    for (auto& [id, p] : peers_)
        if (now - p.connection->last_receive() > kTimeoutSeconds) timed_out.push_back(id);
    for (u32 id : timed_out) drop(id, "timed out", false);

    // Forget addresses that have gone quiet, so the table is as big as the recent traffic and no
    // bigger.
    if (now - last_prune_ > 10.0) {
        last_prune_ = now;
        std::erase_if(by_ip_, [&](auto& kv) {
            AddressRecord& rec = kv.second;
            std::erase_if(rec.connects, [&](double t) { return now - t >= 60.0; });
            return rec.live == 0 && rec.connects.empty();
        });
    }

    for (auto& e : deferred_) events.push_back(std::move(e));
    deferred_.clear();
}

void NetServer::flush(double now) {
    std::vector<std::vector<u8>> datagrams;
    for (auto& [id, p] : peers_) {
        datagrams.clear();
        p.connection->write(now, datagrams);
        for (const auto& d : datagrams) send_raw(p.address, d);
    }
}

void NetServer::send(u32 peer, std::vector<u8> message, bool reliable) {
    auto it = peers_.find(peer);
    if (it != peers_.end()) it->second.connection->send(std::move(message), reliable);
}

Address NetServer::address(u32 peer) const {
    auto it = peers_.find(peer);
    return it == peers_.end() ? Address{} : it->second.address;
}

const ConnectionStats* NetServer::stats(u32 peer) const {
    auto it = peers_.find(peer);
    return it == peers_.end() ? nullptr : &it->second.connection->stats();
}

size_t NetServer::pending_reliable(u32 peer) const {
    auto it = peers_.find(peer);
    return it == peers_.end() ? 0 : it->second.connection->pending_reliable();
}

// ── Client ─────────────────────────────────────────────────────────────────────

NetClient::~NetClient() {
    disconnect("closed");
    if (net_started_) shutdown();
}

bool NetClient::open_socket() {
    if (!net_started_) {
        if (!startup()) return false;
        net_started_ = true;
    }
    return socket_.is_open() || socket_.open(0, local_ip_);
}

bool NetClient::connect(const Address& server, u32 game_version, double now) {
    // A connection already up goes; the socket stays (it may be the one a NAT has mapped).
    if (state_ == State::Connected && connection_) {
        ByteWriter w;
        w.u32(kProtocolMagic);
        w.u8(u8(PacketType::Disconnect));
        w.u32(connection_->session_key());
        w.string("reconnecting");
        for (int i = 0; i < 3; ++i) send_raw(w.data());
    }
    connection_.reset();
    queued_.clear();
    if (!open_socket()) return false;
    server_ = server;
    version_ = game_version;
    nonce_ = u32(entropy() * 0x2545F4914F6CDD1Dull >> 32) | 1;
    state_ = State::Connecting;
    connect_started_ = now;
    last_connect_send_ = -1;
    connection_.reset();
    queued_.clear();
    return true;
}

void NetClient::send_raw(const std::vector<u8>& datagram) { socket_.send(server_, datagram.data(), datagram.size()); }

void NetClient::disconnect(std::string_view reason) {
    if (state_ == State::Connected && connection_) {
        ByteWriter w;
        w.u32(kProtocolMagic);
        w.u8(u8(PacketType::Disconnect));
        w.u32(connection_->session_key());
        w.string(reason);
        for (int i = 0; i < 3; ++i) send_raw(w.data());
    }
    connection_.reset();
    socket_.close();
    queued_.clear();
    if (state_ != State::Idle) state_ = State::Disconnected;
}

void NetClient::fail(std::vector<NetEvent>& events, std::string reason) {
    connection_.reset();
    socket_.close();
    state_ = State::Disconnected;
    NetEvent e;
    e.type = NetEvent::Type::Disconnected;
    e.reason = std::move(reason);
    events.push_back(std::move(e));
}

void NetClient::poll(double now, std::vector<NetEvent>& events) {
    u8 buffer[2048];
    Address from;
    int got;
    while (socket_.is_open() && (got = socket_.receive(from, buffer, sizeof(buffer))) >= 0) {
        ByteReader r(buffer, size_t(got));
        if (r.u32() != kProtocolMagic) {
            if (on_foreign && got >= 4) on_foreign(from, std::span<const u8>(buffer, size_t(got)));
            continue;
        }
        if (!(from == server_) || (state_ != State::Connecting && state_ != State::Connected)) continue;
        PacketType type = PacketType(r.u8());
        switch (type) {
            case PacketType::Accept: {
                u32 nonce = r.u32();
                u32 key = r.u32();
                if (!r.ok() || nonce != nonce_ || state_ != State::Connecting) break;
                connection_ = std::make_unique<Connection>(key, now);
                state_ = State::Connected;
                for (auto& m : queued_) connection_->send(std::move(m), true);
                queued_.clear();
                NetEvent e;
                e.type = NetEvent::Type::Connected;
                events.push_back(std::move(e));
                break;
            }
            case PacketType::Deny: {
                u32 nonce = r.u32();
                std::string reason = r.string(256);
                if (nonce != nonce_ || state_ != State::Connecting) break;
                fail(events, reason.empty() ? "connection refused" : reason);
                return;
            }
            case PacketType::Data: {
                if (!connection_ || r.u32() != connection_->session_key() || !r.ok()) break;
                connection_->receive(r, size_t(got), now);
                std::vector<u8> message;
                while (connection_->pop(message)) {
                    NetEvent e;
                    e.type = NetEvent::Type::Message;
                    e.data = std::move(message);
                    events.push_back(std::move(e));
                }
                break;
            }
            case PacketType::Disconnect: {
                if (!connection_ || r.u32() != connection_->session_key()) break;
                std::string reason = r.string(256);
                fail(events, reason.empty() ? "disconnected by server" : reason);
                return;
            }
            default:
                break;
        }
    }

    if (state_ != State::Connecting && state_ != State::Connected) return;
    if (state_ == State::Connecting) {
        if (now - connect_started_ > kConnectTimeout) {
            fail(events, "no response from server");
            return;
        }
        if (last_connect_send_ < 0 || now - last_connect_send_ >= kConnectResend) {
            ByteWriter w;
            w.u32(kProtocolMagic);
            w.u8(u8(PacketType::Connect));
            w.u32(nonce_);
            w.u32(version_);
            send_raw(w.data());
            last_connect_send_ = now;
        }
    } else if (state_ == State::Connected && now - connection_->last_receive() > kTimeoutSeconds) {
        fail(events, "lost connection to server");
    }
}

void NetClient::flush(double now) {
    if (state_ != State::Connected || !connection_) return;
    std::vector<std::vector<u8>> datagrams;
    connection_->write(now, datagrams);
    for (const auto& d : datagrams) send_raw(d);
}

void NetClient::send(std::vector<u8> message, bool reliable) {
    if (state_ == State::Connected && connection_)
        connection_->send(std::move(message), reliable);
    else if (state_ == State::Connecting && reliable)
        queued_.push_back(std::move(message));
}

}  // namespace eng::net
