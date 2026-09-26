# Static submission batching prototype

The prototype combines up to four consecutive small StaPip bags into one
native VIF1 DMA chain. It targets the fixed submission and wait cost measured
in the Motor District garage while preserving the existing render order and
image quality.

Batching is explicit. A caller opens `StaPipCore::beginSubmissionBatch()` only
around bags whose vertex, texture-coordinate, colour and normal streams are
owned and immutable
through the renderer's next VIF1 synchronization after the matching
`endSubmissionBatch()`. The end call submits asynchronously; it is a submission
boundary, not a completion fence. Frame-owned static streams satisfy this rule
naturally. Generic `StaPipCore::render()` calls retain their existing behaviour
outside that scope. The scope is an ownership promise, not an opaque sorting
request.

The scope must end before external GS operations, view changes or pipeline
switches. Those operations are not implicit submission-batch boundaries; the
caller closes the scope first so pending PATH1 work is submitted in order.
Leaving the static pipeline also drains any outstanding batched texture readers
before unregistering the texture-mutation barrier and releasing packet storage.

Within a scope, only wholly direct, non-billboard bags are retained. Textures
qualify only when their VRAM allocation is
already resident and both wrap axes use REPEAT. Partial-frustum packaging, VU1
or EE clipping, qbuffer copy pools, billboard program switches, packet capacity
custom program overrides and the explicit scope end flush pending work. Custom
program qbuffer writers have no fixed packet-size contract, so the built-in
eight-qword bound does not apply to them. A texture miss or any other
allocation/content mutation invokes the registered cold-path barrier: it sends
pending geometry and drains PATH1 before eviction or PATH3 upload. Resident
binds remain drain-free. Thus an upload cannot reuse VRAM while queued geometry
still samples it, and copied qbuffer slots are never reused by an unsent packet.

Large direct bags append their existing 16-group qbuffer halves to the same
chain. Capacity is checked before each append using the worst-case eight
qwords per command group plus END-tag space. When a bag crosses a packet
boundary, its next geometry-only packet continues from the immediately prior
VU uniform state; no other work can interleave inside `render()`. The split
counter makes these capacity packets visible separately from bags coalesced
with their neighbours.

Each bag keeps its own `FLUSHE`, uniform unpacks and geometry commands in the
original order. The chain is built contiguously in one double-buffered packet;
it does not join separately allocated packets with DMA `NEXT` or `CALL` tags.
MVP, light matrix, light directions and single-colour values are stored inline
because packet2 unpack helpers otherwise emit DMA references to caller or stack
memory. Geometry remains referenced only under the caller's explicit lifetime
promise.

The prototype is dormant until a caller opens a scope, which provides the A/B
fallback. `StaPipTelemetry` reports eligible bags, bags emitted through batch
packets and batch packet count so unsupported or zero-hit captures are clear.

Hardware acceptance needs the complete 105-asset fixture and identical parked
views. Compare unarmed render-submission time, packet submissions, triangle
counts, resource hashes and GS captures. Also capture a detailed timeline to
confirm fewer `VIF1_submit` events without increased waits, then run long enough
to expose intermittent DMA lifetime corruption. PCSX2 compilation and images
alone are insufficient because earlier packet-chain and qbuffer lifetime bugs
only failed on a physical PS2.
