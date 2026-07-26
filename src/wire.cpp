#include "wire.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <utility>

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

// --- WebSocket (RFC 6455) server framing -----------------------------------------
// Wrapped around one accepted TCP connection: the handshake, then message
// framing. Kept as a per-connection CODEC rather than a second Transport
// implementation so the accept/poll/send/kick machinery below stays single -
// only the byte layer differs between the LAN and the phone transport.

// SHA-1 of a byte string (the handshake's only cryptographic requirement).
std::string sha1(const std::string& in) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::string msg = in;
    const uint64_t bits = (uint64_t)in.size() * 8;
    msg.push_back((char)0x80);
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; --i) msg.push_back((char)((bits >> (i * 8)) & 0xFF));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + off + i * 4);
            w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                   (uint32_t)p[3];
        }
        auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
            else f = b ^ c ^ d, k = 0xCA62C1D6u;
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    std::string out;
    for (uint32_t v : h)
        for (int i = 3; i >= 0; --i) out.push_back((char)((v >> (i * 8)) & 0xFF));
    return out;
}

std::string base64(const std::string& in) {
    static const char* kAlpha =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        const unsigned a = (unsigned char)in[i];
        const unsigned b = i + 1 < in.size() ? (unsigned char)in[i + 1] : 0u;
        const unsigned c = i + 2 < in.size() ? (unsigned char)in[i + 2] : 0u;
        const unsigned v = (a << 16) | (b << 8) | c;
        out.push_back(kAlpha[(v >> 18) & 63]);
        out.push_back(kAlpha[(v >> 12) & 63]);
        out.push_back(i + 1 < in.size() ? kAlpha[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < in.size() ? kAlpha[v & 63] : '=');
    }
    return out;
}

// Case-insensitive header lookup in an HTTP request head.
bool headerValue(const std::string& head, const char* name, std::string& out) {
    const std::string lowerHead = [&] {
        std::string s = head;
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }();
    std::string key = name;
    for (char& c : key) c = (char)std::tolower((unsigned char)c);
    size_t pos = 0;
    while ((pos = lowerHead.find(key + ":", pos)) != std::string::npos) {
        // Must sit at the start of a header line.
        if (pos != 0 && lowerHead[pos - 1] != '\n') {
            pos += key.size();
            continue;
        }
        size_t v = pos + key.size() + 1;
        while (v < head.size() && (head[v] == ' ' || head[v] == '\t')) ++v;
        size_t end = head.find("\r\n", v);
        if (end == std::string::npos) end = head.size();
        out = head.substr(v, end - v);
        return true;
    }
    return false;
}

// Server->client frame: never masked, one message per call.
std::string wsFrame(const std::string& payload, unsigned char opcode) {
    std::string out;
    out.push_back((char)(0x80 | opcode));  // FIN
    const size_t n = payload.size();
    if (n < 126) {
        out.push_back((char)n);
    } else if (n <= 0xFFFF) {
        out.push_back((char)126);
        out.push_back((char)((n >> 8) & 0xFF));
        out.push_back((char)(n & 0xFF));
    } else {
        out.push_back((char)127);
        for (int i = 7; i >= 0; --i) out.push_back((char)((n >> (i * 8)) & 0xFF));
    }
    out += payload;
    return out;
}

// One message may not exceed a whole Frame plus its header - the same bound the
// LAN transport enforces, so a hostile peer cannot balloon memory here either.
constexpr size_t kMaxWsMessage = (size_t)kMaxJsonLen + kMaxBinLen + 8;

struct WsCodec {
    std::string in;          // raw bytes not yet consumed
    std::string fragment;    // accumulated payload of a fragmented message
    std::string httpPage;    // served to a plain GET ("" = 404)
    bool upgraded = false;   // handshake complete
    bool dead = false;       // protocol error / close / page served: drop it
    bool announced = false;  // Connected already emitted for this peer

    // Consumes raw socket bytes. Bytes to write back go to `out`, complete
    // message payloads to `msgs`. Returns false when the connection must die
    // (after appending whatever response belongs on the wire first).
    bool feed(const char* data, size_t len, std::string& out,
              std::vector<std::string>& msgs) {
        if (dead) return false;
        in.append(data, len);
        if (!upgraded && !doHandshake(out)) return false;
        if (!upgraded) return true;  // head still incomplete
        return parseFrames(out, msgs);
    }

