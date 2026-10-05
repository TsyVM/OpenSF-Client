#include "Engine/Net/Connection.hpp"

#include "Engine/Core/Log.hpp"

#include <algorithm>

namespace eng::net {

namespace {

constexpr size_t kHeaderSize = 4 + 1 + 4 + 2 + 2 + 4;
constexpr u16 kReliableWindow = 1024;
constexpr double kKeepAlive = 0.1;
constexpr double kAckDelay = 0.015;
constexpr double kProbeInterval = 0.25;
constexpr u8 kProbeFlag = 4;
constexpr int kMaxDatagramsPerWrite = 32;

size_t varint_size(u64 v) {
    size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        ++n;
    }
    return n;
}

}  // namespace

Connection::Connection(u32 session_key, double now) : session_key_(session_key), last_receive_(now) {}

void Connection::send(std::vector<u8> message, bool reliable) {
    if (!reliable && message.size() > kMaxUnreliable) {
        LOG_WARN("Unreliable message of %zu bytes sent reliably instead", message.size());
        reliable = true;
    }
    if (!reliable) {
        // Unreliable data is only worth sending fresh; cap the backlog.
        if (out_unreliable_.size() > 256) out_unreliable_.pop_front();
        out_unreliable_.push_back(std::move(message));
        return;
    }
    size_t offset = 0;
    do {
        size_t n = std::min(kFragmentSize, message.size() - offset);
        OutReliable o;
        o.id = next_out_id_++;
        o.data.assign(message.begin() + offset, message.begin() + offset + n);
        offset += n;
        o.flags = u8(1 | (offset < message.size() ? 2 : 0));
        out_reliable_.push_back(std::move(o));
    } while (offset < message.size());
}

void Connection::receive(ByteReader& r, size_t datagram_size, double now) {
    if (violation_) return;
    u16 seq = r.u16();
    u16 ack = r.u16();
    u32 ack_bits = r.u32();
    if (!r.ok()) return;

    // Packet-level duplicate detection and our acknowledgement state.
    if (!have_remote_) {
        have_remote_ = true;
        remote_seq_ = seq;
        remote_bits_ = 0;
    } else {
        u16 ahead = u16(seq - remote_seq_);
        if (ahead == 0) return;
        if (ahead < 32768) {
            if (ahead > 32)
                remote_bits_ = 0;
            else if (ahead == 32)
                remote_bits_ = 1u << 31;
            else
                remote_bits_ = (remote_bits_ << ahead) | (1u << (ahead - 1));
            remote_seq_ = seq;
        } else {
            u16 back = u16(remote_seq_ - seq);
            if (back > 32) return;   // too old to track; drop
            u32 bit = 1u << (back - 1);
            if (remote_bits_ & bit) return;   // duplicate
            remote_bits_ |= bit;
        }
    }

    last_receive_ = now;
    stats_.packets_received++;
    stats_.bytes_received += datagram_size;

    for (int i = 0; i <= 32; ++i) {
        if (i == 0 || (ack_bits & (1u << (i - 1)))) acknowledge(u16(ack - i), now);
    }

    bool any_message = false;
    bool probed = false;
    while (!r.at_end()) {
        u8 flags = r.u8();
        u16 id = (flags & 1) ? r.u16() : 0;
        u64 len = r.varu();
        auto data = r.view(size_t(len));
        if (!r.ok()) break;
        if (flags & kProbeFlag) {
            probed = true;
            continue;
        }
        any_message = true;
        if (flags & 1)
            accept_reliable(id, flags, data);
        else
            inbox_.emplace_back(data.begin(), data.end());
        if (inbox_.size() > kMaxInbox && !violation_) violation_ = "flooding";
        if (violation_) return;
    }
    if (probed) {
        ack_pending_ = true;
        ack_due_ = now;
    } else if (any_message && !ack_pending_) {
        ack_pending_ = true;
        ack_due_ = now + kAckDelay;
    }
}

void Connection::acknowledge(u16 seq, double now) {
    SentPacket& p = sent_[seq % sent_.size()];
    if (p.seq != seq || p.time < 0 || p.acked) return;
    p.acked = true;
    if (p.probe) {
        const float sample = float(now - p.time);
        stats_.rtt = rtt_measured_ ? stats_.rtt + (sample - stats_.rtt) * 0.1f : sample;
        rtt_measured_ = true;
    }
    for (u16 id : p.reliable_ids) {
        if (out_reliable_.empty()) break;
        u16 index = u16(id - out_reliable_.front().id);
        if (index < out_reliable_.size()) out_reliable_[index].acked = true;
    }
    while (!out_reliable_.empty() && out_reliable_.front().acked) out_reliable_.pop_front();
}

