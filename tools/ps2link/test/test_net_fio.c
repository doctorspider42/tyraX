/* Host harness for tools/ps2link/build/iop/net_fio.c - run it with
 * tools/ps2link/test/run.ps1 (or run.sh).
 *
 * ps2link only runs on real hardware (PCSX2 has no ps2link), so the host:
 * protocol code used to be unverifiable outside "flash it and see". This
 * compiles the REAL patched IOP source against the stub IOP/lwip headers in
 * shim/ and drives it with a scripted fake socket, which is enough to pin down
 * the framing / EOF / clamping behaviour that tyrax.patch changes.
 *
 * Point it at the pristine upstream file instead (run.ps1 -Pristine) and it
 * FAILS - that A/B is the actual evidence, so keep both halves working.
 *
 * Every fake recv() is counted and the harness longjmps out past a call
 * ceiling, so a regression back to the "spin forever on a closed peer" bug
 * shows up as a failure instead of a hung test run.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <types.h>
#include <io_common.h>
#include <ps2ip.h>
/* NB: hostlink.h has no include guard, so only net_fio.c may pull it in. */

/* --- the fake socket ------------------------------------------------------ */

#define RECV_CALL_CEILING 2000

static unsigned char rx[65536]; /* bytes the "PC" has queued for the PS2 */
static int rx_len;
static int rx_pos;
static int rx_eof;      /* once drained, recv() returns 0 (peer closed) */
static int recv_calls;
static int recv_chunk = 0; /* 0 = give everything asked for */
static int disconnects;
static jmp_buf spin_escape;

static void rx_reset(void)
{
    rx_len = 0;
    rx_pos = 0;
    rx_eof = 0;
    recv_calls = 0;
    disconnects = 0;
}

static void rx_push(const void *data, int len)
{
    memcpy(&rx[rx_len], data, len);
    rx_len += len;
}

int recv(int s, void *buf, int len, unsigned int flags)
{
    int avail;
    (void)s;
    (void)flags;

    if (++recv_calls > RECV_CALL_CEILING) {
        /* Bail out of the current test rather than the process, so one
         * spinning function does not hide the rest of the results. */
        longjmp(spin_escape, 1);
    }

    avail = rx_len - rx_pos;
    if (avail <= 0) {
        return rx_eof ? 0 : -1;
    }
    if (len > avail) {
        len = avail;
    }
    if (recv_chunk > 0 && len > recv_chunk) {
        len = recv_chunk; /* force short reads, like a real TCP stream */
    }
    memcpy(buf, &rx[rx_pos], len);
    rx_pos += len;
    return len;
}

int send(int s, const void *buf, int len, unsigned int flags)
{
    (void)s;
    (void)buf;
    (void)flags;
    return len; /* the PS2's requests are not what we are testing */
}

int disconnect(int s)
{
    (void)s;
    disconnects++;
    return 0;
}

int socket(int d, int t, int p) { (void)d; (void)t; (void)p; return 3; }
int bind(int s, struct sockaddr *n, int l) { (void)s; (void)n; (void)l; return 0; }
int listen(int s, int b) { (void)s; (void)b; return 0; }
int accept(int s, struct sockaddr *a, int *l) { (void)s; (void)a; (void)l; return 4; }
int setsockopt(int s, int lv, int on, const void *ov, int ol)
{
    (void)s; (void)lv; (void)on; (void)ov; (void)ol;
    return 0;
}

void DelayThread(int usec) { (void)usec; }
void ExitDeleteThread(void) {}

static unsigned int swap32(unsigned int n)
{
    return ((n & 0xff) << 24) | ((n & 0xff00) << 8) |
           ((n & 0xff0000) >> 8) | ((n & 0xff000000u) >> 24);
}
static unsigned short swap16(unsigned short n)
{
    return (unsigned short)(((n & 0xff) << 8) | ((n & 0xff00) >> 8));
}
unsigned int htonl(unsigned int n) { return swap32(n); }
unsigned int ntohl(unsigned int n) { return swap32(n); }
unsigned short htons(unsigned short n) { return swap16(n); }
unsigned short ntohs(unsigned short n) { return swap16(n); }

/* Pull in the real file, statics and all, so the tests can place the socket. */
#include "net_fio.c"

/* --- helpers to build the replies a PC would send ------------------------- */

static void queue_file_rly(unsigned int cmd, int retval)
{
    pko_pkt_file_rly r;
    r.cmd = htonl(cmd);
    r.len = htons((unsigned short)sizeof(r));
    r.retval = htonl((unsigned int)retval);
    rx_push(&r, sizeof(r));
}

static void queue_read_rly(int retval, int nbytes)
{
    pko_pkt_read_rly r;
    r.cmd = htonl(PKO_READ_RLY);
    r.len = htons((unsigned short)sizeof(r));
    r.retval = htonl((unsigned int)retval);
    r.nbytes = htonl((unsigned int)nbytes);
    rx_push(&r, sizeof(r));
}

static int failures;

static void check(const char *what, int ok)
{
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        failures++;
    }
}

/* --- the tests ------------------------------------------------------------ */

static void test_eof_does_not_spin(void)
{
    char buf[128];
    int ret;

    printf("closed peer mid-transfer\n");
    rx_reset();
    pko_fileio_sock = 7;
    rx_eof = 1; /* nothing queued: recv() returns 0 straight away */

    ret = pko_recv_bytes(pko_fileio_sock, buf, (int)sizeof(buf));
    check("pko_recv_bytes() reports an error instead of looping", ret < 0);
    check("the dead socket was closed", disconnects == 1);
    check("and pko_fileio_sock was invalidated", pko_fileio_sock < 0);
}

