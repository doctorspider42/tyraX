/*
 * devlink - the devkit's channel through the IOP (docs/devlink.md).
 *
 * TyraX original, not a fork. Loaded by a DEBUG game running under ps2link,
 * on top of the IOP ps2link keeps resident: it imports ps2link's ps2ip (lwIP)
 * for its socket and owns nothing else.
 *
 * One low-priority thread, one UDP socket on DEVLINK_PORT:
 *
 *  INBOUND  (editor -> game). A datagram is routed by its first word - the
 *           magic of the file format it carries - and DMA'd (SIF0) into that
 *           channel's mailbox in EE RAM. The game reads mailboxes through the
 *           uncached segment: no RPC, no syscall, no waiting.
 *             'TXPD'  Remote Pad   -> pad mailbox   (livepad.bin, 36 bytes)
 *             'TXDC'  Live Debugger command -> cmd mailbox (livedbg.cmd,
 *                     length-prefixed: the format is variable-sized)
 *
 *  OUTBOUND (game -> editor). The game SIF1-DMAs a report (a whole
 *           livedbg.bin) into one of two slots allocated here, header first
 *           and footer last. This thread notices a complete slot, sends it as
 *           numbered 'TXDO' chunks to whoever sent the last 'TXHI' hello
 *           (from this same socket, so a stateful PC firewall lets it in as
 *           a reply), then tells the EE the slot is free again by
 *           DMA'ing the acknowledged sequence into the EE's hello box.
 *
 * Setup is the module's ARGUMENTS (EE addresses: "pad=0x...", "cmd=0x...",
 * "hello=0x..."); the IOP answers by DMA'ing its slot addresses into the hello
 * box. No SIF RPC and no SIF command handlers anywhere - the RPC path has
 * crashed under devkit load before (docs/devkit.md), and the user SIF command
 * table is shared with modules we do not own.
 *
 * Rules (each one is a past incident somewhere in docs/):
 *  - priority BELOW audsrv (40) and below ps2link's servers: the IOP schedules
 *    strictly by priority and TCP traffic has starved audsrv before;
 *  - one DMA in flight, waited out before its source buffer is reused;
 *  - inbound payloads are absolute state with a sequence number, so a datagram
 *    lost to UDP is simply replaced by the next one; outbound reports are
 *    snapshots, the next one supersedes a lost one.
 */

#include "irx_imports.h"

#define MODNAME "tyrax_devlink"
IRX_ID(MODNAME, 1, 6);

#define DEVLINK_PORT 18198

#define PAD_MAGIC 0x44505854U   /* "TXPD" */
#define PAD_SIZE 36
#define PAD_SLOT 48

#define CMD_MAGIC 0x43445854U   /* "TXDC" */
#define CMD_MAX 720             /* >= livedbg.cmd's largest (708) */
#define CMD_BOX (16 + CMD_MAX)  /* u32 len + 12 pad, then the payload */

#define HELLO_MAGIC 0x4C484458U /* "XDHL" */
#define HELLO_SIZE 32

#define OUT_MAGIC 0x4F445854U   /* "TXDO" */
#define OUT_SLOTS 2
#define OUT_MAX (32 * 1024)     /* largest report a slot takes */
#define OUT_SLOT (16 + OUT_MAX + 16)
#define OUT_CHUNK 1024
#define FOOTER_XOR 0x5A5A5A5AU
#define CHUNK_GAP_US 400        /* between two datagrams of one slot */

/* Report kinds - the 4th header word of a slot. */
#define KIND_REPORT 0   /* a whole livedbg.bin - sent as TXDO chunks */
#define KIND_REGISTER 1 /* the game's file mirrors - consumed here */
#define KIND_FILE 2     /* a segment of a file the game writes - TXDF chunks */
#define KIND_LOG 3      /* a frame's worth of log text - TXLG chunks */
#define KIND_HEARTBEAT 4 /* "the EE is alive" - consumed here */
#define OUTF_MAGIC 0x46445854U  /* "TXDF" */
#define OUTL_MAGIC 0x474C5854U  /* "TXLG" */
#define WATCHDOG_MAGIC 0x44575854U /* "TXWD" */
#define HELLO_PC_MAGIC 0x49485854U /* "TXHI" */

