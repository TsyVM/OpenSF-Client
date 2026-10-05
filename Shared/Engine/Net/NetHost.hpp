// Connection management on top of Connection: a server that accepts many peers on
// one UDP port, and a client that connects to one server. (TacticalFPS's.) The server's socket
// also carries datagrams that are not ours to connect (a lobby's replies, NAT punches): those
// go to `on_foreign`, and `send_unconnected` sends one.
#pragma once

#include "Engine/Net/Connection.hpp"
#include "Engine/Net/Socket.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eng::net {

struct NetEvent {
    enum class Type { Connected, Disconnected, Message };
    Type type = Type::Message;
    u32 peer = 0;                 // server side: the peer id; client side: 0
    std::vector<u8> data;         // Message
    std::string reason;           // Disconnected
};

// What one machine may do to the server before anything knows who is on it. The connection caps
// are counted per IP rather than per peer, because a peer costs nothing to make and a limit per
// peer limits nothing. The rates are per peer, because by then there is one to hold to account.
struct NetLimits {
    u32 peers_per_address = 16;     // live connections from one IP; 0 = no cap
    u32 connects_per_minute = 30;   // connections one IP may open in any sixty seconds; 0 = no cap
    // What a connected peer may deliver: a steady rate and a burst it may borrow against. A client
    // sends one command message per tick (64 a second) and little else, and about as many
    // datagrams, so the defaults leave it four times that. 0 turns a rate off.
    float messages_per_second = 256.0f;
    float message_burst = 768.0f;
    float datagrams_per_second = 256.0f;
    float datagram_burst = 768.0f;
    // Loopback is the operator's own machine -- the bots, a local client, a second server -- and
    // the connection caps are there for strangers. A test fixture turns this on to prove the caps
    // work; the rates apply to everyone regardless.
    bool limit_loopback = false;
};

class NetServer {
public:
    ~NetServer();

    void set_limits(const NetLimits& limits) { limits_ = limits; }
    // `bind_ip` (host order): one address of this machine to listen on (0: every one).
    bool start(u16 port, u32 max_peers, u32 game_version, u32 bind_ip = 0);
    void stop();

    // Receives datagrams, handles handshakes and timeouts, returns events in order.
    void poll(double now, std::vector<NetEvent>& events);
    // Sends whatever each connection has queued.
    void flush(double now);

    void send(u32 peer, std::vector<u8> message, bool reliable);
    // Tells the peer why, forgets it, and reports a Disconnected event on the next poll.
    void disconnect(u32 peer, std::string_view reason);

    bool connected(u32 peer) const { return peers_.count(peer) != 0; }
    Address address(u32 peer) const;
    const ConnectionStats* stats(u32 peer) const;
    // Reliable messages queued to a peer and not yet acknowledged. A bulk transfer sends its next
    // piece only while this is low, so it never delays the game's own messages behind it.
    size_t pending_reliable(u32 peer) const;
    size_t peer_count() const { return peers_.size(); }
    u16 port() const { return socket_.local_port(); }
    bool running() const { return started_; }

    // A datagram with another protocol's magic that arrived on the server's port.
    std::function<void(const Address& from, std::span<const u8> datagram)> on_foreign;
    // A datagram straight out of the server's port (so a NAT maps it as the game's own).
    void send_unconnected(const Address& to, std::span<const u8> datagram) { socket_.send(to, datagram.data(), datagram.size()); }
    // Peers reached through a relay (UdpSocket::set_relay).
    void set_relay(const Address& via) { socket_.set_relay(via); }

private:
    struct Peer {
        u32 id = 0;
        Address address;
        u32 nonce = 0;
        std::unique_ptr<Connection> connection;
        // Token buckets for the two rates in NetLimits, refilled on arrival rather than by a timer.
        float message_tokens = 0;
        float datagram_tokens = 0;
        double tokens_at = 0;
    };
    // One IP's recent history: when it opened its connections, so the per-minute cap can be read
    // off a window rather than a counter that has to be reset by something.
    struct AddressRecord {
        std::vector<double> connects;
        u32 live = 0;
    };

    void send_raw(const Address& to, const std::vector<u8>& datagram);
    void drop(u32 peer, std::string reason, bool notify_remote);
    // Empty when this address may open another connection now; otherwise why not.
    const char* refuse_connect(const Address& from, double now);
    // Spends one datagram's tokens and `messages` message tokens. False once either runs dry.
    bool spend(Peer& peer, int messages, double now);

    UdpSocket socket_;
    bool started_ = false;
    u32 max_peers_ = 0;
    u32 version_ = 0;
    u32 next_id_ = 1;
    u64 key_seed_ = 0;
    NetLimits limits_;
    std::unordered_map<u32, Peer> peers_;
    std::unordered_map<Address, u32, AddressHash> by_address_;
    std::unordered_map<u32, AddressRecord> by_ip_;
    double last_prune_ = 0;
    std::vector<NetEvent> deferred_;
};

class NetClient {
public:
    enum class State { Idle, Connecting, Connected, Disconnected };

    ~NetClient();

    // Which local address to send from; 0 (the default) lets the system choose. Test tools use it
    // to be several machines at once -- 127.0.0.2, 127.0.0.3 -- which is the only way to prove
    // that something counted per address is counted per address.
    void set_local_ip(u32 ip) { local_ip_ = ip; }
    bool connect(const Address& server, u32 game_version, double now);
    void disconnect(std::string_view reason = "left");
    // The socket opened ahead of connecting, so datagrams sent from it first (a lobby's join
    // request) come from the port the connection will use: the port a NAT has mapped.
    bool open_socket();
    void send_unconnected(const Address& to, std::span<const u8> datagram) { socket_.send(to, datagram.data(), datagram.size()); }
    // The host reached through a relay (UdpSocket::set_relay).
    void set_relay(const Address& via) { socket_.set_relay(via); }
    // A datagram with another protocol's magic (the lobby's answer), whatever the state.
    std::function<void(const Address& from, std::span<const u8> datagram)> on_foreign;

    // Connected / Disconnected / Message events. A failed connect reports Disconnected.
    void poll(double now, std::vector<NetEvent>& events);
    void flush(double now);
    void send(std::vector<u8> message, bool reliable);

    State state() const { return state_; }
    const ConnectionStats* stats() const { return connection_ ? &connection_->stats() : nullptr; }
    const Address& server() const { return server_; }

private:
    void send_raw(const std::vector<u8>& datagram);
    void fail(std::vector<NetEvent>& events, std::string reason);

    UdpSocket socket_;
    bool net_started_ = false;
    u32 local_ip_ = 0;
    State state_ = State::Idle;
    Address server_;
    u32 version_ = 0;
    u32 nonce_ = 0;
    double connect_started_ = 0;
    double last_connect_send_ = -1;
    std::unique_ptr<Connection> connection_;
    std::vector<std::vector<u8>> queued_;   // messages sent before the handshake completed
};

}  // namespace eng::net
