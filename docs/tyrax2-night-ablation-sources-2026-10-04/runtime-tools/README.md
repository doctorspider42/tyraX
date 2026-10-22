# ROOT-only completed night evidence tools

freeze-completed.py never controls processes/devices, stops an emulator, or deletes live outputs. Root invokes it only after NIGHTDONE5400 and then separately verifies the exact owned PID/client and removes only the archived live artifact whose hash equals machine-evidence.json/liveArtifactSha256.

Emulator invocation (Ubuntu): python3 freeze-completed.py --launch /mnt/f/Projects/tyrax2-lab-20261001/night-ablation-emulator-joint-v2-attempt2-launch/owned-launch.json --stdout /home/spider/night-ablation-emulator-joint-v2-attempt2/emulator.log --artifact /mnt/f/Projects/tyrax2-lab-20261001/night-ablation-physical-v2/game/bin/night-ablation.log --environment emulator --out NEW_EVIDENCE_DIR.

Physical: normalize-physical.py RAW.log NEW-utf8.log --proof NEW-normalization.json; then freeze-completed.py with --environment ps2 --raw-stdout RAW.log --normalization-proof NEW-normalization.json and --stdout NEW-utf8.log. Windows and WSL fixture/archive authority paths translate without altering record bytes. Physical record requires same fixture/launchFiles/plan/ELF identity; process ownership/client stop remains root-controlled.

Binder rejects changed frozen497sources/manifest, ELF/symbol text/config, actual298assets incl extra/missing, native/source/build log hashes, root freeze host/review authority or launch archive hashes. It invokes exact current pinned analyze-night-cli.py, preserves rejected output and never writes runtimePASS before completed analysis/copy checks. Copies raw stdout/artifact, five authority/config/ELF classes, parser sources, build log and available launch screenshots; hashes all copied files. Sparse submission witnesses are not timed-window activation counts. No performance inference or independent screenshot review is made.

Latin1 normalizer preserves original raw bytes via exact roundtrip and unchanged authoritative ASCII NIGHT rows, records raw SHA and UTF8 SHA, and rejects existing outputs/nonASCII protocol. No original capture is changed. No host suite repeat or actual capture freezer execution by preparer.
