// One end of a UDP connection: sequence numbers, acknowledgements, reliable ordered
// messages (fragmented when large) and unreliable messages, all sharing one socket.
// (TacticalFPS's, as it was proven there by its bots.)
//
// Datagram layout (all little-endian):
//
//   u32 magic 'LSF1'   u8 type
//   Connect     u32 client_nonce   u32 game_version
//   Accept      u32 client_nonce   u32 session_key
//   Deny        u32 client_nonce   string reason
//   Data        u32 session_key    u16 seq   u16 ack   u32 ack_bits   messages...
//   Disconnect  u32 session_key    string reason
//
//   message     u8 flags (bit 0 reliable, bit 1 more fragments follow, bit 2 probe)
//               [u16 reliable_id]  varint length   bytes
//
// ack is the newest packet sequence received and bit n of ack_bits acknowledges
// ack - (n + 1). A reliable message is resent until a packet carrying it is acked.
//
// The round trip is timed on probes only: an empty message a few times a second that the far
// end acknowledges on its very next write. Other packets are acknowledged late on purpose (the
// ack delay, or not until the far end's next keep-alive when nothing is flowing), so timing them
// measured the waiting, and an idle loopback read as 90 ms.
#pragma once

#include "Engine/Core/ByteStream.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng::net {

inline constexpr u32 kProtocolMagic = fourcc('L', 'S', 'F', '1');
inline constexpr size_t kMaxDatagram = 1200;
inline constexpr size_t kMaxUnreliable = 1100;
inline constexpr size_t kFragmentSize = 1000;
inline constexpr double kTimeoutSeconds = 12.0;
// The largest reliable message a connection will put back together (a session's welcome, with
// every player in it, is a few kilobytes); without a ceiling a peer could send "more fragments
// follow" forever and the far end would keep every one.
inline constexpr size_t kMaxMessage = 64 * 1024;
// Messages delivered but not yet collected. The owner drains this every tick, so a backlog this
// deep means one peer is sending faster than anything could legitimately need.
inline constexpr size_t kMaxInbox = 4096;

enum class PacketType : u8 { Connect = 1, Accept = 2, Deny = 3, Data = 4, Disconnect = 5 };

struct ConnectionStats {
    float rtt = 0.1f;   // smoothed round trip, seconds
    float loss = 0.0f;  // share of recent packets never acknowledged
    u64 bytes_sent = 0;
    u64 bytes_received = 0;
    u32 packets_sent = 0;
    u32 packets_received = 0;
    u32 resends = 0;
};

class Connection {
public:
    Connection(u32 session_key, double now);

    void send(std::vector<u8> message, bool reliable);
    // `r` is positioned just after the session key of a Data datagram.
    void receive(ByteReader& r, size_t datagram_size, double now);
    // Appends complete datagrams that should go out now (possibly none).
    void write(double now, std::vector<std::vector<u8>>& datagrams);
    // Next delivered message. Reliable messages come out in the order they were sent.
    bool pop(std::vector<u8>& message);

    double last_receive() const { return last_receive_; }
    u32 session_key() const { return session_key_; }
    const ConnectionStats& stats() const { return stats_; }
    size_t pending_reliable() const { return out_reliable_.size(); }
    // Why the far end broke the rules, or nullptr. Set once and never cleared: the owner is
    // expected to drop the connection, and nothing further is accepted from it meanwhile.
    const char* violation() const { return violation_; }

private:
    struct OutReliable {
        u16 id = 0;
        u8 flags = 0;
        std::vector<u8> data;
        double sent_at = -1.0;
        bool acked = false;
    };
    struct SentPacket {
        u16 seq = 0;
        double time = -1.0;
        bool acked = false;
        bool probe = false;
        std::vector<u16> reliable_ids;
    };
    struct InReliable {
        u8 flags = 0;
        std::vector<u8> data;
    };

    void acknowledge(u16 seq, double now);
    void accept_reliable(u16 id, u8 flags, std::span<const u8> data);
    void update_loss(double now);

    u32 session_key_;
    ConnectionStats stats_;
    double last_receive_;
    double last_send_ = -1.0;
    double last_probe_ = -1.0;
    bool rtt_measured_ = false;
    double last_loss_update_ = 0.0;

    // Sending
    u16 next_seq_ = 0;
    u16 next_out_id_ = 0;
    std::deque<OutReliable> out_reliable_;
    std::deque<std::vector<u8>> out_unreliable_;
    std::array<SentPacket, 1024> sent_{};

    // Receiving
    bool have_remote_ = false;
    u16 remote_seq_ = 0;
    u32 remote_bits_ = 0;
    bool ack_pending_ = false;
    double ack_due_ = 0.0;
    u16 next_in_id_ = 0;
    std::unordered_map<u16, InReliable> in_reliable_;
    std::vector<u8> assembling_;
    std::deque<std::vector<u8>> inbox_;
    const char* violation_ = nullptr;
};

}  // namespace eng::net
