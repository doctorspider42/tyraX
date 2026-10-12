# Fixed-view static plan evidence

This archive records the completed36-phase private descriptor/classification
comparison, not a production optimization. See the
[report](../tyrax2-static-plan.md) for scope, results and limitations.

- `oracle`: source binding, manifest, build log, all6480 rows, strict analysis,
  runtime log and central/diagonal visual smoke checks.9720 plan comparisons
  complete without a mismatch; geometry byte oracles also pass.
- `emulator`: same performance ELF later used on PS2, all36 phases accepted.
- `physical`: all36 phases accepted,18 cold original comparisons, no mismatch;
  raw CSV/logs, strict analysis, launch input hashes and ELF launch authority.
- `sources`: reproduction tools and frozen relevant generated source snapshots.
- `host-controls.log`: actual cache algorithm under ASan/UBSan with shim types,
  plus11 malformed analyzer inputs rejected against oracle evidence.
- `physical-host-controls.log`: the same controls against physical evidence.
- `evidence-sha256.json`: hashes of archived payload, excluding itself.

External fixtures are `F:/Projects/tyrax2-static-plan-20261008-oracle2` and
`F:/Projects/tyrax2-static-plan-20261008-perf`. The earlier `oracle` directory
was generated but superseded before building; it is not accepted evidence.
Binaries and game assets remain outside the repository in these fixtures.
Record bytes exclude container overhead. Fixed-view package-field and geometry
byte oracles do not establish full DMA-stream or exact framebuffer equality.
No night, vehicle-driving or reflection-cost claim follows from this subset.
