// The script debugger's GDB server (docs/script-debugger.md).
//
//   GDB / VS Code  --GDB remote protocol-->  gdbstub::Server  --PINE-->  PCSX2
//                                                                         |
//                                    the game's BREAK handler <-- RAM ----+
//
// PINE, PCSX2's IPC socket, reads and writes guest RAM and nothing else, so the
// game does the CPU-side work itself (src/gen/script_debug.gen.cpp): it takes
// the BREAK exception, publishes its register frame in a mailbox and parks
// until the host says go, applying any code patches the host queued on the way
// out. Everything here is data moved through that mailbox (scriptdbg.hpp).
//
// No project.hpp, no GL, no ImGui: one worker thread, a few atomics for the UI.
#pragma once

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform {
class Process;
}
namespace wire {
class Stream;
}

namespace gdbstub {

/** Guest RAM, word-addressed the way PINE offers it. */
class Target {
public:
    virtual ~Target() = default;
    virtual bool read8(uint32_t addr, uint8_t& v) = 0;
    virtual bool read32(uint32_t addr, uint32_t& v) = 0;
    virtual bool read64(uint32_t addr, uint64_t& v) = 0;
    virtual bool write8(uint32_t addr, uint8_t v) = 0;
    virtual bool write32(uint32_t addr, uint32_t v) = 0;
};

/** PCSX2's PINE socket: TCP 127.0.0.1:slot on Windows, a Unix socket elsewhere
 * (pcsx2.sock, or pcsx2.sock.<slot> for a non-default slot, under
 * $XDG_RUNTIME_DIR / $TMPDIR / /tmp). One client at a time - PCSX2 serves the
 * next connection only after the previous one closes. */
class PineClient : public Target {
public:
    ~PineClient() override;
    /** "" or why not (PCSX2 not up yet, PINE off, ...). */
    std::string connect(int slot);
    bool connected() const { return stream_ != nullptr; }

    bool read8(uint32_t addr, uint8_t& v) override;
    bool read32(uint32_t addr, uint32_t& v) override;
    bool read64(uint32_t addr, uint64_t& v) override;
    bool write8(uint32_t addr, uint8_t v) override;
    bool write32(uint32_t addr, uint32_t v) override;

    /** One PINE request carrying many commands - PCSX2 runs them in order and
     * answers once. A word costs ~0.1 ms either way, so 140 words are 14 ms
     * one by one and 0.13 ms in a batch (measured, PCSX2 2.9.93). */
    struct Op {
        uint8_t code;      // PINE opcode: 0/2/3 read 8/32/64, 4/6 write 8/32
        uint32_t addr;
        uint32_t value = 0;
    };
    /** Runs `ops`; the read results land in `reads`, concatenated in order. */
    bool batch(const std::vector<Op>& ops, std::vector<uint8_t>& reads);
    /** `n` bytes from `addr` in as few requests as the size allows. */
    bool readBlock(uint32_t addr, uint32_t n, uint8_t* out);
    /** PCSX2's own VM state: 0 running, 1 paused, 2 shut down, -1 no answer. */
    int status();

private:
    bool call(const std::vector<uint8_t>& body, std::vector<uint8_t>& reply);
    std::unique_ptr<wire::Stream> stream_;
};

/** True for an instruction with a delay slot (branches, jumps, jr/jalr). */
bool hasDelaySlot(uint32_t insn);

/** Where the next instruction can be after the one at `pc` (delay slots
 * included): one address, or the taken target and the fall-through. The EE has
 * no hardware single-step, so a step is a trap on every candidate. `insn` is
 * the instruction at pc, `reg(n)` the value of GPR n. */
std::vector<uint32_t> nextPcs(uint32_t pc, uint32_t insn,
                              const std::function<uint64_t(int)>& reg);

struct Config {
    std::string elfPath;   // the unstripped ELF (bin/<name>.elf.sym)
    int pineSlot = 28011;
    int port = 23400;      // GDB's port
    // Windows: GDB runs inside WSL, which cannot reach Windows' localhost. A
    // relay in WSL (tools/toolchain/gdb-relay.sh) listens on `port` for GDB
    // and on port + 1 for us, so we CONNECT to port + 1 instead of listening.
    // Empty = listen on 127.0.0.1:port directly (Linux, macOS).
    std::string relayCommand;
    // bin/gdbserver.json: written while a game is being served, so the VS Code
    // extension can tell "attach" from "start one". Deleted on the way out.
    std::string sessionFile;
};

enum class State {
    WaitingForGame,  // no PINE, or the game has not routed BREAK yet
    WaitingForGdb,   // the game is up; nobody attached
    Running,         // GDB attached, game running
    Stopped,         // GDB attached, game parked in the handler
    Failed,          // cannot serve at all (see message())
};
const char* stateName(State s);

class Server {
public:
    explicit Server(Config cfg);
    ~Server();  // stops and joins
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    State state() const { return state_.load(); }
    /** The stop's pc while Stopped. */
    uint32_t pc() const { return pc_.load(); }
    /** One line for a status chip or log: what it is waiting for, or why not. */
    std::string message() const;
    const Config& config() const { return cfg_; }

private:
    void run();
    void setState(State s, const std::string& msg);
    void writeSessionFile();
    bool attachGame();               // PINE + mailbox; false while not there
    void serve(wire::Stream& gdb);   // one GDB connection, until it leaves