/* Phase 5: the IOP outlives a dead EE. Once the game has been heard from,
 * this long without a slot means it stopped - say so to the editor, every
 * WATCHDOG_REPEAT_MS, until it speaks again. */
#define WATCHDOG_SILENT_MS 3000
#define WATCHDOG_REPEAT_MS 2000

/* Inbound file mirrors (docs/devlink.md, phase 3): the editor pushes whole
 * devkit files ('TXFP' chunks) and they land in the game's double buffers. */
#define FILE_MAGIC 0x50465854U  /* "TXFP" */
#define FILE_HEADER 24
#define FILE_CHUNK 1024
#define MAX_FILES 8
#define MAX_FILE_CHUNKS 128      /* 128 KB per mirrored file */

typedef struct {
    u32 id;          /* fnv32 of the file name; 0 = free entry */
    u32 ctrl;        /* EE: control quadword {active half, len, seq, foot} */
    u32 half[2];     /* EE: the two halves */
    u32 cap;
    u32 active;      /* which half the game reads */
    u32 committed;   /* seq of the content the game has */
    u32 assembling;  /* seq of the content arriving */
    u32 have;
    u32 bits[MAX_FILE_CHUNKS / 32];
} Mirror;

static Mirror mirrors[MAX_FILES];

#define THREAD_PRIORITY 100
#define THREAD_STACK 0x1000

static u32 eePad;   /* EE physical addresses; 0 = channel off */
static u32 eeCmd;
static u32 eeHello;

static u8 rxbuf[FILE_HEADER + FILE_CHUNK + 64] __attribute__((aligned(64)));
static u8 dmabuf[FILE_CHUNK + 64] __attribute__((aligned(64)));
static u32 ctrlbuf[4] __attribute__((aligned(16)));
static u32 hello[HELLO_SIZE / 4] __attribute__((aligned(64)));
static u8 txbuf[16 + OUT_CHUNK] __attribute__((aligned(16)));

static volatile u32 *outSlot[OUT_SLOTS];
static u32 outDone[OUT_SLOTS];

static struct sockaddr_in peer;
static int havePeer;

static int heardEE;          /* the game has handed over at least one slot */
static u32 lastHeardMs;
static u32 lastWatchdogMs;
static u32 lastEESeq;

static u32 nowMs(void)
{
    iop_sys_clock_t c;
    u32 sec, usec;
    GetSystemTime(&c);
    SysClock2USec(&c, &sec, &usec);
    return sec * 1000U + usec / 1000U;
}

static u32 parseHex(const char *s)
{
    u32 v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    for (; *s; ++s) {
        char c = *s;
        u32 d;
        if (c >= '0' && c <= '9')
            d = (u32)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (u32)(c - 'A' + 10);
        else
            break;
        v = (v << 4) | d;
    }
    return v;
}

