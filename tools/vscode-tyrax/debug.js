// The `tyrax` debug type: F5 on a TyraX project stops the game in PCSX2 on a
// line of a C++ script (docs/script-debugger.md).
//
// There is no debug adapter here. The editor already serves GDB's remote
// protocol for the running game (its BREAK handler + PINE), and the C/C++
// extension already drives GDB, so a `tyrax` configuration is RESOLVED into an
// ordinary `cppdbg` one: the GDB the toolchain ships, the port the editor
// serves, and - on Windows, where GDB runs inside WSL - the WSL pipe and the
// /mnt/<drive> path map. Everything machine-specific comes from
// .vscode/tyrax.json, which every build writes.
//
// F5 is the whole workflow (`decide`). It saves the open files first; with the
// editor serving a game built from the current code it attaches; with the
// editor open but no game, the debugger off or the code changed since the
// build, it asks the editor through bin/gdbserver.request (which rebuilds and
// relaunches) and waits for bin/gdbserver.json to say "waiting for GDB"; with
// no editor it runs `tyrax-editor --debug-run --enable` itself, asking first
// when that turns the debugger on.
"use strict";

const vscode = require("vscode");
const cp = require("child_process");
const fs = require("fs");
const path = require("path");

const TYPE = "tyrax";
const CPPTOOLS = "ms-vscode.cpptools";
const DOCS = "https://github.com/doctorspider42/tyraX/blob/main/docs/script-debugger.md";

let output = null;
function log(line) {
  if (!output) output = vscode.window.createOutputChannel("TyraX");
  output.appendLine(line);
}

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch (e) {
    return null;
  }
}

function pidAlive(pid) {
  if (!pid) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (e) {
    return e.code === "EPERM";
  }
}

/** bin/gdbserver.json when an editor is serving this project right now. */
function liveServer(projectDir) {
  const s = readJson(path.join(projectDir, "bin", "gdbserver.json"));
  if (!s || !pidAlive(s.pid)) return null;
  return s;
}

/** "C:\a\b" -> "/mnt/c/a/b" (WSL's default automount, as the editor writes it). */
function wslPath(win) {
  const m = /^([A-Za-z]):[\\/]?(.*)$/.exec(win);
  if (!m) return win.replace(/\\/g, "/");
  return "/mnt/" + m[1].toLowerCase() + "/" + m[2].replace(/\\/g, "/");
}

/**
 * The cppdbg configuration a `tyrax` one becomes. Pure (no VS Code calls), so
 * the offline harness can check it: `cfg` is .vscode/tyrax.json, `base` the
 * user's launch configuration, `platform` process.platform.
 */