    // Mailbox / frame access.
    bool mb32(uint32_t off, uint32_t& v);
    bool stopped();
    bool pause();                    // the poll trap; true once parked
    bool resume();                   // queue patches, release the game
    // Everything a stop reads is read once: the mailbox header and the
    // register frame in two batched requests, memory in 256-byte lines. Valid
    // until the next resume (the game is parked, nothing else writes).
    bool snapshot();
    void invalidate();
    bool regAt(int n, uint8_t*& at, uint32_t& addr, int& size);
    uint64_t getReg(int n);
    void setReg(int n, uint64_t v);
    bool readMem(uint32_t a, uint32_t n, std::vector<uint8_t>& out);  // our traps masked
    bool writeMem(uint32_t a, const std::vector<uint8_t>& b);
    uint32_t codeAt(uint32_t addr);  // real instruction, never one of our traps
    void addTrap(uint32_t addr);     // a single-step / range-step trap
    void dropTraps();
    std::string stopReply();

    enum class Act { Reply, Continue, Step, Range, Quit };
    struct Action {
        Act act = Act::Reply;
        uint32_t start = 0, end = 0;  // Range: step while start <= pc < end
    };
    std::string handle(const std::string& pkt, Action& a);
    void trace(const char* fmt, ...);

    Config cfg_;
    std::atomic<bool> quit_{false};
    std::atomic<State> state_{State::WaitingForGame};
    std::atomic<uint32_t> pc_{0};
    mutable std::mutex msgMutex_;
    std::string msg_;
    std::thread worker_;

    // Worker-thread state.
    PineClient pine_;
    uint32_t mailbox_ = 0;
    // Words sampled across the ELF's .text: the game on the other end of PINE
    // must match them, or it is another PCSX2's game (one PINE slot is shared
    // by every instance) or an old build still running.
    std::vector<std::pair<uint32_t, uint32_t>> textSamples_;
    bool verified_ = false;
    uint32_t stopsSeen_ = 0;
    std::map<uint32_t, uint32_t> origCode_;  // every trapped address -> its word
    std::map<uint32_t, bool> bps_;           // GDB's breakpoints
    std::map<uint32_t, bool> steps_;         // our single-step traps
    std::vector<std::pair<uint32_t, uint32_t>> pending_;  // code patches
    std::unique_ptr<platform::Process> relay_;
    bool snapValid_ = false;
    std::vector<uint8_t> mbox_;   // mailbox header, kOffPatches bytes
    std::vector<uint8_t> frame_;  // the EE_RegFrame of the stop
    uint32_t frameAddr_ = 0;
    std::map<uint32_t, std::vector<uint8_t>> lines_;  // 256-byte memory lines
    FILE* trace_ = nullptr;  // TYRAX_GDB_TRACE=<file>: packets + timings
};

}  // namespace gdbstub
