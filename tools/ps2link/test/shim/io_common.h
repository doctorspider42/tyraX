#ifndef SHIM_IO_COMMON_H
#define SHIM_IO_COMMON_H

typedef struct
{
    unsigned int mode;
    unsigned int attr;
    unsigned int size;
    unsigned char ctime[8];
    unsigned char atime[8];
    unsigned char mtime[8];
    unsigned int hisize;
} io_stat_t;

typedef struct
{
    io_stat_t stat;
    char name[256];
    void *privdata;
} io_dirent_t;

#endif
