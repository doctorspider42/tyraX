/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2003,2004 adresd (adresd_ps2dev@yahoo.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <types.h>
#include <ioman.h>
#include <sysclib.h>
#include <stdio.h>
#include <thbase.h>
#include <intrman.h>
#include <sifman.h>
#include <sifrpc.h>
#include <modload.h>
#include <ps2lib_err.h>
#include <poweroff.h>
#include <sifcmd.h>

#include <spu2regs.h>

#include "ps2ip.h"
#include "hostlink.h"
#include "net_fsys.h"
#include "globals.h"

#define BUF_SIZE 1024
static char recvbuf[BUF_SIZE] __attribute__((aligned(16)));
static unsigned int rpc_data[1024 / 4] __attribute__((aligned(16)));

int excepscrdump = 1;

#define PKO_DMA_DEST ((void *)0x200ff800)
//unsigned int *dma_ptr =(unsigned int*)(0x20100000-2048);

//////////////////////////////////////////////////////////////////////////
/* TyraX: silence the SPU2.
 *
 * The SPU2 is a separate block from the IOP core and it keeps its register
 * state across an IOP reset, so it happily carries on looping whatever voices
 * the dead game had keyed and mixing whatever autodma left in its input. That
 * is why a redeploy or a "Stop on PS2" used to leave sound effects droning and
 * music grinding, and why the editor resorted to execee'ing a silencer ELF
 * afterwards just to get an audsrv_init() to run.
 *
 * Doing it here instead makes it unconditional: nothing has to be deployed,
 * nothing has to be timed, and it also covers the redeploy case the silencer
 * never did.
 *
 * Every write below is either a zero or a key-off, so this can only ever make
 * the chip quieter - it cannot set a bit that was not set before, which rules
 * out tripping some undocumented reset/enable line by accident. And it is safe
 * to clobber all of it because libsd's init (what audsrv_init() ends up
 * calling) reprograms exactly this set for both cores - verified by
 * disassembling $PS2SDK/iop/irx/libsd.irx and mapping its halfword stores onto
 * the register map: VMIXL/VMIXEL/VMIXR/VMIXER, MMIX, CORE_ATTR, KON, KOFF and
 * the whole SD_P volume block, master volume included. So the next game's
 * audsrv_init() puts everything back.
 *
 * Deliberately NOT touched: the IOP DMA channels (SD_DMA_*). MMIX = 0 already
 * unroutes the autodma input from the output mix, so a transfer still in
 * flight is inaudible and dies with the IOP reset a moment later - no reason to
 * poke a live DMA controller to achieve nothing extra.
 */
void spu2Silence(void)
{
    int core;
    int voice;

    for (core = 0; core < 2; core++) {
        /* Order matters: everything that has to be *heard* to take effect is
         * written first, while the core is still in its normal running state,
         * and CORE_ATTR is cleared last. Clearing it first would risk the core
         * ignoring the writes below - a silence that silently does not happen. */

        /* Key off all 24 voices. Writing 0xffff to both halves covers voices
         * 0-23 without having to care which half holds which range; bits above
         * 23 address voices that do not exist. */
        U16_REGISTER_WRITE(SD_A_KOFF_HI(core), 0xffff);
        U16_REGISTER_WRITE(SD_A_KOFF_LO(core), 0xffff);

        /* A keyed-off voice still bleeds through its ADSR release, so zero the
         * per-voice volumes too - that is immediate. */
        for (voice = 0; voice < 24; voice++) {
            U16_REGISTER_WRITE(SD_VP_VOLL(core, voice), 0);
            U16_REGISTER_WRITE(SD_VP_VOLR(core, voice), 0);
        }

        /* Unroute every voice from the dry and reverb mixes... */
        U16_REGISTER_WRITE(SD_S_VMIXL_HI(core), 0);
        U16_REGISTER_WRITE(SD_S_VMIXL_LO(core), 0);
        U16_REGISTER_WRITE(SD_S_VMIXR_HI(core), 0);
        U16_REGISTER_WRITE(SD_S_VMIXR_LO(core), 0);
        U16_REGISTER_WRITE(SD_S_VMIXEL_HI(core), 0);
        U16_REGISTER_WRITE(SD_S_VMIXEL_LO(core), 0);
        U16_REGISTER_WRITE(SD_S_VMIXER_HI(core), 0);
        U16_REGISTER_WRITE(SD_S_VMIXER_LO(core), 0);

        /* ...and unroute the core input, which is the autodma stream. This is
         * the half the looping-voice fix alone would miss. */
        U16_REGISTER_WRITE(SD_P_MMIX(core), 0);

        /* Finally the output stage, so anything still ringing (a reverb tail)
         * goes nowhere either. */
        U16_REGISTER_WRITE(SD_P_MVOLL(core), 0);
        U16_REGISTER_WRITE(SD_P_MVOLR(core), 0);
        U16_REGISTER_WRITE(SD_P_EVOLL(core), 0);
        U16_REGISTER_WRITE(SD_P_EVOLR(core), 0);
        U16_REGISTER_WRITE(SD_P_AVOLL(core), 0);
        U16_REGISTER_WRITE(SD_P_AVOLR(core), 0);
        U16_REGISTER_WRITE(SD_P_BVOLL(core), 0);
        U16_REGISTER_WRITE(SD_P_BVOLR(core), 0);

        /* Last: stop the core consuming/emitting anything at all - clears the
         * DMA transfer mode, the effect (reverb) enable and the IRQ enable. */
        U16_REGISTER_WRITE(SD_CORE_ATTR(core), 0);
    }
}

//////////////////////////////////////////////////////////////////////////
static void
pkoExecIop(char *buf, int len)
{
    pko_pkt_execiop_req *cmd;
    int retval;
    int arglen;
    char *path;
    char *args;
    unsigned int argc;
    int id;
    int i;

    cmd = (pko_pkt_execiop_req *)buf;

    dbgprintf("IOP cmd: EXECIOP\n");

    if (len != sizeof(pko_pkt_execiop_req)) {
        dbgprintf("IOP cmd: exec_iop got a broken packet (%d)!\n", len);
        return;
    }

    // Make sure arg vector is null-terminated
    cmd->argv[PKO_MAX_PATH - 1] = '\0';

    printf("IOP cmd: %ld args\n", ntohl(cmd->argc));

    path = &cmd->argv[0];
    args = &cmd->argv[strlen(cmd->argv) + 1];
    argc = ntohl(cmd->argc);

    printf("IOP binary path: %s\n", path);

    /* TyraX: argc comes off the wire. It was compared as `i < (argc - 1)`
     * with i an int and argc unsigned, so argc == 0 wrapped the bound to
     * 0xffffffff and the loop walked strlen() across all of IOP RAM until it
     * faulted. Clamp to the room argv actually has. */
    if (argc == 0) {
        argc = 1;
    }

    arglen = 0;
    for (i = 0; i < (int)(argc - 1); i++) {
        if (arglen >= PKO_MAX_PATH) {
            printf("IOP cmd: exec_iop arg vector overruns the packet\n");
            return;
        }
        printf("arg %d: %s (%d)\n", i, &args[arglen], arglen);
        arglen += strlen(&args[arglen]) + 1;
    }

    id = LoadStartModule(cmd->argv, arglen, args, &retval);

    if (id < 0) {
        printf("Error loading module: ");
        switch (-id) {
            case E_IOP_INTR_CONTEXT:
                printf("IOP is in exception context.\n");
                break;
            case E_IOP_DEPENDANCY:
                printf("Inter IRX dependancy error.\n");
                break;
            case E_LF_NOT_IRX:
                printf("Invalid IRX module.\n");
                break;
            case E_LF_FILE_NOT_FOUND:
                printf("Unable to open executable file.\n");
                break;
            case E_LF_FILE_IO_ERROR:
                printf("Error while accessing file.\n");
                break;
            case E_IOP_NO_MEMORY:
                printf("IOP is out of memory.\n");
                break;
            default:
                printf("Unknow error code: %d\n", -retval);
                break;
        }
    } else {
        printf("loadmodule: id %d, ret %d\n", id, retval);
    }
}

//////////////////////////////////////////////////////////////////////////
unsigned int
pkoSetSifDma(void *dest, void *src, unsigned int length, unsigned int mode)
{
    struct t_SifDmaTransfer sendData;
    int oldIrq;
    int id;

    sendData.src = (unsigned int *)src;
    sendData.dest = (unsigned int *)dest;
    sendData.size = length;
    sendData.attr = mode;

    CpuSuspendIntr(&oldIrq);
    id = sceSifSetDma(&sendData, 1);
    CpuResumeIntr(oldIrq);

    return id;
}

//////////////////////////////////////////////////////////////////////////
unsigned int
pkoSendSifCmd(unsigned int cmd, void *src, unsigned int len)
{
    unsigned int dmaId;
    unsigned int dmaLen;

    rpc_data[0] = cmd;

    memcpy(&rpc_data[1], src,
           (len > sizeof(rpc_data) ? sizeof(rpc_data) : len));

    len = len > sizeof(rpc_data) ? sizeof(rpc_data) : len;

    /* Round DMA size up to 16-byte boundary Ă˘â‚¬â€ť real HW SIF DMA
       requires aligned transfer sizes.  rpc_data is 1KB so the
       extra bytes are just trailing zeros. */
    dmaLen = (len + 15) & ~15;

    dmaId = pkoSetSifDma(PKO_DMA_DEST, rpc_data, dmaLen, 4);

    if (dmaId == 0) {
        printf("IOP: sifSendCmd %x failed\n", cmd);
        return -1;
    }
    return 0;
}


//////////////////////////////////////////////////////////////////////////
static void
pkoExecEE(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_EXECEE, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoGSExec(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_GSEXEC, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoNetDump(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_NETDUMP, buf, len);
};
//////////////////////////////////////////////////////////////////////////
static void
pkoScrDump(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_SCRDUMP, buf, len);
};

//////////////////////////////////////////////////////////////////////////
static void
pkoPowerOff()
{
    PoweroffShutdown();
}

//////////////////////////////////////////////////////////////////////////
static void
pkoReset(char *buf, int len)
{
    dbgprintf("IOP cmd: RESET\n");

    if (len != sizeof(pko_pkt_reset_req)) {
        dbgprintf("IOP cmd: exec_ee got a broken packet (%d)!\n", len);
        return;
    }

    /* PRIVATE: valid reset only, while old tty/network is still resident.
     * The message may be lost during restart; silence is not nonreceipt. */
    printf("RESETDIAG IOP_RECEIVED_VALID_RESET\n");

    /* TyraX: before anything else, while we still own the IOP. The SPU2
     * survives the reset the EE is about to perform, so if the dead game's
     * voices are not stopped here they keep playing straight through it. */
    spu2Silence();

    /* TyraX: this used to fsysUnmount() and DelDrv("tty") here, because the EE
     * was about to reboot the IOP out from under both. It does not any more -
     * a reset stops the running program and leaves ps2link armed (see
     * ee/cmdHandler.c) - so tearing them down would only cost us the host:
     * mount and the console log that the NEXT deploy needs. It was a race
     * besides: a game of ours polls host: every frame, and this ran while
     * those transfers were in flight. */
    pkoSendSifCmd(PKO_RPC_RESET, buf, len);
};

static void
pkoStopVU(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_STOPVU, buf, len);
};