function toCppdbg(cfg, base, platform) {
  const program = cfg.wsl ? wslPath(cfg.elf) : cfg.elf;
  const out = {
    type: "cppdbg",
    request: "launch",
    name: base.name || "TyraX: debug game in PCSX2",
    // Ours, not cppdbg's (it ignores unknown keys): which TyraX project and
    // ELF a running session debugs - the stale-code watch and Restart use it.
    tyrax: { project: cfg.project, elf: cfg.elf },
    MIMode: "gdb",
    miDebuggerPath: cfg.gdb,
    program,
    cwd: cfg.wsl ? cfg.sourceMapFrom || wslPath(cfg.project) : cfg.project,
    // The connection is spelled out instead of miDebuggerServerAddress: with
    // pipeTransport (GDB in WSL) the C/C++ extension ignores that key and
    // sends -exec-run, which a remote target refuses ("Don't know how to
    // run"). Same commands everywhere, so every platform takes one path.
    customLaunchSetupCommands: [
      { text: "-file-exec-and-symbols " + JSON.stringify(program) },
      { text: "-target-select remote 127.0.0.1:" + cfg.port },
    ],
    launchCompleteCommand: "exec-continue",
    // Custom launch commands skip the step where the extension asks GDB for
    // the architecture, and without it launch fails ("Parameter 'arch'").
    targetArchitecture: "mips",
    externalConsole: false,
    // Async MI is what lets Pause (-exec-interrupt) reach a running target.
    setupCommands: [
      { description: "Async MI, so Pause works", text: "-gdb-set mi-async on", ignoreFailures: true },
      { description: "Pretty printing", text: "-enable-pretty-printing", ignoreFailures: true },
    ],
  };
  if (cfg.wsl && platform === "win32") {
    out.pipeTransport = {
      pipeCwd: "",
      pipeProgram: "wsl.exe",
      pipeArgs: ["--exec", "bash", "-c"],
      debuggerPath: cfg.gdb,
    };
    // The game's DWARF names /mnt/<drive>/... (the native build compiles from
    // there); the editor wants C:\... - both ways, breakpoints included.
    const map = {};
    const add = (from, to) => {
      map[from] = { editorPath: to, useForBreakpoints: true };
    };
    if (cfg.sourceMapFrom) add(cfg.sourceMapFrom, cfg.project);
    const drives = new Set();
    for (const p of [cfg.project, cfg.editor]) {
      const m = /^([A-Za-z]):/.exec(p || "");
      if (m) drives.add(m[1].toLowerCase());
    }
    for (const d of drives) add("/mnt/" + d + "/", d.toUpperCase() + ":\\");
    out.sourceFileMap = map;
  }
  for (const k of ["setupCommands", "sourceFileMap", "logging", "postRemoteConnectCommands"])
    if (base[k] !== undefined) out[k] = base[k];
  return out;
}

/** Starts `tyrax-editor --debug-run --enable` and waits for "[gdb] ready". */
function startServer(cfg, base, token, progress) {
  return new Promise((resolve, reject) => {
    const args = ["--debug-run", cfg.project, "--enable"];
    if (base.build === false) args.push("--no-build");
    log("> " + cfg.editor + " " + args.join(" "));
    let child;
    try {
      child = cp.spawn(cfg.editor, args, { cwd: cfg.project, windowsHide: true });
    } catch (e) {
      reject(new Error("could not start the TyraX editor: " + e.message));
      return;
    }
    let done = false;
    const finish = (err) => {
      if (done) return;
      done = true;
      if (err) reject(err);
      else resolve();
    };
    let tail = "";
    const onData = (buf) => {
      const text = buf.toString();
      text.split(/\r?\n/).forEach((l) => {
        if (!l) return;
        log(l);
        if (/^\[(editor|gdb)\]/.test(l)) progress.report({ message: l.replace(/^\d\d:\d\d:\d\d /, "").slice(0, 90) });
      });
      tail = (tail + text).slice(-4096);
      if (/\[gdb\] ready on /.test(tail)) finish();
    };
    child.stdout.on("data", onData);
    child.stderr.on("data", onData);
    child.on("error", (e) => finish(new Error("could not start the TyraX editor: " + e.message)));
    child.on("exit", (code) =>
      finish(new Error("the TyraX editor stopped (exit " + code + ") before the game was ready - see Output > TyraX"))
    );
    token.onCancellationRequested(() => {
      child.kill();
      finish(new Error("cancelled"));
    });
  });
}

const READY = "waiting for GDB";

// What a build compiles that a person edits: scripts, extensions, custom node
// and effect code, headers. Scene data is the editor's (Live Link carries it).
const SOURCE_DIRS = ["src", "inc", "flow-nodes", "screen-effects"];
const SOURCE_EXT = /\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inc|flownode|screenfx)$/i;

/** The project's most recently modified code file: { time (ms), file }. */
function newestSource(projectDir) {
  let newest = 0, newestFile = "";
  const walk = (dir, depth) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch (e) {
      return;
    }
    for (const e of entries) {
      const full = path.join(dir, e.name);
      if (e.isDirectory()) {
        if (depth < 12) walk(full, depth + 1);
      } else if (SOURCE_EXT.test(e.name)) {
        try {
          const t = fs.statSync(full).mtimeMs;
          if (t > newest) {
            newest = t;
            newestFile = full;
          }
        } catch (err) {
          // vanished mid-walk
        }
      }
    }
  };
  for (const d of SOURCE_DIRS) walk(path.join(projectDir, d), 0);
  return { time: newest, file: newestFile };
}

