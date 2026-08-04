// Language features for TyraX custom flow nodes (.flownode) and screen
// effects (.screenfx). Both formats share the same shape: a `key = value`
// header, a line that is exactly `---`, then a C++ body with {placeholder}
// substitutions. This file provides diagnostics, hovers and completion on top
// of the TextMate grammars. Plain JS on purpose: no build step, so the folder
// installs straight into ~/.vscode/extensions.
//
// Keep the SPEC tables in sync with the editor parsers:
//   flownode  -> src/flownode.cpp + docs/custom-flow-nodes.md
//   screenfx  -> src/screenfx.cpp + docs/custom-screen-effects.md
//
// VU1 microprograms (.vclpp/.vcl/.vsm) are a third language and live in
// ./vu.js - kept separate because their catalogue is NOT a table here at all.
// The macros and the VU1_* addresses are read from the engine tree at
// activation, so they cannot drift the way a copied SPEC would; only the
// instruction set, which is fixed silicon, is written down (docs/vu-framework.md).
"use strict";

const vscode = require("vscode");
const vu = require("./vu");
const vuscript = require("./vuscript");

// ---- Per-language specification --------------------------------------------

const PIN_KINDS = ["object", "position", "bool", "text"];

const SPEC = {
  "tyrax-flownode": {
    // key -> { doc, kind }. kind drives value validation & completion.
    keys: {
      title: { doc: "Display name in the add-menu and node title bar. Defaults to the file name.", kind: "text" },
      category: { doc: "Add-menu submenu the node appears under. Defaults to `Custom`.", kind: "text" },
      desc: { doc: "What the NODE does - the node's tooltip in the editor and its entry in the AI flow-graph generator's catalog. Say what a parameter does in its own `tipN` instead.", kind: "text" },
      tip_string: { doc: "What the STRING param does - one line, shown when the cursor rests on that widget inside the node.", kind: "text" },
      string: { doc: "The string param: `none`, `text`, or `object` (gives the node its target object input).", kind: "enum", values: ["none", "text", "object"] },
      in: { doc: "Extra input pins: any space-separated set of `object position bool text`.", kind: "pins" },
      out: { doc: "Output pins: any of `object position bool text`. Requires `call = fn`.", kind: "pins" },
      exec_out: { doc: "`true` adds a follow-up exec output that fires downstream after the node runs.", kind: "bool" },
      call: { doc: "Name of a C++ function in `inc/scripts/flow_nodes.hpp` to run. Omit for an inline body.", kind: "text" },
    },
    // indexed families: `num0`..`num3` (+ their `tip0`..`tip3`). A tip is
    // OPTIONAL and independent of the label run - `sparse` skips the
    // contiguity rule - but it needs the param it documents to exist, which
    // `needs` checks.
    indexed: [
      { base: "num", max: 3, doc: "Label for a numeric param. Define num0..num3 contiguously from num0." },
      { base: "tip", max: 3, sparse: true, needs: "num", doc: "What that numeric param DOES - one line, shown when the cursor rests on the widget inside the node, and listed under `desc` in the node's tooltip. Write one: `desc` explains the node, a tip explains the knob." },
    ],
    placeholders: {
      obj: "Resolved target object index (the object pin/dropdown, or self).",
      self: "Index of the object that owns this graph.",
      str: 'String param as a quoted, escaped C string ("hi").',
      num0: "num0 as a float literal (5.0F).", num1: "num1 as a float literal.",
      num2: "num2 as a float literal.", num3: "num3 as a float literal.",
      int0: "num0 as an integer literal (5).", int1: "num1 as an integer literal.",
      int2: "num2 as an integer literal.", int3: "num3 as an integer literal.",
    },
    // placeholder -> the indexed key it requires be present in the header.
    placeholderNeeds: { num0: "num0", num1: "num1", num2: "num2", num3: "num3", int0: "num0", int1: "num1", int2: "num2", int3: "num3" },
  },
  "tyrax-screenfx": {
    keys: {
      title: { doc: "Display name in the UI Editor screen stack. Defaults to the file name.", kind: "text" },
    },
    indexed: [{ base: "param", max: 3, doc: "A slider: `Label` or `Label, default, min, max` (default 0, range 0..1). Define param0..param3 contiguously.", value: "param" }],
    placeholders: {
      p0: "param[0] (the placement's first parameter value).",
      p1: "param[1].", p2: "param[2].", p3: "param[3].",
    },
    placeholderNeeds: { p0: "param0", p1: "param1", p2: "param2", p3: "param3" },
  },
};

// ---- Parsing ---------------------------------------------------------------

