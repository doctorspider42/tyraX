# Actual V23 / V6 allocation and schedule review

Native TC is4048 bytes /506 rounded instructions, image
`1239bcee9da5f46f6d62fe05b685ad050ff909661b9a2df42137745f637d4f62`.
Actual assembled object equals the linked image;500 sources /491 mirrors and
source/mirror equality were checked read-only before artifact preservation.
VU1Clip1834+206billboards=2040, EEClip1634+206=1840, draw-finish2042. No images
were dropped. This observes native byte/budget readiness, not target execution.

The changed masks are physically distinct from results: `fogInt` is VI07,
`singleColorEnabled` is VI03. Six-corner RGBA mask15 and Z/F mask3 are loaded
into VI07 alongside ITOF; final Q mask2 is loaded alongside its SUB. Next
instruction has nop upper and FMAND VI03,VI07 lower; compare reads VI03 versus
VI07 without a intervening overwrite. These are the same upper SUB-to-FMAND
relationships as V5, while removing redundant lower writes. Q bounds retain
VI07=0x20 across both comparisons, with no intervening VI07 writer.

ADC now uses stock persistent VI02, initialized to0x8000 at image entry.
The assembled reduction contains `iand VI03,VI03,VI02`; it does not write VI02.
The marker and original count share one actual header read into VI01, then use
scratchVI07/VI08; no-marker branch delay only sets the count mask. All marker
success/rejection joins still restore VI03 from options8 before XGKICK. Buffer
VI05 is scratch only after its last package-header use and is reacquired by
XTOP at the next begin. Six-corner and per-quad loops, final Z/F/ADC/Q/RGBA
checks, original material state and endpoint compaction remain.

No additional definite allocation, persistent-register or branch-merge blocker
was found in the reviewed actual schedule. Upper MAC producers and lower FMAND
consumers are identified concretely; hardware MAC timing and whole-program VU
semantics are not independently formally proven. Actual positive/rejected output
and raster remain required. Static probe positivity cannot prove ordinary-night
activation or performance.
