# syntax=docker/dockerfile:1
#
# The TyraX toolchain image, inherited half - now the A/B REFERENCE, not the
# published image.
#
# THE SHIPPED IMAGE IS Dockerfile.fromsource, next to this file, and it is the
# only one CI builds. This one is the only image that can run Sony's `vcl`, and
# every assembler comparison in docs/toolchain-image.md is measured against it -
# same base, same GCC, same PS2SDK, only VCL_IMPL differs, which is what makes a
# difference attributable to the assembler alone. Keeping it is cheap; publishing
# it is not (a second job on every push to docker/, a second GHCR package, and
# the licence question below), so it is built locally by whoever needs it. That
# also means CHANGES HERE ARE NOT COVERED BY CI - run docker/build.ps1 before
# committing one.
#
# Until this file existed, every project's docker-compose.yml named `h4570/tyra`,
# and nothing in this repo could rebuild it: a Docker Hub image published in July
# 2022 (Ubuntu 20.04, GCC 11.3), built from a Dockerfile that lives in the
# upstream Tyra repo, referenced at `:latest` so a rebuild could silently move
# under us. This file took over that reference so the image could be pinned,
# inspected and added to.
#
# What it changes, deliberately kept to the minimum:
#
#   * the toolchain layer is PINNED BY DIGEST instead of `:latest`, so it cannot
#     move at all;
#   * our patched ps2link is built in and shipped inside the image, so a console
#     can be flashed without running tools/ps2link/build.* first;
#   * both assemblers are installed, and VCL_IMPL decides which wears the name
#     `vcl` - the A/B knob this image now exists for.
#
# What it does NOT change: the compiler, the PS2SDK, `vcl` and `vclpp` are the
# same bits every game in this repo has ever been built with. That is the whole
# point of inheriting rather than rebuilding - see "Why the toolchain is
# inherited" in docs/toolchain-image.md for the measurements behind that. Short
# version: the from-source `openvcl` compiles all 25 of the engine's VU1
# microprograms and all 45 a project can generate, a game built with it renders a
# pixel-identical frame in PCSX2, and its resident VU1 set is SMALLER than Sony's
# (1988 words against 2028, ceiling 2042). The clip_c miscompile that used to be
# the last blocker is fixed. So `openvcl` is not an experiment here; it is what
# the published image uses exclusively, and VCL_IMPL below is what lets this
# image still produce the legacy side of a comparison.
#
# Build it: docker/build.ps1 (Windows) or docker/build.sh (Linux).
#
# The build context is the REPO ROOT (tools/ps2link/tyrax.patch is copied in);
# Dockerfile.dockerignore next to this file keeps everything else out of it.

# h4570/tyra:latest as it stood on 2026-08-04 - Ubuntu 20.04, GCC 11.3.0,
# PS2SDK+gsKit under /usr/local/ps2dev, vcl (a 32-bit x86 binary, hence the
# qemu-i386/libstdc++5:i386 layers) and vclpp on PATH, rsync and make installed.
# Bump this digest ONLY as a deliberate toolchain change, with a game rebuilt and
# booted to prove it (tyra-testing, layer 3).
ARG TOOLCHAIN_IMAGE=h4570/tyra@sha256:1fa117cbd140b3e6506a41654fb1476c73c568ed1522a6470744eee50fab525f

# Builds ps2link only - a host-side artifact for the console, not part of any
# game compile, so it uses the CURRENT official toolchain rather than the 2022
# one above. Same tag tools/ps2link/build.* pins, so both produce the same ELF.
ARG PS2DEV_IMAGE=ps2dev/ps2dev:v2.0.0

# Which VU assembler `vcl` resolves to for the engine's 25 microprograms.
#
#   legacy  - Sony's vcl 1.4beta7, the one every game here has been built with.
#   openvcl - the from-source replacement, built below. Installed either way as
#             /usr/local/bin/openvcl so both can be run side by side; this ARG
#             only decides which one wears the name `vcl` that Makefile.base
#             invokes.
#
# Switching this changes the microcode of the whole renderer. It is a migration
# with its own verification (render A/B + FPS), tracked in
# docs/toolchain-image.md - not a flag to flip casually.
#
# The default stays `legacy` although openvcl won the comparison, and that is not
# inertia: this image's remaining job IS to be the legacy side of it. An openvcl
# build of THIS image is still worth having - it isolates the assembler from the
# compiler, which the published image cannot, since it changes both at once.
ARG VCL_IMPL=legacy

