#include "Engine/Net/Socket.hpp"

#include "Engine/Core/Log.hpp"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace eng::net {

namespace {

int g_refs = 0;
std::mutex g_lock;
std::atomic<bool> g_loopback{false};

#ifdef _WIN32
using Native = SOCKET;
constexpr Native kBad = INVALID_SOCKET;
void close_native(Native s) { closesocket(s); }
int last_error() { return WSAGetLastError(); }
using socklen = int;
#else
using Native = int;
constexpr Native kBad = -1;
void close_native(Native s) { ::close(s); }
int last_error() { return errno; }
using socklen = socklen_t;
#endif

}  // namespace

std::string Address::ip_string() const {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return buf;
}

std::string Address::to_string() const { return ip_string() + ":" + std::to_string(port); }

void set_loopback_only(bool on) { g_loopback = on; }
bool loopback_only() { return g_loopback; }

bool is_private(u32 ip) {
    const u32 a = ip >> 24, b = (ip >> 16) & 255;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127) ||
           (a == 169 && b == 254);
}

std::optional<Address> Address::resolve(const std::string& host_and_port, u16 default_port) {
    std::string host = host_and_port;
    while (!host.empty() && (host.back() == ' ' || host.back() == '\t')) host.pop_back();
    while (!host.empty() && (host.front() == ' ' || host.front() == '\t')) host.erase(0, 1);
    u16 port = default_port;
    const size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
        const int p = std::atoi(host.c_str() + colon + 1);
        if (p <= 0 || p > 65535) return std::nullopt;
        port = u16(p);
        host.resize(colon);
    }
    if (host.empty()) host = "127.0.0.1";
    if (!startup()) return std::nullopt;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    std::optional<Address> result;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
        Address a;
        a.ip = ntohl(reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr);
        a.port = port;
        freeaddrinfo(res);
        result = a;
    }
    shutdown();
    return result;
}

// Under one lock: a second user (a name looked up on another thread) must not go ahead while
// the first is still starting Winsock, nor may a last user clean up under another's feet.
bool startup() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_refs > 0) {
        ++g_refs;
        return true;
    }
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        LOG_ERROR("WSAStartup failed");
        return false;
    }
#endif
    ++g_refs;
    return true;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_refs <= 0) return;
#ifdef _WIN32
    if (--g_refs == 0) WSACleanup();
#else
    --g_refs;
#endif
}

std::vector<u32> local_addresses() {
    std::vector<u32> out;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, list, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, list, &size);
    }
    if (rc != NO_ERROR) return out;
    for (auto* a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
            if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET) {
                const u32 ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
                if (!is_loopback(ip)) out.push_back(ip);
            }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (auto* a = list; a; a = a->ifa_next)
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET) {
            const u32 ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
            if (!is_loopback(ip)) out.push_back(ip);
        }
    freeifaddrs(list);
#endif
    return out;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(u16 port, u32 bind_ip, bool reuse) {
    close();
    const Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBad) return false;
#ifdef _WIN32
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    // Windows reports ICMP "port unreachable" as a recv error on UDP; ignore it.
    BOOL no_reset = FALSE;
    DWORD ignored = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &no_reset, sizeof(no_reset), nullptr, 0, &ignored, nullptr, nullptr);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int buffer = 1 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    if (reuse) {
        int on = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(bind_ip ? bind_ip : g_loopback ? INADDR_LOOPBACK : INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        LOG_WARN("Cannot bind UDP port %u (error %d)", port, last_error());
        close_native(s);
        return false;
    }
    sockaddr_in bound{};
    socklen len = sizeof(bound);
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    port_ = ntohs(bound.sin_port);
    handle_ = u64(s);
    return true;
}

void UdpSocket::close() {
    if (is_open()) {
        close_native(Native(handle_));
        handle_ = kInvalid;
        port_ = 0;
    }
    relay_ = {};
}

bool UdpSocket::enable_broadcast() {
    if (!is_open()) return false;
    int on = 1;
    return setsockopt(Native(handle_), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on)) == 0;
}

bool UdpSocket::send(const Address& to, const u8* data, size_t size) {
    if (!is_open()) return false;
    if (is_relayed(to)) {
        // Through the relay, with the link in front.
        u8 wrapped[2048 + kRelayHeader];
        if (!relay_.valid() || size > 2048) return false;
        std::memcpy(wrapped, kRelayTag, 4);
        wrapped[4] = u8(to.ip >> 24);
        wrapped[5] = u8(to.ip >> 16);
        wrapped[6] = u8(to.ip >> 8);
        wrapped[7] = u8(to.ip);
        std::memcpy(wrapped + kRelayHeader, data, size);
        return send(relay_, wrapped, size + kRelayHeader);
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);
    const auto sent = ::sendto(Native(handle_), reinterpret_cast<const char*>(data), int(size), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == decltype(sent)(size);
}

int UdpSocket::receive(Address& from, u8* buffer, size_t capacity) {
    if (!is_open()) return -1;
    for (;;) {
        sockaddr_in addr{};
        socklen len = sizeof(addr);
        const auto got = ::recvfrom(Native(handle_), reinterpret_cast<char*>(buffer), int(capacity), 0, reinterpret_cast<sockaddr*>(&addr), &len);
        if (got < 0) {
#ifdef _WIN32
            const int err = WSAGetLastError();
            if (err == WSAECONNRESET || err == WSAEMSGSIZE) continue;   // stale ICMP / oversized datagram
#else
            if (errno == ECONNREFUSED) continue;
#endif
            return -1;
        }
        from.ip = ntohl(addr.sin_addr.s_addr);
        from.port = ntohs(addr.sin_port);
        // Someone reached through the relay: as from their link.
        if (relay_.valid() && from == relay_ && size_t(got) >= kRelayHeader && std::memcmp(buffer, kRelayTag, 4) == 0) {
            const u32 link = (u32(buffer[4]) << 24) | (u32(buffer[5]) << 16) | (u32(buffer[6]) << 8) | u32(buffer[7]);
            const size_t n = size_t(got) - kRelayHeader;
            std::memmove(buffer, buffer + kRelayHeader, n);
            from = relayed(link);
            return int(n);
        }
        return int(got);
    }
}

}  // namespace eng::net
