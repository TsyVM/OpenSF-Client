// UDP sockets and addresses (IPv4), the same on Windows (Winsock) and Android (BSD sockets).
// Ported from TacticalFPS's net layer; broadcast and the machine's own addresses added for
// finding games on the local network.
#pragma once

#include "Engine/Core/Types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace eng::net {

struct Address {
    u32 ip = 0;     // host byte order
    u16 port = 0;

    std::string to_string() const;
    std::string ip_string() const;
    bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
    bool valid() const { return port != 0; }

    // "host", "host:port", "1.2.3.4" — `default_port` applies when none is given.
    static std::optional<Address> resolve(const std::string& host_and_port, u16 default_port);
    static constexpr u32 kBroadcast = 0xFFFFFFFFu;
};

struct AddressHash {
    size_t operator()(const Address& a) const { return (size_t(a.ip) << 16) ^ a.port; }
};

inline bool is_loopback(u32 ip) { return (ip >> 24) == 127; }

// Relaying: someone reached through a server that passes datagrams on (a game's lobby, when two
// players cannot reach each other). They are known by an address in 0.0.0.0/8, which no real
// sender has: the link the server gave them. On the wire a relayed datagram is kRelayTag, the
// link (u32, big-endian), then the datagram.
inline constexpr u8 kRelayTag[4] = {'S', 'H', 'R', 'Y'};
inline constexpr size_t kRelayHeader = 8;
inline Address relayed(u32 link) { return {link & 0x00FFFFFFu, 1}; }
inline bool is_relayed(const Address& a) { return a.ip != 0 && (a.ip >> 24) == 0 && a.port == 1; }
// 10/8, 172.16/12, 192.168/16, 100.64/10 (carrier NAT), link-local 169.254/16.
bool is_private(u32 ip);

// Tests: every socket binds to 127.0.0.1 (no firewall prompt, nothing leaves the machine).
void set_loopback_only(bool on);
bool loopback_only();

// Socket library reference counting (Winsock's; nothing on Android); safe from several owners.
bool startup();
void shutdown();

// This machine's IPv4 addresses on its network adapters (not loopback), for showing the host
// where friends can reach them.
std::vector<u32> local_addresses();

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Binds to all interfaces, or to `bind_ip` when it is not 0. Port 0 picks an ephemeral port.
    // `reuse`: other sockets on this machine may bind the same port (LAN discovery listeners).
    bool open(u16 port, u32 bind_ip = 0, bool reuse = false);
    void close();
    bool is_open() const { return handle_ != kInvalid; }
    // Lets send() reach Address::kBroadcast.
    bool enable_broadcast();
    // The server relayed addresses go through: a datagram to one goes to `via`, wrapped; one from
    // `via` so wrapped comes back as from its link.
    void set_relay(const Address& via) { relay_ = via; }
    const Address& relay() const { return relay_; }

    bool send(const Address& to, const u8* data, size_t size);
    // Returns the datagram size, or -1 when nothing is waiting.
    int receive(Address& from, u8* buffer, size_t capacity);

    u16 local_port() const { return port_; }

private:
    static constexpr u64 kInvalid = ~u64(0);
    u64 handle_ = kInvalid;
    u16 port_ = 0;
    Address relay_;
};

}  // namespace eng::net
