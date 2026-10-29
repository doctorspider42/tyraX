// The script debugger end to end, in a REAL VS Code window - what the offline
// tests cannot reach: F5 with the `tyrax` configuration, a breakpoint, a code
// change, then the Restart command; PASS when the restarted session stops on
// the breakpoint running the new code.
//
//   node tools/vscode-tyrax/test/integration/run.js <projectDir>
//
// <projectDir>: a debug-profile project with the script debugger on, built
// once (.vscode/tyrax.json), whose src/scripts/cookbook/motion_math.cpp has a
// `volatile int tyraxProbe = <n>;` line before line 31 - a copy of
// examples/script-cookbook with that line added. The test rewrites the number.
// Open the project in the TyraX editor first to test the editor path; leave it
// closed for the headless one.
//
// It opens a VS Code window (its own profile under the temp dir: workspace
// trust off, so no dialog blocks F5; your extensions folder, for C/C++) and a
// PCSX2 window. Only one game can own PCSX2's PINE slot: close other games
// first. Exit code 0 = PASS.
"use strict";
const cp = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

const project = process.argv[2];
if (!project || !fs.existsSync(path.join(project, ".vscode", "tyrax.json"))) {
  console.error("usage: node run.js <projectDir>   (a built project with .vscode/tyrax.json)");
  process.exit(2);
}
const profile = path.join(os.tmpdir(), "tyrax-vscode-it-profile");
fs.mkdirSync(path.join(profile, "User"), { recursive: true });
fs.writeFileSync(
  path.join(profile, "User", "settings.json"),
  JSON.stringify(
    {
      "security.workspace.trust.enabled": false,
      "extensions.autoUpdate": false,
      "update.mode": "none",
      "telemetry.telemetryLevel": "off",
      "workbench.startupEditor": "none",
    },
    null,
    2
  )
);
const log = path.join(os.tmpdir(), "tyrax-vscode-it.log");
try {
  fs.unlinkSync(log);
} catch (e) {
  // first run
}
const ext = path.resolve(__dirname, "..", "..");
const args = [
  "--user-data-dir", profile,
  "--extensions-dir", path.join(os.homedir(), ".vscode", "extensions"),
  "--extensionDevelopmentPath", ext,
  "--extensionTestsPath", __dirname,
  "--new-window", "--skip-welcome", "--skip-release-notes",
  project,
];
console.log("code " + args.join(" "));
const child = cp.spawn("code", args, {
  shell: process.platform === "win32",
  env: { ...process.env, TYRAX_IT_LOG: log },
  stdio: "ignore",
});
// The `code` launcher may return before the window does: follow the log the
// test writes until it says "done".
child.on("exit", () => {
  const end = Date.now() + 15 * 60 * 1000;
  const timer = setInterval(() => {
    const text = fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "";
    if (/ done\n/.test(text) || Date.now() > end) {
      clearInterval(timer);
      process.stdout.write(text || "(no log)\n");
      process.exit(/RESULT PASS/.test(text) ? 0 : 1);
    }
  }, 1000);
});
