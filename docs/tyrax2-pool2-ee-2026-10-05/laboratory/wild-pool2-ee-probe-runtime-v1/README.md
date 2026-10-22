# Released private Pool2 EE probe helper overlay

This overlay is prepared for root's serial twelve ordinary inside captures.
Preparation and134 offline synthetic acceptance/rejection checks pass. No target
build, launch, PINE call, screenshot, capture or process stop has been executed
by this preparation work. Keep the overlay immutable during root's captures.

Root driver example (Linux/WSL):

```
python3 /mnt/f/Projects/tyrax2-lab-20261001/wild-pool2-ee-probe-runtime-v1/run-case.py --arm 0 --vertices 96 --repeats 1 --slot 172 --native-proof /mnt/f/Projects/tyrax2-lab-20261001/wild-pool2-ee-probe-native-root-review-v1/proof.json --tag 20261005-v1
```

Use172..183 uniquely across vertices96/192 × arm0/1 × repeats1/2/3. Each case
gets a new `pool2-ee-probe-a96/b192-armN-repeatN-TAG` profile/archive and requires
an unoccupied existing PINE slot28205. The driver retains the established exact
owned entry snapshot, observed Run action, READY/halt snapshot, provenance-bound
capture and exact owned stop. It targets current `wild-pool2-ee-probe-physical-v1`;
no old fixture or old source hash is embedded. Source closure currently requires
the actual500-file frozen manifest, with every file hash checked at launch.
The native proof path is explicit and must match current ELF/symbol/source.
Runtime asset hashes are validated at launch and their manifest identity bound
through completion. Entry SaveState slot140 is private to each fresh profile.

`verify-capture.py` retains exact ELF/symbol text, paused PINE bracket, CRC-valid
SaveState archive, EE READY and halt-PC checks. It additionally validates the
independent native review fields and all final saved VU output RGBA words.
`decode-vu1-output.py` maps surviving inside source indices A96=0/75 and
B192=75/150; every vertex must contain epoch red31/32/33 for member0 or95/94/93
for member1, with exact remaining RGB/alpha values. Exact integer output follows
the actual pinned unlit FixColor clamp/ftoi0 macro. Table marker, descriptor,
GIF material/primitive headers and paired output comparison remain checked.

`protocol.py` requires unique original configuration and kind7/lazy/oracle
configuration, exact SOURCE generations1..N with ordinary readyCount0, all FRAME
counters zero, source-before-frame order and exactly one final READY. The finish
helper checks this protocol, capture authority, source/native/config/assets
identities, then retains existing screenshot/archive/owned-stop behavior.

Pairwise offline comparison example:

```
python3 decode-vu1-output.py --savestates --baseline BASELINE/state.p2s --table CANDIDATE/state.p2s --case A96 --repeats 3 --out NEW-pair.json
```

Passing this tested gate proves final requested color epoch and surviving
ordinary inside output equality. Intermediate epochs are protocol-bound rather
than individually captured; B192's first75 output vertices have been overwritten
in the final two banks. GS consumption, all game geometry, clipping, DMA/cache
ownership and performance are outside this gate.

No clip runtime verifier is released. Source-only clip design remains available
in the sibling workshop, but this inside decoder rejects marker/topology changes.
readyCount>0 alone cannot qualify clip output. A separate actual route/layout
decoder and matched baseline/candidate captures are required before acceptance.

Native review schema: status starts `PASS_`, blockers absent/empty,
actualELFSha256, actualSymbolSha256, sourceManifestSha256 and diagnosticOnly=true.
Preparation hashes and exact input derivation are in `preparation-proof.json`;
`offline-controls.json` explicitly records synthetic-only verification.