# Flags the `vcl` wrapper passes to openvcl when VCL_IMPL=openvcl. Overridable so
# a suspect flag can be bisected against a booting game without editing anything:
#   docker/build.ps1 -Tag t:probe -VclImpl openvcl `
#     -VclFlags "--schedule-flag-readers --fmac-interlock"
#   docker/build.ps1 -Tag t:stock -VclImpl openvcl -VclFlags ''   # none at all
# Each one is measured in docs/toolchain-image.md; none is on by default in the tool
# itself, and with all twenty openvcl is SMALLER than Sony's vcl on every corpus
# measured here - the resident VU1 set at 1978 words against SCE's 2028 (ceiling 2042),
# the engine's 25 at 3928 against 3982, the 45 a project generates at 9254 against 9264 -
# while the rendered frame does not move: the nineteenth flag's build was checked
# pixel-identical against Sony's, and the twentieth is pixel-identical against the
# nineteenth's. Keep this string equal to the one in
# Dockerfile.fromsource - the two images are only comparable while the assembler
# is invoked identically. The Runner rebuilds the microcode when this string changes,
# so a bisection does not need --rebuild - but check the log says "VU assembler
# changed" before trusting a picture.
#
# --pair-best-of-many supersedes --pair-best-of-two (its strategy table has that
# pair as its first two entries); both are passed so a bisection can drop the
# newer one and land on the older behaviour rather than on no best-of at all.
#
# The six allocator flags are not optional in the way the density ones are: WITHOUT
# them openvcl compiles only 23 of the 45 VU1 programs a project generates when it
# uses the VU authoring layer (Sony's vcl compiles 45). With them it compiles all 45.
# Dropping one to bisect a rendering fault is fine; shipping without them is not.
#
# --drop-dead-writes is the last one and it is worth its own note: SCE's vcl does
# dead-code elimination and openvcl did none, so a description that names a whole
# 4-component constant while the body reads two components paid for all four. On the
# generated corpus that was 312 instructions nobody reads - 93 loi, 65 lq, 55 muli
# and the rest - and removing them closed the size gap against SCE from 242 words to
# 44 (0.47%) while taking the resident set to 1988.
#
# --exempt-full-clip-masks and --clip-exemption-best-of are the eighteenth and the
# nineteenth, and they are one idea in two halves. openvcl's emitter and openvcl's
# scheduler disagreed about the CLIP window: the emitter exempted a reader whose mask
# covers the whole 24-bit register ("is anything outside" - it gets the same answer
# from any window position) while the scheduler held every reader four cycles behind
# its CLIP regardless of mask, so the emitter's exemption could never pay. The first
# flag makes the scheduler agree.
#
# On its own that was not a pure win. Freeing a reader lets it issue early, and every
# row after it then sees a different ready set: stapip_cull_d and stapip_cull_td each
# came out a row worse while stapip_cull_tce came out a row better, so the RESIDENT
# set went 1988 -> 1990 while the generated corpus went 9308 -> 9286. The second flag
# stops that being a trade. It schedules each program BOTH ways and keeps whichever
# emits fewer instruction words, ties going to "off". Both arms are legal - the
# exemption only ever removes a constraint the emitter does not impose either - so the
# choice is pure size, and it is made against the WHOLE program's word count, because
# the segment holding the freed reader is never the one that pays.
#
# Measured over all 70 programs, against the seventeen flags above:
#   generated 45   9308 -> 9286   (gap to SCE 9264: +44 -> +22, 0.24%)
#   engine 25      3992 -> 3976   (SCE 3982 - openvcl is now 6 words UNDER Sony)
#   resident 10    1988 -> 1986   (SCE 2028, ceiling 2042)
# Every win the eighteenth flag had is kept and all three of its regressions are gone;
# 10 of the 70 programs take the exempt arm and 60 keep the schedule they had. The
# loop-weighted cost of one batch falls 816 cycles over the 70, and mcpip_cull_vu1's
# gap to SCE goes from +21.8% to +7.7%. All 156 positional readers keep exactly the
# distance the stock scheduler gave them - that is the safety property, and it is
# measured, not argued.
#
# What it costs is compile time. 26 of the 70 programs hold a full-window reader and
# are scheduled twice; the other 44 are detected and skipped, because with no such
# reader the second schedule is provably the first one. The 70-program microcode build
# goes from ~118s to ~127s here, about +7%, once per engine rebuild.
#
# --sink-loads-best-of is the twentieth, and it is the four sinking flags being made to
# pay their own way. Sinking a load down to its reader shortens the live range that does
# not fit, and without it eleven of the forty-five generated programs run out of
# registers - that is why those flags are here. But where the unsunk arm DOES allocate
# it is usually the better code: the load lands next to the instruction reading it, and
# register allocation, which runs BEFORE scheduling in this compiler, writes an anti-
# dependence between the two that the scheduler cannot undo. Over the 59 programs that
# compile both ways the unsunk arm is smaller far more often than larger, so the four
# flags were paying for eleven programs out of the other fifty-nine's pockets.
#
# So compile each program BOTH ways and keep the smaller - per program, on the finished
# word count, the same criterion the size gate uses. The sinking arm becomes the
# fallback for exactly the programs that need it. Ties keep the sinking arm, so the flag
# can only ever read as a win over the build without it.
#
#   generated 45   9286 -> 9254   (SCE 9264: +22 -> -10, the last corpus Sony still won)
#   engine 25      3976 -> 3928   (SCE 3982)
#   resident 10    1986 -> 1978   (SCE 2028, ceiling 2042)
#
# The 70 split 18 / 41 / 11: 18 take the unsunk arm, 41 keep the sunk one (35 because
# the two arms are the same size and 6 because the unsunk one is bigger), and 11 fall
# back because the unsunk arm cannot allocate at all. So 52 programs come out byte-
# identical to the nineteen-flag build, and every one of the 18 that change is byte-
# identical to what the unsunk arm emits when it is the ONLY arm - which is the check
# that the two-pass plumbing leaves nothing of the first attempt behind. Cycles fall as
# well, with the FMAC read-after-write interlock modelled: -217 on the generated corpus,
# -216 on the engine, -111 on the resident set, because a load that was not sunk is an
# anti-dependence that was not written. examples/vu-lab renders 0 of 526264 pixels
# different from the nineteen-flag build, against an idle control that also measures 0.
#
# The cost is compile time and this one is NOT cheap. Nothing can predict whether the
# unsunk arm will allocate, so there is no predicate to skip it with the way
# --clip-exemption-best-of has one: allocation AND emission run twice for all 70. Run
# serially in a container that is 170s -> 333s for the 70; the engine's own 25-program
# rebuild inside a real build is `make -j`-parallel and goes 26/29s -> 43/47s over
# alternated rounds, about +64%, once per engine rebuild.
ARG VCL_FLAGS="--schedule-flag-readers --fmac-interlock --sce-latencies --emit-delay-fillers --branch-interlock --branch-bubble-on-dependency --loop-liveness-always --upper-move-with-w --pair-best-of-two --pair-best-of-many --trim-uncarried-ranges --coalesce-float-writes --split-dead-float-ranges --clip-window-liveness --sink-loads --sink-loads-across-stores --sink-loads-into-loops --sink-loads-past-branches --sink-loads-best-of --drop-dead-writes --exempt-full-clip-masks --clip-exemption-best-of"

