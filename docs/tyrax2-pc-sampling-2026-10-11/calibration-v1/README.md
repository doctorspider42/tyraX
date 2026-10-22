# A37 Leaf Calibration Evidence

Native490 bound source files, ELF
`a87be09eeee5f91ee90f75876a62be9daca35d3ca368ce07ba739fefc0caf2c3`.
Two distinct noinline120ms leaf loops, each at997us and1733us, ordinary SDK
alarms, no -pg. This test does not draw or price Vehicle Playground.

| Target | Samples per phase | Callback leaf hits | CP0 EPC leaf hits |
| --- | --- | --- | --- |
| Hidden emulator | 119/119/69/68 | 118/119/68/66 | 0/0/0/0 |
| Physical PS2 | 118/118/68/68 | 118/115/67/68 | 0/0/0/0 |

Both pass zero overflow, successful release and no callback during a stopped
leaf spin, distinct linked leaf ranges and five rejecting reader mutations.
Raw out-of-leaf samples are retained. Callback address, NOT CP0 EPC read from
inside the SDK callback, qualifies for later instruction-residency sampling.
This does not prove unbiased renderer sampling or exact branch-delay attribution.

Installed and native-mirror libkernel.a match SHA256
`fb31fbe66c3772ef76ffac2aaafe1f90b68ed423cb8ba0b5e536d01b21320061`.
Recorded actual TimerHandler_callback disassembly forwards saved incoming a2
through s4 into callback a4. Library and installed headers are copied/pinned.
First builds needed debug.hpp and SDK u32 rather than uint32_t pointer types;
these harness compile failures are preserved, not renderer defects.

PS2 used normal editor-equivalent network reset/deploy. Client39728 stopped
after PCDONE/export; owned emulator26/Xvfb15 stopped after complete exports.
Closed reports and raw inputs are preserved in the519-file inventory. Copied
reader replay produces byte-identical reports for both targets. Archive includes
bound sources, not a complete runnable asset/build mirror. Devices remain on.

Next collect preserved current-baseline day/night driving at distinct periods,
A35 OFF; stop before export, retain unknown/kernel samples, qualify travel and
sampler-off return rasters. Histogram is attribution, not optimization or60fps.
