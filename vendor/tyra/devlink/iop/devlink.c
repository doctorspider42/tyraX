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
 *           numbered 'TXDO' chunks to whoever sent the last valid inbound
 *           datagram (from this same socket, so a stateful PC firewall lets it
 *           in as a reply), then tells the EE the slot is free again by
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
IRX_ID(MODNAME, 1, 2);

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

#define THREAD_PRIORITY 100
#define THREAD_STACK 0x1000

static u32 eePad;   /* EE physical addresses; 0 = channel off */
static u32 eeCmd;
static u32 eeHello;

static u8 rxbuf[1024] __attribute__((aligned(64)));
static u8 dmabuf[CMD_BOX] __attribute__((aligned(64)));
static u32 hello[HELLO_SIZE / 4] __attribute__((aligned(64)));
static u8 txbuf[16 + OUT_CHUNK] __attribute__((aligned(16)));

static volatile u32 *outSlot[OUT_SLOTS];
static u32 outDone[OUT_SLOTS];

static struct sockaddr_in peer;
static int havePeer;

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

/* Copies `size` bytes (a multiple of 16) to EE RAM and waits for the transfer
 * to finish, so the caller may reuse `src` at once. No completion interrupt on
 * the EE side (attr 0): the game polls its mailboxes, it does not want to be
 * told. */
static int toEE(void *src, u32 dest, int size)
{
    SifDmaTransfer_t t;
    int state;
    int id;

    t.src = src;
    t.dest = (void *)dest;
    t.size = size;
    t.attr = 0;

    CpuSuspendIntr(&state);
    id = sceSifSetDma(&t, 1);
    CpuResumeIntr(state);
    if (id == 0)
        return -1;
    while (sceSifDmaStat(id) >= 0)
        DelayThread(20);
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
    /* Only a datagram that parsed as ours names the editor: a stray packet on
     * the port must not redirect the reports. */
    if (ok) {
        memcpy(&peer, from, sizeof(peer));
        havePeer = 1;
    }
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
    if (havePeer) {
        chunks = (len + OUT_CHUNK - 1) / OUT_CHUNK;
        for (i = 0, off = 0; i < chunks; ++i, off += OUT_CHUNK) {
            u32 n = len - off < OUT_CHUNK ? len - off : OUT_CHUNK;
            u32 h[4];
            h[0] = OUT_MAGIC;
            h[1] = seq;
            h[2] = (chunks << 16) | i;
            h[3] = len;
            memcpy(txbuf, h, 16);
            memcpy(txbuf + 16, (const u8 *)s + 16 + off, n);
            lwip_sendto(sock, txbuf, (int)(16 + n), 0,
                        (struct sockaddr *)&peer, sizeof(peer));
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