/** True when the code changed after the game being served was built. */
function codeChanged(cfg) {
  let built = 0;
  try {
    built = fs.statSync(cfg.elf).mtimeMs;
  } catch (e) {
    return true;  // never built
  }
  const n = newestSource(cfg.project);
  if (n.time <= built) return false;
  log("changed since the last build: " + path.relative(cfg.project, n.file));
  return true;
}

/**
 * What F5 does, from .vscode/tyrax.json (`cfg`), bin/gdbserver.json
 * (`session`, null when no live process owns the project) and whether the code
 * changed since the served build (`stale`). Pure, so the offline harness can
 * walk every case:
 *   attach   - a game with the current code is served and nobody is attached
 *   request  - an editor owns the project: ask it (it turns the debugger on,
 *              builds and runs; `rebuild` = even if it is serving a game)
 *   respawn  - our own headless editor serves an old build: replace it
 *   wait     - a headless editor is still getting its game ready
 *   spawn    - no editor: run `tyrax-editor --debug-run --enable` ourselves;
 *              `ask` = the debugger is off and turning it on needs a yes
 *   error    - `message` says why
 * `ours`: one of this window's own debug sessions is attached to the project -
 * the Restart button, which resolves the new configuration BEFORE it drops the
 * old one. That attach is not "another debugger".
 */
function decide(cfg, session, stale, ours) {
  if (!cfg) {
    return {
      action: "error",
      message: "this folder has no .vscode/tyrax.json yet - open the project in TyraX and build it once.",
    };
  }
  if (cfg.profile === "release" || (session && session.state === "release")) {
    return {
      action: "error",
      message: "this project builds with the release profile, which carries no debugger - switch it to debug in TyraX (the profile pill left of PCSX2 | PS2).",
    };
  }
  if (session) {
    const editor = session.owner === "editor";
    if (/^attached/.test(session.state || "")) {
      if (!ours) return { action: "error", message: "another debugger is already attached to this game." };
      // Restart: unchanged code reattaches once VS Code drops the old session;
      // changed code rebuilds (which ends the old session with its game).
      if (!stale) return { action: "attach" };
      return editor ? { action: "request", rebuild: true } : { action: "respawn" };
    }
    if (session.state === READY && !stale) return { action: "attach" };
    if (editor) return { action: "request", rebuild: !!stale };
    return stale ? { action: "respawn" } : { action: "wait" };
  }
  return { action: "spawn", ask: cfg.scriptDebugger === false };
}

/** Waits until bin/gdbserver.json says READY (and, with `id`, that the editor
 * handled that request - so a rebuild never attaches to the old game). */
function waitReady(cfg, token, progress, id) {
  return new Promise((resolve, reject) => {
    const started = Date.now();
    let last = "";
    const timer = setInterval(() => {
      if (token.isCancellationRequested) {
        clearInterval(timer);
        reject(new Error("cancelled"));
        return;
      }
      const s = liveServer(cfg.project);
      if (!s) {
        // The Runner deletes the file for a moment on every launch; only a
        // long silence is final.
        if (Date.now() - started > 30000) {
          clearInterval(timer);
          reject(new Error("the TyraX editor stopped answering"));
        }
        return;
      }
      const line = s.state + (s.message && s.message !== s.state ? ": " + s.message : "");
      if (line !== last) {
        last = line;
        log("editor: " + line);
        progress.report({ message: line.slice(0, 90) });
      }
      const handled = !id || s.request === id;
      if (handled && s.state === READY) {
        clearInterval(timer);
        resolve();
      } else if (handled && (s.state === "failed" || s.state === "release")) {
        clearInterval(timer);
        reject(new Error(s.message || s.state));
      } else if (Date.now() - started > 30 * 60 * 1000) {
        clearInterval(timer);
        reject(new Error("timed out waiting for the game"));
      }
    }, 300);
  });
}

