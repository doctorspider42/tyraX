# Builds the TyraX toolchain image - the image generated games compile in. The
# Windows twin of build.sh; keep the two in step (same defaults, same flags).
#
# Two images, and which one you get is -FromSource:
#
#   .\build.ps1                        # docker/Dockerfile - the inherited A/B
#                                      # reference. CI does NOT build this one,
#                                      # so this script is its only check.
#   .\build.ps1 -FromSource            # docker/Dockerfile.fromsource - the image
#                                      # CI publishes as tyrax-toolchain-src.
#   .\build.ps1 -Tag ghcr.io/OWNER/tyrax-toolchain:test -Push
#   .\build.ps1 -NoCache
#
# Both land on -Tag, which defaults to tyrax-toolchain:local either way - so the
# tag says nothing about which Dockerfile produced it. Pass -Tag when you want to
# keep both around.
#
# -Toolchain replaces the digest-pinned compile environment the image inherits
# (docker/Dockerfile: TOOLCHAIN_IMAGE). That is a compiler change, not a
# packaging one - read "Why the toolchain is inherited" in
# docs/toolchain-image.md before using it, and rebuild + boot a game to verify.
#
#   .\build.ps1 -Toolchain ps2dev/ps2dev:v2.0.0   # GCC 15.2, breaks vcl (musl)
#
# Point a project at the result by writing ONE line into the project directory:
#
#   'TYRAX_IMAGE=tyrax-toolchain:local' | Set-Content <project>\.env
#
# and rebuilding - docker-compose.yml is regenerated on every build but reads
# that variable, so nothing in the editor has to change. See
# docs/toolchain-image.md.
# -FromSource builds docker/Dockerfile.fromsource instead: the same toolchain
# assembled from the official ps2dev base with openvcl, vclpp and bin2s built
# from source, plus the audsrv fork's EE half compiled in. It carries no
# unlicensed binary and is arm64-capable; it also has no Sony `vcl`, so
# -VclImpl does not apply to it. The inherited image stays this script's default
# precisely because it is the only way to run Sony's assembler for an A/B -
# which is now the only reason it exists.
param(
    [string]$Tag = 'tyrax-toolchain:local',
    [string]$Toolchain = '',
    [ValidateSet('legacy', 'openvcl')]
    [string]$VclImpl = 'legacy',
    [string]$VclFlags = '',
    [switch]$FromSource,
    [switch]$Push,
    [switch]$NoCache
)

# docker writes its build progress to stderr, which Stop would treat as fatal.
# We check $LASTEXITCODE instead (same reason as tools/ps2link/build.ps1).
$ErrorActionPreference = 'Continue'

Set-Location (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..')  # context = repo root

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'docker not found - this builds a Docker image.'
}

# The from-source image has no legacy assembler to choose between, so VCL_IMPL
# is not one of its build args - passing it would fail the build on an unknown
# ARG rather than being ignored.
$dockerfile = if ($FromSource) { 'docker/Dockerfile.fromsource' } else { 'docker/Dockerfile' }
$extra = if ($FromSource) { @() } else { @('--build-arg', "VCL_IMPL=$VclImpl") }
if ($FromSource -and $PSBoundParameters.ContainsKey('VclImpl')) {
    throw '-VclImpl does not apply to -FromSource: that image ships openvcl only.'
}
# Passed whenever it was given, EMPTY INCLUDED: `-VclFlags ''` is how you ask for
# a stock openvcl with no flags at all, and skipping the build-arg would silently
# hand you the Dockerfile's default set instead - which is exactly the mistake
# that made the first flag bisection meaningless.
if ($PSBoundParameters.ContainsKey('VclFlags')) { $extra += @('--build-arg', "VCL_FLAGS=$VclFlags") }
if ($Toolchain) {
    if ($FromSource) { throw '-Toolchain is the inherited image; -FromSource has none.' }
    $extra += @('--build-arg', "TOOLCHAIN_IMAGE=$Toolchain")
}
if ($Push)      { $extra += '--push' }
if ($NoCache)   { $extra += '--no-cache' }

# BuildKit, not the classic builder: Dockerfile.dockerignore (which is what keeps
# vendor\ and build\ out of a repo-root context) is a BuildKit feature.
$env:DOCKER_BUILDKIT = '1'

Write-Host "== Building $Tag ($dockerfile) =="
docker build -f $dockerfile -t $Tag @extra .
if ($LASTEXITCODE -ne 0) { throw "docker build failed (exit $LASTEXITCODE)" }

# A green build proves the layers ran, not that the image can compile anything.
# These are the checks a broken image fails on silently: vclpp/vcl missing their
# C++ runtime shows up as every VU1 program failing, not as a missing tool.
Write-Host '== Checking the image =='
$check = @'
set -e
for t in vcl vclpp make rsync mips64r5900el-ps2-elf-g++ dvp-as; do
    command -v "$t" >/dev/null || { echo "MISSING: $t"; exit 1; }
done
vclpp 2>&1 | grep -q Usage || { echo "vclpp does not run"; exit 1; }
# A bare `nop` will NOT do - the vcl in this image rejects it outside RAW mode.
printf ".init_vf_all\n.init_vi_all\n--enter\n--endenter\n\tadd.xyzw\tvf01, vf00, vf00\n--exit\n--endexit\n" > /tmp/t.vcl
vcl /tmp/t.vcl > /tmp/t.vsm
[ -s /tmp/t.vsm ] || { echo "vcl produced nothing"; exit 1; }
dvp-as /tmp/t.vsm -o /tmp/t.o
ls -l /usr/local/share/tyrax/ps2link/
echo "OK"
'@ -replace "`r`n", "`n"
docker run --rm $Tag sh -c $check
if ($LASTEXITCODE -ne 0) { throw "image check failed (exit $LASTEXITCODE)" }
Write-Host "== OK: $Tag =="