# ------------------------------------------------------------------- openvcl --
# Built on the SAME distro release the final image runs (Ubuntu 20.04 / glibc
# 2.31), because that is what makes the binary runnable there: the official
# ps2dev images are Alpine/musl, and a musl binary cannot execute here. Focal's
# cmake 3.16 and g++ 9 clear openvcl's requirements (cmake >= 3.13, C++11).
FROM ubuntu:20.04 AS openvcl-build

ARG OPENVCL_REPO=https://github.com/ps2dev/openvcl.git
ARG OPENVCL_COMMIT=a5867c3daf03828806ee966aca4116622da3f671
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake make git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# openvcl-tyrax.patch - one fix this engine needs, and it is worth upstreaming
# (see "The openvcl migration" in docs/toolchain-image.md): accept CLIPw's implied
# w component, instead of rejecting the operand and then emitting the program
# WITHOUT the clipw while keeping the fcand that reads its clip flags.
#
# It used to carry a second hunk that relaxed the register allocator's conflict
# test (live ranges that merely touch). It is gone: openvcl's own test suite
# asserts the strict rule, and the register pressure it was working around
# belonged to the engine, where three programs were holding GIF tags live across
# a whole batch loop. Fixing it there made the allocator change unnecessary -
# all 25 programs allocate with stock openvcl, and its 419 tests stay green.
COPY docker/openvcl-tyrax.patch /tmp/openvcl-tyrax.patch

# BUILD_EXAMPLES defaults to ON when $PS2SDK is set and cross-compiles four PS2
# ELFs we throw away; BUILD_TESTING likewise builds a unit-test binary.
RUN git clone --quiet "${OPENVCL_REPO}" /src/openvcl \
    && git -C /src/openvcl checkout --quiet "${OPENVCL_COMMIT}" \
    && git -C /src/openvcl apply --verbose /tmp/openvcl-tyrax.patch \
    && cmake -S /src/openvcl -B /src/openvcl/build -DCMAKE_BUILD_TYPE=Release \
             -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
    && cmake --build /src/openvcl/build -j"$(nproc)" \
    && install -D -m 0755 /src/openvcl/openvcl /out/bin/openvcl

