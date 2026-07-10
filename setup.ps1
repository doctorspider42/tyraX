# Clones third-party dependencies into vendor/
$ErrorActionPreference = 'Stop'

$deps = @(
    @{ Url = 'https://github.com/ocornut/imgui.git'; Branch = 'docking'; Dir = 'vendor/imgui' },
    @{ Url = 'https://github.com/glfw/glfw.git';     Branch = '3.4';     Dir = 'vendor/glfw' },
    @{ Url = 'https://github.com/CedricGuillemet/ImGuizmo.git'; Branch = 'master'; Dir = 'vendor/imguizmo' },
    @{ Url = 'https://github.com/Nelarius/imnodes.git'; Branch = 'master'; Dir = 'vendor/imnodes' },
    @{ Url = 'https://github.com/nothings/stb.git';  Branch = 'master';  Dir = 'vendor/stb' },
    @{ Url = 'https://github.com/h4570/tyra.git';    Branch = 'master';  Dir = 'vendor/tyra' }
)

foreach ($d in $deps) {
    if (Test-Path (Join-Path $d.Dir '.git')) {
        Write-Host "OK: $($d.Dir) already present"
    }
    else {
        git clone --depth 1 --branch $d.Branch $d.Url $d.Dir
    }
}

# Ensure the stb single-headers we #include are present, even when vendor/stb
# is a stale/partial directory that predates the full clone above (no .git,
# so the clone step skips it). Back-fill any missing header directly.
$stbHeaders = @('stb_image.h', 'stb_truetype.h', 'stb_image_write.h')
foreach ($h in $stbHeaders) {
    $path = "vendor/stb/$h"
    if (-not (Test-Path $path)) {
        Write-Host "Fetching $h"
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/$h" -OutFile $path
    }
}

# Real-PS2 network deploy tools ("Run on PS2" in the editor): ps2client.exe
# talks to a console running ps2link. The Runner looks for it in
# tools/ps2client/bin; ps2link goes onto the console's memory card once
# (edit IPCONFIG.DAT for your LAN - format: "ip netmask gateway").
$ps2Tools = @(
    @{ Url = 'https://github.com/ps2dev/ps2client/releases/download/v1.3.0/ps2client-211df54b-windows-latest.tar.gz'
       Dir = 'tools/ps2client'; Probe = 'tools/ps2client/bin/ps2client.exe' },
    @{ Url = 'https://github.com/ps2dev/ps2link/releases/download/RenameMe/ps2link-0269a955-highloading.tar.gz'
       Dir = 'tools/ps2link';   Probe = 'tools/ps2link/ps2link/PS2LINK.ELF' }
)
foreach ($t in $ps2Tools) {
    if (Test-Path $t.Probe) {
        Write-Host "OK: $($t.Dir) already present"
        continue
    }
    Write-Host "Fetching $($t.Url)"
    New-Item -ItemType Directory -Force $t.Dir | Out-Null
    $tarball = Join-Path $t.Dir 'download.tar.gz'
    Invoke-WebRequest -Uri $t.Url -OutFile $tarball
    tar -xzf $tarball -C $t.Dir
    Remove-Item $tarball
}