static int startsWith(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

/* Set once a DMA to the EE never completed: the EE is not taking SIF0 any
 * more (crashed, hung with interrupts off). From then on nothing is DMA'd -
 * a second wait would be forever - and the watchdog keeps talking. Measured
 * 2026-11-08: a hung EE left a devlink without this stuck inside its first
 * DMA, watchdog and all, while padman printed "freepad: DMA Busy". */
static int eeDeaf;

/* Copies `size` bytes (a multiple of 16) to EE RAM and waits - at most
 * ~50 ms - for the transfer to finish, so the caller may reuse `src` at once.
 * No completion interrupt on the EE side (attr 0): the game polls its
 * mailboxes, it does not want to be told. */
static int toEE(void *src, u32 dest, int size)
{
    SifDmaTransfer_t t;
    int state;
    int id;
    int waited;

    if (eeDeaf)
        return -1;
    t.src = src;
    t.dest = (void *)dest;
    t.size = size;
    t.attr = 0;

    CpuSuspendIntr(&state);
    id = sceSifSetDma(&t, 1);
    CpuResumeIntr(state);
    if (id == 0)
        return -1;
    for (waited = 0; sceSifDmaStat(id) >= 0; waited += 20) {
        if (waited >= 50000) {
            eeDeaf = 1;
            return -1;
        }
        DelayThread(20);
    }
    return 0;
}

static void sendHello(void)
{
    if (eeHello)
        toEE(hello, eeHello, HELLO_SIZE);
}

static int handlePad(const u8 *p, int len)
{
    if (!eePad || len != PAD_SIZE)
        return 0;
    memcpy(dmabuf, p, PAD_SIZE);
    memset(dmabuf + PAD_SIZE, 0, PAD_SLOT - PAD_SIZE);
    toEE(dmabuf, eePad, PAD_SLOT);
    return 1;
}

static int handleCmd(const u8 *p, int len)
{
    u32 n = (u32)len;
    int size;
    if (!eeCmd || len < 36 || len > CMD_MAX)
        return 0;
    memset(dmabuf, 0, 16);
    memcpy(dmabuf, &n, 4);
    memcpy(dmabuf + 16, p, len);
    size = (16 + len + 15) & ~15;
    toEE(dmabuf, eeCmd, size);
    return 1;
}

static void commitMirror(Mirror *m, u32 half, u32 len, u32 seq)
{
    ctrlbuf[0] = half;
    ctrlbuf[1] = len;
    ctrlbuf[2] = seq;
    ctrlbuf[3] = seq ^ FOOTER_XOR;
    toEE(ctrlbuf, m->ctrl, 16);
    m->active = half;
    m->committed = seq;
    m->assembling = 0;
    m->have = 0;
}

/* One chunk of a pushed file: written straight into the half the game is NOT
 * reading; when the last one is in, one 16-byte DMA flips the control word.
 * A re-push of content the game already has (same seq - the editor uses a
 * content hash) costs nothing but this check. */
static int handleFile(const u8 *p, int len)
{
    u32 h[6];
    u32 id, seq, total, index, count, n, off, i;
    Mirror *m = 0;
    if (len < FILE_HEADER)
        return 0;
    memcpy(h, p, FILE_HEADER);
    id = h[1];
    seq = h[2];
    total = h[3];
    index = h[4];
    count = h[5];
    for (i = 0; i < MAX_FILES; ++i)
        if (mirrors[i].id == id)
            m = &mirrors[i];
    if (!m || seq == 0)
        return 0; /* the game has not asked for this file (yet) */
    if (seq == m->committed)
        return 1;
    if (total == 0) { /* the editor deleted the file */
        commitMirror(m, m->active, 0, seq);
        return 1;
    }
    if (total > m->cap || count == 0 || count > MAX_FILE_CHUNKS ||
        count != (total + FILE_CHUNK - 1) / FILE_CHUNK || index >= count)
        return 0;
    off = index * FILE_CHUNK;
    n = total - off < FILE_CHUNK ? total - off : FILE_CHUNK;
    if ((u32)len != FILE_HEADER + n)
        return 0;
    if (seq != m->assembling) {
        m->assembling = seq;
        m->have = 0;
        memset(m->bits, 0, sizeof(m->bits));
    }
    if (m->bits[index / 32] & (1U << (index % 32)))
        return 1;
    memcpy(dmabuf, p + FILE_HEADER, n);
    if (toEE(dmabuf, m->half[m->active ^ 1] + off, (int)((n + 15) & ~15U)) != 0)
        return 1;
    m->bits[index / 32] |= 1U << (index % 32);
    if (++m->have == count)
        commitMirror(m, m->active ^ 1, total, seq);
    return 1;
}

/* KIND_REGISTER: {id, ctrl, halfA, halfB, cap} per file. Re-registering an id
 * (a rebooted game) starts it over. */
static void registerMirrors(volatile u32 *w, u32 len)
{
    u32 e, i;
    for (e = 0; e + 20 <= len; e += 20, w += 5) {
        Mirror *m = 0;
        for (i = 0; i < MAX_FILES && !m; ++i)
            if (mirrors[i].id == w[0])
                m = &mirrors[i];
        for (i = 0; i < MAX_FILES && !m; ++i)
            if (mirrors[i].id == 0)
                m = &mirrors[i];
        if (!m || (w[1] & 15U) || (w[2] & 15U) || (w[3] & 15U))
            continue;
        memset(m, 0, sizeof(*m));
        m->id = w[0];
        m->ctrl = w[1];
        m->half[0] = w[2];
        m->half[1] = w[3];
        m->cap = w[4];
    }
}

static void handleInbound(const u8 *p, int len, const struct sockaddr_in *from)
{
    u32 magic;
    int ok = 0;
    if (len < 4)
        return;
    memcpy(&magic, p, 4);
    if (magic == PAD_MAGIC)
        ok = handlePad(p, len);
    else if (magic == CMD_MAGIC)
        ok = handleCmd(p, len);
    else if (magic == FILE_MAGIC)
        ok = handleFile(p, len);
    else if (magic == HELLO_PC_MAGIC)
        ok = 2; /* "send your reports here" - the editor's session, every 0.5 s */
    /* Only the session's hello names the editor. A pad or a command may come
     * from another process on the PC (--pad from a shell, while the editor or
     * --run-ps2 owns the session): letting those redirect the reports split a
     * file's ~900 chunks between two sockets, and neither got a whole one. */
    if (ok == 2) {
        memcpy(&peer, from, sizeof(peer));
        havePeer = 1;
    }
}

/* One datagram of a slot. A full slot is 32 of these back to back, and a
 * 1 MB screenshot is 33 such slots in about two seconds, while the stack has
 * room for only a few datagrams in flight (ps2ip's pbuf pool, the adapter's
 * transmit ring). Nothing retransmits one, so: a send that fails is tried
 * again after the stack has had time to drain, and every send is followed by
 * a short sleep that lets the transmit thread run. With this in, 1 MB arrived
 * whole on a PS2 (2026-11-10); the unpaced loop was never measured at this
 * size, so the gap is a precaution, not a proven need. */
static void sendChunk(int sock, int size)
{
    int tries;
    for (tries = 0; tries < 20; ++tries) {
        if (lwip_sendto(sock, txbuf, size, 0, (struct sockaddr *)&peer,
                        sizeof(peer)) >= 0)
            break;
        DelayThread(2000);
    }
    DelayThread(CHUNK_GAP_US);
}

/* A slot is complete when its header carries a new sequence and the footer
 * after the payload echoes it - the EE's DMA writes in ascending order, so
 * the footer lands last. */
static void serviceSlot(int sock, int k)
{
    volatile u32 *s = outSlot[k];
    u32 seq, len, foot, chunks, i, off;
    u32 footAt;
    if (s[0] != OUT_MAGIC)
        return;
    seq = s[1];
    len = s[2];
    if (seq == outDone[k] || len == 0 || len > OUT_MAX)
        return;
    footAt = (16 + len + 15) & ~15U;
    foot = s[footAt / 4];
    if (foot != (seq ^ FOOTER_XOR))
        return; /* still arriving */
    heardEE = 1;
    lastHeardMs = nowMs();
    lastEESeq = seq;
    if (s[3] == KIND_REGISTER) {
        registerMirrors(s + 4, len);
    } else if (s[3] == KIND_HEARTBEAT) {
        /* nothing to forward - being heard was the point */
    } else if (havePeer) {
        const u32 chunkMagic = s[3] == KIND_FILE  ? OUTF_MAGIC
                               : s[3] == KIND_LOG ? OUTL_MAGIC
                                                  : OUT_MAGIC;
        chunks = (len + OUT_CHUNK - 1) / OUT_CHUNK;
        for (i = 0, off = 0; i < chunks; ++i, off += OUT_CHUNK) {
            u32 n = len - off < OUT_CHUNK ? len - off : OUT_CHUNK;
            u32 h[4];
            h[0] = chunkMagic;
            h[1] = seq;
            h[2] = (chunks << 16) | i;
            h[3] = len;
            memcpy(txbuf, h, 16);
            memcpy(txbuf + 16, (const u8 *)s + 16 + off, n);
            sendChunk(sock, (int)(16 + n));
        }
    }
    /* Free the slot even with nobody to send to: a report the editor cannot
     * receive is stale by the next one anyway, and a slot held forever would
     * stop the game reporting at all. */
    outDone[k] = seq;
    hello[4 + k] = seq;
    sendHello();
}

static void serverThread(void *arg)
{
    struct sockaddr_in addr;
    int sock;
    int k;
    (void)arg;

    sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        printf("devlink: socket failed (%d)\n", sock);
        ExitDeleteThread();
        return;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEVLINK_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lwip_bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("devlink: bind to udp/%d failed\n", DEVLINK_PORT);
        lwip_close(sock);
        ExitDeleteThread();
        return;
    }
    printf("devlink: listening on udp/%d (pad 0x%08x, cmd 0x%08x, out %s)\n",
           DEVLINK_PORT, (unsigned int)eePad, (unsigned int)eeCmd,
           outSlot[0] ? "on" : "off");

    for (;;) {
        fd_set rd;
        struct timeval tv;
        int r;
        FD_ZERO(&rd);
        FD_SET(sock, &rd);
        tv.tv_sec = 0;
        tv.tv_usec = 4000; /* the outbound slots' polling period */
        r = lwip_select(sock + 1, &rd, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(sock, &rd)) {
            struct sockaddr_in from;
            int fromLen = sizeof(from);
            int len = lwip_recvfrom(sock, rxbuf, sizeof(rxbuf), 0,
                                    (struct sockaddr *)&from, &fromLen);
            if (len > 0)
                handleInbound(rxbuf, len, &from);
        } else if (r < 0) {
            /* Never spin: a persistently failing socket at any priority is
             * a CPU the IOP does not have (the ps2link r2 lesson). */
            DelayThread(100 * 1000);
        }
        for (k = 0; k < OUT_SLOTS; ++k)
            if (outSlot[k])
                serviceSlot(sock, k);

        /* Phase 5 watchdog: {magic, ms silent, last seq heard, 0}. */
        if (heardEE && havePeer) {
            u32 t = nowMs();
            u32 silent = t - lastHeardMs;
            if (silent >= WATCHDOG_SILENT_MS &&
                t - lastWatchdogMs >= WATCHDOG_REPEAT_MS) {
                u32 w[4];
                w[0] = WATCHDOG_MAGIC;
                w[1] = silent;
                w[2] = lastEESeq;
                w[3] = (u32)eeDeaf; /* 1 = a DMA to the EE never completed */
                lwip_sendto(sock, w, 16, 0, (struct sockaddr *)&peer,
                            sizeof(peer));
                lastWatchdogMs = t;
            }
        }
    }
}

