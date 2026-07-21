#include "wire.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>

namespace wire {

uint64_t fnv1a64(const void* data, size_t len, uint64_t seed) {
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t h = seed;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) h = (h ^ p[i]) * kPrime;
    return h;
}

bool hashFile(const std::string& path, uint64_t& outHash, uint64_t& outSize) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint64_t h = kFnvSeed;
    uint64_t size = 0;
    char buf[64 * 1024];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        const size_t n = (size_t)f.gcount();
        h = fnv1a64(buf, n, h);
        size += n;
        if (!f) break;
    }
    if (f.bad()) return false;
    outHash = h;
    outSize = size;
    return true;
}

// --- Frame codec ---------------------------------------------------------------

static void putU32(std::string& out, uint32_t v) {
    out.push_back((char)(v & 0xFF));
    out.push_back((char)((v >> 8) & 0xFF));
    out.push_back((char)((v >> 16) & 0xFF));
    out.push_back((char)((v >> 24) & 0xFF));
}

static uint32_t getU32(const char* p) {
    const auto* u = reinterpret_cast<const unsigned char*>(p);
    return (uint32_t)u[0] | ((uint32_t)u[1] << 8) | ((uint32_t)u[2] << 16) |
           ((uint32_t)u[3] << 24);
}

std::string encodeFrame(const Frame& f) {
    std::string out;
    out.reserve(8 + f.json.size() + f.bin.size());
    putU32(out, (uint32_t)f.json.size());
    putU32(out, (uint32_t)f.bin.size());
    out += f.json;
    out += f.bin;
    return out;
}

void FrameDecoder::feed(const char* data, size_t len) {
    if (error_) return;
    buf_.append(data, len);
}

bool FrameDecoder::next(Frame& out) {
    if (error_ || buf_.size() < 8) return false;
    const uint32_t jsonLen = getU32(buf_.data());
    const uint32_t binLen = getU32(buf_.data() + 4);
    if (jsonLen > kMaxJsonLen || binLen > kMaxBinLen) {
        error_ = true;  // protocol violation - the connection must die
        return false;
    }
    const size_t total = 8 + (size_t)jsonLen + binLen;
    if (buf_.size() < total) return false;
    out.json.assign(buf_, 8, jsonLen);
    out.bin.assign(buf_, 8 + (size_t)jsonLen, binLen);
    buf_.erase(0, total);
    return true;
}

// --- TCP transport ---------------------------------------------------------------

namespace {

// Process-wide Winsock init, done lazily on first transport construction.
// Never torn down - WSACleanup at exit buys nothing and can race other users.
void ensureWinsock() {
    static const int once = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa);
    }();
    (void)once;
}

std::string wsaError(const char* what) {
    return std::string(what) + " failed (WSA error " + std::to_string(WSAGetLastError()) +
           ")";
}

void configureSocket(SOCKET s) {
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    BOOL noDelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
}

struct Conn {
    SOCKET sock = INVALID_SOCKET;
    FrameDecoder decoder;
    std::string outbuf;   // encoded bytes not yet accepted by the OS
    std::string address;  // remote ip:port (diagnostics/UI)
    bool connected = true;
};

class TcpTransport final : public Transport {
  public:
    TcpTransport() { ensureWinsock(); }
    ~TcpTransport() override { close(); }