// Split a document into { header: [{line, key, value, keyRange, valueRange}],
// separatorLine, bodyStart, definedKeys:Set }.
function parse(doc) {
  const header = [];
  const definedKeys = new Set();
  let separatorLine = -1;
  for (let i = 0; i < doc.lineCount; i++) {
    const text = doc.lineAt(i).text;
    if (text.trimEnd() === "---") { separatorLine = i; break; }
    const trimmed = text.trim();
    if (trimmed === "" || trimmed.startsWith("#")) continue;
    const m = /^(\s*)([^=\s]+)\s*(=)\s*(.*)$/.exec(text);
    if (!m) { header.push({ line: i, malformed: true, text }); continue; }
    const key = m[2];
    const keyStart = m[1].length;
    const valStart = m.index + m[0].length - m[4].length;
    header.push({
      line: i,
      key,
      value: m[4],
      keyRange: new vscode.Range(i, keyStart, i, keyStart + key.length),
      valueRange: new vscode.Range(i, valStart, i, text.length),
    });
    definedKeys.add(key);
  }
  return {
    header,
    definedKeys,
    separatorLine,
    bodyStart: separatorLine >= 0 ? separatorLine + 1 : -1,
  };
}

function knownKey(spec, key) {
  if (spec.keys[key]) return spec.keys[key];
  for (const fam of spec.indexed) {
    const m = new RegExp("^" + fam.base + "([0-9]+)$").exec(key);
    if (m && Number(m[1]) <= fam.max) return { doc: fam.doc, kind: "indexed", family: fam };
  }
  return null;
}

// ---- Diagnostics -----------------------------------------------------------

function refreshDiagnostics(doc, collection) {
  const spec = SPEC[doc.languageId];
  if (!spec) return;
  const diags = [];
  const p = parse(doc);
  const warn = (range, msg) => diags.push(new vscode.Diagnostic(range, msg, vscode.DiagnosticSeverity.Warning));
  const err = (range, msg) => diags.push(new vscode.Diagnostic(range, msg, vscode.DiagnosticSeverity.Error));
  const info = (range, msg) => diags.push(new vscode.Diagnostic(range, msg, vscode.DiagnosticSeverity.Information));

  const seen = new Set();
  for (const h of p.header) {
    if (h.malformed) {
      warn(new vscode.Range(h.line, 0, h.line, h.text.length),
        "Expected `key = value`, a `# comment`, or the `---` separator.");
      continue;
    }
    if (seen.has(h.key)) warn(h.keyRange, `Duplicate key \`${h.key}\` — the later value wins.`);
    seen.add(h.key);

    const def = knownKey(spec, h.key);
    if (!def) {
      warn(h.keyRange, `Unknown key \`${h.key}\`. It is ignored by the editor.`);
      continue;
    }
    const v = h.value.trim();
    if (def.kind === "enum" && v !== "" && !def.values.includes(v))
      err(h.valueRange, `\`${h.key}\` must be one of: ${def.values.join(", ")}.`);
    if (def.kind === "bool" && v !== "" && v !== "true" && v !== "false")
      warn(h.valueRange, `\`${h.key}\` expects \`true\` or \`false\`.`);
    if (def.kind === "pins" && v !== "") {
      for (const tok of v.split(/\s+/)) {
        if (!PIN_KINDS.includes(tok)) {
          const at = doc.lineAt(h.line).text.indexOf(tok, h.valueRange.start.character);
          err(new vscode.Range(h.line, at, h.line, at + tok.length),
            `Unknown pin kind \`${tok}\`. Use: ${PIN_KINDS.join(", ")}.`);
        }
      }
    }
    if (def.kind === "indexed" && def.family.value === "param" && v !== "") {
      // "Label" or "Label, default, min, max"
      const parts = v.split(",");
      if (parts.length !== 1 && parts.length !== 4)
        warn(h.valueRange, "Expected `Label` or `Label, default, min, max`.");
      else for (let i = 1; i < parts.length; i++)
        if (!/^\s*[-+]?[0-9]*\.?[0-9]+\s*$/.test(parts[i]))
          warn(h.valueRange, "default, min and max must be numbers.");
    }
  }

  // Indexed families must be contiguous from 0 — except a `sparse` one (tips),
  // which instead has to name a param that exists.
  for (const fam of spec.indexed) {
    const present = [];
    for (let i = 0; i <= fam.max; i++) if (p.definedKeys.has(fam.base + i)) present.push(i);
    for (const i of present) {
      const h = p.header.find((x) => x.key === fam.base + i);
      if (!h) continue;
      if (fam.needs && !p.definedKeys.has(fam.needs + i)) {
        warn(h.keyRange, `\`${fam.base}${i}\` documents \`${fam.needs}${i}\`, which is not defined — the editor drops it.`);
      } else if (!fam.sparse && i > 0 && !p.definedKeys.has(fam.base + (i - 1))) {
        warn(h.keyRange, `\`${fam.base}${i}\` defined but \`${fam.base}${i - 1}\` is missing — params must be contiguous from ${fam.base}0.`);
      }
    }
  }

  // Body / separator rules.
  const isFlow = doc.languageId === "tyrax-flownode";
  const hasCall = isFlow && p.definedKeys.has("call") && (p.header.find((h) => h.key === "call")?.value.trim() || "") !== "";
  const bodyLines = [];
  if (p.bodyStart >= 0)
    for (let i = p.bodyStart; i < doc.lineCount; i++) bodyLines.push({ line: i, text: doc.lineAt(i).text });
  const bodyHasCode = bodyLines.some((l) => l.text.trim() !== "" && !l.text.trim().startsWith("#"));

  if (p.separatorLine < 0) {
    if (isFlow && !hasCall)
      warn(new vscode.Range(0, 0, 0, doc.lineAt(0).text.length),
        "Inline node has no `---` separator: add one followed by the C++ body (or set `call = fn`).");
    if (!isFlow)
      warn(new vscode.Range(0, 0, 0, doc.lineAt(0).text.length),
        "No `---` separator: a screen effect needs a `---` line followed by its C++ body.");
  } else {
    if (isFlow && hasCall && bodyHasCode)
      info(new vscode.Range(p.separatorLine, 0, p.separatorLine, 3),
        "`call = fn` is set, so the body after `---` is ignored — put the code in flow_nodes.hpp.");
    if (isFlow && !hasCall && !bodyHasCode)
      warn(new vscode.Range(p.separatorLine, 0, p.separatorLine, 3),
        "Inline node has an empty body — it will do nothing. Add C++ after `---` or set `call = fn`.");
    if (!isFlow && !bodyHasCode)
      warn(new vscode.Range(p.separatorLine, 0, p.separatorLine, 3),
        "Screen effect has an empty body — add the C++ blit code after `---`.");

    // Placeholder validation in the body.
    const phRe = /\{([A-Za-z0-9_]+)\}/g;
    for (const l of bodyLines) {
      let m;
      while ((m = phRe.exec(l.text)) !== null) {
        const name = m[1];
        if (!(name in spec.placeholders)) {
          warn(new vscode.Range(l.line, m.index, l.line, m.index + m[0].length),
            `Unknown placeholder \`{${name}}\`. Known: ${Object.keys(spec.placeholders).map((k) => "{" + k + "}").join(", ")}.`);
        } else if (spec.placeholderNeeds[name] && !p.definedKeys.has(spec.placeholderNeeds[name])) {
          warn(new vscode.Range(l.line, m.index, l.line, m.index + m[0].length),
            `\`{${name}}\` is used but \`${spec.placeholderNeeds[name]}\` is not defined in the header.`);
        }
      }
    }
  }

  collection.set(doc.uri, diags);
}