void Connection::accept_reliable(u16 id, u8 flags, std::span<const u8> data) {
    // An honest sender never has more than its window unacknowledged, so an id further ahead than
    // that is either nonsense or somebody trying to make us hold a thousand fragments each.
    u16 ahead = u16(id - next_in_id_);
    if (ahead >= 32768 || ahead >= kReliableWindow) return;   // old duplicate or nonsense
    if (in_reliable_.count(id)) return;
    in_reliable_[id] = InReliable{flags, std::vector<u8>(data.begin(), data.end())};
    for (;;) {
        auto it = in_reliable_.find(next_in_id_);
        if (it == in_reliable_.end()) break;
        if (assembling_.size() + it->second.data.size() > kMaxMessage) {
            violation_ = "message too large";
            assembling_.clear();
            in_reliable_.clear();
            return;
        }
        assembling_.insert(assembling_.end(), it->second.data.begin(), it->second.data.end());
        if (!(it->second.flags & 2)) {
            inbox_.push_back(std::move(assembling_));
            assembling_.clear();
        }
        in_reliable_.erase(it);
        ++next_in_id_;
    }
}

void Connection::update_loss(double now) {
    if (now - last_loss_update_ < 0.5) return;
    last_loss_update_ = now;
    int total = 0, lost = 0;
    for (const SentPacket& p : sent_) {
        if (p.time < 0) continue;
        double age = now - p.time;
        if (age < 1.0 || age > 3.0) continue;
        ++total;
        if (!p.acked) ++lost;
    }
    stats_.loss = total ? float(lost) / float(total) : 0.0f;
}

void Connection::write(double now, std::vector<std::vector<u8>>& datagrams) {
    update_loss(now);
    double resend_delay = std::clamp(double(stats_.rtt) * 1.5 + 0.03, 0.06, 1.0);
    bool ack_now = ack_pending_ && now >= ack_due_;
    bool keepalive = last_send_ < 0 || now - last_send_ >= kKeepAlive;
    bool probe = last_probe_ < 0 || now - last_probe_ >= kProbeInterval;

    for (int n = 0; n < kMaxDatagramsPerWrite; ++n) {
        ByteWriter w(kMaxDatagram);
        w.u32(kProtocolMagic);
        w.u8(u8(PacketType::Data));
        w.u32(session_key_);
        w.u16(next_seq_);
        w.u16(remote_seq_);
        w.u32(have_remote_ ? remote_bits_ : 0);

        SentPacket record;
        record.seq = next_seq_;
        bool payload = false;
        bool more = false;

        if (probe) {
            w.u8(kProbeFlag);
            w.varu(0);
            record.probe = true;
            last_probe_ = now;
        }

        if (!out_reliable_.empty()) {
            u16 base = out_reliable_.front().id;
            for (OutReliable& o : out_reliable_) {
                if (u16(o.id - base) >= kReliableWindow) break;
                if (o.acked) continue;
                if (o.sent_at >= 0 && now - o.sent_at < resend_delay) continue;
                size_t cost = 1 + 2 + varint_size(o.data.size()) + o.data.size();
                if (w.size() + cost > kMaxDatagram) {
                    more = true;
                    break;
                }
                w.u8(o.flags);
                w.u16(o.id);
                w.varu(o.data.size());
                w.raw(o.data.data(), o.data.size());
                if (o.sent_at >= 0) stats_.resends++;
                o.sent_at = now;
                record.reliable_ids.push_back(o.id);
                payload = true;
            }
        }
        while (!out_unreliable_.empty()) {
            const auto& m = out_unreliable_.front();
            size_t cost = 1 + varint_size(m.size()) + m.size();
            if (w.size() + cost > kMaxDatagram) {
                more = true;
                break;
            }
            w.u8(0);
            w.varu(m.size());
            w.raw(m.data(), m.size());
            out_unreliable_.pop_front();
            payload = true;
        }

        if (!payload && !ack_now && !keepalive && !probe) break;

        record.time = now;
        sent_[record.seq % sent_.size()] = std::move(record);
        ++next_seq_;
        stats_.packets_sent++;
        stats_.bytes_sent += w.size();
        datagrams.push_back(w.take());
        last_send_ = now;
        ack_pending_ = false;
        ack_now = false;
        keepalive = false;
        probe = false;
        if (!more) break;
    }
}

bool Connection::pop(std::vector<u8>& message) {
    if (inbox_.empty()) return false;
    message = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}

}  // namespace eng::net
