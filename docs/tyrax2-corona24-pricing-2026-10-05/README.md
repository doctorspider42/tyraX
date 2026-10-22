# Ordinary 24-bit corona pricing checkpoint

Private experiment; production code and project format are unchanged. Build V28 uses exactly the four qualified isolated engine deltas over the ordinary night game, with no diagnostic waits. Reconstruct the 501-file fixture using the base manifest and these postimages; previous archives preserve the base and isolated qualification.

Both physical orders completed 5400 loops, 384 raw samples and 15 chunks with strict source/native/config/asset closure. Off/on/off non-pacing phase means: 19.002688 / 18.909617 / 18.944845 ms. On/off/on: 19.092832 / 19.164022 / 19.089063 ms. Candidate minus mean controls is -0.064149 ms in the forward order and -0.073075 ms in reverse, using actual on/off phase roles. Both point estimates favor the candidate by only 0.06-0.07 ms; this is comparable to forward outer-arm drift (0.057842 ms), with one forward adjacent contrast only 0.035228 ms. No robust general speedup or 60 fps is established. Presentation remains approximately 33.37 ms.

Sparse EE request counts are 2 and 4. They are requests, not accepted VU sprite counts. Ordinary full-window acceptance remains unknown. The isolated output and drawing raster qualification remains valid, independently of this end-to-end pricing.

Emulator forward raw completion is valid but its observer missed phase 0: only phases 1 and 2 were captured. It is not a three-phase raster pass. Reverse captured all three owned-window images with identity and phase checks; qualitative inspection found no obvious corruption. These are emulator images, not console screenshots. Console visual feedback is recorded separately if available.

Raw records, source manifests, parsers, launch identities and machine evidence are preserved. ELF, objects and layout blobs are hash-only. Paths refer to the original lab for reproducibility; no archived script should be executed without rebinding and reviewing those paths. The complete payload is pinned by payload-sha256.json.
