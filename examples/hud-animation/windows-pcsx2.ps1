# =======================
$CUSTOM_PCSX2_PATH = "" # "D:/My/Path/To/PCSX2"
# =======================

function GetTargetELFName {
    return (Select-String -Path './Makefile' -Pattern "[^ ]*.elf").Matches.Value
}

function FindPCSX2Directory {
    if (-not [string]::IsNullOrEmpty($CUSTOM_PCSX2_PATH)) {
        return $CUSTOM_PCSX2_PATH
    }
    else {
        $pcsx2Path = "${Env:ProgramFiles}/PCSX2"
        $pcsx2Pathx86 = "${Env:ProgramFiles(x86)}/PCSX2"

        if (Test-Path -Path $pcsx2Path) {
            return $pcsx2Path
        }
        elseif (Test-Path -Path $pcsx2Pathx86) {
            return $pcsx2Pathx86
        }
        else {
            throw "PCSX2 directory not found!"
        }
    }
}

function FindPCSX2Executable {
    param ([string]$directory)

    foreach ($name in 'pcsx2.exe', 'pcsx2-qt.exe') {
        if (Test-Path -Path (Join-Path $directory $name)) { return $name }
    }
    throw "PCSX2 executable not found in: $directory!"
}

function RunPCSX2 {
    $dirPath = FindPCSX2Directory
    $isNewVersion = Test-Path -Path "$dirPath/qt.conf"
    $executableName = FindPCSX2Executable -directory $dirPath
    $executableNameWithoutExt = (Split-Path $executableName -Leaf).Split('.')[0]
    $targetFileName = "$PWD/bin/$(GetTargetELFName)"

    Stop-Process -Name $executableNameWithoutExt -ErrorAction 'SilentlyContinue'

    if ($isNewVersion) {
        Start-Process -FilePath "$dirPath/$executableName" -ArgumentList "-elf", $targetFileName
    }
    else {
        Start-Process -FilePath "$dirPath/$executableName" -ArgumentList "--elf=$targetFileName"
    }
}
