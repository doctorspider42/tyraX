#include "scripts/script.hpp"

// This file intentionally registers no script. It used to be the seeded
// "press X to toggle the sky color" example, which reads as a glitch in a
// two-player demo (X is also the jump button). The file itself stays so the
// build's write-if-missing pass does not recreate the old behavior; replace
// it with your own scripts, or add new files next to it (docs/object-scripts.md).
