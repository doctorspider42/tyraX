# ps2client (patched)

`bin/ps2client.exe` is [ps2dev/ps2client](https://github.com/ps2dev/ps2client)
by the [ps2dev project](https://ps2dev.github.io/) contributors, built from
v1.3.0-era sources with `nodelay.patch` applied: `TCP_NODELAY` on the ps2link
request socket. All credit for the tool goes to its ps2dev authors; upstream
carries no explicit license file, so the binary is redistributed here in the
spirit of the ps2dev homebrew SDK, unmodified except for the vendored patch.

Without it, ps2link's synchronous request/response fileio collides with
Nagle + the PS2's delayed ACKs and every exchange stalls ~200 ms - measured
~4 KB/s serving files to a real console (a 424 KB texture took 106 s, a
game boot 10 minutes). With the patch the same texture loads in ~1 s and a
full boot takes ~30 s; streaming 44.1 kHz stereo music works.

The stock release binary is kept as `bin/ps2client-stock.exe` for
comparison. `fsclient.exe` and the `tools/ps2link` release package are
fetched by `setup.ps1` (only when missing).

To rebuild: clone ps2client, `git apply nodelay.patch`, then
`gcc -O2 -std=gnu99 -o ps2client.exe src/ps2client.c src/ps2link.c
src/ps2netfs.c src/network.c src/utility.c -lwsock32` (mingw).
The patch is worth upstreaming to ps2dev/ps2client.