/** Asks the editor that owns the project for a debug run and waits until it serves. */
function requestFromEditor(cfg, token, progress, rebuild) {
  const id = String(Date.now());
  try {
    fs.mkdirSync(path.join(cfg.project, "bin"), { recursive: true });
    fs.writeFileSync(
      path.join(cfg.project, "bin", "gdbserver.request"),
      JSON.stringify({ id, rebuild: !!rebuild }) + "\n"
    );
  } catch (e) {
    return Promise.reject(new Error("could not reach the TyraX editor: " + e.message));
  }
  log("asked the TyraX editor for a debug run" + (rebuild ? " (the code changed: rebuild)" : ""));
  return waitReady(cfg, token, progress, id);
}

/** Stops a headless editor serving an old build, then starts a fresh one. */
async function respawn(cfg, base, token, progress, session) {
  log("the code changed since the running build: restarting the headless editor");
  try {
    process.kill(session.pid);
  } catch (e) {
    // already gone
  }
  for (let i = 0; i < 50 && pidAlive(session.pid); ++i) await new Promise((r) => setTimeout(r, 200));
  return startServer(cfg, base, token, progress);
}

class Provider {
  provideDebugConfigurations() {
    return [{ type: TYPE, request: "launch", name: "TyraX: debug game in PCSX2" }];
  }

  async resolveDebugConfiguration(folder, base, token) {
    if (!base.type && !base.request && !base.name) {
      base = { type: TYPE, request: "launch", name: "TyraX: debug game in PCSX2" };
    }
    const root = folder ? folder.uri.fsPath : (vscode.workspace.workspaceFolders || [])[0]?.uri.fsPath;
    if (!root) {
      vscode.window.showErrorMessage("TyraX: open a TyraX project folder to debug its game.");
      return undefined;
    }
    if (!vscode.extensions.getExtension(CPPTOOLS)) {
      const pick = await vscode.window.showErrorMessage(
        "TyraX debugging runs GDB through Microsoft's C/C++ extension, which is not installed.",
        "Install C/C++"
      );
      if (pick) await vscode.commands.executeCommand("workbench.extensions.installExtension", CPPTOOLS);
      return undefined;
    }
    // What is in the editor tabs is what F5 means: save it before deciding
    // whether the game needs a rebuild.
    if (vscode.workspace.saveAll) await vscode.workspace.saveAll(false);
    const cfg = readJson(path.join(root, ".vscode", "tyrax.json"));
    const session = cfg ? liveServer(cfg.project) : null;
    const key = cfg ? path.normalize(cfg.project) : "";
    const ours = !!cfg && ownSessions.has(key);
    const plan = decide(cfg, session, cfg ? codeChanged(cfg) : false, ours);
    // VS Code's Restart resolves while its old session still runs. When the
    // rebuild closes a RUNNING game, the old session ends on its own ("has
    // exited with code 0") and VS Code cancels this resolution instead of
    // starting the new session - finishRestart() starts it then. (A game
    // parked on a breakpoint does not end the session, and VS Code restarts
    // normally; starting one too would make two.)
    const restart = ours && session && /^attached/.test(session.state || "") ? { key } : null;
    if (restart) restartsInFlight.set(key, restart);
    if (plan.action === "error") {
      const pick = await vscode.window.showErrorMessage("TyraX: " + plan.message, "Open docs");
      if (pick) vscode.env.openExternal(vscode.Uri.parse(DOCS));
      return undefined;
    }
    if (plan.action === "spawn" && plan.ask) {
      const pick = await vscode.window.showInformationMessage(
        "Turn on the TyraX script debugger for this project? Your scripts will compile unoptimised " +
          "(-O0) so breakpoints and variables match the source, and the game rebuilds once.",
        { modal: true },
        "Turn on and debug"
      );
      if (!pick) return undefined;
    }
    if (plan.action !== "attach") {
      const titles = {
        request: plan.rebuild
          ? "TyraX: the code changed - rebuilding the game"
          : "TyraX: the editor is getting the game ready",
        respawn: "TyraX: the code changed - rebuilding the game",
        wait: "TyraX: waiting for the game",
        spawn: "TyraX: building and starting the game in PCSX2",
      };
      try {
        await vscode.window.withProgress(
          { location: vscode.ProgressLocation.Notification, title: titles[plan.action], cancellable: true },
          (progress, token) => {
            if (plan.action === "request") return requestFromEditor(cfg, token, progress, plan.rebuild);
            if (plan.action === "respawn") return respawn(cfg, base, token, progress, session);
            if (plan.action === "wait") return waitReady(cfg, token, progress);
            return startServer(cfg, base, token, progress);
          }
        );
      } catch (e) {
        if (restart) restartsInFlight.delete(key);
        if (e.message !== "cancelled") {
          output && output.show(true);
          vscode.window.showErrorMessage("TyraX: " + e.message);
        }
        return undefined;
      }
    }
    if (restart) finishRestart(restart, folder || (vscode.workspace.workspaceFolders || [])[0], token);
    return toCppdbg(cfg, base, process.platform);
  }
}

