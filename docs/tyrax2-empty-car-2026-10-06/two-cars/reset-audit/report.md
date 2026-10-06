# Read-only reset audit

No console requests, process mutations, builds, or production edits. Root's current two-car run stays untouched. Source is current repository plus existing local ps2link build tree; actual deployed host client was independently disassembled. Source checks are not proof of the flashed console image.

## Concrete host-client behavior

Actual tools/ps2client/bin/ps2client.exe SHA256 f1dc497a1496925fc58723d81280602ba0ce3bf81c950aa7857811bd057414c9 (88419 bytes); ps2client-running.exe is identical. Actual ps2link_connect at 0x140001520 opens a UDP console listener, then calls blocking network_connect(TCP,18193) at0x140001567, before creating UDP command socket18194 at0x140001584. network_connect calls Winsock connect at0x140003673, without nonblocking mode or a connect timeout. -t10 applies after connect and the reset command overrides its later mainloop timeout to0. Actual mainloop confirms timeout0 returns immediately (0x140001619 ->0x14000165a); only a negative timeout waits forever. A12-second wait should not expire because of the ordinary reset mainloop. Killing the reset process after12 seconds therefore does not establish that the reset packet was sent. Empty stdout is compatible with a pre-send TCP stall.

Actual network_send at0x140003740 loops over send return values without stopping on0/-1; on failure it can loop indefinitely and move its pointer before the packet. Actual ps2link_connect returns0 even if socket connection calls fail. These are real error-handling flaws, but their participation in the observed failure is unproven without a packet or host-stack witness. ps2link_disconnect calls pthread_cancel before closing sockets; command socket is already closed by mainloop, and a second close can early-return before other closes, although process exit ultimately closes remaining handles. Timeout is not a reset-completion acknowledgement.

## Actual version ambiguity

Repository patch and local build/ee source contain r7 hardware-quiesce-before-screen fix. However tools/ps2link/ps2link.elf (SHA48fa81aa7d9e97fcbd565830cb2c8b73de24eba9fb0641addec69dd977fe427a) actually contains r6 USB banner. Local build/ee/ps2link.elf (SHAe9ff3e068c55021c4bd65dac4e1a527878d69a9b98063d8d6e04a51121bd8f66) contains r7 noUSB banner. Packed low-noUSB image identity is in identities.json; its compressed payload banner cannot be inferred by ordinary strings. Running memory-card image is unknown. The existence of r7 patch does not prove r7 is on the console. Successful earlier resets do not settle this because r6 failure was transfer-state dependent.

## Console-side path and a remaining source suspect

IOP pkoReset validates the6-byte command, silences SPU2, then forwards RESET to EE. EE pkoReset terminates the user thread, ResetEE quiesces DMA/VU/VIF/GIF/IPU, re-arms SIF0, reinstalls exception vectors, then init_scr and ExecPS2; fresh main performs the IOP reboot. This is the r7 intended order; it cannot repair a reset command that never reached EE. Ping proves only network reachability. If UDP is actually observed and TV reaches no handover breadcrumb, EE/SIF/command-thread failure remains plausible. If handover appears but restart stalls, crt0/IOP/module path is next.

A possible IOP reconnect race remains in net_fio.c: pko_recv_bytes operates on its sock argument, but EOF/error calls pko_close_socket(), which closes the global latest pko_fileio_sock. A concurrent accept thread closes/replaces that global. A delayed old-connection EOF can therefore close a newly accepted connection. This is a source-level interleaving risk, not an observed runtime diagnosis. The helper abruptly kills the previous server and immediately starts a fresh TCP-connected reset client, exposing reconnect timing; adding an arbitrary sleep alone would not prove a fix.

Engine IrxLoader skips SifIopReset when keepIopResident is true. Generated main sets it from the ps2link argument or ps2link.run marker; the owned launch helper writes the marker. Existing successful5400-loop games prove the loader/host path worked on those boots. No new engine IOP-reset change was found in this audit.

## Evidence and bounded next qualification

The eight earlier closeout reset records finish exit0 with zero stdout. New empty-car-v2 and road-car-v2 trials also finish valid CLOSEOUTDONE5400 but their following reset stdout is0 bytes and no finished reset record exists. Thus the preceding game is not demonstrated to be hung; the failure begins in stop/reset/new-start transition.

Next qualification should independently witness TCP SYN/SYN-ACK and the actual UDP18194 six-byte RESET packet. A UDP-only reset sender can bypass the irrelevant file-server TCP connect; that would test delivery separately, not certify console reset success. Repo hostlink.h defines command0xbabe0201, packed u32/u16 header length6, command port0x4712; actual ps2client_command_reset disassembly confirms network-endian six-byte packet BA BE 02 01 00 06. No sender was executed or sent. Root should observe TV breadcrumbs and verify a fresh ELF start after any request. Confirm the running ps2link r7 banner/address or flashed artifact identity before treating source fixes as resident.

Primary ps2dev references for the host semantics: [v1.3.0 client](https://raw.githubusercontent.com/ps2dev/ps2client/v1.3.0/src/ps2client.c), [v1.3.0 network](https://raw.githubusercontent.com/ps2dev/ps2client/v1.3.0/src/network.c), [current connection sequence](https://raw.githubusercontent.com/ps2dev/ps2client/master/src/ps2link.c). Actual binary disassembly is authoritative for the installed client and stored alongside this report.
