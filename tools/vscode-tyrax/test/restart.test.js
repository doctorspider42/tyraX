// Offline check of restarting (debug.js rebuildAndRestart / finishRestart):
//   node tools/vscode-tyrax/test/restart.test.js
// 1. Rebuild and Restart stops the session, waits until it is gone, starts again.
// 2. VS Code's own Restart of a RUNNING game: the old session ends on its own
//    while the new configuration resolves ("has exited with code 0") and VS
//    Code cancels the resolution; the extension starts the session.
// 3. A game parked on a breakpoint: no cancel, VS Code restarts by itself -
//    the extension must not start a second session.
"use strict";
const assert = require("assert");
const fs = require("fs"), os = require("os"), path = require("path");
const Module = require("module");

const project = fs.mkdtempSync(path.join(os.tmpdir(), "tyrax-restart-"));
for (const d of [".vscode", "bin", path.join("src", "scripts")])
  fs.mkdirSync(path.join(project, d), { recursive: true });
const elf = path.join(project, "bin", "g.elf.sym");
const script = path.join(project, "src", "scripts", "a.cpp");
fs.writeFileSync(script, "int a;");
const past = new Date(Date.now() - 60000);
fs.utimesSync(script, past, past);
fs.writeFileSync(elf, "elf");
fs.writeFileSync(path.join(project, ".vscode", "tyrax.json"), JSON.stringify({
  version: 2, scriptDebugger: true, profile: "debug", editor: "x", project, elf,
  gdb: "gdb", wsl: false, port: 23500, sourceMapFrom: "",
}));
// The editor serves the game and OUR session is attached to it.
fs.writeFileSync(path.join(project, "bin", "gdbserver.json"), JSON.stringify({
  state: "attached, running", port: 23500, pid: process.pid, owner: "editor",
}));

const handlers = {};
const commands = {};
const calls = [];
const folder = { uri: { fsPath: project }, name: "game" };
const stub = {
  window: {
    createOutputChannel: () => ({ appendLine() {}, show() {} }),
    createStatusBarItem: () => ({ show() {}, hide() {} }),
    showWarningMessage: () => Promise.resolve(undefined),
    showErrorMessage: async (m) => { throw new Error("unexpected error: " + m); },
    withProgress: (opts, fn) => fn({ report() {} }, { isCancellationRequested: false, onCancellationRequested() {} }),
  },
  ProgressLocation: { Notification: 15 },
  debug: {
    registerDebugConfigurationProvider: () => ({ dispose() {} }),
    onDidStartDebugSession: (f) => (handlers.start = f, { dispose() {} }),
    onDidTerminateDebugSession: (f) => (handlers.end = f, { dispose() {} }),
    stopDebugging: async (s) => {
      calls.push("stop");
      setTimeout(() => handlers.end(s), 50);
    },
    startDebugging: async (f, c) => {
      calls.push("start " + c.type);
      return true;
    },
  },
  commands: {
    registerCommand: (id, f) => ((commands[id] = f), { dispose() {} }),
    executeCommand: async () => {},
  },
  extensions: { getExtension: () => ({}) },
  workspace: {
    workspaceFolders: [folder],
    saveAll: async () => true,
    onDidSaveTextDocument: () => ({ dispose() {} }),
    getConfiguration: () => ({ inspect: () => ({}), update: async () => {} }),
  },
  StatusBarAlignment: { Left: 1 },
  ThemeColor: function () {},
  ConfigurationTarget: { WorkspaceFolder: 3 },
  DebugConfigurationProviderTriggerKind: { Dynamic: 2 },
};
const real = Module._resolveFilename;
Module._resolveFilename = function (q, ...a) { return q === "vscode" ? q : real.call(this, q, ...a); };
require.cache.vscode = { id: "vscode", filename: "vscode", loaded: true, exports: stub };
const { activate, Provider } = require("../debug");
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const session = (id) => ({ id, workspaceFolder: folder, configuration: { tyrax: { project, elf } } });

(async () => {
  activate({ subscriptions: [] });

  // 1. The notification / status bar button.
  const s1 = session("s1");
  handlers.start(s1);
  await commands["tyrax.rebuildAndRestart"]();
  assert.deepStrictEqual(calls, ["stop", "start tyrax"], "stop, wait for the end, then start");

  // 2. The green Restart of a running game; the code changed, so the editor
  //    is asked to rebuild, and the old session ends while that runs.
  calls.length = 0;
  const s2 = session("s2");
  handlers.start(s2);
  fs.writeFileSync(script, "int changed;");  // newer than the build
  const cancelled = { isCancellationRequested: false };
  const pending = new Provider().resolveDebugConfiguration(folder, {
    type: "tyrax", request: "launch", name: "TyraX: debug game in PCSX2",
  }, cancelled);
  const req = path.join(project, "bin", "gdbserver.request");
  for (let i = 0; i < 100 && !fs.existsSync(req); ++i) await wait(20);
  assert.ok(fs.existsSync(req), "the editor is asked for a rebuild");
  const id = JSON.parse(fs.readFileSync(req, "utf8")).id;
  handlers.end(s2);  // the rebuild closed the game: "has exited with code 0"
  cancelled.isCancellationRequested = true;  // ...and VS Code drops the restart
  fs.writeFileSync(elf, "new elf");  // the editor rebuilt and serves again
  fs.writeFileSync(path.join(project, "bin", "gdbserver.json"), JSON.stringify({
    state: "waiting for GDB", port: 23500, pid: process.pid, owner: "editor", request: id,
  }));
  const cfg = await pending;
  assert.ok(cfg && cfg.type === "cppdbg", "the restart resolves");
  await wait(800);
  assert.deepStrictEqual(calls, ["start tyrax"], "VS Code dropped it: the extension starts it");
  fs.writeFileSync(path.join(project, "bin", "gdbserver.json"), JSON.stringify({
    state: "attached, running", port: 23500, pid: process.pid, owner: "editor",
  }));

  // 3. Same, but VS Code does start the new session itself.
  calls.length = 0;
  const s3 = session("s3");
  handlers.start(s3);
  await new Provider().resolveDebugConfiguration(folder, {
    type: "tyrax", request: "launch", name: "TyraX: debug game in PCSX2",
  }, { isCancellationRequested: false });
  handlers.end(s3);
  await wait(800);
  handlers.start(session("s4"));  // VS Code's own relaunch, however late
  assert.deepStrictEqual(calls, [], "no second session");

  fs.rmSync(project, { recursive: true, force: true });
  console.log("restart.test.js: all checks passed");
  process.exit(0);
})();