# ------------------------------------------------------------------ ps2link --
# The twin of tools/ps2link/build.sh - keep the pin and the flags in step with it
# (and with build.ps1). Two images, because which one to flash depends on how the
# console boots it; tools/ps2link/README.md has the measurements.
FROM ${PS2DEV_IMAGE} AS ps2link-build

ARG PS2LINK_REPO=https://github.com/ps2dev/ps2link.git
ARG PS2LINK_COMMIT=0c6138c5553760423070d1797ac475c4d98a06e6

RUN apk add --no-cache make git

COPY tools/ps2link/tyrax.patch /tmp/tyrax.patch

RUN mkdir -p /out \
    && git clone --quiet "${PS2LINK_REPO}" /src/ps2link \
    && git -C /src/ps2link checkout --quiet "${PS2LINK_COMMIT}" \
    && git -C /src/ps2link apply /tmp/tyrax.patch

# The dev default: high (0x01ee8000), unpacked, USB HID baked in - the same
# flags tools/ps2link/build.* uses with no switches.
RUN cd /src/ps2link && make ee LOADHIGH=1 NOUSB=0 \
    && cp ee/ps2link.elf /out/ps2link.elf

# The build to flash: low (0x00094000) + packed + no USB. `make` with no target
# runs ps2-packer and writes bin/PS2LINK.ELF - an unpacked low image
# black-screens from FreeMcBoot's menu.
RUN cd /src/ps2link && make clean && make LOADHIGH=0 NOUSB=1 \
    && cp bin/PS2LINK.ELF /out/ps2link-low-nousb-packed.elf

# -------------------------------------------------------------------- image --
FROM ${TOOLCHAIN_IMAGE}
ARG VCL_IMPL
ARG VCL_FLAGS

# Host-side artifacts, not part of the compile: the console loader, ready to copy
# out of the image (docker compose cp) instead of built per machine. See
# docs/ps2link-setup.md.
COPY --from=ps2link-build /out/ /usr/local/share/tyrax/ps2link/

# Both assemblers, always - the legacy one is already on PATH as /usr/bin/vcl.
COPY --from=openvcl-build /out/bin/openvcl /usr/local/bin/openvcl

# `vcl` is what Makefile.base runs. /usr/local/bin precedes /usr/bin on this
# image's PATH, so shadowing it there is enough and the original stays reachable
# at /usr/bin/vcl for an A/B run. Fail loudly on a typo in VCL_IMPL rather than
# silently building a game with the assembler nobody asked for.
#
# The openvcl form is a wrapper, not a symlink, because it has to pass
# --schedule-flag-readers: that flag lets instructions reading the MAC/CLIP flags
# take part in list scheduling, which is worth 12% of the emitted cycles on this
# engine's microprograms (6728 -> 5913). It is off by default in the tool because
# it changes what the known-loop-optimisation path decides - see "The openvcl
# migration" in docs/toolchain-image.md.
RUN set -e; \
    case "${VCL_IMPL:-legacy}" in \
      legacy)  ln -sf /usr/bin/vcl /usr/local/bin/vcl ;; \
      openvcl) printf '#!/bin/sh\nexec /usr/local/bin/openvcl %s "$@"\n' "${VCL_FLAGS}" \
                 > /usr/local/bin/vcl && chmod 0755 /usr/local/bin/vcl ;; \
      *) echo "VCL_IMPL must be 'legacy' or 'openvcl', got '${VCL_IMPL}'" >&2; exit 1 ;; \
    esac; \
    echo "${VCL_IMPL:-legacy}" > /usr/local/share/tyrax/vcl-impl; \
    vcl --version 2>/dev/null | head -1 || true

# NOT Apache-2.0, and the label must not claim it is: the Dockerfile is ours, the
# 812 MB it assembles are not. This image carries GCC and binutils
# (GPL-3.0-or-later, so publishing it publicly carries their source-offer duty),
# PS2SDK (AFL-2.0), ps2link and vclpp - and `vcl`, a binary that upstream ships
# with NO license at all. NOASSERTION is the honest SPDX answer for a mix like
# that; see docs/toolchain-image.md ("Licensing of the published image").
LABEL org.opencontainers.image.title="TyraX toolchain" \
      org.opencontainers.image.description="The PS2DEV toolchain TyraX games are compiled in, plus the TyraX ps2link" \
      org.opencontainers.image.source="https://github.com/doctorspider42/tyra-editor" \
      org.opencontainers.image.licenses="NOASSERTION"

WORKDIR /src
CMD ["bash"]