static void test_eof_halfway(void)
{
    char buf[64];
    int ret;

    printf("peer closes after delivering half the body\n");
    rx_reset();
    pko_fileio_sock = 7;
    rx_push("0123456789ABCDEF", 16);
    rx_eof = 1;

    ret = pko_recv_bytes(pko_fileio_sock, buf, 32);
    check("a short-then-closed stream errors out", ret < 0);
    check("without an unbounded number of recv() calls", recv_calls < 10);
}

static void test_negative_length(void)
{
    char buf[16];
    printf("negative byte count\n");
    rx_reset();
    pko_fileio_sock = 7;
    check("pko_recv_bytes() rejects it", pko_recv_bytes(7, buf, -8) < 0);
}

static void test_read_reply_larger_than_buffer(void)
{
    struct
    {
        char buf[64];
        unsigned char canary[16];
    } target;
    int ret;
    int canary_intact;

    printf("READ_RLY claiming more bytes than the caller asked for\n");
    rx_reset();
    pko_fileio_sock = 7;
    memset(&target, 0, sizeof(target));
    memset(target.canary, 0xAA, sizeof(target.canary));

    /* The PC says "here come 4000 bytes" for a 64-byte request. */
    queue_read_rly(4000, 4000);
    rx_eof = 1;

    ret = pko_read_file(11, target.buf, (int)sizeof(target.buf));

    canary_intact = 1;
    {
        unsigned int i;
        for (i = 0; i < sizeof(target.canary); i++) {
            if (target.canary[i] != 0xAA) {
                canary_intact = 0;
            }
        }
    }

    check("pko_read_file() refuses the oversized reply", ret < 0);
    check("nothing was written past the caller's buffer", canary_intact);
}

static void test_absurd_read_length_drops_the_link(void)
{
    char buf[64];
    int ret;

    printf("READ_RLY with a wild byte count\n");
    rx_reset();
    pko_fileio_sock = 7;

    /* Too big to be a real packet, so there is nothing sane to drain - the
     * connection has to go rather than have us eat the rest of the stream. */
    queue_read_rly(0x40000000, 0x40000000);
    rx_eof = 1;

    ret = pko_read_file(11, buf, (int)sizeof(buf));
    check("pko_read_file() refuses it", ret < 0);
    check("and drops the link instead of draining forever", pko_fileio_sock < 0);
    check("without an unbounded number of recv() calls", recv_calls < 10);
}

static void test_unexpected_reply_keeps_stream_framed(void)
{
    int first;
    int second;

    printf("unexpected reply type in the stream\n");
    rx_reset();
    pko_fileio_sock = 7;

    /* A stray CLOSE_RLY, then the OPEN_RLY the caller is actually waiting
     * for. Without draining the stray packet's 4-byte body, the next header
     * read starts 4 bytes off and every later reply is garbage. */
    queue_file_rly(PKO_CLOSE_RLY, 0);
    queue_file_rly(PKO_OPEN_RLY, 7);
    queue_file_rly(PKO_OPEN_RLY, 9);
    rx_eof = 1;

    first = pko_open_file("host:whatever", 1);
    second = pko_open_file("host:whatever", 1);

    check("the call that met the stray packet fails", first < 0);
    check("the NEXT call still parses correctly (stream resynced)", second == 7);
    check("the socket was not torn down over it", pko_fileio_sock == 7);
}

static void test_short_reads_are_reassembled(void)
{
    char buf[300];
    int ret;

    printf("reply split across many TCP segments\n");
    rx_reset();
    pko_fileio_sock = 7;
    recv_chunk = 3; /* 3 bytes at a time */

    queue_read_rly(256, 256);
    {
        int i;
        for (i = 0; i < 256; i++) {
            unsigned char b = (unsigned char)i;
            rx_push(&b, 1);
        }
    }
    rx_eof = 1;

    ret = pko_read_file(11, buf, (int)sizeof(buf));
    recv_chunk = 0;

    check("pko_read_file() returns the full payload", ret == 256);
    check("the payload bytes are in order", (unsigned char)buf[0] == 0 &&
                                                (unsigned char)buf[255] == 255);
}

static void test_truncated_header_length(void)
{
    pko_pkt_hdr bad;
    char buf[64];
    int ret;

    printf("reply whose len field is below the header size\n");
    rx_reset();
    pko_fileio_sock = 7;

    bad.cmd = htonl(PKO_OPEN_RLY);
    bad.len = htons(2); /* nonsense: smaller than the 6-byte header */
    rx_push(&bad, sizeof(bad));
    rx_eof = 1;

    ret = pko_accept_pkt(7, buf, (int)sizeof(buf), PKO_OPEN_RLY);
    check("pko_accept_pkt() errors out", ret < 0);
    check("and drops the out-of-frame connection", pko_fileio_sock < 0);
}

static void run(const char *name, void (*fn)(void))
{
    if (setjmp(spin_escape) != 0) {
        printf("  %-62s %s\n", "did not spin on a closed/short stream", "FAIL");
        printf("    (recv() was called %d+ times - %s is looping)\n",
               RECV_CALL_CEILING, name);
        failures++;
        recv_chunk = 0;
        return;
    }
    fn();
}

int main(void)
{
    printf("ps2link net_fio.c host harness\n\n");

    run("pko_recv_bytes", test_eof_does_not_spin);
    run("pko_recv_bytes", test_eof_halfway);
    run("pko_recv_bytes", test_negative_length);
    run("pko_read_file", test_read_reply_larger_than_buffer);
    run("pko_read_file", test_absurd_read_length_drops_the_link);
    run("pko_accept_pkt", test_unexpected_reply_keeps_stream_framed);
    run("pko_read_file", test_short_reads_are_reassembled);
    run("pko_accept_pkt", test_truncated_header_length);

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
