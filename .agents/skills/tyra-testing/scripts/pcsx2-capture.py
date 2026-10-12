"""Unattended PCSX2 captures - savestates and GS dumps - plus offline analysis.

Runs its OWN PCSX2 instance with its own settings directory (-datapath), so the
user's PCSX2, its global PCSX2.ini and any other worktree's emulator are never
touched. Nothing here sends global input: savestates go over PINE (PCSX2's IPC
socket) and the GS dump is PCSX2's own hotkey, rebound to F12 in the private ini
and posted as a window message to this instance's window only.

    run    <elf> [--states N] [--gsdump] [--boot-wait S] [--out DIR] [--keep]
    gs     <dump.gs[.zst]> [--passes] [--frame K]
    state  <state.p2s> [--span HEX]

`run` launches, waits for the game, captures, analyses everything it captured
into <out>/report.txt and kills the instance it started (by its own PID).
`gs` summarises a GS dump: display/draw buffer formats, dithering, fill per
render target, primitive and state-change counts, texture uploads; `--passes`
prints the per-frame pass timeline. `state` reads the DMAC registers from a
savestate and recovers the VIF1 chains of the last frame from EE RAM.

Needs Python 3.8+ and `pip install zstandard` (savestates and dumps are zstd).
The GS-dump capture is Windows-only (it needs a window message); savestates and
both analysers work on Linux too. Background and traps:
docs/emulator-captures.md.
"""
import argparse, collections, io, os, pathlib, re, shutil, socket, struct, subprocess, sys, time, zipfile

try:
    import zstandard
except ImportError:  # the analysers cannot work without it; say so up front
    sys.exit('pcsx2-capture: needs the zstandard module (pip install zstandard)')

IS_WIN = os.name == 'nt'

# ---------------------------------------------------------------------------
# PINE: u32 total length (incl. itself) + u8 opcode + args; the reply is
# u32 length + u8 result (0 OK) + payload. Hardware registers (0x1000xxxx)
# read back as 0 over PINE - DMAC state only comes from a savestate.
# ---------------------------------------------------------------------------
PINE_READ32, PINE_VERSION, PINE_SAVESTATE, PINE_TITLE, PINE_STATUS = 2, 8, 9, 11, 15


class Pine:
    def __init__(self, slot, timeout=5.0):
        if IS_WIN:
            self.s = socket.create_connection(('127.0.0.1', slot), timeout=timeout)
        else:
            run = os.environ.get('XDG_RUNTIME_DIR', '/tmp')
            name = 'pcsx2.sock' if slot == 28011 else 'pcsx2.sock.%d' % slot
            self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.s.settimeout(timeout)
            self.s.connect(os.path.join(run, name))

    def _recv(self, n):
        b = b''
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise ConnectionError('PINE socket closed')
            b += c
        return b

    def call(self, body):
        self.s.sendall(struct.pack('<I', len(body) + 4) + body)
        n = struct.unpack('<I', self._recv(4))[0]
        rest = self._recv(n - 4)
        if rest[0] != 0:
            raise RuntimeError('PINE command %d failed' % body[0])
        return rest[1:]

    def text(self, op): return self.call(bytes([op]))[4:].split(b'\0')[0].decode(errors='replace')
    def status(self): return ['running', 'paused', 'shutdown'][struct.unpack('<I', self.call(bytes([PINE_STATUS])))[0]]
    def save_state(self, slot): self.call(struct.pack('<BB', PINE_SAVESTATE, slot))


# ---------------------------------------------------------------------------
# The private PCSX2 instance
# ---------------------------------------------------------------------------
def user_documents():
    if IS_WIN:
        import ctypes, ctypes.wintypes
        buf = ctypes.create_unicode_buffer(ctypes.wintypes.MAX_PATH)
        ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buf)  # CSIDL_PERSONAL, follows OneDrive
        return pathlib.Path(buf.value)
    return pathlib.Path.home() / 'Documents'


def find_user_ini(exe):
    # Same order as src/pcsx2_config.cpp: portable install first.
    cands = [pathlib.Path(exe).parent / 'inis' / 'PCSX2.ini']
    if IS_WIN:
        cands.append(user_documents() / 'PCSX2' / 'inis' / 'PCSX2.ini')
    else:
        xdg = os.environ.get('XDG_CONFIG_HOME', str(pathlib.Path.home() / '.config'))
        cands += [pathlib.Path(xdg) / 'PCSX2' / 'inis' / 'PCSX2.ini',
                  pathlib.Path.home() / '.var/app/net.pcsx2.PCSX2/config/PCSX2/inis/PCSX2.ini']
    for c in cands:
        if c.is_file():
            return c
    sys.exit('pcsx2-capture: no PCSX2.ini found (looked in %s)' % ', '.join(map(str, cands)))


# exe -> does this build understand -datapath? Probed at most once per process.
_datapath_probe = {}


def find_pcsx2(explicit):
    if explicit:
        return explicit
    for c in (r'C:\Program Files\PCSX2\pcsx2-qt.exe', shutil.which('pcsx2-qt'), shutil.which('pcsx2')):
        if c and os.path.isfile(c):
            return c
    sys.exit('pcsx2-capture: PCSX2 not found - pass --pcsx2')


