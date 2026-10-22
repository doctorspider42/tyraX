/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2004 adresd (adresd_ps2dev@yahoo.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <iopheap.h>
#include <debug.h>
#include <startup.h>
#include <ee_regs.h>

#include "excepHandler.h"
#include "byteorder.h"
#include "ps2regs.h"
#include "hostlink.h"
#include "ps2link.h"
#include "globals.h"

#define STAT_SIF0      0x20

////////////////////////////////////////////////////////////////////////
// Prototypes
static int cmdThread(void *arg);
static int pkoExecEE(pko_pkt_execee_req *cmd);
static int pkoStopVU(pko_pkt_stop_vu *);
static int pkoStartVU(pko_pkt_start_vu *);
static int pkoDumpMem(pko_pkt_mem_io *);
static int pkoDumpReg(pko_pkt_dump_regs *);
void pkoReset(void);
static int pkoLoadElf(char *path);
static int pkoGSExec(pko_pkt_gsexec_req *);
static int pkoWriteMem(pko_pkt_mem_io *);

////////////////////////////////////////////////////////////////////////

int userThreadID = 0;
static struct sargs_start userArgs;
static int cmdThreadID = 0;
static char userThreadStack[16 * 1024] __attribute__((aligned(16)));
static char cmdThreadStack[16 * 1024] __attribute__((aligned(64)));
static char dataBuffer[16384] __attribute__((aligned(16)));


int sifCmdSema;
int sif0HandlerId = 0;
// XXX: Hardcoded address atm.. Should be configurable!!
unsigned int *sifDmaDataPtr = (unsigned int *)(0x20100000 - 2048);
int excepscrdump = 1;


////////////////////////////////////////////////////////////////////////
// Create the argument struct to send to the user thread
static int makeArgs(int cmdargc, char *cmdargv, struct sargs_start *arg_data)
{
    int i;
    int t;

    if (cmdargc > MAX_ARGS)
        cmdargc = MAX_ARGS;
    cmdargv[PKO_MAX_PATH - 1] = '\0';

    memcpy(arg_data->args.payload, cmdargv, PKO_MAX_PATH);

    dbgprintf("cmd->argc %d, argv[0]: %s\n", cmdargc, cmdargv);

    i = 0;
    t = 0;
    do {
        arg_data->args.argv[i] = &arg_data->args.payload[t];
        dbgprintf("arg_data->args.argv[%d]=%s\n", i, arg_data->args.argv[i]);
        dbgprintf("arg_data->args.payload[%d]=%s\n", t, &arg_data->args.payload[t]);
        t += strlen(&arg_data->args.payload[t]);
        t++;
        i++;
    } while ((i < cmdargc) && (t < PKO_MAX_PATH));

    arg_data->args.argc = i;

    return 0;
}

////////////////////////////////////////////////////////////////////////
// Load the actual elf, and create a thread for it
// Return the thread id
static int
pkoLoadElf(char *path)
{
    ee_thread_t th_attr;
    t_ExecData elfdata;
    int ret;
    int pid;

    ret = SifLoadElf(path, &elfdata);

    if (ret < 0) {
        dbgprintf("EE: Could not load ELF file\n");
        return -1;
    }

    FlushCache(0);
    FlushCache(2);

    dbgprintf("EE: LoadElf returned %d\n", ret);

    dbgprintf("EE: Creating user thread (ent: %x, gp: %x, st: %x)\n",
              elfdata.epc, elfdata.gp, elfdata.sp);

    if (elfdata.epc == 0) {
        dbgprintf("EE: Could not load file\n");
        return -1;
    }

    th_attr.func = (void *)elfdata.epc;
    th_attr.stack = userThreadStack;
    th_attr.stack_size = sizeof(userThreadStack);
    th_attr.gp_reg = (void *)elfdata.gp;
    th_attr.initial_priority = 64;

    pid = CreateThread(&th_attr);
    if (pid < 0) {
        scr_printf("EE: Create user thread failed %d\n", pid);
        return -1;
    }

    dbgprintf("EE: Created user thread: %d\n", pid);

    return pid;
}


////////////////////////////////////////////////////////////////////////
// Load and start the requested elf
static int
pkoExecEE(pko_pkt_execee_req *cmd)
{
    char path[PKO_MAX_PATH];
    int ret;
    int pid;

    if (userThreadID) {
        return -1;
    }

    dbgprintf("EE: Executing file %s...\n", cmd->argv);
    memcpy(path, cmd->argv, PKO_MAX_PATH);
    // TyraX: makeArgs() terminates it, but only after we have already printed
    // and opened it. Do it before the first use of path as a string.
    path[PKO_MAX_PATH - 1] = '\0';

    scr_printf("Executing file %s...\n", path);

    pid = pkoLoadElf(path);
    if (pid < 0) {
        scr_printf("Could not execute file %s\n", path);
        return -1;
    }

    FlushCache(0);
    FlushCache(2);

    userThreadID = pid;

    makeArgs(ntohl(cmd->argc), path, &userArgs);

    // Hack away..
    userArgs.pid = (int)&userThreadID;

    ret = StartThread(userThreadID, &userArgs);
    if (ret < 0) {
        scr_printf("EE: Start user thread failed %d\n", ret);
        /* TyraX: this used to zero cmdThreadID - the wrong variable. That is
         * the thread the SIF DMA handler wakes, so after one failed launch
         * every later packet went to iWakeupThread(0) and ps2link stopped
         * answering ps2client entirely: the console had to be reset. Clear
         * userThreadID instead, which also lets the next EXECEE through. */
        DeleteThread(userThreadID);
        userThreadID = 0;
        return -1;
    }
    return ret;
}

////////////////////////////////////////////////////////////////////////
// takes cmd->data and sends it to GIF via path1
static int
pkoGSExec(pko_pkt_gsexec_req *cmd)
{
    int fd;
    int len;
    int qwords;
    fd = open((const char *)cmd->file, O_RDONLY);
    if (fd < 0) {
        return fd;
    }
    len = read(fd, dataBuffer, 128);
    close(fd);

    if (len <= 0) {
        scr_printf("EE: pkoGSExec failed\n");
        return -1;
    }

    /* TyraX: the qword count came straight from the packet while only 128
     * bytes were ever read, so a size field above 128 DMA'd whatever followed
     * dataBuffer straight into the GIF - a hang or a wedged GS, with nothing
     * pointing back here. Send only what we actually hold. */
    qwords = ntohs(cmd->size) / 16;
    if (qwords > len / 16) {
        qwords = len / 16;
    }
    if (qwords <= 0) {
        scr_printf("EE: pkoGSExec: nothing to send\n");
        return -1;
    }

    // stop/reset dma02

    // dmasend via GIF channel
    asm __volatile__(
        "move   $10, %0;"
        "move   $11, %1;"
        "lui    $8, 0x1001;"
        "sw     $11, -24544($8);"
        "sw     $10, -24560($8);"
        "ori    $9, $0, 0x101;"
        "sw     $9, -24576($8);"
      ::"r"(dataBuffer), "r"(qwords));

    // dmawait for GIF channel
    asm __volatile__(
        "lui    $8, 0x1001;"
        "dmawait:"
        "lw     $9, -24576($8);"
        "nop;"
        "andi   $9, $9, 0x100;"
        "nop;"
        "bnez   $9, dmawait;"
        "nop;");
    return 0;
}

////////////////////////////////////////////////////////////////////////
// command to dump cmd->size bytes of memory from cmd->offset
static int
pkoDumpMem(pko_pkt_mem_io *cmd)
{
    int fd;
    int total, len;
    unsigned int offset;
    unsigned int size;
    char path[PKO_MAX_PATH];
    int ret = 0;
    size = ntohl(cmd->size);
    offset = ntohl(cmd->offset);
    scr_printf("dump mem from 0x%x, size %d\n",
               ntohl(cmd->offset), ntohl(cmd->size));
    memcpy(path, cmd->argv, PKO_MAX_PATH);
    path[PKO_MAX_PATH - 1] = '\0';
    fd = open(path, O_WRONLY | O_CREAT);
    if (fd < 0) {
        scr_printf("EE: pkoDumpMem() could not open %s\n", path);
        return -1;
    }
    total = 0;
    len = 16 * 1024;
    while (total < (int)size) {
        if ((int)size < len) {
            len = size;
        } else if ((int)(size - total) < len) {
            len = size - total;
        }

        memcpy(dataBuffer, (void *)offset, len);

        ret = write(fd, (void *)dataBuffer, len);
        if (ret <= 0) {
            scr_printf("EE: pkoDumpMem() write failed\n");
            /* TyraX: `return fd` here leaked the host: fd on both sides -
             * ps2link never closed it and ps2client kept its end open too. A
             * few failed dumps and the PS2 was out of iomanX descriptors. */
            close(fd);
            return -1;
        }
        offset = offset + len;
        total += len;
    }
    close(fd);
    return ret;
}

static int
pkoWriteMem(pko_pkt_mem_io *cmd)
{
    int fd, len, total;
    unsigned int offset;
    unsigned int size;
    char path[PKO_MAX_PATH];
    int ret = 0;
    size = ntohl(cmd->size);
    offset = ntohl(cmd->offset);
    scr_printf("write mem to 0x%x, size %d\n",
               ntohl(cmd->offset), ntohl(cmd->size));
    memcpy(path, cmd->argv, PKO_MAX_PATH);
    path[PKO_MAX_PATH - 1] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        scr_printf("EE: pkoWriteMem() could not open %s\n", path);
        return -1;
    }
    total = 0;
    len = 16 * 1024;
    while (total < (int)size) {
        if ((int)size < len) {
            len = size;
        } else if ((int)(size - total) < len) {
            len = size - total;
        }

        ret = read(fd, (void *)dataBuffer, len);
        if (ret <= 0) {
            scr_printf("EE: pkoWriteMem() read failed\n");
            close(fd); // TyraX: was a bare `return fd`, leaking the host: fd
            return -1;
        }

        /* TyraX: copy what read() actually delivered, not what we asked for.
         * A short read (any read over host: can be short) used to splice
         * whatever was left in dataBuffer from a previous command into the
         * target address - a silent, very confusing memory corruption. */
        memcpy((void *)offset, dataBuffer, ret);
        offset = offset + ret;
        total += ret;
    }
    close(fd);
    return ret;
}

