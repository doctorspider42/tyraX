/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <stdio.h>
#include <sysclib.h>
#include <loadcore.h>
#include <intrman.h>
#include <types.h>
#include <sifrpc.h>
#include <cdvdman.h>

#include "excepHandler.h"
#include "net_fsys.h"
#include "cmdHandler.h"
#include "nprintf.h"

#define MODNAME "ps2link"
IRX_ID(MODNAME, 1, 8);

////////////////////////////////////////////////////////////////////////
// main
//   start threads & init rpc & filesys
int _start(int argc, char **argv)
{
    FlushDcache();
    CpuEnableIntr();

    sceCdInit(1);
    sceCdStop();

    SifInitRpc(0);

    /* TyraX: and again on the way back up. pkoReset() silenced the SPU2 before
     * the reset, but the dying game had a few milliseconds left to key a voice
     * after that, and an IOP reset triggered by anything other than pkoReset()
     * (a game resetting it, a crash) never went through that path at all.
     * The call is idempotent, so it costs nothing to be sure. The log line is
     * deliberate: it is how you confirm on real hardware that this build is
     * the one doing the silencing. */
    spu2Silence();
    printf("SPU2 silenced\n");

    fsysMount();
    printf("host: mounted\n");
    cmdHandlerInit();
    printf("IOP cmd thread started\n");
    naplinkRpcInit();
    printf("Naplink thread started\n");

    installExceptionHandlers();

    return 0;
}
