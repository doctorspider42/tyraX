# Historical Lab Cleanup

User-requested cleanup is restricted to `F:/Projects/tyrax2-lab-20261001`.
Current October 12 fixture directories and repository evidence archives are
outside this scope and are not removed.

`tools/tyrax2-lab-cleanup.ps1` removes regenerable `obj` and `__pycache__`
directories, then lab files with byte-identical copies under repository `docs`.
Filename and size only narrow candidates; SHA-256 of BOTH files is checked
immediately before deletion. Each deletion records the retained archive path
and hash in a timestamped CSV inside the lab. Standalone unique object files,
sources, reports, binaries and emulator states remain unless an exact archived
copy is found. Git metadata and reparse points are excluded. Recursive targets
are checked as resolved absolute descendants of the explicitly named lab.

Run without `-Apply` for an audit only. The current cleanup was explicitly
authorized by the user and is complete.
After the primary pass, `-RootBinaries` compares root-level ELF/symbol/library
files against archives by size and SHA-256 without requiring the same filename.
This retains unique binaries and catches renamed copies. Do not run applying
passes concurrently: the primary pass's file inventory is a snapshot.
The initial PowerShell-cmdlet pass was deliberately stopped after approximately
330000 inspected files to replace repeated provider calls with direct .NET
SHA-256 and checked file deletion. The stopped worker's exact command/PID was
verified first; its CSV remains available. A fresh pass resumes from remaining
files, with the same immediate source/archive hash checks. Aggregate accounting
uses all CSVs and verifies absence of each recorded removed target.

## Verified Result

- 286741 redundant archived file copies removed.
- 525 regenerable build/cache directories removed.
- A further 41693 empty directories removed.
- Removed logical payload size: 5631398820 bytes (5.245 GiB).
- 6394 distinct retained archive payloads rehashed successfully after cleanup.
- No recorded removed target remains present; interrupted-pass records are
  deduplicated by their original path.
- 314 unmatched root-level ELF/symbol/library files retained; the second
  content-based binary pass found no additional exact duplicates.
- Unique/unmatched source, raw evidence and emulator states remain. The lab
  itself is retained, not recursively deleted wholesale.

Machine-readable verification lists the
three deletion CSVs. `tools/verify-tyrax2-lab-cleanup.ps1` verifies removed
origins and all retained archive hashes. Logical bytes are not a measurement
of NTFS allocation or unrelated concurrent disk usage.

Historical archive provenance retains its original paths. Those records are
immutable evidence, not a promise that redundant working copies still exist.
Use the cleanup CSV's retained archive location for removed copies.