////////////////////////////////////////////////////////////////////////
// command to dump various registers ( gs|vif|intc etc )
static int
pkoDumpReg(pko_pkt_dump_regs *cmd)
{
    int fd, ret = 0;
    char path[PKO_MAX_PATH];
    unsigned int i, j, size;
    unsigned int dmaregs[52] = {
        0x1000e000, 0x1000e010, 0x1000e020, 0x1000e030,
        0x1000e040, 0x1000e050, 0x1000e060, 0x1000f520,                         // Dma common registers
        0x10008000, 0x10008010, 0x10008020, 0x10008030, 0x10008040, 0x10008050, // DMA0
        0x10009000, 0x10009010, 0x10009020, 0x10009030, 0x10009040, 0x10009050, // DMA1
        0x1000a000, 0x1000a010, 0x1000a020, 0x1000a030, 0x1000a040, 0x1000a050, // DMA2
        0x1000b000, 0x1000b010, 0x1000b020,                                     // DMA3
        0x1000b400, 0x1000b410, 0x1000b420, 0x1000b430,                         // DMA4
        0x1000c000, 0x1000c010, 0x1000c020,                                     // DMA5
        0x1000c400, 0x1000c410, 0x1000c420, 0x1000c430,                         // DMA6
        0x1000c800, 0x1000c810, 0x1000c820,                                     // DMA7
        0x1000d000, 0x1000d010, 0x1000d020, 0x1000d080,                         // DMA8
        0x1000d400, 0x1000d410, 0x1000d420, 0x1000d430, 0x1000d480              // DMA9
    };
    unsigned int intcregs[2] = {
        0x1000F000, 0x1000F010};
    unsigned int gsregs[2] = {
        0x12001000, 0x12001080};
    unsigned int gifregs[8] = {
        0x10003020, 0x10003040, 0x10003050, 0x10003060,
        0x10003070, 0x10003080, 0x10003090, 0x100030a0};
    unsigned int timerregs[14] = {
        0x10000000, 0x10000010, 0x10000020, 0x10000030,
        0x10000800, 0x10000810, 0x10000820, 0x10000830,
        0x10001000, 0x10001010, 0x10001020,
        0x10001800, 0x10001810, 0x10001820};
    unsigned int sifregs[1] = {
        0x1000F230};
    unsigned int vif0regs[19] = {
        0x10003800, 0x10003810, 0x10003820, 0x10003830, 0x10003840,
        0x10003850, 0x10003860, 0x10003870, 0x10003880, 0x10003890,
        0x100038d0, 0x10003900, 0x10003910, 0x10003920, 0x10003930,
        0x10003940, 0x10003950, 0x10003960, 0x10003970};
    unsigned int vif1regs[23] = {
        0x10003c00, 0x10003c10, 0x10003c20, 0x10003c30, 0x10003c40,
        0x10003c50, 0x10003c60, 0x10003c70, 0x10003c80, 0x10003c90,
        0x10003ca0, 0x10003cb0, 0x10003cc0, 0x10003cd0, 0x10003ce0,
        0x10003d00, 0x10003d10, 0x10003d20, 0x10003d30, 0x10003d40,
        0x10003d50, 0x10003d60, 0x10003d70};
    unsigned int fiforegs[2] = {
        0x10005000, 0x10007000};
    unsigned int ipuregs[4] = {
        0x10002000, 0x10002010, 0x10002020, 0x10002030};
    unsigned int REGALL_SIZE =
        sizeof(dmaregs) + sizeof(intcregs) + sizeof(timerregs) +
        sizeof(gsregs) + sizeof(sifregs) + sizeof(fiforegs) +
        sizeof(gifregs) + sizeof(vif0regs) + sizeof(vif1regs) +
        sizeof(ipuregs);
    /* TyraX: this was `unsigned int regs[REGALL_SIZE]` - a byte count used as
     * an element count, so 4x too big. Do NOT "fix" that to REGALL_SIZE/4:
     * REGALL_SIZE is 508 bytes but the REGVU0/REGVU1 asm below stores 896
     * bytes here, so the accidental over-allocation was load-bearing and the
     * tidy-looking fix would overflow the stack frame. Size it for the real
     * worst case instead (896 bytes rather than 2032).
     *
     * aligned(16) because the REGVU0/REGVU1 asm reaches this buffer with
     * sqc2/sq, and the R5900 MASKS the low 4 bits of an lq/sq address instead
     * of faulting: on a 4-byte-aligned array every store would silently land
     * up to 12 bytes BELOW where it was aimed, off the front of the buffer. */
#define REGDUMP_VU_SIZE 896
    unsigned int regs[((REGALL_SIZE > REGDUMP_VU_SIZE ? REGALL_SIZE : REGDUMP_VU_SIZE) + 3) / 4]
        __attribute__((aligned(16)));

    size = 0;
    j = 0;
    switch (ntohl(cmd->regs)) {
        case REGDMA:
            for (i = 0; i < sizeof(dmaregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)dmaregs[i]);
            }
            size = sizeof(dmaregs);
            break;
        case REGINTC:
            for (i = 0; i < sizeof(intcregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)intcregs[i]);
            }
            size = sizeof(intcregs);
            break;
        case REGTIMER:
            for (i = 0; i < sizeof(timerregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)timerregs[i]);
            }
            size = sizeof(timerregs);
            break;
        case REGGS:
            for (i = 0; i < sizeof(gsregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)gsregs[i]);
            }
            size = sizeof(gsregs);
            break;
        case REGSIF:
            for (i = 0; i < sizeof(sifregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)sifregs[i]);
            }
            size = sizeof(sifregs);
            break;
        case REGFIFO:
            for (i = 0; i < sizeof(fiforegs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)fiforegs[i]);
            }
            size = sizeof(fiforegs);
            break;
        case REGGIF:
            for (i = 0; i < sizeof(gifregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)gifregs[i]);
            }
            size = sizeof(gifregs);
            break;
        case REGVIF0:
            for (i = 0; i < sizeof(vif0regs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)vif0regs[i]);
            }
            size = sizeof(vif0regs);
            break;
        case REGVIF1:
            for (i = 0; i < sizeof(vif1regs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)vif1regs[i]);
            }
            size = sizeof(vif1regs);
            break;
        case REGIPU:
            for (i = 0; i < sizeof(ipuregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)ipuregs[i]);
            }
            size = sizeof(ipuregs);
            break;
        case REGVU1:
            asm __volatile__(
                ".set noat;"
                // stop VU0 and VU1
                "ori     $8, $0, 0x101;"
                "ctc2    $8, $28;"
                "vnop;"
                // FIXME remember to save/restore vi1 and vf1

                // Save VU1 floats
                "move    $8, %0;"      // save away dst
                "ori     $1, $0, 1024;" // start of vu1 mapped floats
                "ctc2    $1, $vi1;"     // ctc2 t0, vi1
                "ori     $1, $0, 32;"
                "vu1_float_loop:"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 0($8);"
                "addiu   $1, -1;"
                "addiu   $8, 16;"
                "nop;"
                "bnez    $1, vu1_float_loop;"
                "nop;"

                // Save VU1 Integers
                "ori     $1, $0, 1056;"
                "ctc2    $1, $vi1;" // ctc2 t0, vi1
                "ori     $1, $0, 16;"
                "vu1_int_loop:"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 0($8);"
                "addiu   $1, -1;"
                "addiu   $8, 16;"
                "nop;"
                "bnez    $1, vu1_int_loop;"
                "nop;"

                // Stop GIF
                // Save VU1 Control registers
                "ori     $1, $0, 1072;"
                "ctc2    $1, $vi1;"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 0($8);"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 16($8);"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 32($8);"
                "viaddi  $vi1, $vi1, 1;"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 48($8);"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 64($8);"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 80($8);"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 96($8);"
                "viaddi  $vi1, $vi1, 2;"
                "vlqi    $vf1, ($vi1++);"
                "sqc2    $vf1, 112($8);"

                // FIXME remember to save/restore vi1 and vf1

                "ori     $8, $0, 0x202;"
                "ctc2    $8, $28;"
                "vnop;"
                :: "r"(regs));
            size = 896;
            break;
        case REGVU0:
            asm __volatile__(
                // stop VU0
                "ori     $8, $0, 0x1;"
                "ctc2    $8, $28;"
                "vnop;"
                // Dump VU0 float registers
                "sqc2   $vf0, 0(%0);"
                "sqc2   $vf1, 16(%0);"
                "sqc2   $vf2, 32(%0);"
                "sqc2   $vf3, 48(%0);"
                "sqc2   $vf4, 64(%0);"
                "sqc2   $vf5, 80(%0);"
                "sqc2   $vf6, 96(%0);"
                "sqc2   $vf7, 112(%0);"
                "sqc2   $vf8, 128(%0);"
                "sqc2   $vf9, 144(%0);"
                "sqc2   $vf10, 160(%0);"
                "sqc2   $vf11, 176(%0);"
                "sqc2   $vf12, 192(%0);"
                "sqc2   $vf13, 208(%0);"
                "sqc2   $vf14, 224(%0);"
                "sqc2   $vf15, 240(%0);"
                "sqc2   $vf16, 256(%0);"
                "sqc2   $vf17, 272(%0);"
                "sqc2   $vf18, 288(%0);"
                "sqc2   $vf19, 304(%0);"
                "sqc2   $vf20, 320(%0);"
                "sqc2   $vf21, 336(%0);"
                "sqc2   $vf22, 352(%0);"
                "sqc2   $vf23, 368(%0);"
                "sqc2   $vf24, 384(%0);"
                "sqc2   $vf25, 400(%0);"
                "sqc2   $vf26, 416(%0);"
                "sqc2   $vf27, 432(%0);"
                "sqc2   $vf28, 448(%0);"
                "sqc2   $vf29, 464(%0);"
                "sqc2   $vf30, 480(%0);"
                "sqc2   $vf31, 496(%0);"

                // Save VU0 integer registers
                "cfc2   $8, $vi0;"
                "sq     $8, 512(%0);"
                "cfc2   $8, $vi1;"
                "sq     $8, 528(%0);"
                "cfc2   $8, $vi2;"
                "sq     $8, 544(%0);"
                "cfc2   $8, $vi3;"
                "sq     $8, 560(%0);"
                "cfc2   $8, $vi4;"
                "sq     $8, 576(%0);"
                "cfc2   $8, $vi5;"
                "sq     $8, 592(%0);"
                "cfc2   $8, $vi6;"
                "sq     $8, 608(%0);"
                "cfc2   $8, $vi7;"
                "sq     $8, 624(%0);"
                "cfc2   $8, $vi8;"
                "sq     $8, 640(%0);"
                "cfc2   $8, $vi9;"
                "sq     $8, 656(%0);"
                "cfc2   $8, $vi10;"
                "sq     $8, 672(%0);"
                "cfc2   $8, $vi11;"
                "sq     $8, 688(%0);"
                "cfc2   $8, $vi12;"
                "sq     $8, 704(%0);"
                "cfc2   $8, $vi13;"
                "sq     $8, 720(%0);"
                "cfc2   $8, $vi14;"
                "sq     $8, 736(%0);"
                "cfc2   $8, $vi15;"
                "sq     $8, 752(%0);"

                // Reset VU0
                "ori    $8, $0, 0x2;"
                "ctc2   $8, $28;"
                "vnop;"
                :: "r"(regs));
            size = 896;
            break;
        case REGALL:
            for (i = 0; i < sizeof(dmaregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)dmaregs[i]);
            }
            for (i = 0; i < sizeof(intcregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)intcregs[i]);
            }
            for (i = 0; i < sizeof(timerregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)timerregs[i]);
            }
            for (i = 0; i < sizeof(gsregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)gsregs[i]);
            }
            for (i = 0; i < sizeof(sifregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)sifregs[i]);
            }
            for (i = 0; i < sizeof(fiforegs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)fiforegs[i]);
            }
            for (i = 0; i < sizeof(gifregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)gifregs[i]);
            }
            for (i = 0; i < sizeof(vif0regs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)vif0regs[i]);
            }
            for (i = 0; i < sizeof(vif1regs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)vif1regs[i]);
            }
            for (i = 0; i < sizeof(ipuregs) / 4; i++) {
                regs[j++] = *((volatile unsigned int *)ipuregs[i]);
            }
            size = REGALL_SIZE;
            break;
        default:
            /* TyraX: an unknown register set left size == 0, which then made
             * write() return 0, took the "write failed" branch and returned
             * without close() - leaking the fd. Reject it up front. */
            scr_printf("EE: pkoDumpReg() unknown register set %d\n", ntohl(cmd->regs));
            return -1;
    }

    memcpy(path, cmd->argv, PKO_MAX_PATH);
    path[PKO_MAX_PATH - 1] = '\0';
    fd = open(path, O_WRONLY | O_CREAT);
    if (fd < 0) {
        scr_printf("EE: pkoDumpReg() could not open %s\n", path);
        return -1;
    }
    ret = write(fd, regs, size);
    if (ret <= 0) {
        scr_printf("EE: pkoDumpReg() write failed\n");
        close(fd); // TyraX: was a bare `return fd`, leaking the host: fd
        return -1;
    }
    close(fd);
    return ret;
}

