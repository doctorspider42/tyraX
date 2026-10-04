#ifndef SHIM_PS2IP_H
#define SHIM_PS2IP_H

#define AF_INET      2
#define SOCK_STREAM  1
#define INADDR_ANY   0
#define IPPROTO_TCP  6
#define TCP_NODELAY  1

struct in_addr
{
    unsigned long s_addr;
};

struct sockaddr
{
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in
{
    unsigned char sin_len;
    unsigned char sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

/* The fake socket the harness drives. */
int recv(int s, void *buf, int len, unsigned int flags);
int send(int s, const void *buf, int len, unsigned int flags);
int disconnect(int s);
int socket(int domain, int type, int protocol);
int bind(int s, struct sockaddr *name, int namelen);
int listen(int s, int backlog);
int accept(int s, struct sockaddr *addr, int *addrlen);
int setsockopt(int s, int level, int optname, const void *optval, int optlen);

unsigned int htonl(unsigned int n);
unsigned int ntohl(unsigned int n);
unsigned short htons(unsigned short n);
unsigned short ntohs(unsigned short n);

#endif
