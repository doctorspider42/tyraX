/*
 * devlink - the devkit's channel through the IOP (docs/devlink.md).
 *
 * TyraX original, not a fork. Loaded by a DEBUG game running under ps2link,
 * on top of the IOP ps2link keeps resident: it imports ps2link's ps2ip (lwIP)
 * for its socket and owns nothing else.
 *
 * What it does (phase 1): one low-priority thread blocks in recvfrom() on UDP
 * port DEVLINK_PORT. A datagram whose first word names a known channel is
 * DMA'd straight into that channel's mailbox in EE RAM; the game reads the
 * mailbox through the uncached segment. The EE never waits for the network,
 * never makes an RPC, and never touches a file to get a Remote Pad state.
 *
 * The mailbox addresses come in as the module's ARGUMENTS
 * ("pad=0x00123440"), so setup needs no SIF RPC either - the SIF RPC path has
 * crashed under heavy devkit traffic before (docs/devkit.md), and nothing here
 * needs a reply.
 *
 * Rules (each one is a past incident somewhere in docs/):
 *  - priority BELOW audsrv (40) and below ps2link's servers: the IOP schedules
 *    strictly by priority and TCP traffic has starved audsrv before;
 *  - one DMA in flight, waited out before the source buffer is reused;
 *  - payloads are absolute state with a sequence number, so a datagram lost
 *    to UDP is simply replaced by the next one.
 */

#include "irx_imports.h"

#define MODNAME "tyrax_devlink"
IRX_ID(MODNAME, 1, 1);

#define DEVLINK_PORT 18198

/* Channel 1: Remote Pad. The datagram IS livepad.bin (src/livepad.hpp):
 * u32 magic 'TXPD', u32 version, u32 seq, u32 flags, u32 buttons[2],
 * s8 axes[2][4], u32 footer = seq ^ 0x5A5A5A5A -> 36 bytes. The mailbox is 48
 * (three quadwords - SIF DMA moves whole quadwords). */
#define PAD_MAGIC 0x44505854U
#define PAD_SIZE 36
#define PAD_SLOT 48

#define THREAD_PRIORITY 100
#define THREAD_STACK 0x800

static u32 eePad; /* EE physical address of the pad mailbox; 0 = channel off */

static u8 rxbuf[256] __attribute__((aligned(64)));
static u8 dmabuf[PAD_SLOT] __attribute__((aligned(64)));

static u32 rxCount;
static u32 dmaFail;

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
 * the EE side (attr 0): the game polls the mailbox, it does not want to be
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

static void handlePad(const u8 *p, int len)
{
    u32 magic;
    if (!eePad || len != PAD_SIZE)
        return;
    memcpy(&magic, p, 4);
    if (magic != PAD_MAGIC)
        return;
    memcpy(dmabuf, p, PAD_SIZE);
    memset(dmabuf + PAD_SIZE, 0, PAD_SLOT - PAD_SIZE);
    if (toEE(dmabuf, eePad, PAD_SLOT) != 0)
        dmaFail++;
}

static void serverThread(void *arg)
{
    struct sockaddr_in addr;
    int sock;
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
    printf("devlink: listening on udp/%d (pad mailbox 0x%08x)\n", DEVLINK_PORT,
           (unsigned int)eePad);

    for (;;) {
        struct sockaddr_in from;
        int fromLen = sizeof(from);
        int len = lwip_recvfrom(sock, rxbuf, sizeof(rxbuf), 0,
                                (struct sockaddr *)&from, &fromLen);
        if (len <= 0) {
            /* Never spin: a persistently failing socket at any priority is a
             * CPU the IOP does not have (the ps2link r2 lesson). */
            DelayThread(100 * 1000);
            continue;
        }
        rxCount++;
        handlePad(rxbuf, len);
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
    }
    if (eePad & 15U) {
        printf("devlink: pad mailbox 0x%08x is not quadword-aligned\n",
               (unsigned int)eePad);
        return MODULE_NO_RESIDENT_END;
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
