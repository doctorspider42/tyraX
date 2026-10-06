/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2004 adresd (adresd_ps2dev@yahoo.com)
 * Copyright (C) 2021 fjtrujy (fjtrujy@gmail.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#ifndef PS2LINK_H
#define PS2LINK_H

#include <tamtypes.h>

extern char elfName[];

/* TyraX: pkoReset() re-arms ps2link in place instead of re-execing the whole
 * EE image (see ee/cmdHandler.c), so it needs the three steps main() uses to
 * bring the console up. */
void restartIOP(void);
void loadModules(void);
void printIpConfig(void);

/* TyraX: if a Stop ever wedges the console again, the screen is the only
 * channel and text may not be available - the failure can be earlier than
 * init_scr(). The probe that cracked this one: write the GS background colour
 * (`*(volatile unsigned long *)0x120000E0`, format 0x00BBGGRR) at the point you
 * want to prove was reached. One store, no DMA, no GIF packet, nothing to
 * block on.
 *
 * Two ways it silently reports nothing, both of which cost a measurement here:
 * the background is only visible where no framebuffer is drawn (so a marker set
 * while the debug screen is up is invisible), and only when the display circuit
 * is on (so one set after ExecPS2() has blanked it is invisible too). Turn the
 * display back on first with `SetGsCrt(1, mode, 0)`, taking the mode from the
 * ROM region byte at 0x1FC7FF54 ('E' = PAL = 3, else 2) - a guessed mode fails
 * to sync and looks exactly like the black screen you are diagnosing. */

/* PRIVATE reset-only breadcrumbs, no EE stdio or DMA before quiesce. */
extern volatile unsigned int resetDiagStage;
void resetDiagEarly(unsigned int stage, u64 colour);
#endif /* PS2LINK_H */