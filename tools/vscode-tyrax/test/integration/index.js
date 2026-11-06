// Integration test run INSIDE a real VS Code (--extensionTestsPath): F5 with
// the `tyrax` configuration, a breakpoint, a code change, then the Restart
// command - does the new session stop on the breakpoint with the new code?
"use strict";
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");

const LOG = process.env.TYRAX_IT_LOG || path.join(__dirname, "it.log");
const t0 = Date.now();
function log(...a) {
  fs.appendFileSync(LOG, ((Date.now() - t0) / 1000).toFixed(1) + "s " + a.join(" ") + "\n");
}

function waitFor(pred, ms, what) {
  return new Promise((resolve, reject) => {
    const end = Date.now() + ms;
    const tick = () => {
      const v = pred();
      if (v) return resolve(v);
      if (Date.now() > end) return reject(new Error("timeout: " + what));
      setTimeout(tick, 250);
    };
    tick();
  });
}

exports.run = async function () {
  fs.writeFileSync(LOG, "");
  try {
    const folder = vscode.workspace.workspaceFolders[0];
    const root = folder.uri.fsPath;
    const src = path.join(root, "src", "scripts", "cookbook", "motion_math.cpp");
    const stops = [];
    const sessions = [];
    vscode.debug.registerDebugAdapterTrackerFactory("*", {
      createDebugAdapterTracker(session) {
        log("tracker for session", session.id, session.type, session.name);
        return {
          onDidSendMessage(m) {
            if (m.type === "event" && ["stopped", "terminated", "exited", "initialized"].includes(m.event)) {
              log("  event", m.event, session.id, JSON.stringify(m.body || {}).slice(0, 100));
              if (m.event === "stopped") stops.push({ session, body: m.body });
            }
            if (m.type === "response" && !m.success) log("  failed", m.command, m.message);
          },
          onError(e) { log("  adapter error", e && e.message); },
          onExit(code) { log("  adapter exit", code); },
        };
      },
    });
    vscode.debug.onDidStartDebugSession((s) => { sessions.push(s); log("session started", s.id, s.type); });
    vscode.debug.onDidTerminateDebugSession((s) => log("session terminated", s.id, s.type));

    vscode.debug.addBreakpoints([
      new vscode.SourceBreakpoint(new vscode.Location(vscode.Uri.file(src), new vscode.Position(30, 0))),
    ]);
    log("start debugging");
    const ok = await vscode.debug.startDebugging(folder, {
      type: "tyrax", request: "launch", name: "TyraX: debug game in PCSX2",
    });
    log("startDebugging ->", ok);
    await waitFor(() => stops.length >= 1, 300000, "first stop");
    log("FIRST STOP ok, session", stops[0].session.id);

    // The way people do it: the game RUNS (no breakpoint) when they restart.
    const running = process.env.TYRAX_IT_RUNNING !== "0";
    if (running) {
      vscode.debug.removeBreakpoints(vscode.debug.breakpoints);
      await stops[0].session.customRequest("continue", { threadId: stops[0].body.threadId || 1 });
      log("continued: the game runs, no breakpoints");
      await new Promise((r) => setTimeout(r, 2000));
    }
    const probe = 100 + Math.floor(Math.random() * 800);
    let text = fs.readFileSync(src, "utf8");
    text = text.replace(/volatile int tyraxProbe = \d+;/, "volatile int tyraxProbe = " + probe + ";");
    fs.writeFileSync(src, text);
    log("edited the script: tyraxProbe =", probe);

    const how = process.env.TYRAX_IT_RESTART || "workbench.action.debug.restart";
    log("RESTART via", how);
    const sessionsBefore = sessions.length;
    const before = stops.length;  // a parked game stops again as soon as it reattaches
    // VS Code may reject its own restart ("Canceled") when the old session
    // ends mid-way; the user just sees nothing - keep waiting like they would.
    try {
      await vscode.commands.executeCommand(how);
    } catch (e) {
      log("restart command:", e && e.message);
    }
    await waitFor(() => sessions.length > sessionsBefore, 300000, "a new session after restart");
    log("new session started after restart");
    const serverState = () => {
      try {
        return JSON.parse(fs.readFileSync(path.join(root, "bin", "gdbserver.json"), "utf8")).state;
      } catch (e) {
        return "?";
      }
    };
    for (let i = 0; i < 4; ++i) {
      await new Promise((r) => setTimeout(r, 1500));
      log("  server:", serverState(), "debug sessions so far:", sessions.length);
    }
    if (running) {
      vscode.debug.addBreakpoints([
        new vscode.SourceBreakpoint(new vscode.Location(vscode.Uri.file(src), new vscode.Position(30, 0))),
      ]);
    }
    try {
      await waitFor(() => stops.length > before, 30000, "stop after restart");
    } catch (e) {
      log("  no breakpoint stop; server:", serverState(), "- trying a pause");
      const last = sessions[sessions.length - 1];
      try {
        await last.customRequest("pause", { threadId: 1 });
        log("  pause sent");
      } catch (err) {
        log("  pause failed:", err && err.message);
      }
      await waitFor(() => stops.length > before, 30000, "stop after pause");
      log("  stopped by pause - the session IS attached; breakpoint did not bind");
      throw e;
    }
    const s = stops[stops.length - 1];
    log("STOP AFTER RESTART, session", s.session.id);
    const st = await s.session.customRequest("stackTrace", { threadId: s.body.threadId || 1, levels: 1 });
    const ev = await s.session.customRequest("evaluate", {
      expression: "tyraxProbe", frameId: st.stackFrames[0].id, context: "watch",
    });
    log("tyraxProbe after restart =", ev.result, "(expected " + probe + ")");
    log(String(ev.result) === String(probe) ? "RESULT PASS" : "RESULT FAIL");
  } catch (e) {
    log("RESULT ERROR", e && e.stack ? e.stack : e);
  } finally {
    try {
      await vscode.debug.stopDebugging();
    } catch (e) {
      // ignore
    }
    log("done");
  }
};
