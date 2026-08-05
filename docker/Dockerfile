# syntax=docker/dockerfile:1
#
# The TyraX toolchain image - what generated games are compiled in.
#
# Until now every project's docker-compose.yml named `h4570/tyra`, and nothing in
# this repo could rebuild it: a Docker Hub image published in July 2022 (Ubuntu
# 20.04, GCC 11.3), built from a Dockerfile that lives in the upstream Tyra repo,
# referenced at `:latest` so a rebuild could silently move under us. This file
# takes over that reference. It is built and published from THIS repo
# (.github/workflows/toolchain-image.yml), so the image projects pull is one we
# can pin, inspect and add to.
#
# What it changes, deliberately kept to the minimum:
#
#   * the toolchain layer is PINNED BY DIGEST instead of `:latest`, so it cannot
#     move at all;
#   * our patched ps2link is built in and shipped inside the image, so a console
#     can be flashed without running tools/ps2link/build.* first.
#
# What it does NOT change: the compiler, the PS2SDK, `vcl` and `vclpp` are the
# same bits every game in this repo has ever been built with. That is the whole
# point of inheriting rather than rebuilding - see "Why the toolchain is
# inherited" in docs/toolchain-image.md for the measurements behind that. Short
# version: the from-source `openvcl` compiles all 25 of the engine's VU1
# microprograms and a game built with it RUNS - pixel-identical to Sony's vcl in
# PCSX2 (2026-08-05), which is why `openvcl` is a supported choice here (see
# VCL_IMPL) and no longer an experiment. It is not the default for one reason:
# with the VU1 clipper its resident program set needs 2075 instructions against a
# 2042 ceiling. 33 words of scheduling density, not correctness, is what remains.
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
ARG VCL_IMPL=legacy

# Flags the `vcl` wrapper passes to openvcl when VCL_IMPL=openvcl. Overridable so
# a suspect flag can be bisected against a booting game without editing anything:
#   docker/build.ps1 -Tag t:probe -VclImpl openvcl `
#     -VclFlags "--schedule-flag-readers --fmac-interlock"
#   docker/build.ps1 -Tag t:stock -VclImpl openvcl -VclFlags ''   # none at all
# Each one is measured in docs/toolchain-image.md; none is on by default in the
# tool itself, and with all four the rendered frame is pixel-identical to the one
# Sony's vcl produces. When bisecting, force the microcode to rebuild
# (--build --rebuild) - the engine's make will not do it for an image swap.
ARG VCL_FLAGS="--schedule-flag-readers --fmac-interlock --sce-latencies --emit-delay-fillers"

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
