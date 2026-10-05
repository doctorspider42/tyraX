# Fixed-hour day/night clock

Motor District previously wrote midnight or noon on every script update. The
renderer then advanced that hour by real frame dt before evaluating the cycle.
The next frame overwrote the advancement. A fixed mood therefore varied with
dt, even though the player had selected a constant time of day.

The generated API now provides `daynight::setHour(float)` and
`daynight::setPaused(bool)`. Pause suppresses only hour advancement: sky, fog,
sun/moon, stars, lighting and grading still evaluate, and physics retains real
dt. The clock starts unpaused; scene reset clears pause before its active-cycle
guard. Motor District selects and pauses its hour once per mood or scene
generation change. Other projects retain normal running clocks by default.

## Physical contrast

The private kind-20 ELF keeps the promoted colors-only sky path in both arms.
Both arms still overwrite midnight each frame, isolating pause from moving the
script's setters behind its change guard. It compares pause Off/On/Off and
On/Off/On, with 5400 loops per order and the existing common observer. Sparse
clock witnesses are outside the priced windows; no new timed Count scopes were
added. Exact midnight intentionally differs from the old near-midnight value.

| Order | First phase, ms | Middle phase, ms | Last phase, ms | Pause saving against mean outer controls, ms |
| --- | ---: | ---: | ---: | ---: |
| Off / On / Off | 18.809295 | 18.703086 | 18.824875 | 0.113999 |
| On / Off / On | 18.701654 | 18.871561 | 18.718517 | 0.161476 |

These are complete non-pacing loop costs, including existing waits, rather
than isolated EE execution or sky-only costs. Outer-phase drift is 0.015580
and 0.016863 ms. Both orders favor pause, but neither reaches the 16.67 ms
budget or establishes 60 FPS. The set-on-change production script itself was
not separately priced.

Cold PS2 witnesses keep hour exactly zero with pause and advance it without
pause; their dt matches the existing frame context. The checked paused frames
need no sky retint, while the running-clock frames retint. Those sparse
mediator counts do not establish the frequency across every priced frame.

## Qualification and evidence

The editor build, actual native compiler and source-mirror audit pass. Both PS2
orders pass strict source, native artifact, configuration and asset binding.
The same private ELF completes 5400 loops in PCSX2 and its three warm night
captures show the car, lamps and HUD without visible polygon corruption.
Emulator FPS is not a hardware result.

A freshly generated production fixture exercises default unpaused state,
positive/negative hour wrapping, active reset, an inactive-index reset, pause
with effect evaluation retained, and resume. It also switches day/night across
three scenes and returns to the first scene, asserting exact requested hours,
pause state and positive physics dt. Six warm day/night captures show the expected
mood changes and no visible stretched triangles. Its private diagnostics are absent from
production templates. No physical visual confirmation of this particular
clock-policy change was requested or obtained.

[The immutable archive](tyrax2-paused-clock-2026-10-06/payload-manifest.json)
contains source blobs, actual build records, native/ABI checks, host parser
controls, raw physical logs and emulator qualification. ELF/symbol hashes are
recorded instead of committing binaries. Failed private compilation and
rejected observer/capture attempts are retained separately; they are not
accepted performance or visual evidence.

This follows the larger [sky geometry retention](tyrax2-sky-retint.md) gain.
It repairs fixed-mood semantics and removes a small repeatable cost; the
remaining night bottleneck is still open.
