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