static void
pkoStartVU(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_STARTVU, buf, len);
};

static void
pkoDumpMem(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_DUMPMEM, buf, len);
};

static void
pkoDumpReg(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_DUMPREG, buf, len);
};

static void
pkoWriteMem(char *buf, int len)
{
    pkoSendSifCmd(PKO_RPC_WRITEMEM, buf, len);
};

//////////////////////////////////////////////////////////////////////////
static void
cmdListener(int sock)
{
    while (1) {
        struct sockaddr_in remote_addr;
        int len;
        int addrlen;
        pko_pkt_hdr *header;
        unsigned int cmd;

        addrlen = sizeof(remote_addr);
        len = recvfrom(sock, &recvbuf[0], BUF_SIZE, 0,
                       (struct sockaddr *)&remote_addr,
                       &addrlen);
        dbgprintf("IOP cmd: received packet (%d)\n", len);

        if (len < 0) {
            /* TyraX: an error that persists (the interface going down, say)
             * used to spin this thread flat out at priority 60 and starve the
             * IOP - the console stopped answering ps2client and had to be
             * reset. Back off between retries. */
            printf("IOP: cmdListener: recvfrom error (%d)\n", len);
            DelayThread(100000); // 100 ms
            continue;
        }
        if (len < (int)sizeof(pko_pkt_hdr)) {
            continue;
        }

        header = (pko_pkt_hdr *)recvbuf;
        cmd = ntohl(header->cmd);
        switch (cmd) {

            case PKO_EXECIOP_CMD:
                pkoExecIop(recvbuf, len);
                break;
            case PKO_EXECEE_CMD:
                pkoExecEE(recvbuf, len);
                break;
            case PKO_POWEROFF_CMD:
                pkoPowerOff();
                break;
            case PKO_RESET_CMD:
                pkoReset(recvbuf, len);
                break;
            case PKO_SCRDUMP_CMD:
                excepscrdump = 1;
                pkoScrDump(recvbuf, len);
                break;
            case PKO_NETDUMP_CMD:
                excepscrdump = 0;
                pkoNetDump(recvbuf, len);
                break;
            case PKO_START_VU:
                pkoStartVU(recvbuf, len);
                break;
            case PKO_STOP_VU:
                pkoStopVU(recvbuf, len);
                break;
            case PKO_DUMP_MEM:
                pkoDumpMem(recvbuf, len);
                break;
            case PKO_DUMP_REG:
                pkoDumpReg(recvbuf, len);
                break;
            case PKO_GSEXEC_CMD:
                pkoGSExec(recvbuf, len);
                break;
            case PKO_WRITE_MEM:
                pkoWriteMem(recvbuf, len);
                break;
            default:
                dbgprintf("IOP cmd: Uknown cmd received\n");
                break;
        }
        dbgprintf("IOP cmd: waiting for next pkt\n");
    }
}