// This window's running TyraX sessions: normalized project dir -> session.
const ownSessions = new Map();
// Restarts being resolved: project -> { ended: the old session already ended }.
const restartsInFlight = new Map();

function startGame(folder) {
  return vscode.debug.startDebugging(folder, {
    type: TYPE,
    request: "launch",
    name: "TyraX: debug game in PCSX2",
  });
}

/** After a restart's configuration resolved: VS Code cancelled it (the old
 * session ended on its own while the game was rebuilt), so it will not start
 * the new session - start it here. Measured in a real VS Code: the running
 * case cancels; the parked-on-a-breakpoint case does not, and restarts by
 * itself. */
function finishRestart(restart, folder, token) {
  restartsInFlight.delete(restart.key);
  if (!token || !token.isCancellationRequested) return;
  setTimeout(() => {
    if (ownSessions.has(restart.key)) return;
    log("VS Code dropped the restart (the old session ended during the rebuild): attaching");
    startGame(folder);
  }, 500);
}

/** Rebuild and Restart, in an order VS Code cannot drop: stop the session,
 * wait until it is gone, start again - F5's own resolution then rebuilds the
 * changed code and attaches to the new game. */
async function rebuildAndRestart() {
  const session = [...ownSessions.values()][0] || vscode.debug.activeDebugSession;
  const folder = (session && session.workspaceFolder) || (vscode.workspace.workspaceFolders || [])[0];
  if (session) {
    const t = tyraxOf(session);
    const key = t ? path.normalize(t.project) : "";
    await vscode.debug.stopDebugging(session);
    for (let i = 0; i < 100 && key && ownSessions.get(key) === session; ++i)
      await new Promise((r) => setTimeout(r, 100));
  }
  return startGame(folder);
}
let staleItem = null;
const staleNotified = new Set();

function tyraxOf(session) {
  const t = session && session.configuration && session.configuration.tyrax;
  return t && t.project ? t : null;
}

/** Says so - in the status bar, and once per session in a notification - when
 * the code changed after the build the running game was made from: the game
 * cannot pick the change up until it is rebuilt, and breakpoints in edited
 * lines may land on the wrong line. Restart rebuilds. */
