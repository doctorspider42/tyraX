# Private Pool-lattice source and evidence checkpoint

The [result page](../tyrax2-pool-lattice.md) records the completed physical pair: all four candidate contrasts were slower. Production rendering was not changed.

`postimages/pricing` contains fourteen exact source changes relative to the previously archived Pool2 EE producer. `postimages/probe` contains two additional fog-diagnostic changes. `metadata` binds their complete 501/502 input inventories; `evidence` preserves strict controls, native and runtime reports, both ordinary emulator orders, twelve fog captures, six output pairs and both physical orders. Earlier no-fog and rejected ordinary attempts remain explicitly separate.

Start from the exact 499-file source-only Pool2 EE restoration and run:

```powershell
python reconstruct.py --base BASE499 --out NEW --mode pricing
python reconstruct.py --base BASE499 --out OTHER_NEW --mode probe
```

Both modes were exercised by root and reproduce every source byte. The helper rejects extra/missing/changed preimages and creates only a new output directory. It does not build, supply resources or start a device. Restore authored resources and preserve native ADPCM conversion separately.

`SHA256.json` binds portable archive paths. Historical absolute paths identify original evidence and are not portable launch instructions. ELFs, symbols, objects, SaveStates and asset payloads are not embedded; their recorded hashes do not constitute a fresh binary build. Physical appearance confirmation remains unaccepted at this checkpoint.
