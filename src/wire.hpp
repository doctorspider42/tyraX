#pragma once

// Collaboration transport layer: length-prefixed frames over a swappable
// byte-stream transport. This file is deliberately independent of the project
// model (pure bytes) - everything above it (session.cpp) speaks Frames, so a
// future internet transport (WebSocket through a tunnel) only has to implement
// the Transport interface, not touch the protocol.
//
// Frame layout on the wire (little-endian):
//   [u32 jsonLen][u32 binLen][jsonLen bytes UTF-8 JSON][binLen bytes raw]
// The JSON part carries the message ("t" field = type); the binary trailer
// carries bulk payloads (file chunks, heightmap grids) so raw bytes never pass
// through the JSON layer (json.cpp collapses \u escapes - binary must not go
// near it). Hard caps below bound a malicious/corrupt peer's memory use.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wire {

// FNV-1a 64 (the codebase's hash idiom - same constants as project.cpp's
// live-link hashing). Streamable: pass the previous result as seed.
constexpr uint64_t kFnvSeed = 1469598103934665603ull;
uint64_t fnv1a64(const void* data, size_t len, uint64_t seed = kFnvSeed);

// Content hash + size of a file on disk (streamed, no full load). Returns
// false when the file cannot be opened/read.
bool hashFile(const std::string& path, uint64_t& outHash, uint64_t& outSize);

// One protocol frame. `bin` uses std::string as a byte buffer (matches file
// contents and keeps the API allocation-simple).
struct Frame {
    std::string json;
    std::string bin;
};

// Frame size caps (per part). A frame that declares more is a protocol error
// and kills the connection - bulk data must be chunked below these.
constexpr uint32_t kMaxJsonLen = 4u * 1024 * 1024;
constexpr uint32_t kMaxBinLen = 16u * 1024 * 1024;

std::string encodeFrame(const Frame& f);

// Incremental frame decoder (handles arbitrary short reads). feed() bytes as
// they arrive, then drain next() until it returns false. error() latches on a
// malformed/oversized header; the connection must be dropped then.
class FrameDecoder {
  public:
    void feed(const char* data, size_t len);
    bool next(Frame& out);
    bool error() const { return error_; }

  private:
    std::string buf_;
    bool error_ = false;
};

// Transport-level connection id. Host side: one per accepted client (never
// reused within a session). Client side: the single connection is kHostPeer.
using PeerId = int;
constexpr PeerId kHostPeer = 0;

// What poll() surfaces. Connected/Disconnected bracket a peer's lifetime;
// every Frame event's peer is guaranteed to be inside such a bracket.
struct Event {
    enum class Type { Connected, Disconnected, Frame };
    Type type = Type::Frame;
    PeerId peer = 0;
    Frame frame;       // Frame events
    std::string info;  // Connected: remote address; Disconnected: reason
};

// A byte-stream transport carrying Frames. Single-threaded by contract: all
// calls must come from one thread (the session worker). Exactly one of
// listen()/connect() per instance.
class Transport {
  public:
    virtual ~Transport() = default;

    // Host role: start accepting connections. Returns "" or an error text
    // (e.g. the port is in use).
    virtual std::string listen(uint16_t port) = 0;

    // Client role: connect to a host. Blocks up to timeoutMs. Returns "" or
    // an error text.
    virtual std::string connect(const std::string& host, uint16_t port,
                                int timeoutMs) = 0;

    // Pumps I/O for up to timeoutMs milliseconds and appends decoded events.
    virtual void poll(std::vector<Event>& out, int timeoutMs) = 0;

    // Queues a frame for delivery (non-blocking; bytes drain in poll()).
    // False when the peer is unknown/closed. Callers pace bulk transfers via
    // sendBacklog() so a slow peer cannot balloon memory.
    virtual bool send(PeerId peer, const Frame& f) = 0;

    // Bytes queued but not yet handed to the OS for this peer.
    virtual size_t sendBacklog(PeerId peer) const = 0;

    // Closes one peer's connection (host-side kick). No Disconnected event is
    // emitted for a local kick - the caller decided it.
    virtual void kick(PeerId peer) = 0;

    // Tears down every connection and the listener.
    virtual void close() = 0;
};

// The LAN transport: raw TCP (Winsock2, WSAPoll, TCP_NODELAY).
std::unique_ptr<Transport> makeTcpTransport();

// Local IPv4 addresses (for "hosting on <ip>:<port>" UI), loopback excluded.
std::vector<std::string> localIPv4();

}  // namespace wire
