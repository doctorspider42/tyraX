// Offline check of the `tyrax` -> `cppdbg` resolution (debug.js), no VS Code:
//   node tools/vscode-tyrax/test/debug.test.js
// The cases are what the editor writes into .vscode/tyrax.json on Windows
// (GDB inside WSL) and on Linux/macOS (native GDB).
"use strict";
const assert = require("assert");
const Module = require("module");

// debug.js requires "vscode" at load time; nothing below calls into it.
const realResolve = Module._resolveFilename;
Module._resolveFilename = function (request, ...rest) {
  if (request === "vscode") return request;
  return realResolve.call(this, request, ...rest);
};
require.cache.vscode = {
  id: "vscode", filename: "vscode", loaded: true,
  exports: { window: { createOutputChannel: () => ({ appendLine() {}, show() {} }) } },
};

const { toCppdbg, wslPath, decide } = require("../debug");

assert.strictEqual(wslPath("C:\\Users\\a\\b"), "/mnt/c/Users/a/b");
assert.strictEqual(wslPath("F:/x"), "/mnt/f/x");
assert.strictEqual(wslPath("/already/posix"), "/already/posix");

const win = {
  version: 1,
  editor: "F:\\Projects\\tyra-editor\\build\\tyrax-editor.exe",
  project: "C:\\Users\\p\\game",
  elf: "C:\\Users\\p\\game\\bin\\game.elf.sym",
  gdb: "/mnt/c/Users/p/AppData/Local/tyra-editor/toolchain/ps2dev/ee/bin/mips64r5900el-ps2-elf-gdb",
  wsl: true,
  port: 24058,
  sourceMapFrom: "/mnt/c/Users/p/game",
};
const w = toCppdbg(win, { name: "TyraX: debug game in PCSX2" }, "win32");
assert.strictEqual(w.type, "cppdbg");
assert.strictEqual(w.MIMode, "gdb");
assert.strictEqual(w.miDebuggerServerAddress, undefined, "ignored under pipeTransport");
assert.deepStrictEqual(w.customLaunchSetupCommands, [
  { text: '-file-exec-and-symbols "/mnt/c/Users/p/game/bin/game.elf.sym"' },
  { text: "-target-select remote 127.0.0.1:24058" },
]);
assert.strictEqual(w.launchCompleteCommand, "exec-continue");
assert.strictEqual(w.program, "/mnt/c/Users/p/game/bin/game.elf.sym");
assert.strictEqual(w.cwd, "/mnt/c/Users/p/game");
assert.deepStrictEqual(w.pipeTransport, {
  pipeCwd: "",
  pipeProgram: "wsl.exe",
  pipeArgs: ["--exec", "bash", "-c"],
  debuggerPath: win.gdb,
});
assert.deepStrictEqual(w.sourceFileMap["/mnt/c/Users/p/game"], {
  editorPath: "C:\\Users\\p\\game",
  useForBreakpoints: true,
});
assert.ok(w.sourceFileMap["/mnt/c/"] && w.sourceFileMap["/mnt/f/"], "both drives mapped");
assert.ok(w.setupCommands.some((c) => c.text === "-gdb-set mi-async on"), "async MI for Pause");

const nix = {
  version: 1,
  editor: "/opt/tyrax/bin/tyrax-editor",
  project: "/home/p/game",
  elf: "/home/p/game/bin/game.elf.sym",
  gdb: "/home/p/.config/tyra-editor/toolchain/ps2dev/ee/bin/mips64r5900el-ps2-elf-gdb",
  wsl: false,
  port: 23500,
  sourceMapFrom: "",
};
const n = toCppdbg(nix, { name: "x", setupCommands: [{ text: "info sharedlibrary" }] }, "linux");
assert.strictEqual(n.program, nix.elf);
assert.strictEqual(n.cwd, nix.project);
assert.strictEqual(n.miDebuggerPath, nix.gdb);
assert.strictEqual(n.customLaunchSetupCommands[1].text, "-target-select remote 127.0.0.1:23500");
assert.strictEqual(n.pipeTransport, undefined);
assert.strictEqual(n.sourceFileMap, undefined);
assert.deepStrictEqual(n.setupCommands, [{ text: "info sharedlibrary" }], "the user's setupCommands win");

// What F5 does, every case (decide()).
const { codeChanged } = require("../debug");
const on = { ...win, scriptDebugger: true, profile: "debug" };
const off = { ...win, scriptDebugger: false, profile: "debug" };
const rel = { ...win, scriptDebugger: true, profile: "release" };
const ed = (state) => ({ state, owner: "editor", pid: 1 });
const cli = (state) => ({ state, pid: 2 });
assert.strictEqual(decide(null, null, false).action, "error", "no tyrax.json");
assert.strictEqual(decide(rel, null, false).action, "error", "release profile");
assert.strictEqual(decide(on, ed("release"), false).action, "error", "editor says release");
assert.deepStrictEqual(decide(on, ed("waiting for GDB"), false), { action: "attach" });
assert.deepStrictEqual(decide(on, ed("waiting for GDB"), true), { action: "request", rebuild: true }, "code changed");
assert.strictEqual(decide(on, ed("attached, stopped"), true).action, "error", "one debugger at a time");
// Restart: VS Code resolves the new session before dropping its own old one.
assert.deepStrictEqual(decide(on, ed("attached, stopped"), false, true), { action: "attach" }, "restart, same code");
assert.deepStrictEqual(decide(on, ed("attached, running"), true, true), { action: "request", rebuild: true }, "restart, code changed");
assert.deepStrictEqual(decide(on, cli("attached, stopped"), true, true), { action: "respawn" }, "restart, headless, code changed");
for (const st of ["off", "idle", "building", "waiting for the game", "failed"])
  assert.deepStrictEqual(decide(off, ed(st), false), { action: "request", rebuild: false }, st);
assert.deepStrictEqual(decide(on, ed("idle"), true), { action: "request", rebuild: true });
assert.deepStrictEqual(decide(on, cli("waiting for GDB"), false), { action: "attach" });
assert.deepStrictEqual(decide(on, cli("waiting for GDB"), true), { action: "respawn" });
assert.deepStrictEqual(decide(on, cli("waiting for the game"), false), { action: "wait" });
assert.deepStrictEqual(decide(off, null, true), { action: "spawn", ask: true });
assert.deepStrictEqual(decide(on, null, false), { action: "spawn", ask: false });

// codeChanged(): sources newer than the built ELF.
const fs = require("fs"), os = require("os"), path = require("path");
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "tyrax-stale-"));
fs.mkdirSync(path.join(tmp, "src", "scripts"), { recursive: true });
fs.mkdirSync(path.join(tmp, "bin"));
const elf = path.join(tmp, "bin", "g.elf.sym");
const script = path.join(tmp, "src", "scripts", "a.cpp");
const c = { project: tmp, elf };
fs.writeFileSync(script, "int a;");
assert.strictEqual(codeChanged(c), true, "never built");
fs.writeFileSync(elf, "elf");
const past = new Date(Date.now() - 60000);
fs.utimesSync(script, past, past);
assert.strictEqual(codeChanged(c), false, "built after the edit");
fs.writeFileSync(script, "int b;");
assert.strictEqual(codeChanged(c), true, "edited after the build");
fs.writeFileSync(path.join(tmp, "src", "scripts", "notes.txt"), "x");
fs.utimesSync(script, past, past);
assert.strictEqual(codeChanged(c), false, "a non-code file does not count");
fs.rmSync(tmp, { recursive: true, force: true });

console.log("debug.test.js: all checks passed");