    std::string listen(uint16_t port) override {
        if (listener_ != INVALID_SOCKET || !conns_.empty())
            return "transport already in use";
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return wsaError("socket");
        // No SO_REUSEADDR on purpose: a second host on the same port must get
        // a clean "port in use" error, not steal the socket.
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
            const int err = WSAGetLastError();
            ::closesocket(s);
            return err == WSAEADDRINUSE ? "port is already in use"
                                        : "bind failed (WSA error " + std::to_string(err) + ")";
        }
        if (::listen(s, 8) != 0) {
            std::string e = wsaError("listen");
            ::closesocket(s);
            return e;
        }
        configureSocket(s);
        listener_ = s;
        return "";
    }

    std::string connect(const std::string& host, uint16_t port, int timeoutMs) override {
        if (listener_ != INVALID_SOCKET || !conns_.empty())
            return "transport already in use";
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 ||
            !res)
            return "cannot resolve host: " + host;
        SOCKET s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s == INVALID_SOCKET) {
            freeaddrinfo(res);
            return wsaError("socket");
        }
        configureSocket(s);
        int rc = ::connect(s, res->ai_addr, (int)res->ai_addrlen);
        freeaddrinfo(res);
        if (rc != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
            ::closesocket(s);
            return wsaError("connect");
        }
        // Non-blocking connect: wait for writability (or failure) up to the
        // timeout.
        WSAPOLLFD pfd{};
        pfd.fd = s;
        pfd.events = POLLOUT;
        rc = WSAPoll(&pfd, 1, timeoutMs);
        if (rc <= 0 || (pfd.revents & (POLLERR | POLLHUP))) {
            ::closesocket(s);
            return rc == 0 ? "connection timed out" : "connection refused";
        }
        int soErr = 0;
        int len = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
        if (soErr != 0) {
            ::closesocket(s);
            return "connection failed (error " + std::to_string(soErr) + ")";
        }
        Conn c;
        c.sock = s;
        c.address = host + ":" + std::to_string(port);
        conns_[kHostPeer] = std::move(c);
        return "";
    }

    void poll(std::vector<Event>& out, int timeoutMs) override {
        std::vector<WSAPOLLFD> fds;
        std::vector<PeerId> fdPeer;  // parallel to fds; -1 = listener
        if (listener_ != INVALID_SOCKET) {
            fds.push_back({listener_, POLLRDNORM, 0});
            fdPeer.push_back(-1);
        }
        for (auto& [id, c] : conns_) {
            SHORT ev = POLLRDNORM;
            if (!c.outbuf.empty()) ev |= POLLWRNORM;
            fds.push_back({c.sock, ev, 0});
            fdPeer.push_back(id);
        }
        if (fds.empty()) {
            if (timeoutMs > 0) Sleep((DWORD)timeoutMs);
            return;
        }
        const int rc = WSAPoll(fds.data(), (ULONG)fds.size(), timeoutMs);
        if (rc <= 0) return;

        std::vector<PeerId> dead;
        for (size_t i = 0; i < fds.size(); ++i) {
            if (fdPeer[i] == -1) {
                if (fds[i].revents & POLLRDNORM) acceptPending(out);
                continue;
            }
            const PeerId id = fdPeer[i];
            Conn& c = conns_[id];
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                dead.push_back(id);
                continue;
            }
            if (fds[i].revents & POLLWRNORM) {
                if (!flushSend(c)) {
                    dead.push_back(id);
                    continue;
                }
            }
            if (fds[i].revents & POLLRDNORM) {
                if (!drainRecv(id, c, out)) {
                    dead.push_back(id);
                    continue;
                }
            }
        }
        for (PeerId id : dead) dropPeer(id, "connection lost", out);
    }

    bool send(PeerId peer, const Frame& f) override {
        auto it = conns_.find(peer);
        if (it == conns_.end()) return false;
        it->second.outbuf += encodeFrame(f);
        // Opportunistic flush so small control frames go out without waiting
        // for the next poll tick.
        flushSend(it->second);
        return true;
    }

    size_t sendBacklog(PeerId peer) const override {
        auto it = conns_.find(peer);
        return it == conns_.end() ? 0 : it->second.outbuf.size();
    }

    void kick(PeerId peer) override {
        auto it = conns_.find(peer);
        if (it == conns_.end()) return;
        // Best-effort: let queued bytes (the "bye" frame) reach the OS first.
        flushSend(it->second);
        ::closesocket(it->second.sock);
        conns_.erase(it);
    }

    void close() override {
        for (auto& [id, c] : conns_) ::closesocket(c.sock);
        conns_.clear();
        if (listener_ != INVALID_SOCKET) {
            ::closesocket(listener_);
            listener_ = INVALID_SOCKET;
        }
    }

  private:
    void acceptPending(std::vector<Event>& out) {
        for (;;) {
            sockaddr_in addr{};
            int alen = sizeof(addr);
            SOCKET s = ::accept(listener_, (sockaddr*)&addr, &alen);
            if (s == INVALID_SOCKET) break;
            configureSocket(s);
            char ip[64] = {0};
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            Conn c;
            c.sock = s;
            c.address = std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
            const PeerId id = nextPeer_++;
            conns_[id] = std::move(c);
            Event e;
            e.type = Event::Type::Connected;
            e.peer = id;
            e.info = conns_[id].address;
            out.push_back(std::move(e));
        }
    }

    // False = the connection failed and must be dropped.
    bool flushSend(Conn& c) {
        while (!c.outbuf.empty()) {
            const int n = ::send(c.sock, c.outbuf.data(),
                                 (int)std::min(c.outbuf.size(), (size_t)256 * 1024), 0);
            if (n > 0) {
                c.outbuf.erase(0, (size_t)n);
                continue;
            }
            return WSAGetLastError() == WSAEWOULDBLOCK;
        }
        return true;
    }

    bool drainRecv(PeerId id, Conn& c, std::vector<Event>& out) {
        char buf[64 * 1024];
        for (;;) {
            const int n = ::recv(c.sock, buf, sizeof(buf), 0);
            if (n > 0) {
                c.decoder.feed(buf, (size_t)n);
                Frame f;
                while (c.decoder.next(f)) {
                    Event e;
                    e.type = Event::Type::Frame;
                    e.peer = id;
                    e.frame = std::move(f);
                    out.push_back(std::move(e));
                    f = Frame{};
                }
                if (c.decoder.error()) return false;  // oversized/corrupt header
                continue;
            }
            if (n == 0) return false;  // orderly remote close
            return WSAGetLastError() == WSAEWOULDBLOCK;
        }
    }

    void dropPeer(PeerId id, const std::string& reason, std::vector<Event>& out) {
        auto it = conns_.find(id);
        if (it == conns_.end()) return;
        ::closesocket(it->second.sock);
        conns_.erase(it);
        Event e;
        e.type = Event::Type::Disconnected;
        e.peer = id;
        e.info = reason;
        out.push_back(std::move(e));
    }

    SOCKET listener_ = INVALID_SOCKET;
    std::map<PeerId, Conn> conns_;
    PeerId nextPeer_ = 1;  // 0 is reserved for the client's host connection
};

}  // namespace

std::unique_ptr<Transport> makeTcpTransport() {
    return std::make_unique<TcpTransport>();
}

std::vector<std::string> localIPv4() {
    ensureWinsock();
    std::vector<std::string> out;
    // GetAdaptersAddresses would be more precise, but a UDP-connect trick plus
    // gethostname enumeration keeps us dependency-light. Enumerate via
    // getaddrinfo on the host name; loopback filtered out.
    char host[256] = {0};
    if (gethostname(host, sizeof(host)) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* a = res; a; a = a->ai_next) {
        char ip[64] = {0};
        const auto* sin = reinterpret_cast<sockaddr_in*>(a->ai_addr);
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        std::string s = ip;
        if (s.rfind("127.", 0) == 0 || s.empty()) continue;
        bool dup = false;
        for (const auto& e : out) dup |= (e == s);
        if (!dup) out.push_back(s);
    }
    freeaddrinfo(res);
    return out;
}

}  // namespace wire
