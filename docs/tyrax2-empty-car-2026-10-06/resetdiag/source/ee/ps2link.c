/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2003,2004 adresd (adresd_ps2dev@yahoo.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#include <kernel.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <debug.h>
#include <ps2sdkapi.h>


#include "irx_variables.h"

#include "hostlink.h"
#include "excepHandler.h"
#include "cmdHandler.h"
#include "globals.h"

////////////////////////////////////////////////////////////////////////

// Argv name+path & just path
/* PRIVATE: nonzero initializer forces .data, like cached IP config. It survives
 * ExecPS2's .bss clear, until a fresh ELF load supplies the initial image. */
volatile unsigned int resetDiagStage = 1;
void resetDiagEarly(unsigned int stage, u64 colour) {
    resetDiagStage = stage;
    *(volatile u64 *)0x120000E0 = colour;
}

char elfName[MAXNAMLEN] __attribute__((aligned(16)));
////////////////////////////////////////////////////////////////////////
#define IPCONF_MAX_LEN 64 // Don't reduce even more this value

// Make sure the "cached config" is in the data section
// To prevent it from being "zeroed" on a restart of ps2link
char if_conf[IPCONF_MAX_LEN] __attribute__((section(".data"))) = "";
int if_conf_len __attribute__((section(".data"))) = 0;

////////////////////////////////////////////////////////////////////////

void printIpConfig(void)
{
    int i;
    int t;

    scr_printf("Net config: ");
    for (t = 0, i = 0; t < 3; t++) {
        scr_printf("%s  ", &if_conf[i]);
        i += strlen(&if_conf[i]) + 1;
    }
    scr_printf("\n");
}

// Parse network configuration from IPCONFIG.DAT
// Note: parsing really should be made more robust...
static int readIpConfigFromFile(char *buf)
{
    int fd;
    int len;

    fd = open("IPCONFIG.DAT", O_RDONLY);

    if (fd < 0) {
        dbgprintf("Error reading ipconfig.dat\n");
        return -1;
    }

    len = read(fd, buf, IPCONF_MAX_LEN - 1); // Let the last byte be '\0'
    close(fd);

    if (len < 0) {
        dbgprintf("Error reading ipconfig.dat\n");
        return -1;
    }

    dbgscr_printf("ipconfig: Read %d bytes\n", len);
    return 0;
}

static int readDefaultIpConfig(char *buf)
{
    int i = 0;

    strncpy(&buf[i], DEFAULT_IP, 15);
    i += strlen(&buf[i]) + 1;
    strncpy(&buf[i], DEFAULT_NETMASK, 15);
    i += strlen(&buf[i]) + 1;
    strncpy(&buf[i], DEFAULT_GW, 15);

    return 0;
}

static void getIpConfig(void)
{
    int i;
    int t;
    char c;
    char buf[IPCONF_MAX_LEN];

    // Clean buffers
    memset(buf, 0x00, IPCONF_MAX_LEN);
    memset(buf, 0x00, IPCONF_MAX_LEN);

    if (readIpConfigFromFile(buf))
        readDefaultIpConfig(buf);

    i = 0;
    // Clear out spaces (and potential ending CR/LF)
    while ((c = buf[i]) != '\0') {
        if ((c == ' ') || (c == '\r') || (c == '\n'))
            buf[i] = '\0';
        i++;
    }

    scr_printf("Saving IP config...\n");
    for (t = 0, i = 0; t < 3; t++) {
        strncpy(&if_conf[i], &buf[i], 15);
        i += strlen(&if_conf[i]) + 1;
    }

    if_conf_len = i;
}

////////////////////////////////////////////////////////////////////////
// Wrapper to load irx module from disc/rom
static void pkoLoadModule(char *path, int argc, char *argv)
{
    int ret;

    ret = SifLoadModule(path, argc, argv);
    if (ret < 0) {
        scr_printf("Could not load module %s: %d\n", path, ret);
        SleepThread();
    }
    dbgscr_printf("Load %s, returned [%d]\n", path, ret);
}

////////////////////////////////////////////////////////////////////////
// Load all the irx modules we need
void loadModules(void)
{
    int ret;

    if (if_conf_len == 0) {
        pkoLoadModule("rom0:SIO2MAN", 0, NULL);
        pkoLoadModule("rom0:MCMAN", 0, NULL);
        pkoLoadModule("rom0:MCSERV", 0, NULL);
        return;
    }

    dbgscr_printf("Exec poweroff module. (%x,%d) ", (unsigned int)poweroff_irx, size_poweroff_irx);
    SifExecModuleBuffer(poweroff_irx, size_poweroff_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2dev9 module. (%x,%d) ", (unsigned int)ps2dev9_irx, size_ps2dev9_irx);
    SifExecModuleBuffer(ps2dev9_irx, size_ps2dev9_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec netman module. (%x,%d) ", (unsigned int)netman_irx, size_netman_irx);
    SifExecModuleBuffer(netman_irx, size_netman_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec smap module. (%x,%d) ", (unsigned int)smap_irx, size_smap_irx);
    SifExecModuleBuffer(smap_irx, size_smap_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2ip module. (%x,%d) ", (unsigned int)ps2ip_nm_irx, size_ps2ip_nm_irx);
    SifExecModuleBuffer(ps2ip_nm_irx, size_ps2ip_nm_irx, if_conf_len, &if_conf[0], &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec udptty module. (%x,%d) ", (unsigned int)udptty_irx, size_udptty_irx);
    SifExecModuleBuffer(&udptty_irx, size_udptty_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ioptrap module. (%x,%d) ", (unsigned int)ioptrap_irx, size_ioptrap_irx);
    SifExecModuleBuffer(ioptrap_irx, size_ioptrap_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2link module. (%x,%d) ", (unsigned int)ps2link_irx, size_ps2link_irx);
    SifExecModuleBuffer(ps2link_irx, size_ps2link_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);

#ifdef TYRAX_USB_HID
    /* TyraX: USB HID stack. usbd first (ps2kbd/ps2mouse import it), so a
     * game deployed over this ps2link can reuse the resident drivers and get
     * a keyboard AND mouse without loading its own usbd. Built out entirely
     * by NOUSB=1 - see ee/Makefile. */
    dbgscr_printf("Exec usbd module. (%x,%d) ", (unsigned int)usbd_irx, size_usbd_irx);
    SifExecModuleBuffer(usbd_irx, size_usbd_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2kbd module. (%x,%d) ", (unsigned int)ps2kbd_irx, size_ps2kbd_irx);
    SifExecModuleBuffer(ps2kbd_irx, size_ps2kbd_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2mouse module. (%x,%d) ", (unsigned int)ps2mouse_irx, size_ps2mouse_irx);
    SifExecModuleBuffer(ps2mouse_irx, size_ps2mouse_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
#endif /* TYRAX_USB_HID */

    dbgscr_printf("All modules loaded on IOP.\n");
}

static void printWelcomeInfo()
{
    scr_printf("\n\n\n\n");
    /* TyraX: the r-number is how you tell a flashed build apart. Bump it
     * whenever tyrax.patch changes behaviour, and say so in
     * docs/ps2link-setup.md - r1 was USB HID only, r2 added the host:/reset
     * hardening, r3 silences the SPU2 on reset, r4 lets commands preempt the
     * host: file server and quiesces the EE peripherals before restarting, r5
     * takes stdio out of every error path in cmdHandler.c, r6 stops rebooting
     * the IOP twice per Stop - which is what made a Stop wedge the console. */
#ifdef TYRAX_USB_HID
    scr_printf("Welcome to TyraX ps2link r7-resetdiag (USB keyboard + mouse)\n");
#else
    /* TyraX: the banner has to say which of the two this is - they differ in
     * what stays resident on the IOP and in how far the image reaches, and the
     * second is the whole reason this variant exists. */
    scr_printf("Welcome to TyraX ps2link r7-resetdiag (no USB)\n");
#endif
    scr_printf("based on ps2link %s\n", APP_VERSION);
    scr_printf("ps2link loaded at 0x%08X-0x%08X, size: 0x%08X\n", (unsigned int)&__start, (unsigned int)&_end, (unsigned int)&_end - (unsigned int)&__start);
    scr_printf("Initializing...\n");
}

void restartIOP()
{
    dbgscr_printf("reset iop\n");

   SifInitRpc(0);
   while(!SifIopReset(NULL, 0)){};
   resetDiagStage = 9;
   /* PRIVATE ledger only: no screen draw between reset and BOOTEND waits. */
   /* TyraX: BOTH edges, in this order. SifIopSync() is a single bit (BOOTEND,
    * SMFLAG bit 18), so one loop can only wait for one of two things: for the
    * reset to be TAKEN (bit clears), then for the boot to FINISH (bit sets).
    * Waiting only for the second, starting from a bit still set by the previous
    * boot, falls straight through and waits for nothing. That mattered little
    * while pkoReset() did its own reboot before ExecPS2(); this is now the only
    * IOP reboot in the program, so everything below depends on it. */
   while(SifIopSync()){};
   while(!SifIopSync()){};


   SifInitRpc(0);
   sbv_patch_enable_lmb();
   sbv_patch_disable_prefix_check();

    dbgscr_printf("reseted iop\n");
}

////////////////////////////////////////////////////////////////////////

// This function is defined as weak in ps2sdkc, so how
// we are not using time zone, so we can safe some KB
void _ps2sdk_timezone_update() {}
void _libcglue_rtc_update() {}

DISABLE_PATCHED_FUNCTIONS(); // Disable the patched functionalities
DISABLE_EXTRA_TIMERS_FUNCTIONS(); // Disable the extra functionalities for timers
#ifdef PS2_DISABLE_AUTOSTART_PTHREAD
PS2_DISABLE_AUTOSTART_PTHREAD(); // Disable pthread functionality (newer ps2sdk)
#endif

int main(int argc, char *argv[])
{
    /* TyraX: the screen first, before anything that can block. Coming back
     * from a reset there is no tty and no network until loadModules() below,
     * so every line between here and "Ready with ..." is the only evidence of
     * how far this got. Cheap, and it makes a wedged restart legible instead
     * of a black screen. */
    init_scr();
    scr_printf("\nRESETDIAG MAIN_ENTER previousStage=%u\n", resetDiagStage);
    resetDiagStage = 6;
    scr_printf("\n\nps2link: image started, initializing SIF...\n");
    SifInitRpc(0);
    resetDiagStage = 7;
    scr_printf("RESETDIAG SIF_UP stage=7\n");
    scr_printf("ps2link: SIF up\n");
    init_scr();

    printWelcomeInfo();
    if (if_conf_len == 0) {
        /* TyraX: this called pkoReset(), which no longer re-execs the image -
         * it re-arms ps2link in place, which is right for a reset command and
         * wrong here. The initial boot really does want the re-exec: it is
         * what makes ps2link the EE program when another ps2link loaded it
         * over the network (otherwise both images stay resident and both
         * answer commands - measured, and it eats every deploy). */
        scr_printf("Initial boot, will load config then restart\n");
        getIpConfig();
        pkoRestartImage(); // Will restart execution
    }

    installExceptionHandlers();
    scr_printf("ps2link: exception handlers installed\n");
    resetDiagStage = 8;
    scr_printf("RESETDIAG IOP_RESET_BEGIN stage=8\n");
    restartIOP();
    resetDiagStage = 10;
    scr_printf("RESETDIAG IOP_RESET_COMPLETE stage=10\n");
    scr_printf("ps2link: IOP restarted, loading modules\n");

    strcpy(elfName, argv[0]);
    dbgscr_printf("argv[0] is %s\n", elfName);   

    dbgscr_printf("loading modules\n");
    loadModules();
    resetDiagStage = 11;
    scr_printf("RESETDIAG MODULES_LOADED stage=11\n");

    scr_printf("Using cached config\n");
    printIpConfig();

    dbgscr_printf("init cmdrpc\n");
    {
        int diagCmdResult = initCmdRpc();
        resetDiagStage = diagCmdResult >= 0 ? 12 : 13;
        scr_printf("RESETDIAG EE_CMD_INIT_RESULT result=%d stage=%u\n",
                   diagCmdResult, resetDiagStage);
    }
    scr_printf("Ready with %i memory bytes\n", GetMemorySize());

    ExitDeleteThread();
    return 0;
}
