# Independent lattice native confirmation

Read-only confirmation of the completed pricing native fixture `wild-pool-lattice-physical-v5`, performed while the root held the shared native cache. The checker rehashed all 501 manifest sources, 1,958 input pins, 492 source mirrors, 171 dependency/object records, archives and external dependencies. It independently parsed ELF32 sections and symbols instead of relying on reported VU lengths.

The pricing ELF and symbol ELF match the root provenance, and their `.text` bytes are identical. The linked TC image contains 464 microinstructions and matches both the actual assembled cache object and the retained successful native V17 object. Expanded VCL and scheduled VSM also match V17. All other 15 linked VU images remain byte-identical to qualified pool2 V2.

Resident programs including both billboards occupy 1,998 words for VU1 clipping and 1,798 for EE clipping, below draw finish at 2,042. The actual R5900 compile-only ABI binary contains the expected ten words; the lattice counter occupies 28 bytes. All 298 runtime assets match the frozen baseline, including four ADPCM files.

`proof.json` records exact hashes and counts; `confirm.py` is the independent checker. No builds, device execution, source edits or shared-cache mutations were performed. Only this report directory was created. This confirms source and native artifact identity. It does not accept target activation, VU semantic output, pixel equivalence, actual runtime program selection or physical performance.
