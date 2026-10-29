// Offline check of the stale-code warning (debug.js watchSessions/checkStale):
//   node tools/vscode-tyrax/test/stale.test.js
// While a TyraX session runs, saving a code file newer than the game's build
// shows a status bar warning and one notification whose button restarts.
"use strict";
const assert = require("assert");
const fs = require("fs"), os = require("os"), path = require("path");
const Module = require("module");

const project = fs.mkdtempSync(path.join(os.tmpdir(), "tyrax-stale-watch-"));
fs.mkdirSync(path.join(project, "src", "scripts"), { recursive: true });
fs.mkdirSync(path.join(project, "bin"));
const elf = path.join(project, "bin", "g.elf.sym");
const script = path.join(project, "src", "scripts", "a.cpp");
fs.writeFileSync(script, "int a;");
const past = new Date(Date.now() - 60000);
fs.utimesSync(script, past, past);
fs.writeFileSync(elf, "elf");

const handlers = {};
const events = [];
const item = { show: () => events.push("show"), hide: () => events.push("hide") };
const stub = {
  window: {
    createOutputChannel: () => ({ appendLine() {}, show() {} }),
    createStatusBarItem: () => item,
    showWarningMessage: (m, b) => { events.push("warn"); return Promise.resolve(b); },
  },
  debug: {
    registerDebugConfigurationProvider: () => ({ dispose() {} }),
    onDidStartDebugSession: (f) => (handlers.start = f, { dispose() {} }),
    onDidTerminateDebugSession: (f) => (handlers.end = f, { dispose() {} }),
  },
  commands: {
    registerCommand: () => ({ dispose() {} }),
    executeCommand: async (c) => events.push("exec " + c),
  },
  workspace: {
    workspaceFolders: [],
    onDidSaveTextDocument: (f) => (handlers.save = f, { dispose() {} }),
    getConfiguration: () => ({ inspect: () => ({}), update: async () => {} }),
  },
  StatusBarAlignment: { Left: 1 },
  ThemeColor: function (id) { this.id = id; },
  ConfigurationTarget: { WorkspaceFolder: 3 },
  DebugConfigurationProviderTriggerKind: { Dynamic: 2 },
};
const real = Module._resolveFilename;
Module._resolveFilename = function (q, ...a) { return q === "vscode" ? q : real.call(this, q, ...a); };
require.cache.vscode = { id: "vscode", filename: "vscode", loaded: true, exports: stub };
const { activate, ownSessions } = require("../debug");

(async () => {
  activate({ subscriptions: [] });
  const session = { id: "s1", configuration: { tyrax: { project, elf } } };
  handlers.start(session);
  assert.ok(ownSessions.has(path.normalize(project)), "the session is tracked");
  assert.ok(!events.includes("show"), "fresh build: no warning");

  fs.writeFileSync(script, "int b;");  // an edit after the build
  handlers.save({ fileName: script });
  await new Promise((r) => setTimeout(r, 20));
  assert.ok(events.includes("show"), "status bar warning");
  assert.strictEqual(events.filter((e) => e === "warn").length, 1, "one notification");
  assert.ok(events.includes("exec tyrax.rebuildAndRestart"), "its button rebuilds and restarts");

  handlers.save({ fileName: script });
  await new Promise((r) => setTimeout(r, 20));
  assert.strictEqual(events.filter((e) => e === "warn").length, 1, "not again in the same session");

  handlers.end(session);
  assert.ok(!ownSessions.size, "untracked when it ends");
  assert.strictEqual(events[events.length - 1], "hide", "warning gone");
  fs.rmSync(project, { recursive: true, force: true });
  console.log("stale.test.js: all checks passed");
})();