  private:
    bool doHandshake(std::string& out) {
        const size_t headEnd = in.find("\r\n\r\n");
        if (headEnd == std::string::npos) {
            // A request head this long is not a handshake.
            if (in.size() > 16 * 1024) return false;
            return true;
        }
        const std::string head = in.substr(0, headEnd);
        in.erase(0, headEnd + 4);
        std::string key, upgrade;
        headerValue(head, "Upgrade", upgrade);
        for (char& c : upgrade) c = (char)std::tolower((unsigned char)c);
        if (upgrade != "websocket" || !headerValue(head, "Sec-WebSocket-Key", key)) {
            // An ordinary browser GET: hand out the test client and hang up.
            if (!httpPage.empty()) {
                out += "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Content-Length: " + std::to_string(httpPage.size()) +
                       "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
                out += httpPage;
            } else {
                out += "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n"
                       "Connection: close\r\n\r\n";
            }
            dead = true;
            return false;
        }
        static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
               base64(sha1(key + kGuid)) + "\r\n\r\n";
        upgraded = true;
        return true;
    }

    bool parseFrames(std::string& out, std::vector<std::string>& msgs) {
        for (;;) {
            if (in.size() < 2) return true;
            const auto* p = reinterpret_cast<const unsigned char*>(in.data());
            const bool fin = (p[0] & 0x80) != 0;
            const unsigned char opcode = p[0] & 0x0F;
            const bool masked = (p[1] & 0x80) != 0;
            uint64_t plen = p[1] & 0x7F;
            size_t hdr = 2;
            if (plen == 126) {
                if (in.size() < 4) return true;
                plen = ((uint64_t)p[2] << 8) | p[3];
                hdr = 4;
            } else if (plen == 127) {
                if (in.size() < 10) return true;
                plen = 0;
                for (int i = 0; i < 8; ++i) plen = (plen << 8) | p[2 + i];
                hdr = 10;
            }
            // A client MUST mask; an unmasked frame is a protocol violation.
            if (!masked || plen > kMaxWsMessage) return false;
            const size_t total = hdr + 4 + (size_t)plen;
            if (in.size() < total) return true;
            unsigned char mask[4];
            for (int i = 0; i < 4; ++i) mask[i] = p[hdr + i];
            std::string payload(in, hdr + 4, (size_t)plen);
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] = (char)((unsigned char)payload[i] ^ mask[i & 3]);
            in.erase(0, total);

            if (opcode == 0x8) {  // close: echo it back, then die
                out += wsFrame(std::string(), 0x8);
                dead = true;
                return false;
            }
            if (opcode == 0x9) {  // ping -> pong, same payload
                out += wsFrame(payload, 0xA);
                continue;
            }
            if (opcode == 0xA) continue;  // pong (keepalive answer)
            if (opcode == 0x0) {          // continuation
                if (fragment.size() + payload.size() > kMaxWsMessage) return false;
                fragment += payload;
                if (fin) {
                    msgs.push_back(std::move(fragment));
                    fragment.clear();
                }
                continue;
            }
            if (opcode != 0x1 && opcode != 0x2) return false;  // reserved
            if (fin) {
                msgs.push_back(std::move(payload));
            } else {
                fragment = std::move(payload);
            }
        }
    }
};

struct Conn {
    SOCKET sock = INVALID_SOCKET;
    FrameDecoder decoder;
    std::string outbuf;   // encoded bytes not yet accepted by the OS
    std::string address;  // remote ip:port (diagnostics/UI)
    bool connected = true;
    // WebSocket mode only (nullptr for the raw LAN transport).
    std::unique_ptr<WsCodec> ws;
    // Graceful close: keep the connection alive purely to drain outbuf, then
    // drop it. Needed because a WebSocket connection that turned out to be an
    // ordinary browser GET must still deliver the served page before closing.
    bool closeAfterFlush = false;
};

