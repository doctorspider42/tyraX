# Template exception for generated projects

TyraX itself is licensed under the Apache License 2.0 (see [LICENSE](LICENSE)).
This document grants an **additional permission** on top of it, and nothing here
takes anything away: if you would rather rely on the plain Apache-2.0 terms, you
may.

## The grant

TyraX generates a project's C++ by filling in code templates that live in this
repository (chiefly `src/templates.cpp`). Absent this exception, that generated
code would begin life as a copy of Apache-2.0-licensed text and carry Apache-2.0
obligations into your game.

> As a special exception, the copyright holder of TyraX gives you unlimited
> permission to use, modify, distribute and sell the source files that TyraX
> generates into your project, and any binary built from them, **without any
> condition whatsoever** arising from TyraX's own copyright — no attribution, no
> license text, no notice, no source disclosure, and no restriction on
> commercial use. This permission applies to the generated output only; it does
> not apply to the TyraX editor's own sources, which remain under Apache-2.0.

In plain terms: **the game is yours.** Ship it closed-source, sell it, license it
however you like. TyraX asks nothing.

## What this exception cannot do

A license can only waive the rights of whoever holds them, and a generated game
contains code TyraX did not write:

- **The Tyra engine** ([h4570/tyra](https://github.com/h4570/tyra)) — Apache-2.0,
  copyright Sandro Sobczyński and the Tyra contributors. Every generated game
  links it.
- **PS2SDK** ([ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk)) — Academic Free
  License v2.0. Every generated game links it.
- **PS2SDK's `audsrv`** — **GNU Library GPL v2**, and the exception to the line
  above: every file of that module says so, where the rest of the SDK says AFL.
  Every generated game with any sound includes it, forked in-tree at
  [`vendor/tyra/audsrv`](vendor/tyra/audsrv/README.md).

Those are not TyraX's rights to waive, so their terms follow your game.

**This is still fine for a commercial closed-source release**, but the two
licenses ask for different things and it is worth knowing which is which.
Apache-2.0 and AFL-2.0 are *attribution* licenses: ship the credit and you are
done. The LGPL on `audsrv` is a **library** copyleft — it does not reach your
game code, art, audio or levels, and it does not oblige you to publish your
source, but it does ask that `audsrv`'s own source stay available and that
someone holding your binary be able to relink it against a modified copy.
TyraX carries that source in its public repository, which is what satisfies it
for a game built here; if you ship a game built from a private fork of the
editor, keep that directory public. None of the three restricts commercial use.

## How to comply, concretely

Every project TyraX creates gets a **`THIRD-PARTY-NOTICES.txt`** at its root,
pre-filled with exactly the notices above. Ship that file with your game — beside
the ELF, inside the package, or reproduced in an in-game credits screen — and you
are done. It is written once and never regenerated, so you can extend it with
your own credits and keep them.

If your project predates this file, run one build in the editor and it will
appear.

Two footnotes worth knowing:

- If you modify the **engine** (`vendor/tyra/engine`) and ship the result,
  Apache-2.0 §4(b) asks you to mark the files you changed as modified. TyraX's
  own engine changes are already marked `Modified by TyraX`.
- The exception above covers TyraX's *generated output*. Redistributing the
  **editor** — its sources, or a binary of it — is plain Apache-2.0, and a binary
  release needs [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) to travel with
  it.
