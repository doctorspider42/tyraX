/* The script debugger's WSL relay (docs/script-debugger.md).
 *
 * On Windows the PS2 toolchain - GDB included - is a Linux bundle run inside
 * WSL, and WSL's default (NAT) networking cannot reach a socket listening on
 * Windows' 127.0.0.1. The other direction works: Windows can connect to a
 * socket listening on WSL's 127.0.0.1 (localhost forwarding). So this listens
 * on two WSL ports and pipes bytes between them:
 *
 *   GDB (in WSL)  ->  127.0.0.1:PORT      relay   127.0.0.1:PORT+1  <-  the editor
 *
 * The editor connects first and waits; the next GDB is paired with it. When
 * either side leaves, both are closed and the pairing starts over. Started by
 * the editor through gdb-relay.sh, which compiles this with the WSL host's cc.
 */
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int listen_on(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    struct sockaddr_in a;
    if (s < 0) return -1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(s, 1) != 0) {
        fprintf(stderr, "gdb-relay: cannot listen on 127.0.0.1:%d: %s\n", port, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

static int accept_one(int listener) {
    int c = accept(listener, NULL, NULL);
    int one = 1;
    if (c >= 0) setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return c;
}

/* Copies until either side closes. */
static void pump(int a, int b) {
    char buf[16384];
    struct pollfd p[2];
    for (;;) {
        int i;
        p[0].fd = a;
        p[0].events = POLLIN;
        p[1].fd = b;
        p[1].events = POLLIN;
        if (poll(p, 2, -1) < 0) {
            if (errno == EINTR) continue;
            return;
        }
        for (i = 0; i < 2; ++i) {
            if (p[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t n = recv(p[i].fd, buf, sizeof(buf), 0), off = 0;
                if (n <= 0) return;
                while (off < n) {
                    ssize_t w = send(p[i ? 0 : 1].fd, buf + off, (size_t)(n - off), MSG_NOSIGNAL);
                    if (w <= 0) return;
                    off += w;
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    int port, lg, le;
    if (argc != 2 || (port = atoi(argv[1])) <= 0 || port > 65534) {
        fprintf(stderr, "usage: gdb-relay <gdb-port>  (the editor connects to port + 1)\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    lg = listen_on(port);
    le = listen_on(port + 1);
    if (lg < 0 || le < 0) return 1;
    printf("gdb-relay: GDB on 127.0.0.1:%d, editor on 127.0.0.1:%d\n", port, port + 1);
    fflush(stdout);
    for (;;) {
        int editor = accept_one(le), gdb = -1;
        if (editor < 0) continue;
        /* Wait for GDB - or for the editor to give up on this connection. */
        for (;;) {
            struct pollfd p[2];
            p[0].fd = editor;
            p[0].events = POLLIN;
            p[1].fd = lg;
            p[1].events = POLLIN;
            if (poll(p, 2, -1) < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (p[0].revents) {
                char x;
                if (recv(editor, &x, 1, MSG_PEEK) <= 0) break; /* the editor left */
            }
            if (p[1].revents & POLLIN) {
                gdb = accept_one(lg);
                break;
            }
        }
        if (gdb >= 0) pump(editor, gdb);
        if (gdb >= 0) close(gdb);
        close(editor);
    }
}
