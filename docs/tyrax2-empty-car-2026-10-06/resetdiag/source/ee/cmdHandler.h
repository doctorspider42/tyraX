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

int initCmdRpc(void);
void pkoReset(void);
/* TyraX: the initial boot's re-exec, split out of pkoReset(). It is how a
 * ps2link loaded over the network takes the EE over from the ps2link that
 * loaded it; the reset command must NOT go through it (see cmdHandler.c). */
void pkoRestartImage(void);

extern int userThreadID;
extern int excepscrdump;

#endif /* CMD_HANDLER_H */