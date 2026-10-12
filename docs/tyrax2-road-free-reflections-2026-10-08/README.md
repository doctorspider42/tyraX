# Road-free reflection evidence

The [report](../tyrax2-road-free-reflections.md) defines the nine same-bake
capture/body-pass interventions and the limits of the accepted measurements.

- `physical`: raw CSV, strict analysis (including seven rejection tests),
  reset/deploy logs and records, and 303 launch-input hashes.
- `emulator`: raw CSV, strict analysis, launch identity and visual smoke images.
  The read-only PINE capture record documents four moving captures and one miss.
- `sources`: original manifest snapshot, generated manifest, native/source
  binding, build logs, symbols and selected exact instrumented runtime sources.
- `tools`: frozen generator, analyzer, sampler and launch/capture helpers.
- `evidence-sha256.json`: hashes of archive payload, excluding itself.

The full fixture and ELF remain at
`F:/Projects/tyrax2-roadfree-reflections-20261008-v1`; assets/ELF are not archived
in the repository. Source manifests identify all inputs, but selected sources
are not a complete stand-alone source distribution. The user's source project
was not edited. Emulator timings do not price PS2 performance. There is no
production renderer change or visual-quality promotion in this experiment.
