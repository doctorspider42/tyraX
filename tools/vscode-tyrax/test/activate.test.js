// Offline check of activation (debug.js activate / claimRunButton), no VS Code:
//   node tools/vscode-tyrax/test/activate.test.js
// In a TyraX folder the run button in the editor title is ours and the C/C++
// extension's build-and-debug-file one is hidden - unless the user set it.
"use strict";
const fs = require("fs"), os = require("os"), path = require("path");
const project = fs.mkdtempSync(path.join(os.tmpdir(), "tyrax-activate-"));
fs.mkdirSync(path.join(project, ".vscode"));
fs.writeFileSync(path.join(project, ".vscode", "tyrax.json"), "{}");
const Module = require("module");
const assert = require("assert");
const calls = [];
const mkConf = (preset) => ({
  inspect: () => ({ workspaceFolderValue: preset, workspaceValue: undefined }),
  update: async (k, v, t) => calls.push(["update", k, v, t]),
});
let preset;
const stub = {
  window: { createOutputChannel: () => ({ appendLine: (l) => calls.push(["log", l]), show() {} }) },
  debug: { registerDebugConfigurationProvider: () => ({ dispose() {} }), startDebugging: async () => true },
  commands: {
    registerCommand: () => ({ dispose() {} }),
    executeCommand: async (...a) => calls.push(["exec", ...a]),
  },
  workspace: {
    workspaceFolders: [{ uri: { fsPath: project }, name: "game" }],
    getConfiguration: () => mkConf(preset),
  },
  ConfigurationTarget: { WorkspaceFolder: 3 },
  DebugConfigurationProviderTriggerKind: { Dynamic: 2 },
};
const real = Module._resolveFilename;
Module._resolveFilename = function (q, ...a) { return q === "vscode" ? q : real.call(this, q, ...a); };
require.cache.vscode = { id: "vscode", filename: "vscode", loaded: true, exports: stub };
const { activate } = require("../debug");
(async () => {
  activate({ subscriptions: [] });
  await new Promise((r) => setTimeout(r, 50));
  assert.ok(calls.some((c) => c[0] === "exec" && c[1] === "setContext" && c[2] === "tyrax.project" && c[3] === true));
  assert.ok(calls.some((c) => c[0] === "update" && c[1] === "debugShortcut" && c[2] === false && c[3] === 3));
  calls.length = 0;
  preset = true;  // the user chose to keep it
  activate({ subscriptions: [] });
  await new Promise((r) => setTimeout(r, 50));
  assert.ok(!calls.some((c) => c[0] === "update"), "a user's own setting is left alone");
  calls.length = 0;
  preset = undefined;
  stub.workspace.workspaceFolders = [{ uri: { fsPath: "C:/nowhere" }, name: "other" }];
  activate({ subscriptions: [] });
  await new Promise((r) => setTimeout(r, 50));
  assert.ok(calls.some((c) => c[0] === "exec" && c[2] === "tyrax.project" && c[3] === false));
  assert.ok(!calls.some((c) => c[0] === "update"), "not a TyraX folder: untouched");
  fs.rmSync(project, { recursive: true, force: true });
  console.log("activate.test.js: all checks passed");
})();