static void
cmdPowerOff(void *arg)
{
#ifdef PWOFFONRESET
    pkoPowerOff();
#else
    pko_pkt_reset_req reset;

    reset.cmd = htonl(PKO_RESET_CMD);
    reset.len = 0;

    pkoReset((char *)&reset, sizeof(reset));
#endif
}

//////////////////////////////////////////////////////////////////////////
static void
cmdThread(void *arg)
{
    struct sockaddr_in serv_addr;
    //    struct sockaddr_in remote_addr;
    int sock;
    int ret;

    dbgprintf("IOP cmd: Server Thread Started.\n");

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        dbgprintf("IOP cmd: Socket error %d\n", sock);
        ExitDeleteThread();
    }

    memset((void *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PKO_CMD_PORT);

    ret = bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        dbgprintf("IOP cmd: Udp bind error (%d)\n", sock);
        ExitDeleteThread();
    }

    // Do tha thing
    dbgprintf("IOP cmd: Listening\n");
    printf("RESETDIAG IOP_UDP_BOUND port=18194\n");

    cmdListener(sock);

    ExitDeleteThread();
}


//////////////////////////////////////////////////////////////////////////
int cmdHandlerInit(void)
{
    iop_thread_t thread;
    int pid;

    dbgprintf("IOP cmd: Starting thread\n");

    SifInitRpc(0);
    SetPowerButtonHandler(cmdPowerOff, NULL);

    thread.attr = 0x02000000;
    thread.option = 0;
    thread.thread = (void *)cmdThread;
    thread.stacksize = 0x800;
    /* TyraX: was 60, i.e. BELOW the host: file server (which this patch moves
     * to 10 - see iop/net_fsys.c). IOP scheduling is strictly priority-based,
     * so a game that keeps host: busy starved this thread completely and the
     * console answered no commands at all: "reset" from a running game was
     * simply never processed, while the very same command against an idle
     * ps2link worked instantly. Measured with a game polling ~10 host: files
     * per frame - three resets over 20 s, no reply, and the console had to be
     * power-cycled. A control channel must preempt bulk I/O, so this thread
     * takes USER_HIGHEST_PRIORITY and the file server sits one step below it.
     * (9 is the highest a user thread may have: an earlier attempt at 8 made
     * CreateThread fail and ps2link came up with no command listener at all -
     * it answered ping and nothing else.) The thread spends its life blocked
     * in recvfrom(), so the file server loses nothing. */
    thread.priority = USER_HIGHEST_PRIORITY;

    pid = CreateThread(&thread);
    if (pid >= 0) {
        int ret;
        ret = StartThread(pid, 0);
        if (ret < 0) {
            /* TyraX: dbgprintf, so in a release build this failed in complete
             * silence - and losing this thread means ps2link boots, answers
             * ping, serves nothing and takes no commands, with no way left to
             * ask it what happened. Say it out loud. */
            printf("IOP cmd: could not start the command thread (%d)\n", ret);
            DeleteThread(pid);
        }
    } else {
        printf("IOP cmd: could not create the command thread (%d) - ps2link "
               "will not answer any command\n",
               pid);
    }
    return 0;
}
