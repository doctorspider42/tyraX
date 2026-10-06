/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2004 adresd (adresd_ps2dev@yahoo.com)
 * Copyright (C) 2021 fjtrujy (fjtrujy@gmail.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

extern int excepscrdump;

unsigned int pkoSendSifCmd(unsigned int cmd, void *src, unsigned int len);
int cmdHandlerInit(void);

/* TyraX: shut the SPU2 up. The SPU2 keeps its registers - and so keeps
 * looping voices and mixing the last autodma buffer - straight through an IOP
 * reset, so a redeploy or a Stop used to leave the dead game's sound droning
 * until something else claimed the hardware. Called on both sides of the
 * reset: from pkoReset() before the EE resets us, and from _start() when
 * ps2link.irx comes back up. */
void spu2Silence(void);

#endif /* CMD_HANDLER_H */