int _start(int argc, char *argv[])
{
    iop_thread_t t;
    int tid;
    int i;

    /* From 0: whether argv[0] is the module's name or its first argument
     * depends on the loader, and a "pad=" prefix cannot be mistaken for a
     * name either way. */
    for (i = 0; i < argc; ++i) {
        if (startsWith(argv[i], "pad="))
            eePad = parseHex(argv[i] + 4);
        else if (startsWith(argv[i], "cmd="))
            eeCmd = parseHex(argv[i] + 4);
        else if (startsWith(argv[i], "hello="))
            eeHello = parseHex(argv[i] + 6);
    }
    if ((eePad | eeCmd | eeHello) & 15U) {
        printf("devlink: a mailbox is not quadword-aligned\n");
        return MODULE_NO_RESIDENT_END;
    }

    /* Outbound slots only when the game gave us somewhere to acknowledge
     * them; without the hello box the EE could never learn their address. */
    if (eeHello) {
        for (i = 0; i < OUT_SLOTS; ++i) {
            outSlot[i] = (volatile u32 *)AllocSysMemory(0, OUT_SLOT, NULL);
            if (!outSlot[i]) {
                printf("devlink: no IOP memory for report slots\n");
                return MODULE_NO_RESIDENT_END;
            }
            outSlot[i][0] = 0;
            outDone[i] = 0;
        }
        memset(hello, 0, sizeof(hello));
        hello[0] = HELLO_MAGIC;
        hello[1] = (u32)outSlot[0];
        hello[2] = (u32)outSlot[1];
        hello[3] = OUT_MAX;
        sendHello();
    }

    t.attr = TH_C;
    t.option = 0;
    t.thread = serverThread;
    t.stacksize = THREAD_STACK;
    t.priority = THREAD_PRIORITY;
    tid = CreateThread(&t);
    if (tid <= 0) {
        printf("devlink: CreateThread failed (%d)\n", tid);
        return MODULE_NO_RESIDENT_END;
    }
    StartThread(tid, NULL);
    return MODULE_RESIDENT_END;
}