////////////////////////////////////////////////////////////////////////
// command to stop VU0/1 and VIF0/1
static int
pkoStopVU(pko_pkt_stop_vu *cmd)
{
    if (ntohs(cmd->vpu) == 0) {
        asm(
            "ori     $25, $0, 0x1;"
            "ctc2    $25, $28;"
            "sync.p;"
            "vnop;");
        VIF0_FBRST = 0x2;
    } else if (ntohs(cmd->vpu) == 1) {
        asm(
            "ori     $25, $0, 0x100;"
            "ctc2    $25, $28;"
            "sync.p;"
            "vnop;");
        VIF1_FBRST = 0x2;
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////
// command to reset VU0/1 VIF0/1
static int
pkoStartVU(pko_pkt_start_vu *cmd)
{
    if (ntohs(cmd->vpu) == 0) {
        asm(
            "ori     $25, $0, 0x2;"
            "ctc2    $25, $28;"
            "sync.p;"
            "vnop;");
        VIF0_FBRST = 0x8;
    } else if (ntohs(cmd->vpu) == 1) {
        asm(
            "ori     $25, $0, 0x200;"
            "ctc2    $25, $28;"
            "sync.p;"
            "vnop;");
        VIF1_FBRST = 0x8;
    }
    return 0;
}


////////////////////////////////////////////////////////////////////////
// Sif dma interrupt handler, wakes up the cmd handler thread if
// data was sent to our dma area

static int pkoCmdIntrHandler(int channel)
{
    int flag;

    flag = *sifDmaDataPtr;

    iSifSetDChain();

    if (flag) {
        // Clear new packet flag
        *sifDmaDataPtr = 0;

        iWakeupThread(cmdThreadID);
    }

    asm __volatile__("sync");
    asm __volatile__("ei");

    ExitHandler();
    return 0;
}

////////////////////////////////////////////////////////////////////////
// Sif cmd handler thread, waits to be woken by the sif dma intr handler
static int cmdThread(void *arg)
{
    int ret;
    int done;

    dbgprintf("EE: Cmd thread\n");

    done = 0;
    while (!done) {
        unsigned int cmd;
        void *pkt;
        SleepThread();

        cmd = ntohl(sifDmaDataPtr[1]);
        pkt = &sifDmaDataPtr[1];

        switch (cmd) {
            case PKO_RESET_CMD:
                /* TyraX: `done = 1` here, because pkoReset() used to end in
                 * ExecPS2() and never return - the exit was unreachable
                 * bookkeeping. It re-arms ps2link in place now, so this thread
                 * IS the listener that has to keep listening; retiring it left
                 * the console reachable but deaf. */
                pkoReset();
                ret = 0;
                break;
            case PKO_NETDUMP_CMD:
                excepscrdump = 0;
                break;
            case PKO_SCRDUMP_CMD:
                excepscrdump = 1;
                break;
            case PKO_EXECEE_CMD:
                dbgprintf("EE: Rpc EXECEE called\n");
                ret = pkoExecEE(pkt);
                break;
            case PKO_START_VU:
                dbgprintf("EE: Start VU\n");
                ret = pkoStartVU(pkt);
                break;
            case PKO_STOP_VU:
                dbgprintf("EE: Stop VU\n");
                ret = pkoStopVU(pkt);
                break;
            case PKO_DUMP_MEM:
                dbgprintf("EE: Dump mem\n");
                ret = pkoDumpMem(pkt);
                break;
            case PKO_DUMP_REG:
                dbgprintf("EE: Dump reg\n");
                ret = pkoDumpReg(pkt);
                break;
            case PKO_GSEXEC_CMD:
                dbgprintf("EE: GS Exec\n");
                ret = pkoGSExec(pkt);
                break;
            case PKO_WRITE_MEM:
                dbgprintf("EE: Write Mem\n");
                ret = pkoWriteMem(pkt);
                break;
            case PKO_IOPEXCEP_CMD:
                iopException(sifDmaDataPtr[3], sifDmaDataPtr[4], sifDmaDataPtr[5], sifDmaDataPtr[2], &sifDmaDataPtr[6], sifDmaDataPtr[41], (char *)&sifDmaDataPtr[43]);
                break;
            default:
                scr_printf("EE: Unknown rpc cmd (%x) !\n", cmd);
                ret = -1;
        }
    }

    ExitDeleteThread();
    return ret;
}

////////////////////////////////////////////////////////////////////////
// Create cmd thread & install sif dma interrupt handler
int initCmdRpc(void)
{
    ee_thread_t th_attr;
    int ret;

    th_attr.func = &cmdThread;
    th_attr.stack = cmdThreadStack;
    th_attr.stack_size = sizeof(cmdThreadStack);
    th_attr.gp_reg = &_gp;
    th_attr.initial_priority = 20;

    ret = CreateThread(&th_attr);
    if (ret < 0) {
        scr_printf("EE: initCmdRpc createThread failed %d\n", ret);
        return -1;
    }

    cmdThreadID = ret;

    ret = StartThread(cmdThreadID, 0);
    if (ret < 0) {
        scr_printf("EE: initCmdRpc startThread failed %d\n", ret);
        /* TyraX: order was reversed, so this deleted thread 0 and leaked the
         * one it had just created. */
        DeleteThread(cmdThreadID);
        cmdThreadID = 0;
        return -1;
    }

    // Install our sif dma interrupt handler
    FlushCache(0);
    sifDmaDataPtr[0] = 0;

    if (_lw(A_EE_D_STAT) & STAT_SIF0)
        _sw(STAT_SIF0, A_EE_D_STAT);

    // If SIF0 (IOP -> EE) is not enabled, enable it.
    if (!(_lw(A_EE_D5_CHCR) & EE_CHCR_STR))
        SifSetDChain();

    sif0HandlerId = AddDmacHandler(DMAC_SIF0, &pkoCmdIntrHandler, 0);
    EnableDmac(DMAC_SIF0);
    scr_printf("RESETDIAG EE_HANDLER_INITIALIZED thread=%d dmacHandler=%d\n",
               cmdThreadID, sif0HandlerId);
    return 0;
}

////////////////////////////////////////////////////////////////////////
/* TyraX: the tail of the original pkoReset(), kept for the one caller that
 * genuinely needs it - main()'s initial boot. Re-executing the image is how
 * ps2link ends up as the EE program proper: when it is loaded over the network
 * by another ps2link it starts life as that one's user thread, and without
 * this the two images would both sit there answering commands. Nothing has
 * run on the hardware at that point, so the re-exec is safe here; issuing it
 * to stop a game is what wedged the console (see pkoReset below). */
void pkoRestartImage(void)
{
    char *argv[1];

    if (sif0HandlerId >= 0) {
        DisableDmac(DMAC_SIF0);
        RemoveDmacHandler(DMAC_SIF0, sif0HandlerId);
    }

    SifInitRpc(0);
    SifExitRpc();

    /* TyraX (r6): no IOP reboot here either, for the same reason it left
     * pkoReset() - crt0 runs ps2sdk's C runtime init before main() and that
     * talks to the IOP over the SIF, so rebooting the IOP immediately before
     * ExecPS2() hands the fresh image an IOP with no modules on it and nobody
     * to answer. main() reboots it a few lines in, which is early enough and
     * happens with the C runtime already up.
     *
     * This path only ever ran on the initial boot, where nothing has touched
     * the hardware yet, which is why it survived a boot from the memory card.
     * It does not survive one image being launched over another by execee -
     * measured, low over high: the EE was gone before the first Stop. */

    argv[0] = elfName;
    SifLoadFileExit();

    ExecPS2(&__start, 0, 1, argv);
}

void pkoReset(void)
{
    char *argv[1];
    /* PRIVATE: one GS privileged register store; no stdio/GIF/DMA. Colour can
     * be hidden by a still-enabled framebuffer; stage ledger is also kept. */
    resetDiagEarly(2, 0x000000FF); /* red: EE reset entered */

    // Check if user thread is running, if so kill it
    if (userThreadID) {
        TerminateThread(userThreadID);
        DeleteThread(userThreadID);
    }
    userThreadID = 0;

    /* TyraX (r7): ORDER. The hardware quiesce, the SIF0 re-arm and the
     * exception vectors all used to come AFTER init_scr(), and that is a way
     * to hang the console rather than recover it. init_scr() draws through the
     * GIF and the DMAC - the very blocks the comment below says the dead game
     * left mid-chain - so on a game that died inside a transfer it can block,
     * and it blocks ps2link's EE COMMAND THREAD, the one thread that executes
     * every command there is. The console then answers no `reset` and no
     * `execee` ever again while its IOP-side file server keeps listening on
     * tcp/18193, and only the reset button recovers it. That is exactly the
     * reported symptom; it is intermittent for exactly the right reason (it
     * depends on whether the game died mid-chain); and the r6 measurement
     * missed it because that payload was too light to be mid-transfer when it
     * was killed.
     *
     * So: silence the hardware first, take the vectors back second, and only
     * then touch the screen - by which point a fault in init_scr() lands in
     * ps2link's own handler and says so, instead of jumping into the dead
     * game's crash handler. */

    /* TyraX: TerminateThread stops the CPU, not the hardware. The game owned
     * the DMAC, both VUs, VIF0/1, the GIF and the IPU, and a transfer left
     * mid-chain keeps running with nobody to service it. */
    ResetEE(INIT_DMAC | INIT_VU1 | INIT_VIF1 | INIT_GIF | INIT_VU0 | INIT_VIF0 |
            INIT_IPU);
    resetDiagEarly(3, 0x0000FFFF); /* yellow: hardware quiesced */

    /* INIT_DMAC took SIF0 - the IOP -> EE command channel - down with it, and
     * no SifInitRpc() will put it back (ps2sdk guards that init as done).
     * Re-arm the chain exactly the way initCmdRpc() does, minus the handler,
     * which is still installed: nothing here is torn down, so nothing here
     * needs rebuilding. */
    FlushCache(0);
    if (_lw(A_EE_D_STAT) & STAT_SIF0)
        _sw(STAT_SIF0, A_EE_D_STAT);
    if (!(_lw(A_EE_D5_CHCR) & EE_CHCR_STR))
        SifSetDChain();
    EnableDmac(DMAC_SIF0);

    /* The game overwrote the EE exception vectors with its own crash handler
     * (Tyra links libeedebug) and that code is now dead memory. Take them
     * back before anything can fault into them. */
    installExceptionHandlers();

    /* TyraX: nothing here may call stdio. printf() takes newlib's stdout lock,
     * and after ExecPS2 re-enters this image that lock is a cleared .bss field
     * until newlib re-initializes - the console then dies in
     * __retarget_lock_acquire_recursive with BadAddr 8, a cycle or two later,
     * looking entirely unrelated. Breadcrumbs in this function are scr_printf
     * only. They were useless over the network anyway: the IOP reboot follows
     * within a millisecond and takes the queued tty with it.
     *
     * That rule was first written for this function alone, which is why the bug
     * survived it: every remaining printf() in this file sits on an ERROR path,
     * and error paths are exactly what a broken restart takes. Measured on the
     * console - EPC 0x00098604, BadAddr 8, i.e. lw v1,8(s0) with s0 = a0 = a
     * NULL lock - the EE died reporting some earlier failure, not causing it.
     * So the rule is now file-wide: stdio nowhere, scr_printf everywhere. The
     * failure that triggered it then survives onto the screen instead of taking
     * the console with it. */
    init_scr();
    resetDiagStage = 4;
    scr_printf("RESETDIAG SCREEN_INITIALIZED stage=4 (EE entry/quiesce reached)\n");
    scr_printf("\n\nps2link: program stopped\n");

    /* The next deploy does need a clean IOP - the game loads its own modules
     * onto whatever it finds (padman, libsd, audsrv...) and a second copy
     * cannot register: measured, the second deploy printed "freepad:
     * RegisterLibraryEntries failed" and hung waiting for a pad that would
     * never answer. Dropping the iopheap and loadfile RPC bindings here is
     * still right, so they cannot be reused across the reboot that follows in
     * the fresh image.
     *
     * TyraX (r6): the IOP reboot USED TO HAPPEN HERE, and that was the bug that
     * made a Stop wedge the console. It is redundant - the re-executed image's
     * main() calls restartIOP() a few lines in, so the clean IOP arrives either
     * way - and doing it here strands what runs in between: ps2sdk's C runtime
     * init (_libcglue_init, __fdman_init, the pthread glue) runs in crt0,
     * BEFORE main(), and talks to the IOP over the SIF. Reboot the IOP and then
     * ExecPS2, and crt0 comes up against an IOP with no modules on it and
     * nobody to answer. Measured: the restarted image never reached main().
     *
     * Leaving the reboot to main() means crt0 runs against the IOP the game
     * left behind - loaded, answering, good enough for an RPC - and the reboot
     * happens with the C runtime already up. Measured after the change: 20
     * consecutive Run -> Stop cycles on the high build against 1-3 before, and
     * the low build, which used to die on the FIRST Stop, likewise. */
    scr_printf("ps2link: handing over to a fresh image...\n");
    SifLoadFileExit();
    SifExitIopHeap();
    /* The two SifIopSync() edges that used to follow the reboot went with it.
     * They are still the right way to wait for one - BOTH edges, in order,
     * because SifIopSync() is a single bit (BOOTEND, SMFLAG bit 18): the first
     * loop waits for the reset to be TAKEN, the second for the boot to FINISH.
     * restartIOP() in ps2link.c is where that now matters. */

    /* TyraX: and now hand over to a freshly started image. Everything above is
     * what makes this survive: the hardware is quiesced, the exception vectors
     * are ours again, and the IOP has actually finished rebooting. What we
     * cannot fix from here is ps2sdk's own state - its "SIF is initialized"
     * flags live in .bss, and only a re-executed image gets them cleared by
     * crt0. Re-arming in place therefore worked exactly once, three different
     * ways, and wedged on the second Stop every time. crt0 clears .bss for
     * free, so let it: from here on every Stop is bit-for-bit the boot path
     * that has always worked. */
    scr_printf("ps2link: IOP is back, restarting ps2link\n");
    argv[0] = elfName;

    /* No breadcrumb here: the debug screen is up at this point, so a background
     * colour would sit behind its framebuffer and never show. The line above is
     * this stage's marker. */
    resetDiagStage = 5;
    scr_printf("RESETDIAG FRESH_IMAGE_HANDOVER stage=5\n");
    ExecPS2(&__start, 0, 1, argv);
}
