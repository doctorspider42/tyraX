/* Silencer for "Stop on PS2": the SPU2 keeps looping voices and the last
 * autodma buffer independently of the IOP, so a ps2link reset alone leaves
 * sound effects playing and hung music droning after the game is killed.
 * This program is execee'd right after the reset: it loads libsd + audsrv
 * on the freshly rebooted IOP (no SifIopReset - ps2link must stay resident)
 * and audsrv_init() performs the full SPU reset that silences everything,
 * then it sleeps. The next deploy resets ps2link again and replaces it. */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <audsrv.h>

extern unsigned char libsd_irx[];
extern int size_libsd_irx;
extern unsigned char audsrv_irx[];
extern int size_audsrv_irx;

int main(void) {
  int ret;
  SifInitRpc(0);
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();
  SifExecModuleBuffer(libsd_irx, size_libsd_irx, 0, NULL, &ret);
  SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret);
  audsrv_init();  /* sdinit: keys off every voice, stops autodma */
  audsrv_stop_audio();
  SleepThread();
  return 0;
}