// ---- Hover -----------------------------------------------------------------

function provideHover(doc, position) {
  const spec = SPEC[doc.languageId];
  if (!spec) return null;
  const p = parse(doc);
  const inBody = p.bodyStart >= 0 && position.line >= p.bodyStart;

  if (inBody) {
    const range = doc.getWordRangeAtPosition(position, /\{[A-Za-z0-9_]+\}/);
    if (range) {
      const name = doc.getText(range).slice(1, -1);
      if (name in spec.placeholders)
        return new vscode.Hover(new vscode.MarkdownString(`**\`{${name}}\`** — ${spec.placeholders[name]}`), range);
    }
    return null;
  }
  // Header: hover the key.
  const h = p.header.find((x) => !x.malformed && x.line === position.line);
  if (h && h.keyRange.contains(position)) {
    const def = knownKey(spec, h.key);
    if (def) return new vscode.Hover(new vscode.MarkdownString(`**\`${h.key}\`** — ${def.doc}`), h.keyRange);
  }
  return null;
}

// ---- Completion ------------------------------------------------------------

function provideCompletions(doc, position) {
  const spec = SPEC[doc.languageId];
  if (!spec) return null;
  const p = parse(doc);
  const line = doc.lineAt(position.line).text;
  const upto = line.slice(0, position.character);
  const inBody = p.bodyStart >= 0 && position.line >= p.bodyStart;

  // Body: placeholder completion (right after `{` or while typing inside it).
  if (inBody) {
    const m = /\{([A-Za-z0-9_]*)$/.exec(upto);
    if (!m) return null;
    const items = [];
    for (const [name, docStr] of Object.entries(spec.placeholders)) {
      const it = new vscode.CompletionItem(name, vscode.CompletionItemKind.Variable);
      it.detail = "TyraX placeholder";
      it.documentation = new vscode.MarkdownString(docStr);
      // Replace what the user typed after `{`; grammar/editor supplies the `}`.
      it.range = new vscode.Range(position.line, position.character - m[1].length, position.line, position.character);
      it.insertText = name + "}";
      if (spec.placeholderNeeds[name] && !p.definedKeys.has(spec.placeholderNeeds[name])) it.sortText = "zz" + name;
      items.push(it);
    }
    return items;
  }

  // Header: value completion after `key =`, else key completion.
  const kv = /^\s*([^=\s]+)\s*=\s*([^\s]*)$/.exec(upto);
  if (kv) {
    const def = knownKey(spec, kv[1]);
    let values = null;
    if (def && def.kind === "enum") values = def.values;
    else if (def && def.kind === "pins") values = PIN_KINDS;
    else if (def && def.kind === "bool") values = ["true", "false"];
    if (values)
      return values.map((v) => new vscode.CompletionItem(v, vscode.CompletionItemKind.EnumMember));
    return null;
  }
  if (/^\s*[^=]*$/.test(upto) && !upto.includes("=")) {
    const items = [];
    const used = p.definedKeys;
    for (const [key, def] of Object.entries(spec.keys)) {
      if (used.has(key)) continue;
      const it = new vscode.CompletionItem(key, vscode.CompletionItemKind.Property);
      it.documentation = new vscode.MarkdownString(def.doc);
      it.insertText = new vscode.SnippetString(`${key} = $0`);
      items.push(it);
    }
    for (const fam of spec.indexed) {
      // suggest the next contiguous index - or, for a family that documents
      // another one (tipN -> numN), the first param that has no tip yet.
      let next = 0;
      if (fam.needs) {
        while (next <= fam.max && (used.has(fam.base + next) || !used.has(fam.needs + next))) next++;
      } else {
        while (next <= fam.max && used.has(fam.base + next)) next++;
      }
      if (next <= fam.max) {
        const it = new vscode.CompletionItem(fam.base + next, vscode.CompletionItemKind.Property);
        it.documentation = new vscode.MarkdownString(fam.doc);
        it.insertText = new vscode.SnippetString(`${fam.base}${next} = $0`);
        items.push(it);
      }
    }
    return items;
  }
  return null;
}

// ---- Activation ------------------------------------------------------------

function activate(context) {
  const selector = [{ language: "tyrax-flownode" }, { language: "tyrax-screenfx" }];
  const collection = vscode.languages.createDiagnosticCollection("tyrax");
  context.subscriptions.push(collection);

  // VU1 microprograms. The catalogue scan is async and best-effort: with no
  // engine tree in the workspace the instruction help still works, only the
  // macro and address completions are missing.
  const vuSelector = [{ language: vu.LANG }];
  vu.loadCatalogue().catch(() => {});
  const vuWatcher = vscode.workspace.createFileSystemWatcher(
    "**/renderer/3d/pipeline/**/*.{i,h}"
  );
  vuWatcher.onDidChange(() => vu.loadCatalogue().catch(() => {}));
  vuWatcher.onDidCreate(() => vu.loadCatalogue().catch(() => {}));
  context.subscriptions.push(
    vuWatcher,
    vscode.languages.registerHoverProvider(vuSelector, { provideHover: vu.provideHover }),
    vscode.languages.registerCompletionItemProvider(
      vuSelector,
      { provideCompletionItems: vu.provideCompletions },
      ".", "{", " ", "_"
    )
  );

  // A project's own VU program - src/vu/*.cpp. Ordinary C++, so this is scoped
  // by PATH: every other .cpp in the project is game code and none of the rules
  // in vuscript.js hold for it.
  context.subscriptions.push(
    vscode.languages.registerHoverProvider(vuscript.SELECTOR, {
      provideHover: vuscript.provideHover,
    }),
    vscode.languages.registerCompletionItemProvider(vuscript.SELECTOR, {
      provideCompletionItems: vuscript.provideCompletions,
    })
  );

  const run = (doc) => {
    if (doc.languageId === vu.LANG) return vu.refreshDiagnostics(doc, collection);
    if (vuscript.applies(doc)) return vuscript.refreshDiagnostics(doc, collection);
    if (SPEC[doc.languageId]) refreshDiagnostics(doc, collection);
  };
  vscode.workspace.textDocuments.forEach(run);
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument(run),
    vscode.workspace.onDidChangeTextDocument((e) => run(e.document)),
    vscode.workspace.onDidCloseTextDocument((d) => collection.delete(d.uri)),
    vscode.languages.registerHoverProvider(selector, { provideHover }),
    vscode.languages.registerCompletionItemProvider(selector, { provideCompletionItems: provideCompletions }, "{", " ", "=")
  );
}

function deactivate() {}

// activate/deactivate are the VS Code entry points; the rest are exported for
// the offline test harness (tools/vscode-tyrax tests) and unused by VS Code.
module.exports = { activate, deactivate, parse, refreshDiagnostics, provideHover, provideCompletions, SPEC };
