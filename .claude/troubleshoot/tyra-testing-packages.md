# troubleshoot/tyra-testing-packages.md

Verifying the packages and the update check (docs/updates.md). Symptom first.

## Neither the package nor the update check can be trusted without this

**Cause:** both halves are checkable on the machine you are on; neither needs CI.
**Fix:** the pure half, then the UI half, then a real install/uninstall, then the self-update.

## The pure half

**Cause:** `update::compareVersions` and `update::parseRelease` have no ImGui, no GL and no `Project`.
**Fix:** they take a 40-line harness (the treegen/placement pattern):

```bash
g++ -std=c++20 -I src -I vendor/glfw/include harness.cpp \
    src/update.cpp src/json.cpp src/platform.cpp build/vendor/glfw/src/libglfw3.a \
    -o uh.exe -lshell32 -lole32 -luuid -lws2_32 -lcomdlg32 -lgdi32
```

Feed `parseRelease` a REAL payload, not only a hand-written one — `curl -sL https://api.github.com/repos/<any>/releases/latest` is one command and it proves the asset picker survives GitHub's actual shape. The same harness can call `fetchLatest()` for the live path; against a repository with no releases the honest answer is *no releases have been published yet*, which is a state and not a failure.

## The UI half

**Cause:** the modal is reachable with no network luck involved, because a failed check opens it too.
**Fix:**

```bash
./build/tyrax-editor.exe --ui-script "frames 10; click Help; click 'Check for updates'; wait 3; shot upd.png; dump"
```

The automatic startup check is deliberately skipped while a UI script is running (`uiScriptActive_`), so no unattended run can have a dialog open itself mid-step.

## The Windows installer is verified by installing it

**Cause:** the thing it can silently get wrong is the LAYOUT. A silent install started from a console-attached shell just sits there until something kills it.
**Fix:** drive it with `Start-Process -Wait`:

```powershell
./installer/build-installer.ps1 -SkipBuild
Start-Process dist\TyraX-Setup-<v>.exe -Wait -ArgumentList `
  '/VERYSILENT','/SUPPRESSMSGBOXES','/CURRENTUSER',"/DIR=$env:TEMP\tyrax-test","/LOG=$env:TEMP\ins.log"
```

Then prove the installed tree WORKS rather than merely exists:

```powershell
$env:TEMP\tyrax-test\bin\tyrax-editor.exe --vu-check           # finds <exe>/../vendor/tyra
$env:TEMP\tyrax-test\bin\tyrax-editor.exe --new t $env:TEMP    # then grep the engine path
Select-String tyrax-test $env:TEMP\t\docker-compose.yml        #   in the generated compose
```

Finish with `unins000.exe /VERYSILENT` and check that the directory, the Start Menu folder and the `HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall` key are all gone.

## The Linux packages are the same idea and cost less

**Cause:** all three come out of one staged tree.
**Fix:** the tarball is the one to unpack and RUN — it proves the layout for the other two:

```bash
./installer/build-package.sh --skip-build --all --out-dir /tmp/pkg
tar xzf /tmp/pkg/tyrax-<v>-linux-x86_64.tar.gz -C /tmp/pkg
/tmp/pkg/tyrax-<v>/bin/tyrax-editor --new t /tmp/pkg   # then grep the engine path
grep engine-src /tmp/pkg/t/docker-compose.yml          #   in the generated compose
```

That last line must name the UNPACKED tree's `vendor/tyra`, not your checkout's. `--deb` needs `dpkg-deb` (`apt-get install dpkg-dev`), `--rpm` needs `rpmbuild` (`apt-get install rpm`); inspect what they declare — `dpkg-deb -I`/`-c` and `rpm -qip`/`-qRp`/`-qlp` print the metadata, the dependency list and the file list, and a missing `zenity | kdialog` there is an editor whose Open button does nothing.

## The self-update is testable without a release

**Cause:** every branch of it is a refusal.
**Fix:**

```bash
g++ -std=c++20 -I src harness.cpp src/update.cpp src/json.cpp src/platform.cpp -o uh
```

Put that binary at `<root>/bin/`, write a `.tyrax-package` marker beside it and call `update::installKind`/`selfInstallBlocked` for each word: `tarball` (empty = the button appears), `deb`, `rpm`, no file at all (source checkout), and `tarball` in a `chmod a-w` directory. Then drive the real thing — build a throwaway "new release" tarball of the same harness, call `update::runInstaller` on it, and check afterwards that the tree carries the new files, that a file the new release does NOT contain survived (the overlay is deliberate) and that the binary was relaunched.