def assert_datapath_support(exe):
    """`-datapath` only exists since Apr 2026 (commit bd486f17); older builds,
    e.g. the 2.0.2 installer, pop a modal "Unknown parameter" dialog that sits
    there unattended. PCSX2 prints its usage to stderr (it is a GUI-subsystem
    exe), so probing `-help` is deterministic - see docs/emulator-captures.md."""
    # Once per interpreter: every invocation spawns the exe a second time just
    # to read its usage text. The answer cannot change inside one process, and
    # on a fast disk the probe is a fifth of the time it takes to reach the
    # guest at all.
    global _datapath_probe
    if exe in _datapath_probe:
        if not _datapath_probe[exe]:
            sys.exit('pcsx2-capture: %s does not support -datapath - install a '
                     'PCSX2 nightly or stable >= 2.8.2' % exe)
        return exe
    r = subprocess.run([exe, '-help'], capture_output=True, text=True, timeout=15,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    ok = '-datapath' in ((r.stdout or '') + (r.stderr or ''))
    _datapath_probe[exe] = ok
    if not ok:
        sys.exit('pcsx2-capture: %s does not support -datapath (usage mentions it: %r) - install a '
                 'PCSX2 nightly or stable >= 2.8.2' % (exe, '-datapath' in (r.stdout or '') + (r.stderr or '')))
    return exe


def ini_set(text, section, key, value):
    """Sets key in [section], adding the key (or the section) when missing."""
    lines = text.split('\n')
    sec, start, end = None, None, len(lines)
    for i, l in enumerate(lines):
        m = re.match(r'^\[(.+)\]\s*$', l)
        if m:
            if sec == section:
                end = i
                break
            sec = m.group(1)
            if sec == section:
                start = i
    if start is None:
        return text.rstrip('\n') + '\n\n[%s]\n%s = %s\n' % (section, key, value)
    for i in range(start + 1, end):
        # Case-insensitive: user inis may spell keys differently (PCSX2 writes
        # SaveStates, some versions/documentation Savestates) and a missed match
        # would INSERT a duplicate, leaving the user's absolute path live.
        if re.match(r'^%s\s*=' % re.escape(key), lines[i], re.I):
            lines[i] = '%s = %s' % (key, value)
            return '\n'.join(lines)
    lines.insert(end if lines[end - 1].strip() else end - 1, '%s = %s' % (key, value))
    return '\n'.join(lines)


def ini_del(text, section, key):
    """Removes key (and the section, if it empties) from [section].

    Deleting, not blanking: a key set to an empty value is NOT the same thing to
    PCSX2. GetStringList() (MemorySettingsInterface.cpp:221) returns one entry
    per matching key line, so "Paths = " yields a list holding one EMPTY string
    - `!dirs.empty()` is still true and the scan still runs. Only a key that is
    genuinely absent reads back as an empty list.
    """
    lines = text.split('\n')
    sec, start, end = None, None, len(lines)
    for i, l in enumerate(lines):
        m = re.match(r'^\[(.+)\]\s*$', l)
        if m:
            if sec == section:
                end = i
                break
            sec = m.group(1)
            if sec == section:
                start = i
    if start is None:
        return text
    body = lines[start + 1:end]
    kept = [l for l in body if not re.match(r'^%s\s*=' % re.escape(key), l, re.I)]
    if len(kept) == len(body):
        return text
    rest = lines[end:]
    if any(l.strip() for l in kept):
        return '\n'.join(lines[:start + 1] + kept + rest)
    return '\n'.join(lines[:start] + rest)


def ini_get(text, section, key):
    sec = None
    for l in text.split('\n'):
        m = re.match(r'^\[(.+)\]\s*$', l)
        if m:
            sec = m.group(1)
        elif sec == section:
            m = re.match(r'^%s\s*=\s*(.*)$' % re.escape(key), l, re.I)
            if m:
                return m.group(1).strip()
    return None


# Canonical [Folders] keys (as PCSX2 writes them) -> private sub-directory name.
# Every entry is rewritten to an ABSOLUTE path under the private root: user inis
# routinely carry absolute overrides (e.g. D:\PSX ROMs\PCSX2_Data\sstates), and
# any key we skip - by casing or by not knowing it - would leak savestates,
# memory cards and cache into the user's real PCSX2 data directory.
FOLDER_KEYS = {'Bios': 'bios', 'Snapshots': 'snaps', 'SaveStates': 'sstates',
               'MemoryCards': 'memcards', 'Logs': 'logs', 'Cheats': 'cheats',
               'Patches': 'patches', 'UserResources': 'resources', 'Cache': 'cache',
               'Textures': 'textures', 'InputProfiles': 'inputprofiles',
               'Videos': 'videos', 'Covers': 'covers'}


def prepare_datapath(dp, user_ini, slot, gs_renderer='13'):
    """PCSX2 given -datapath DIR reads DIR/PCSX2/inis/PCSX2.ini and
    DIR/PCSX2/bios - not DIR/inis. A missing ini pops the setup wizard on the
    user's screen, so it is always written before launch."""
    root = dp / 'PCSX2'
    (root / 'inis').mkdir(parents=True, exist_ok=True)
    text = user_ini.read_text(encoding='utf-8-sig')
    bios_dir = ini_get(text, 'Folders', 'Bios') or 'bios'
    src_bios = pathlib.Path(bios_dir)
    if not src_bios.is_absolute():
        src_bios = user_ini.parent.parent / src_bios
    bios = ini_get(text, 'Filenames', 'BIOS')
    dst_bios = root / 'bios'
    dst_bios.mkdir(exist_ok=True)
    stem = pathlib.Path(bios).stem if bios else None
    for f in src_bios.iterdir() if src_bios.is_dir() else []:
        if f.is_file() and (stem is None or f.stem == stem) and not (dst_bios / f.name).exists():
            shutil.copy2(f, dst_bios / f.name)
    for key, sub in FOLDER_KEYS.items():
        (root / sub).mkdir(exist_ok=True)
        text = ini_set(text, 'Folders', key, str(root / sub))
    for sec, key, val in (('EmuCore', 'EnablePINE', 'true'), ('EmuCore', 'PINESlot', str(slot)),
                          ('EmuCore', 'HostFs', 'true'), ('EmuCore', 'EnableMTVU', 'false'),
                          ('Hotkeys', 'GSDumpSingleFrame', 'Keyboard/F12'),
                          ('UI', 'SetupWizardIncomplete', 'false'),
                          # Software GS (13) in the GS section, not EmuCore: the
                          # user's ini here had Vulkan (14) and its swap-chain
                          # creation failed on every GPU slot. Software has no
# Vulkan API dependency, so the game always renders.
                           # 13 = Software GS. A caller that never needs pixels
                           # (a headless logic run) may pass 11 = Null GS, whose
                           # Draw() is an empty body - same guest-visible GS
                           # traffic, none of the CPU rasterization.
                           ('EmuCore/GS', 'Renderer', str(gs_renderer)),
                           # A headless run must NEVER pause when it loses focus:
                           # the user clicking another window silently freezes the
                           # VM mid-gym (QtHost.cpp onApplicationStateChanged) and
                           # every timing turns to noise. The user ini has it on.
                           ('UI', 'PauseOnFocusLoss', 'false'),
                           # GS VSync syncs PRESENTATION to the host monitor's
                           # refresh - a ~60Hz wall-clock throttle the guest
                           # cannot outrun, and one -unlimited does NOT disable
                           # (that only lifts the frame limiter). A headless run
                           # wants the guest to go as fast as the host can
                           # emulate; its own clock stays cycle-driven, so guest
                           # time is unaffected. The user ini has it on.
                           ('EmuCore/GS', 'VsyncEnable', 'false'),
                           # SDL's controller DB costs ~300 ms of the ~3 s to
                           # the first guest line: SDLInputSource::Initialize
                           # Subsystem calls SDL_GetGamepadMappings() and logs
                           # "885 gamepad mappings are loaded" (emulog
                           # 0,04 -> 0,33). A gym presses no pad button.
                           ('InputSources', 'SDL', 'false'),
                           # -logfile forces LOGLEVEL_DEBUG onto the FILE sink
                           # (QtHost.cpp:2269 -> VMManager.cpp:473) and
                           # Console.cpp:334 fflushes every line, so the
                           # un-guarded microVU trace lines become thousands of
                           # synchronous writes for a log nothing reads. The
                           # verdict comes from the GUEST's log.txt.
                           ('Logging', 'EnableVerbose', 'false')):
        text = ini_set(text, sec, key, val)
    # Deleted, not blanked - see ini_del. Each is startup work the run does not
    # need, measured on a 31 s gym run whose first guest line arrives 3.3 s in.
    for sec, key, why in (
            # Recursively walks the USER'S real ROM folder - "Scanning D:\PSX
            # ROMs (recursively)", GameList.cpp:689, ~190 ms - and those paths
            # belong to the machine, not to this private datapath.
            ('GameList', 'Paths', 'walk the rom folders'),
            ('GameList', 'RecursivePaths', 'walk the rom folders recursively'),
            # A stale adapter name costs a failed lookup plus a warning line
            # ("Adapter 'NVIDIA GeForce GTX 970' not found.", D3D.cpp:233)
            # before falling back to the first adapter anyway.
            ('EmuCore/GS', 'Adapter', 'stale adapter name')):
        text = ini_del(text, sec, key)
    # No BOM: the first section header must parse.
    (root / 'inis' / 'PCSX2.ini').write_bytes(text.replace('\r\n', '\n').encode('utf-8'))
    # Guard the privacy promise: after all rewrites, no [Folders] entry may
    # point outside the private root. A future PCSX2 version renaming a key
    # would otherwise silently leak writes into the user's data directory.
    written = (root / 'inis' / 'PCSX2.ini').read_text(encoding='utf-8')
    leaks = [k for k in FOLDER_KEYS if not str(ini_get(written, 'Folders', k) or '').startswith(str(root))]
    if leaks:
        sys.exit('pcsx2-capture: private ini would still write outside %s: %s' % (root, ', '.join(leaks)))
    return root


# Channel files that belong to a RUN, not to the project. Excluded from the
# copy rather than deleted after it: bin/log.txt in the source tree is the
# PREVIOUS run's log, so copy-then-delete both drags stale evidence into the
# staging area and races the emulator that may still hold a handle on it - which
# aborted the whole run with a PermissionError out of unlink().
STALE_CHANNEL_FILES = ('livedbg.cmd', 'livedbg.bin', 'livepad.bin',
                       'frame.tga', 'log.txt', 'ps2link.run')


def stage_elf(elf, work, force):
    """Long host: ELF paths crash the PS2 loader (docs/emulator-captures.md), so
    a deep path is staged: the ELF's whole directory is copied to a short one,
    assets included. --stage forces it, which also keeps the run's channel
    files (livepad.bin, log.txt, ...) apart from an editor running the same
    project."""
    elf = pathlib.Path(elf).resolve()
    if not force and len(str(elf)) <= 100:
        return elf
    dst = work / 'game'
    for attempt in range(3):
        shutil.rmtree(dst, ignore_errors=True)
        try:
            shutil.copytree(elf.parent, dst,
                            ignore=shutil.ignore_patterns(*STALE_CHANNEL_FILES))
            return dst / elf.name
        except OSError:
            if attempt == 2:
                raise
            # The previous emulator can still be releasing handles on the
            # staged tree a moment after its process exited. Back off rather
            # than failing a whole test run over a locked temp file.
            time.sleep(0.5)
    return dst / elf.name


def post_key_to_process(pid, vk):
    """WM_KEYDOWN/WM_KEYUP to the visible windows of ONE process. PostMessage
    only queues on that process's thread: no SendInput, no focus change, so the
    person at the machine is never typed at."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    wins = []
    proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def each(h, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(h):
            wins.append(h)
            user32.EnumChildWindows(h, proc(lambda c, _l: wins.append(c) or True), 0)
        return True

    user32.EnumWindows(proc(each), 0)
    scan = user32.MapVirtualKeyW(vk, 0)
    for h in wins:
        user32.PostMessageW(h, 0x0100, vk, 1 | (scan << 16))
        time.sleep(0.06)
        user32.PostMessageW(h, 0x0101, vk, 1 | (scan << 16) | (1 << 30) | (1 << 31))
    return len(wins)


def wait_for(pred, timeout, step=0.5):
    end = time.time() + timeout
    while time.time() < end:
        r = pred()
        if r:
            return r
        time.sleep(step)
    return None


def stable_new_file(folder, pattern, since, timeout):
    """A file matching pattern written after `since` whose size has stopped
    changing. By time, not by name: a savestate slot overwrites the same file."""
    def probe():
        new = [f for f in folder.glob(pattern) if f.stat().st_mtime > since]
        if not new:
            return None
        f = max(new, key=lambda p: p.stat().st_mtime)
        s = f.stat().st_size
        time.sleep(1.0)
        return f if s > 0 and f.stat().st_size == s else None
    return wait_for(probe, timeout)


def results_dir(a, kind):
    """Where RESULTS land: the project, not the temp app folder.

    The emulator instance (datapath), the staged ELF and the savestate spool
    are SCRATCH and stay under %TEMP%\\tyra-editor-test\\ - they are large,
    disposable, and nothing ever reads them twice. The artefacts a human
    actually wants afterwards (report.txt, gym savestates, GS dumps) are
    RESULTS, so they default to ./test-results/<kind>-<timestamp>/ relative
    to the directory you ran the command from. Run it from the project root
    and the evidence lands inside that project, where it can be inspected,
    diffed or attached to a commit message.

    --out overrides it. The resolved absolute path is always printed, so the
    output location is never a guess.
    """
    stamp = time.strftime('%Y%m%d-%H%M%S')
    out = pathlib.Path(a.out) if a.out else pathlib.Path.cwd() / 'test-results' / ('%s-%s' % (kind, stamp))
    out.mkdir(parents=True, exist_ok=True)
    print('results: %s' % out.resolve())
    return out


def cmd_run(a):
    exe = find_pcsx2(a.pcsx2)
    assert_datapath_support(exe)
    work = pathlib.Path(os.environ.get('TEMP', '/tmp')) / 'tyra-editor-test' / ('pcsx2-capture-%d' % a.slot)
    work.mkdir(parents=True, exist_ok=True)
    root = prepare_datapath(work, find_user_ini(exe), a.slot)
    elf = stage_elf(a.elf, work, a.stage)
    out = results_dir(a, 'run')
    out.mkdir(parents=True, exist_ok=True)
    log = work / 'emulog.txt'
    snaps, sstates = root / 'snaps', root / 'sstates'
    snaps.mkdir(exist_ok=True); sstates.mkdir(exist_ok=True)

    args = [exe, '-batch', '-nogui', '-datapath', str(work), '-logfile', str(log), '-elf', str(elf)]
    print('launch:', ' '.join('"%s"' % x if ' ' in x else x for x in args))
    proc = subprocess.Popen(args)
    print('PID', proc.pid, '(the only process this script will stop)')
    try:
        pine = wait_for(lambda: _try_pine(a.slot), 60, 1.0)
        if not pine:
            sys.exit('pcsx2-capture: PINE slot %d never answered - see %s' % (a.slot, log))
        print('PINE:', pine.text(PINE_VERSION), '|', pine.text(PINE_TITLE), '|', pine.status())
        if a.wait_log:
            rx = re.compile(a.wait_log)
            if not wait_for(lambda: log.exists() and rx.search(log.read_text(errors='replace')), a.boot_wait):
                print('warning: %r not in the log after %ss, capturing anyway' % (a.wait_log, a.boot_wait))
        else:
            time.sleep(a.boot_wait)

        captured = []
        for i in range(a.states):
            slot = 1 + i % 10
            asked = time.time()
            pine.save_state(slot)
            f = stable_new_file(sstates, '*.%02d.p2s' % slot, asked - 1, 60)
            if f:
                dst = out / ('state%d.p2s' % (i + 1))
                shutil.copy2(f, dst); captured.append(dst)
                print('savestate', i + 1, '->', dst.name)
            else:
                print('warning: savestate %d never appeared' % (i + 1))
            if i + 1 < a.states:
                time.sleep(a.interval)
        if a.gsdump:
            if not IS_WIN:
                print('gsdump: Windows-only (needs a window message) - skipped')
            else:
                asked = time.time()
                n = post_key_to_process(proc.pid, 0x7B)  # VK_F12
                f = stable_new_file(snaps, '*.gs*', asked - 1, 60) if n else None
                if f:
                    dst = out / ('frame' + ''.join(f.suffixes[-2:]))
                    shutil.copy2(f, dst); captured.append(dst)
                    print('gsdump ->', dst.name)
                else:
                    print('warning: no GS dump appeared (%d windows messaged)' % n)
    finally:
        if a.keep:
            print('--keep: leaving PID', proc.pid, 'running')
        else:
            proc.kill(); proc.wait(10)
            print('stopped PID', proc.pid)

    report = io.StringIO()
    for f in captured:
        report.write('==== %s\n' % f.name)
        old = sys.stdout; sys.stdout = report
        try:
            analyse_state(f, None) if f.suffix == '.p2s' else analyse_gs(f, False, 1)
        except Exception as e:  # one bad capture must not lose the others' report
            print('analysis failed:', e)
        finally:
            sys.stdout = old
        report.write('\n')
    (out / 'report.txt').write_text(report.getvalue(), encoding='utf-8')
    print('report:', out / 'report.txt')


def _try_pine(slot):
    """PINE answers as soon as the GUI is up, before the VM exists; the title
    command fails until a game is actually running, so it is the readiness test."""
    try:
        p = Pine(slot)
        p.text(PINE_TITLE)
        return p
    except (OSError, ConnectionError, RuntimeError):
        return None


# ---------------------------------------------------------------------------
# GS dump analysis (PCSX2 GSDumpFile layout, state version 9)
# ---------------------------------------------------------------------------
def bits(v, lo, n): return (v >> lo) & ((1 << n) - 1)


PSM = {0: 'CT32', 1: 'CT24', 2: 'CT16', 0xA: 'CT16S', 0x30: 'Z32', 0x31: 'Z24', 0x32: 'Z16', 0x3A: 'Z16S',
       0x13: 'T8', 0x14: 'T4', 0x1B: 'T8H', 0x24: 'T4HL', 0x2C: 'T4HH'}
PRIMN = ['POINT', 'LINE', 'LSTRIP', 'TRI', 'TSTRIP', 'TFAN', 'SPRITE', '?']
AD = {0x00: 'PRIM', 0x06: 'TEX0_1', 0x07: 'TEX0_2', 0x08: 'CLAMP_1', 0x09: 'CLAMP_2', 0x14: 'TEX1_1', 0x15: 'TEX1_2',
      0x18: 'XYOFFSET_1', 0x19: 'XYOFFSET_2', 0x1A: 'PRMODECONT', 0x1B: 'PRMODE', 0x1C: 'TEXCLUT', 0x3B: 'TEXA',
      0x3D: 'FOGCOL', 0x3F: 'TEXFLUSH', 0x40: 'SCISSOR_1', 0x41: 'SCISSOR_2', 0x42: 'ALPHA_1', 0x43: 'ALPHA_2',
      0x44: 'DIMX', 0x45: 'DTHE', 0x46: 'COLCLAMP', 0x47: 'TEST_1', 0x48: 'TEST_2', 0x49: 'PABE', 0x4A: 'FBA_1',
      0x4B: 'FBA_2', 0x4C: 'FRAME_1', 0x4D: 'FRAME_2', 0x4E: 'ZBUF_1', 0x4F: 'ZBUF_2', 0x50: 'BITBLTBUF',
      0x51: 'TRXPOS', 0x52: 'TRXREG', 0x53: 'TRXDIR'}


def psm(v): return PSM.get(v, hex(v))


def read_zst(path):
    raw = pathlib.Path(path).read_bytes()
    if str(path).endswith('.zst'):
        raw = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(raw)).read()
    return raw


class GsWalker:
    """Replays the GIF stream of a dump and accounts every drawing kick."""

    def __init__(self, env):
        self.env = env
        self.verts = []
        self.vsync = 0
        self.st = collections.Counter()
        self.prims = collections.Counter()
        self.fill = collections.Counter()
        self.draws = collections.Counter()
        self.dthe = collections.Counter()
        self.texfill = collections.Counter()
        self.uploads = collections.Counter()
        self.timeline = []   # [key, px, prims]

    def attr(self):
        return self.env['PRIM'] if bits(self.env['PRMODECONT'], 0, 1) else self.env['PRMODE']

    def kick(self, x16, y16, draw):
        e = self.env
        pt = bits(e['PRIM'], 0, 3)
        a = self.attr()
        c = 2 if bits(a, 9, 1) else 1
        xo, yo = bits(e['XYOFFSET_%d' % c], 0, 16) / 16.0, bits(e['XYOFFSET_%d' % c], 32, 16) / 16.0
        self.verts.append((x16 / 16.0 - xo, y16 / 16.0 - yo))
        self.st['vertex_kicks'] += 1
        need = {0: 1, 1: 2, 2: 2, 3: 3, 4: 3, 5: 3, 6: 2}.get(pt, 1)
        if len(self.verts) < need:
            return
        v = self.verts
        tri = v[-3:] if pt in (3, 4) else ([v[0], v[-2], v[-1]] if pt == 5 else None)
        if draw:
            sc = e['SCISSOR_%d' % c]
            sx0, sx1, sy0, sy1 = bits(sc, 0, 11), bits(sc, 16, 11) + 1, bits(sc, 32, 11), bits(sc, 48, 11) + 1
            if tri:
                (ax, ay), (bx, by), (cx, cy) = tri
                area = abs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) / 2.0
                bw, bh = max(ax, bx, cx) - min(ax, bx, cx), max(ay, by, cy) - min(ay, by, cy)
                cw = max(0.0, min(max(ax, bx, cx), sx1) - max(min(ax, bx, cx), sx0))
                ch = max(0.0, min(max(ay, by, cy), sy1) - max(min(ay, by, cy), sy0))
                # Scale by the scissored fraction of the bounding box: a guard-band
                # triangle mostly off screen must not count its whole area.
                px = area * (cw * ch / (bw * bh)) if bw * bh > 0 else 0.0
            elif pt == 6:
                (x0, y0), (x1, y1) = v[-2], v[-1]
                px = max(0.0, min(max(x0, x1), sx1) - max(min(x0, x1), sx0)) * \
                    max(0.0, min(max(y0, y1), sy1) - max(min(y0, y1), sy0))
            else:
                px = 0.0
            fr = e['FRAME_%d' % c]
            key = (bits(fr, 0, 9) * 32, psm(bits(fr, 24, 6)))
            self.prims[PRIMN[pt]] += 1
            self.fill[key] += px
            self.draws[key] += 1
            self.st['pixels'] += px
            self.dthe[(bits(e['DTHE'], 0, 1), key[1])] += px
            tex = None
            if bits(a, 4, 1):
                tx = e['TEX0_%d' % c]
                tex = (psm(bits(tx, 20, 6)), bits(tx, 0, 14))
                self.texfill[tex[0]] += px
                self.st['pixels_textured'] += px
            if bits(a, 6, 1):
                self.st['pixels_blended'] += px
            tk = (self.vsync, key, bits(e['DTHE'], 0, 1), tex, '%010X' % (e['ALPHA_%d' % c] & 0xFFFFFFFFFF)
                  if bits(a, 6, 1) else '-', PRIMN[pt], '%08X' % bits(fr, 32, 32))
            if self.timeline and self.timeline[-1][0] == tk:
                self.timeline[-1][1] += px; self.timeline[-1][2] += 1
            else:
                self.timeline.append([tk, px, 1])
        if pt in (0, 1, 3, 6):
            self.verts = []
        elif pt in (2, 4):
            self.verts = v[-(need - 1):]
        elif pt == 5:
            self.verts = [v[0], v[-1]]

    def reg(self, addr, val):
        if addr in (0x04, 0x05):          # XYZF2 / XYZ2 - drawing kick
            self.kick(bits(val, 0, 16), bits(val, 16, 16), True); return
        if addr in (0x0C, 0x0D):          # XYZF3 / XYZ3 - no drawing
            self.kick(bits(val, 0, 16), bits(val, 16, 16), False); return
        n = AD.get(addr)
        if not n:
            return
        if n == 'PRIM':
            self.verts = []
            self.st['prim_writes'] += 1
        if n.startswith('TEX0') and self.env.get(n) != val:
            self.st['tex0_changes'] += 1
        if n.startswith('FRAME') and self.env.get(n) != val:
            self.st['frame_changes'] += 1
        self.env[n] = val
        if n == 'TRXDIR' and bits(val, 0, 2) == 0:
            self.st['uploads'] += 1
            self.uploads[psm(bits(self.env['BITBLTBUF'], 56, 6))] += 1

    def packet(self, data):
        o, L = 0, len(data)
        while o + 16 <= L:
            lo, hi = struct.unpack_from('<QQ', data, o); o += 16
            nloop, pre, prim, flg = bits(lo, 0, 15), bits(lo, 46, 1), bits(lo, 47, 11), bits(lo, 58, 2)
            nreg = bits(lo, 60, 4) or 16
            if flg == 0:
                if pre:
                    self.reg(0x00, prim)
                for _ in range(nloop):
                    for r in range(nreg):
                        if o + 16 > L:
                            return
                        d0, d1 = struct.unpack_from('<QQ', data, o); o += 16
                        desc = bits(hi, r * 4, 4)
                        if desc == 0xE:
                            self.reg(bits(d1, 0, 8), d0)
                        elif desc == 0x0:
                            self.reg(0x00, d0)
                        elif desc in (0x4, 0x5):   # packed XYZF2/XYZ2, ADC in bit 111
                            self.kick(bits(d0, 0, 16), bits(d0, 32, 16), not bits(d1, 47, 1))
                        elif desc in (0xC, 0xD):
                            self.kick(bits(d0, 0, 16), bits(d0, 32, 16), False)
                        elif desc in (0x6, 0x7, 0x8, 0x9):
                            self.reg(desc, d0)
            elif flg == 1:
                n = nloop * nreg
                for i in range(n):
                    if o + 8 > L:
                        return
                    d = struct.unpack_from('<Q', data, o)[0]; o += 8
                    desc = bits(hi, (i % nreg) * 4, 4)
                    if desc not in (0xE, 0xF):
                        self.reg(desc, d)
                if n & 1:
                    o += 8
            else:
                self.st['image_bytes'] += nloop * 16
                o += nloop * 16


def priv_regs(r):
    p = lambda o: struct.unpack_from('<Q', r, o)[0]
    pm, s2 = p(0), p(0x20)
    out = ['PMODE EN1=%d EN2=%d MMOD=%d AMOD=%d ALP=%d | SMODE2 INT=%d FFMD=%d' % (
        bits(pm, 0, 1), bits(pm, 1, 1), bits(pm, 5, 1), bits(pm, 6, 1), bits(pm, 8, 8), bits(s2, 0, 1), bits(s2, 1, 1))]
    for i, (fb, dp) in enumerate(((0x70, 0x80), (0x90, 0xA0))):
        v, d = p(fb), p(dp)
        out.append('DISPFB%d FBP=%d FBW=%d PSM=%s | DISPLAY DX=%d DY=%d DW=%d DH=%d' % (
            i + 1, bits(v, 0, 9) * 32, bits(v, 9, 6), PSM.get(bits(v, 15, 5), '?'),
            bits(d, 0, 12), bits(d, 12, 11), bits(d, 32, 12) + 1, bits(d, 44, 11) + 1))
    return out


def analyse_gs(path, passes, frame):
    raw = read_zst(path)
    f = io.BytesIO(raw)
    u32 = lambda: struct.unpack('<I', f.read(4))[0]
    if u32() != 0xFFFFFFFF:
        sys.exit('not a new-format PCSX2 GS dump')
    hdr = f.read(u32())
    sver, ssize, soff, slen, crc = struct.unpack_from('<5I', hdr)
    print('serial %s  crc %08X  state version %d' % (hdr[soff:soff + slen].decode(errors='replace'), crc, sver))
    state = f.read(ssize)
    regs = f.read(8192)
    # Freeze order (GSState::Freeze, v9): no PRMODE field.
    env, off = {'PRMODE': 0}, 4
    for n in ('PRIM', 'PRMODECONT', 'TEXCLUT', 'SCANMSK', 'TEXA', 'FOGCOL', 'DIMX', 'DTHE', 'COLCLAMP', 'PABE',
              'BITBLTBUF', 'TRXDIR', 'TRXPOS', 'TRXREG', 'TRXREG_'):
        env[n] = struct.unpack_from('<Q', state, off)[0]; off += 8
    for c in (1, 2):
        for n in ('XYOFFSET', 'TEX0', 'TEX1', 'CLAMP', 'MIPTBP1', 'MIPTBP2', 'SCISSOR', 'ALPHA', 'TEST', 'FBA',
                  'FRAME', 'ZBUF'):
            env['%s_%d' % (n, c)] = struct.unpack_from('<Q', state, off)[0]; off += 8
    w = GsWalker(env)
    final_regs = regs
    while True:
        t = f.read(1)
        if not t:
            break
        t = t[0]
        if t == 0:          # transfer: u8 path, u32 size, data
            f.read(1)
            w.packet(f.read(u32()))
        elif t == 1:        # vsync
            f.read(1); w.vsync += 1
        elif t == 2:        # ReadFIFO2
            u32()
        elif t == 3:        # privileged registers
            final_regs = f.read(8192)
        else:
            print('unknown dump packet %d, stopping' % t); break
    n = max(w.vsync, 1)
    print('\n'.join(priv_regs(final_regs)))
    print('vsyncs in dump: %d (per-vsync figures below are the dump total / %d)' % (w.vsync, n))
    print('pixels/vsync %.0f (textured %.0f, blended %.0f)  prims/vsync %.0f  vertex kicks/vsync %.0f' % (
        w.st['pixels'] / n, w.st['pixels_textured'] / n, w.st['pixels_blended'] / n,
        sum(w.prims.values()) / n, w.st['vertex_kicks'] / n))
    print('per vsync: PRIM writes %.0f, TEX0 changes %.0f, FRAME changes %.0f, uploads %.0f (%.1f KB IMAGE)' % (
        w.st['prim_writes'] / n, w.st['tex0_changes'] / n, w.st['frame_changes'] / n, w.st['uploads'] / n,
        w.st['image_bytes'] / n / 1024))
    print('primitives:', dict(w.prims))
    print('dithering x target format (pixels): ' + ', '.join(
        'DTHE=%d %s %.0f' % (k[0], k[1], v) for k, v in w.dthe.most_common()))
    print('fill per render target (block address, format):')
    for k, v in w.fill.most_common(16):
        print('  FBP=%-6d %-5s %11.0f px %8d prims' % (k[0], k[1], v, w.draws[k]))
    print('textured fill by texture format:', {k: int(v) for k, v in w.texfill.most_common()})
    print('uploads by format:', dict(w.uploads))
    print('note: pixels are geometric coverage clipped to the scissor box - an upper bound, no depth test')
    if passes:
        print('\npass timeline, vsync %d (runs >= 20000 px)' % frame)
        for k, px, cnt in w.timeline:
            if k[0] == frame and px >= 20000:
                print('  FBP=%-6d %-5s DTHE=%d tex=%-18s alpha=%-11s %-6s msk=%s %9.0f px %6d prims' % (
                    k[1][0], k[1][1], k[2], k[3], k[4], k[5], k[6], px, cnt))


# ---------------------------------------------------------------------------
# Savestate analysis: DMAC registers + the VIF1 chains left in EE RAM
# ---------------------------------------------------------------------------
IDN = ['refe', 'cnt', 'next', 'ref', 'refs', 'call', 'ret', 'end']
VIFN = {0: 'NOP', 1: 'STCYCL', 2: 'OFFSET', 3: 'BASE', 4: 'ITOP', 5: 'STMOD', 6: 'MSKPATH3', 7: 'MARK',
        0x10: 'FLUSHE', 0x11: 'FLUSH', 0x13: 'FLUSHA', 0x14: 'MSCAL', 0x15: 'MSCALF', 0x17: 'MSCNT', 0x20: 'STMASK',
        0x30: 'STROW', 0x31: 'STCOL', 0x4A: 'MPG', 0x50: 'DIRECT', 0x51: 'DIRECTHL'}
UNPK = {0: 'S-32', 1: 'S-16', 2: 'S-8', 4: 'V2-32', 5: 'V2-16', 6: 'V2-8', 8: 'V3-32', 9: 'V3-16', 10: 'V3-8',
        12: 'V4-32', 13: 'V4-16', 14: 'V4-8', 15: 'V4-5'}


def vif_name(code):
    c = (code >> 24) & 0x7F
    return 'UNPACK ' + UNPK.get(c & 0xF, '?') if c >= 0x60 else VIFN.get(c, '?%02X' % c)


def read_p2s(path, names):
    """A .p2s is a zip whose entries are zstd (method 93), which Python's
    zipfile refuses - so the raw entry data is decompressed here."""
    out = {}
    with zipfile.ZipFile(path) as z, open(path, 'rb') as fh:
        for i in z.infolist():
            if i.filename not in names:
                continue
            fh.seek(i.header_offset)
            h = fh.read(30)
            nl, el = struct.unpack_from('<HH', h, 26)
            fh.seek(i.header_offset + 30 + nl + el)
            d = fh.read(i.compress_size)
            if i.compress_type == 93:
                d = zstandard.ZstdDecompressor().decompress(d, max_output_size=i.file_size)
            elif i.compress_type == 8:
                import zlib
                d = zlib.decompress(d, -15)
            out[i.filename] = d
    return out


def analyse_state(path, span):
    import numpy as np  # only the chain search needs it
    d = read_p2s(path, ('eeMemory.bin', 'eeHwRegs.bin'))
    ram, hw = d['eeMemory.bin'], d['eeHwRegs.bin']
    r = lambda a: struct.unpack_from('<I', hw, a - 0x10000000)[0]
    for name, base in (('VIF0', 0x10008000), ('VIF1', 0x10009000), ('GIF', 0x1000A000),
                       ('fromSPR', 0x1000D000), ('toSPR', 0x1000D400)):
        print('%-7s CHCR=%08X MADR=%08X QWC=%04X TADR=%08X ASR0=%08X' % (
            name, r(base), r(base + 0x10), r(base + 0x20), r(base + 0x30), r(base + 0x40)))
    chcr, tadr = r(0x10009000), r(0x10009030) & 0x01FFFFF0
    if chcr & 0x100:
        print('VIF1 was mid-transfer; walking from TADR only')
    # PCSX2 saves at VSync, so VIF1 has normally reached END and TADR names
    # that END tag. The frame's chains are still in RAM below it.
    A = np.frombuffer(ram, dtype='<u8').reshape(-1, 2)
    lo, hi = A[:, 0], A[:, 1]
    ids = ((lo >> 28) & 7).astype(np.int64)
    addr = ((lo >> 32) & 0x01FFFFF0).astype(np.int64)
    qwc = (lo & 0xFFFF).astype(np.int64)
    res = (lo >> 16) & 0x3FF
    valid_vif = {0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x13, 0x14, 0x15, 0x17, 0x20, 0x30, 0x31, 0x4A, 0x50, 0x51} | \
        set(range(0x60, 0x80))

    def tag_ok(i):
        h = int(hi[i])
        return res[i] == 0 and ((h >> 24) & 0x7F) in valid_vif and ((h >> 56) & 0x7F) in valid_vif

    def walk(t):
        stack, rec = [], []
        while len(rec) < 100000:
            if t < 0 or t + 16 > len(ram) or t & 15:
                return None
            i = t // 16
            if not tag_ok(i):
                return None
            rec.append((i, len(stack)))
            k = ids[i]
            if k == 1:
                t += 16 + int(qwc[i]) * 16
            elif k == 2:
                t = int(addr[i])
            elif k in (3, 4):
                t += 16
            elif k == 5:
                if len(stack) >= 2:
                    return None
                stack.append(t + 16 + int(qwc[i]) * 16); t = int(addr[i])
            elif k == 6:
                if not stack:
                    return None
                t = stack.pop()
            else:  # refe / end
                return rec if k == 7 else None
        return None

    span = span or 0x200000
    tags, covered, chains = set(), set(), []
    for t in range(max(0, tadr - span), tadr + 16, 16):
        if t // 16 in covered:
            continue
        rec = walk(t)
        if not rec or len(rec) < 2 or any(i in tags for i, _ in rec):
            continue  # not a chain, or a false start that merges into one already found
        chains.append((t, rec))
        for i, depth in rec:
            tags.add(i); covered.add(i)
            if ids[i] in (1, 5, 7) and depth == 0:
                covered.update(range(i + 1, i + 1 + int(qwc[i])))
    print('\n%d VIF1 chain(s) ending in END within 0x%X bytes below TADR %08X' % (len(chains), span, tadr))
    for t, rec in sorted(chains, key=lambda c: -len(c[1]))[:12]:
        top = [i for i, dp in rec if dp == 0]
        inside = [i for i, dp in rec if dp > 0]
        frame_qw = sum(int(qwc[i]) for i in top if ids[i] in (1, 5, 7))
        static_qw = sum(int(qwc[i]) for i in inside if ids[i] in (1, 6)) + \
            sum(int(qwc[i]) for i, _ in rec if ids[i] in (3, 4))
        c = collections.Counter(IDN[ids[i]] for i, _ in rec)
        calls = [i for i in top if ids[i] == 5]
        print('chain @%08X: %d tags %s | inline in the frame buffer %.1f KB | referenced/called %.1f KB'
              ' | %d calls to %d distinct blocks' % (t, len(rec), dict(c), frame_qw / 64, static_qw / 64,
                                                     len(calls), len({int(addr[i]) for i in calls})))
    if chains:
        t, rec = max(chains, key=lambda c: len(c[1]))
        vc = collections.Counter()
        for i, _ in rec:
            h = int(hi[i])
            vc[vif_name(h & 0xFFFFFFFF)] += 1; vc[vif_name(h >> 32)] += 1
        print('VIF codes carried in the largest chain\'s tags:', dict(vc.most_common(16)))
        print('cnt sizes (qw) in its frame buffer:', collections.Counter(
            int(qwc[i]) for i, dp in rec if dp == 0 and ids[i] == 1).most_common(8))


# The ONE line a harness waits for: the adapter's final verdict, after the
# level gyms. Anchored on purpose - the sim runner also reports an intermediate
# "[GYM] SIMGYM 3/3 ...", and a plain "SUMMARY" substring made the harness
# declare the run green at the halfway point and kill the emulator.
GYM_FINAL_RE = re.compile(r'^LOG: \[GYM\] SUMMARY (\d+)/(\d+) passed \((\d+) failed, (\d+) skipped\)\s*$', re.M)


def hide_windows_hook(pid):
    """Return a callable that hides every VISIBLE top-level window owned by
    `pid` (Windows only; a no-op elsewhere).

    A gym run is unattended, and PCSX2's `-nogui` only hides its *main* window:
    the GS display surface is its own top-level window, so a 42-gym suite throws
    42 game windows over whatever the developer is doing, and the next one pops
    while they are still reading the verdict. There is no CLI flag for it, so
    the windows are hidden at the OS level.

    Called repeatedly rather than once: the display window is created a moment
    after the process starts, and a GS restart can build a new one. Hiding does
    NOT stop the guest rendering - the GS thread keeps rasterising into GS
    memory whatever the host widget does, which is why `--capture-frame` (the
    GAME photographing its own framebuffer) still works with the window hidden.

    No-op if ctypes/user32 is unavailable, so a run never dies over cosmetics.
    """
    try:
        if os.name != 'nt':
            return None
        import ctypes
        from ctypes import wintypes
        user32 = ctypes.WinDLL('user32', use_last_error=True)
        EnumWindows = user32.EnumWindows
        GetWindowThreadProcessId = user32.GetWindowThreadProcessId
        IsWindowVisible = user32.IsWindowVisible
        ShowWindow = user32.ShowWindow
        WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND,
                                         wintypes.LPARAM)
    except Exception:
        return None
    SW_HIDE = 0
    state = {'hidden': 0}

    def hook():
        def cb(hwnd, _):
            wpid = wintypes.DWORD()
            GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == pid and IsWindowVisible(hwnd):
                ShowWindow(hwnd, SW_HIDE)
                state['hidden'] += 1
            return True
        EnumWindows(WNDENUMPROC(cb), 0)

    hook.state = state
    return hook


def cmd_gym(a):
    """Unattended gym run (.plan/14-testing-gym.md in the game project): boots
    the game with a gym.run marker, waits for the [GYM] SUMMARY verdict in the
    game's OWN log (next to the staged ELF - not PCSX2's emulog), snapshots a
    savestate on every FAIL line, and EXITS NON-ZERO when any gym failed."""
    exe = find_pcsx2(a.pcsx2)
    assert_datapath_support(exe)
    # --tier picks WHICH gyms (it becomes the gym.run marker). --profile picks
    # HOW they run. They are orthogonal: a level gym can run Null+unlimited for
    # iteration and Software+limited for the one real-renderer validation pass.
    # An explicit --gs-renderer/--unlimited still overrides the profile.
    tier = getattr(a, 'tier', 'all')
    gs_default, unlimited_default = {
        # Software GS, unthrottled - THE full-speed suite, and a REAL renderer:
        # it rasterizes every primitive, so a frame capture works and a GS bug
        # cannot hide behind a Null device. Measured faster than Null GS here
        # (20.4 s vs 22.1 s for all 42) because Null still pays the GS thread and
        # the present path. Guest time stays cycle-driven, so -unlimited costs
        # nothing in timing fidelity (Counters.cpp), and VsyncEnable=false in the
        # staged ini is what stops the host refresh from pacing it.
        'fast': ('13', True),
        # Software GS, frame-limited: true real-time pacing. For anything that
        # measures wall-clock seconds, and for a capture you want in real time.
        'render': ('13', False),
        # Null GS, unthrottled - GSRendererNull::Draw() is an empty body, so
        # there is no rasterization at all. The fastest way to check logic, and
        # the only profile that CANNOT prove anything about pixels (GetOutput
        # returns nullptr, so no --capture-frame).
        'null': ('11', True),
    }.get(getattr(a, 'profile', 'fast'), ('13', True))
    gs_renderer = gs_default if a.gs_renderer is None else a.gs_renderer
    unlimited = unlimited_default if a.unlimited is None else a.unlimited
    work = pathlib.Path(os.environ.get('TEMP', '/tmp')) / 'tyra-editor-test' / ('pcsx2-capture-%d' % a.slot)
    work.mkdir(parents=True, exist_ok=True)
    root = prepare_datapath(work, find_user_ini(exe), a.slot, gs_renderer)
    elf = stage_elf(a.elf, work, True)  # always staged: isolates the game log
    out = results_dir(a, 'gym')
    out.mkdir(parents=True, exist_ok=True)
    # The marker the game reads at boot, written next to the STAGED elf.
    marker = pathlib.Path(elf).parent / 'gym.run'
    marker.write_text(a.only or ('' if tier == 'all' else tier), encoding='ascii')
    # PCSX2's own log. Left at its default location - <datapath>/PCSX2/logs/
    # emulog.txt at LOGLEVEL_INFO (VMManager.cpp:529) - instead of passed with
    # -logfile, which forces LOGLEVEL_DEBUG onto the file sink
    # (QtHost.cpp:2269 -> VMManager.cpp:473) where Console.cpp:334 fflushes
    # every line. The un-guarded microVU trace lines are then written for every
    # VU stall in the run, synchronously, for a log no gym verdict depends on.
    # Pass --emulog to get the DEBUG-level file back when diagnosing a boot.
    emulog = pathlib.Path(a.emulog) if a.emulog else (root / 'logs' / 'emulog.txt')
    game_log = pathlib.Path(elf).parent / 'log.txt'
    sstates = root / 'sstates'
    sstates.mkdir(exist_ok=True)

    args = [exe, '-batch', '-nogui', '-datapath', str(work)]
    if a.emulog:
        args[3:3] = ['-logfile', str(emulog)]
    args += ['-elf', str(elf)]
    if unlimited:
        # Drops PCSX2's frame limiter (pcsx2/VMManager.cpp Throttle()). The
        # guest's clock IS the EE cycle counter and vsync is cycle-driven
        # (Counters.cpp), so guest time stays a self-consistent 60 Hz timeline -
        # it just elapses faster than wall time. Safe for runs asserting on
        # simulated state; NOT for anything measuring wall-clock seconds.
        args.insert(3, '-unlimited')
    print('launch:', ' '.join('"%s"' % x if ' ' in x else x for x in args))
    proc = subprocess.Popen(args)
    print('PID', proc.pid, '(the only process this script will stop)')
    # Unattended by definition: hide the GS display window (see hide_windows_hook).
    hide = None if a.show_window else hide_windows_hook(proc.pid)
    if hide is not None:
        hide()
    verdict, seen_fail, pine = None, set(), None
    try:
        deadline = time.time() + a.timeout
        buf = ''     # everything the guest has written so far
        pos = [0]    # byte offset of what has already been read
        scanned = 0  # lines of buf already checked for FAIL

        def read_new():
            """Whatever the guest appended since the last poll. Only the tail is
            read: the log reaches ~110 KB written line by line, and re-reading
            and re-regexing all of it on every tick is the harness's own tax."""
            try:
                with open(game_log, 'r', errors='replace') as fh:
                    fh.seek(0, os.SEEK_END)
                    if fh.tell() < pos[0]:      # truncated or recreated
                        pos[0] = 0
                    fh.seek(pos[0])
                    data = fh.read()
                    pos[0] = fh.tell()
                return data
            except OSError:
                return ''

        while True:
            if hide is not None:
                hide()  # cheap; catches the display window on the frame it appears
            buf += read_new()
            if GYM_FINAL_RE.search(buf):
                verdict = buf
                break
            lines = buf.splitlines()
            for line in lines[scanned:]:
                if '[GYM] FAIL' in line and line not in seen_fail:
                    seen_fail.add(line)
                    print('GYM FAIL:', line)
                    if pine is None:
                        pine = wait_for(lambda: _try_pine(a.slot), 10, 1.0)
                    if pine:
                        pine.save_state(1)
                        f = stable_new_file(sstates, '*.01.p2s', time.time() - 5, 30)
                        if f:
                            dst = out / ('gym-fail-%02d.p2s' % len(seen_fail))
                            shutil.copy2(f, dst)
                            print('fail savestate ->', dst.name)
            scanned = len(lines)
            if proc.poll() is not None:
                # The guest writes SUMMARY and calls ExitThread() back to back,
                # so a tick that sees the process gone would otherwise report
                # "no SUMMARY" on a log that already contains one.
                buf += read_new()
                if GYM_FINAL_RE.search(buf):
                    verdict = buf
                break
            if time.time() >= deadline:
                break
            # 20 ms, not the old 1.0 s: the verdict lands within a frame or two
            # of being written, and a one-second poll put up to a second of pure
            # waiting on the end of every single run.
            time.sleep(0.02)
        if verdict is None:
            sys.exit('pcsx2-capture: no [GYM] SUMMARY within %ss - see %s' % (a.timeout, game_log))
        (out / 'report.txt').write_text(verdict, encoding='utf-8')
        print(verdict.strip().splitlines()[-1])
        m = GYM_FINAL_RE.search(verdict)
        if m and int(m.group(3)) > 0:            sys.exit('pcsx2-capture: gym run FAILED (%s)' % m.group(0))
        if not m:
            sys.exit('pcsx2-capture: unparsable SUMMARY line')
        print('GYM: all green')
    finally:
        proc.kill(); proc.wait(10)
        marker.unlink(missing_ok=True)
        if hide is not None and hide.state['hidden']:
            print('hid %d PCSX2 window(s) - unattended run (--show-window to watch)'
                  % hide.state['hidden'])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run', help='launch a private PCSX2, capture, analyse')
    r.add_argument('elf')
    r.add_argument('--states', type=int, default=3, help='savestates to take (default 3)')
    r.add_argument('--interval', type=float, default=1.0, help='seconds between savestates')
    r.add_argument('--gsdump', action='store_true', help='also take one single-frame GS dump (Windows)')
    r.add_argument('--boot-wait', type=float, default=40, help='seconds to wait after PINE answers (default 40)')
    r.add_argument('--wait-log', help='regex to wait for in emulog.txt instead of a fixed boot wait')
    r.add_argument('--slot', type=int, default=28190, help='PINE slot/port; one per parallel run')
    r.add_argument('--pcsx2', help='path to pcsx2-qt')
    r.add_argument('--out', help='output directory (default under the run directory)')
    r.add_argument('--keep', action='store_true', help='leave the emulator running')
    r.add_argument('--stage', action='store_true', help='always copy the ELF directory to a private short path')
    y = sub.add_parser('gym', help='unattended gym run; exits non-zero on failures')
    y.add_argument('elf')
    y.add_argument('--only', default='', help='comma-separated gym names to run (default all)')
    y.add_argument('--timeout', type=int, default=300, help='seconds to wait for [GYM] SUMMARY (default 300)')
    y.add_argument('--slot', type=int, default=28190, help='PINE slot/port; one per parallel run')
    y.add_argument('--pcsx2', help='path to pcsx2-qt')
    y.add_argument('--out', help='output directory (default under the run directory)')
    y.add_argument('--emulog', help='pass -logfile PATH to force PCSX2 to write its own log '
                                   'there at DEBUG level; by default it is left at '
                                   '<datapath>/PCSX2/logs/emulog.txt at INFO level, which is '
                                   'much cheaper (every DEBUG line is fflushed)')
    y.add_argument('--tier', choices=['all', 'sim', 'level'], default='all',
                   help="which gyms run: written into gym.run as the literal marker")
    y.add_argument('--profile', choices=['fast', 'render', 'null'], default='fast',
                   help="emulator profile: fast = Software GS + no frame limiter (the default - "
                        "a real renderer at full speed); render = Software GS, real-time paced; "
                        "null = Null GS, no rasterization (fastest logic-only, no pixels)")
    y.add_argument('--gs-renderer', default=None,
                   help='GS renderer id: 13 Software (default), 11 Null - no rasterization, for headless logic runs')
    y.add_argument('--unlimited', default=None, nargs='?', const=True,
                   help='drop the emulator frame limiter; guest time stays 60Hz-consistent but elapses faster than wall time')
    y.add_argument('--show-window', action='store_true',
                   help="leave the emulator's GS display window VISIBLE. Default: hide it. "
                        "-nogui only hides PCSX2's main window; the display surface is its own "
                        "top-level window, so an unattended suite would throw one game window "
                        "per gym over the developer's screen. Hiding does not stop rendering "
                        "or --capture-frame.")
    g = sub.add_parser('gs', help='summarise a GS dump')
    g.add_argument('dump')
    g.add_argument('--passes', action='store_true', help='print the pass timeline of one vsync')
    g.add_argument('--frame', type=int, default=1, help='vsync index for --passes')
    s = sub.add_parser('state', help='DMAC state + VIF1 chains from a savestate')
    s.add_argument('p2s')
    s.add_argument('--span', type=lambda x: int(x, 0), help='bytes below TADR to search (default 0x200000)')
    a = ap.parse_args()
    if a.cmd == 'run':
        cmd_run(a)
    elif a.cmd == 'gym':
        cmd_gym(a)
    elif a.cmd == 'gs':
        analyse_gs(a.dump, a.passes, a.frame)
    else:
        analyse_state(a.p2s, a.span)


if __name__ == '__main__':
    main()