function checkStale() {
  let stale = null;
  for (const [, session] of ownSessions) {
    const t = tyraxOf(session);
    if (t && codeChanged({ project: t.project, elf: t.elf })) stale = session;
  }
  if (!staleItem) {
    staleItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    staleItem.command = "tyrax.rebuildAndRestart";
  }
  if (!stale) {
    staleItem.hide();
    return;
  }
  staleItem.text = "$(warning) TyraX: code changed - Restart to rebuild";
  staleItem.tooltip =
    "The game in PCSX2 runs code from before your last change. Restart (Ctrl+Shift+F5) " +
    "rebuilds it and starts the game again.";
  staleItem.backgroundColor = new vscode.ThemeColor("statusBarItem.warningBackground");
  staleItem.show();
  if (!staleNotified.has(stale.id)) {
    staleNotified.add(stale.id);
    vscode.window
      .showWarningMessage(
        "TyraX: the game runs code from before your last change. Breakpoints in edited lines may " +
          "not match. Restart to rebuild and start it again.",
        "Rebuild and Restart"
      )
      .then((pick) => {
        if (pick) vscode.commands.executeCommand("tyrax.rebuildAndRestart");
      });
  }
}

function watchSessions(context) {
  if (!vscode.debug.onDidStartDebugSession) return;
  context.subscriptions.push(
    vscode.debug.onDidStartDebugSession((s) => {
      const t = tyraxOf(s);
      if (!t) return;
      ownSessions.set(path.normalize(t.project), s);
      checkStale();
    }),
    vscode.debug.onDidTerminateDebugSession((s) => {
      const t = tyraxOf(s);
      if (!t) return;
      const key = path.normalize(t.project);
      if (ownSessions.get(key) === s) ownSessions.delete(key);
      staleNotified.delete(s.id);
      checkStale();
    }),
    vscode.workspace.onDidSaveTextDocument((doc) => {
      if (ownSessions.size && SOURCE_EXT.test(doc.fileName)) checkStale();
    })
  );
}

function activate(context) {
  watchSessions(context);
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider(TYPE, new Provider()),
    vscode.debug.registerDebugConfigurationProvider(
      TYPE,
      { provideDebugConfigurations: () => new Provider().provideDebugConfigurations() },
      vscode.DebugConfigurationProviderTriggerKind.Dynamic
    ),
    vscode.commands.registerCommand("tyrax.rebuildAndRestart", rebuildAndRestart),
    vscode.commands.registerCommand("tyrax.debugGame", (uri) => {
      // From the editor title's run button `uri` is the open file: debug the
      // project it belongs to.
      const folder =
        (uri && vscode.workspace.getWorkspaceFolder && vscode.workspace.getWorkspaceFolder(uri)) ||
        (vscode.workspace.workspaceFolders || [])[0];
      return startGame(folder);
    })
  );
  claimRunButton();
}

/**
 * The run/debug button in the editor title bar. In a TyraX project it should
 * debug the GAME: `tyrax.project` shows our entry there (package.json,
 * `editor/title/run`), and the C/C++ extension's own one - "build and debug
 * the active file" with g++ or cl.exe, which can never run PS2 code - is
 * switched off for these folders through its `C_Cpp.debugShortcut` setting,
 * unless the user set that themselves.
 */
async function claimRunButton() {
  const folders = (vscode.workspace.workspaceFolders || []).filter((f) =>
    fs.existsSync(path.join(f.uri.fsPath, ".vscode", "tyrax.json"))
  );
  await vscode.commands.executeCommand("setContext", "tyrax.project", folders.length > 0);
  for (const f of folders) {
    const conf = vscode.workspace.getConfiguration("C_Cpp", f.uri);
    const seen = conf.inspect("debugShortcut");
    if (seen && seen.workspaceFolderValue === undefined && seen.workspaceValue === undefined) {
      try {
        await conf.update("debugShortcut", false, vscode.ConfigurationTarget.WorkspaceFolder);
        log("hid the C/C++ extension's build-and-debug-file button in " + f.name +
            " (.vscode/settings.json, C_Cpp.debugShortcut)");
      } catch (e) {
        // C/C++ not installed: its button is not there either
      }
    }
  }
}

module.exports = { activate, toCppdbg, wslPath, decide, codeChanged, Provider, ownSessions };
