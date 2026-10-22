# V11 supplemental host and source controls

Actual v11 topology and packet-helper headers pass the existing 7,025,655-check host corpus at O0 and O2. The helper uses an API-recording shim, so actual SDK packet bytes and DMA completion are unproved. The original seven-op fog macro is byte-identical; three scatter calls use the actual cached clip vector, leaving its W lane intact. MAC arithmetic flags are not branch inputs on this path. VU scheduling, allocation, and real fog output still require native and target evidence.