class TcpTransport final : public Transport {
  public:
    // wsPage != nullptr switches this instance into WebSocket-server mode; the
    // string is what a plain browser GET receives (empty = 400).
    explicit TcpTransport(const std::string* wsPage = nullptr)
        : websocket_(wsPage != nullptr), wsPage_(wsPage ? *wsPage : std::string()) {
        ensureWinsock();
    }
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
        if (websocket_) return "the WebSocket transport is host-only";
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
        // Retire connections whose farewell bytes have all reached the OS.
        for (auto it = conns_.begin(); it != conns_.end();) {
            if (it->second.closeAfterFlush && it->second.outbuf.empty()) {
                const PeerId id = it->first;
                ++it;
                dropPeer(id, "closed", out);
            } else {
                ++it;
            }
        }
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
            if ((fds[i].revents & POLLRDNORM) && !c.closeAfterFlush) {
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
        if (it->second.ws) {
            // Nothing may go out before the upgrade completed (the peer is not
            // announced until then, so this only guards a misbehaving caller).
            if (!it->second.ws->upgraded) return false;
            it->second.outbuf += wsFrame(encodeFrame(f), 0x2);
        } else {
            it->second.outbuf += encodeFrame(f);
        }
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
            if (websocket_) {
                c.ws = std::make_unique<WsCodec>();
                c.ws->httpPage = wsPage_;
            }
            const PeerId id = nextPeer_++;
            conns_[id] = std::move(c);
            // WebSocket peers are announced once the upgrade succeeds, so a
            // plain browser GET never becomes a peer at all (see wire.hpp).
            if (!websocket_) {
                Event e;
                e.type = Event::Type::Connected;
                e.peer = id;
                e.info = conns_[id].address;
                out.push_back(std::move(e));
            }
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
                if (!feedDecoder(id, c, buf, (size_t)n, out)) return false;
                continue;
            }
            if (n == 0) return false;  // orderly remote close
            return WSAGetLastError() == WSAEWOULDBLOCK;
        }
    }

    // Raw bytes -> Frame events. In WebSocket mode they first pass the codec
    // (handshake, unmasking, control frames); each decoded message payload is
    // one encodeFrame() image, so the same FrameDecoder does the rest.
    bool feedDecoder(PeerId id, Conn& c, const char* data, size_t len,
                     std::vector<Event>& out) {
        auto emitFrames = [&]() {
            Frame f;
            while (c.decoder.next(f)) {
                Event e;
                e.type = Event::Type::Frame;
                e.peer = id;
                e.frame = std::move(f);
                out.push_back(std::move(e));
                f = Frame{};
            }
            return !c.decoder.error();  // oversized/corrupt header
        };
        if (!c.ws) {
            c.decoder.feed(data, len);
            return emitFrames();
        }
        const bool wasUpgraded = c.ws->upgraded;
        std::vector<std::string> msgs;
        const bool alive = c.ws->feed(data, len, c.outbuf, msgs);
        flushSend(c);  // the handshake reply / pong / served page goes out now
        if (!wasUpgraded && c.ws->upgraded && !c.ws->announced) {
            c.ws->announced = true;
            Event e;
            e.type = Event::Type::Connected;
            e.peer = id;
            e.info = c.address;
            out.push_back(std::move(e));
        }
        for (const std::string& m : msgs) c.decoder.feed(m.data(), m.size());
        if (!emitFrames()) return false;
        if (!alive) {
            // Close handshake / served page / protocol error: keep the socket
            // only long enough to push the queued bytes out, then retire it.
            c.closeAfterFlush = true;
        }
        return true;
    }

    void dropPeer(PeerId id, const std::string& reason, std::vector<Event>& out) {
        auto it = conns_.find(id);
        if (it == conns_.end()) return;
        // A WebSocket connection that never upgraded (an ordinary browser GET)
        // was never announced, so it must not produce a Disconnected either -
        // Connected/Disconnected have to stay a matched bracket.
        const bool announced = !it->second.ws || it->second.ws->announced;
        ::closesocket(it->second.sock);
        conns_.erase(it);
        if (!announced) return;
        Event e;
        e.type = Event::Type::Disconnected;
        e.peer = id;
        e.info = reason;
        out.push_back(std::move(e));
    }

    SOCKET listener_ = INVALID_SOCKET;
    std::map<PeerId, Conn> conns_;
    PeerId nextPeer_ = 1;  // 0 is reserved for the client's host connection
    const bool websocket_;
    const std::string wsPage_;
};

}  // namespace

std::unique_ptr<Transport> makeTcpTransport() {
    return std::make_unique<TcpTransport>();
}

std::unique_ptr<Transport> makeWebSocketTransport(std::string httpPage) {
    return std::make_unique<TcpTransport>(&httpPage);
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
