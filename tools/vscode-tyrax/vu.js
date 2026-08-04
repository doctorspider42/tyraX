// Language features for VU1 microprograms (.vclpp / .vcl / .vsm / .i) - the
// files the VU framework reads, generates and simulates (docs/vu-framework.md).
//
// Two catalogues, deliberately sourced differently:
//
//   - The OPCODES are a fixed ISA, so they live in the table below with a
//     one-line description each. That is the part worth having: `madd.xyz`,
//     `clipw`, `fcand`, `ftoi4` and `xgkick` are not guessable, and the whole
//     reason VU1 reads as impenetrable is that every line is an abbreviation.
//   - The MACROS and the VU1_* addresses are read from the workspace at
//     activation, by scanning the engine's shared .i files and
//     stapip_vu1_shared_defines.h. Copying them here would be a second source
//     of truth that silently rots the first time someone adds a macro - and the
//     whole framework exists because that kind of duplication was the problem.
//
// Plain JS, no build step, same as the rest of this extension.
"use strict";

// `vscode` only exists inside the editor. Falling back to a stub keeps the pure
// parts - `collect`, the opcode table - runnable from a plain node harness,
// which is how the catalogue scan is checked against a real engine tree without
// launching VS Code.
let vscode;
try {
  vscode = require("vscode");
} catch (e) {
  vscode = {
    MarkdownString: class {
      constructor(v) { this.value = v || ""; }
      appendMarkdown(v) { this.value += v; return this; }
      appendCodeblock(v) { this.value += "\n" + v + "\n"; return this; }
    },
    Hover: class { constructor(c, r) { this.contents = c; this.range = r; } },
    CompletionItem: class { constructor(l, k) { this.label = l; this.kind = k; } },
    CompletionItemKind: { Keyword: 0, Function: 1, Constant: 2, Variable: 3 },
    SnippetString: class { constructor(v) { this.value = v; } },
    Diagnostic: class { constructor(r, m, s) { this.range = r; this.message = m; this.severity = s; } },
    DiagnosticSeverity: { Warning: 1 },
    Range: class { constructor(a, b, c, d) { this.a = a; this.b = b; this.c = c; this.d = d; } },
    workspace: { findFiles: async () => [], fs: { readFile: async () => Buffer.alloc(0) } },
  };
}

const LANG = "tyrax-vu";

// ---- The instruction set ----------------------------------------------------
// Grouped the way the hardware is: an upper (float) pipe and a lower pipe, which
// is what makes VCL able to pair two per 64-bit slot.

const OPCODES = {
  // --- upper pipe: float, all take a .xyzw destination mask ---
  add: "dst = a + b, per masked field.",
  sub: "dst = a - b, per masked field.",
  mul: "dst = a * b, per masked field.",
  mula: "acc = a * b. The accumulator variant - starts a madd chain.",
  madd: "dst = acc + a * b. Continues a chain started by mula.",
  madda: "acc = acc + a * b. Middle of a chain; the last step uses madd.",
  msub: "dst = acc - a * b.",
  msuba: "acc = acc - a * b.",
  mini: "dst = min(a, b). With `i` as the second operand this clamps to the I register (see loi).",
  max: "dst = max(a, b). `max.xyz c, c, vf00[x]` is the idiomatic clamp-to-zero.",
  move: "Raw field COPY, not arithmetic - carries integer bit patterns (a packed GIF tag, an ftoi4 result) through unchanged.",
  mr32: "Rotate the fields by one (x<-y, y<-z, z<-w, w<-x). A copy, not arithmetic.",
  abs: "dst = |a|.",
  ftoi4: "Float to 12.4 fixed point, TRUNCATED toward zero, leaving the integer bits in the register. Exactly the GS screen-coordinate format.",
  ftoi0: "Float to integer, truncated. Used to turn a clamped 0..255 colour into the byte the GS reads.",
  itof0: "Integer bits back to float.",
  clipw:
    "Compare xyz against ±|w| and shift six flag bits into the clip register - the frustum test. Read the verdict with fcand.",
  opmula: "Outer-product step (cross products). Rare in this engine.",
  opmsub: "Outer-product step.",

  // --- lower pipe ---
  div: "q = a[field] / b[field]. WRITES Q - anything that needs the previous Q must read it first.",
  rsqrt: "q = a[field] / sqrt(|b[field]|). Also writes Q, which is why a normalize must come BEFORE a perspective divide.",
  sqrt: "q = sqrt(|b[field]|).",
  loi: "Load the I register with a float literal. The next op can use `i` as its second operand.",
  mtir: "Move the LOW 16 BITS of a float register field into an integer register.",
  mfir: "Move an integer register into a float register field, sign-extended.",
  fcand: "VI = (clipFlags & mask) ? 1 : 0. `fcand VI01, 0x3FFFF` asks 'did any of the last three vertices fall outside?'.",
  fcor: "VI = ((clipFlags | mask) == 0xFFFFFF) ? 1 : 0.",
  fceq: "VI = (clipFlags == mask) ? 1 : 0.",
  fcset: "Set the clip flag register to an immediate - `fcset 0x000000` clears it before a loop.",
  fcget: "Read the low 12 clip flag bits into an integer register.",
  fmand: "MAC-flag test. NOT modelled by the host simulator - a program branching on it is not authoritatively simulated.",
  fsand: "STATUS-flag test. Not modelled by the host simulator.",
  lq: "Load a quadword from VU data memory into a float register. Takes a mask: `lq.xyz` leaves W untouched.",
  sq: "Store a float register to VU data memory. Takes a mask: `sq.xyz` writes three words and leaves W alone - which is how the fog byte an isw.w put there survives.",
  ilw: "Load one 32-bit word into an integer register; the mask picks the field (`ilw.w n, 0(buffer)`).",
  isw: "Store an integer register into one word; the mask picks the field.",
  iadd: "Integer add. VU integer registers are 16 bit and wrap.",
  iaddi: "Integer add of a signed 5-bit immediate - this is what a `-3` loop decrement assembles to.",
  iaddiu: "Integer add of an UNSIGNED 15-bit immediate. `iaddiu x, VI01, 0x7FFF` wrapping to 0x8000 is the ADC-bit trick.",
  isub: "Integer subtract.",
  iand: "Integer bitwise and.",
  ior: "Integer bitwise or.",
  b: "Unconditional branch. In VCL you write the logical branch; VCL fills the delay slot.",
  ibeq: "Branch if two integer registers are equal.",
  ibne: "Branch if they differ - the usual loop-again test.",
  iblez: "Branch if <= 0.",
  ibgez: "Branch if >= 0.",
  ibgtz: "Branch if > 0.",
  ibltz: "Branch if < 0.",
  xtop: "Read the base of the double-buffer half the VIF just filled. Every per-buffer program starts here.",
  xitop: "Read the ITOP value the VIF set.",
  xgkick:
    "Hand the GIF packet staged at this address to the GS. The program's output; everything before it was building the packet.",
  waitq: "Wait for a pending div/rsqrt to land in Q. VCL normally handles this for you.",
  nop: "Do nothing.",
};

// Registers and the conventions that are not obvious from the name.
const REGISTERS = {
  vf00: "Hardwired to (0, 0, 0, 1). A very large share of the arithmetic here leans on that W - `vf00[w]` is how you get a 1.0 and `vf00[x]` a 0.0.",
  vi00: "Hardwired to 0.",
  VI01: "The flag-result register: fcand and friends write their answer here.",
  acc: "The accumulator, written by mula/madda and read by madd/msub.",
  q: "Result of div/rsqrt. One in flight at a time - overwriting it before it is read loses the previous result.",
  i: "The I register, loaded by loi.",
};

const DIRECTIVES = {
  ".syntax new": "Use VCL's modern syntax. Always present in this engine.",
  ".name": "The linker symbol stem. The EE side declares `<name>_CodeStart` / `_CodeEnd` and must match exactly.",
  ".vu": "This file is VU code.",
  ".init_vf_all": "Let VCL allocate every float register - which is what lets a program use readable names instead of vf07.",
  ".init_vi_all": "Same for the integer registers. VU1 has 16, and running out is invisible on the host simulator.",
  "--enter": "Start of the program entry block.",
  "--exit": "End of the program; VCL adds the E bit here.",
  "--cont": "The double-buffer continuation point.",
  "--barrier": "Do not schedule across this point. The engine's programs need one after XGKICK for VCL to emit the E bit.",
};

// ---- Catalogue read from the workspace --------------------------------------

/** { macros: {name: {params, file, line, body}}, defines: {name: {value, doc}} } */
let catalogue = { macros: {}, defines: {} };

const ENGINE_GLOBS = [
  "vendor/tyra/engine/src/renderer/3d/pipeline/shared/*.i",
  "vendor/tyra/engine/inc/renderer/3d/pipeline/**/*_shared_defines.h",
];

async function loadCatalogue() {
  const next = { macros: {}, defines: {} };
  for (const glob of ENGINE_GLOBS) {
    let uris = [];
    try {
      uris = await vscode.workspace.findFiles(glob, "**/node_modules/**", 64);
    } catch (e) {
      continue;
    }
    for (const uri of uris) {
      let text = "";
      try {
        text = Buffer.from(await vscode.workspace.fs.readFile(uri)).toString("utf8");
      } catch (e) {
        continue;
      }
      collect(text, uri, next);
    }
  }
  catalogue = next;
  return catalogue;
}

/** Pulls `#macro Name: a, b` (with the comment block above it as documentation)
 * and `#define NAME value` out of one file. Exported so the offline harness can
 * check it against a real engine tree. */
function collect(text, uri, into) {
  const lines = text.split(/\r?\n/);
  const target = into || catalogue;
  let pending = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const comment = line.match(/^\s*(?:;\/\/|;|\/\/)\s?(.*)$/);
    if (comment) {
      const t = comment[1].replace(/^[-\s]+$/, "");
      if (t.trim()) pending.push(t);
      continue;
    }
    const mac = line.match(/^\s*#macro\s+([A-Za-z_][A-Za-z0-9_]*)\s*:?\s*(.*)$/);
    if (mac) {
      const params = mac[2]
        .split(",")
        .map((p) => p.trim())
        .filter(Boolean);
      const body = [];
      for (let j = i + 1; j < lines.length && !/^\s*#endmacro/.test(lines[j]); j++)
        body.push(lines[j]);
      target.macros[mac[1]] = {
        params,
        doc: pending.join("\n"),
        file: uri ? uri.fsPath : "",
        line: i,
        body,
      };
      pending = [];
      continue;
    }
    const def = line.match(/^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*)$/);
    if (def) {
      target.defines[def[1]] = { value: def[2].trim(), doc: pending.join("\n") };
      pending = [];
      continue;
    }
    if (line.trim()) pending = [];
  }
  return target;
}

// ---- Providers --------------------------------------------------------------

function wordAt(doc, position) {
  const range = doc.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*(\.[xyzw]{1,4})?/);
  return range ? { text: doc.getText(range), range } : null;
}

function provideHover(doc, position) {
  const w = wordAt(doc, position);
  if (!w) return undefined;
  const bare = w.text.split(".")[0];
  const mask = w.text.includes(".") ? w.text.split(".")[1] : "";

  const macro = catalogue.macros[w.text] || catalogue.macros[bare];
  if (macro) {
    const md = new vscode.MarkdownString();
    md.appendCodeblock(
      `#macro ${bare}${macro.params.length ? ": " + macro.params.join(", ") : ""}`,
      "tyrax-vu"
    );
    if (macro.doc) md.appendMarkdown("\n" + macro.doc + "\n");
    if (macro.body.length)
      md.appendMarkdown(
        `\n*Expands to ${macro.body.filter((l) => l.trim()).length} instruction(s).*`
      );
    md.appendMarkdown("\n\n*Macros do not nest - a call inside a macro body reaches the assembler verbatim.*");
    return new vscode.Hover(md, w.range);
  }

  const def = catalogue.defines[bare];
  if (def) {
    const md = new vscode.MarkdownString();
    md.appendCodeblock(`#define ${bare} ${def.value}`, "c");
    if (def.doc) md.appendMarkdown("\n" + def.doc);
    return new vscode.Hover(md, w.range);
  }

  const reg = REGISTERS[bare] || REGISTERS[bare.toUpperCase()] || REGISTERS[bare.toLowerCase()];
  if (reg) return new vscode.Hover(new vscode.MarkdownString(`**${bare}** — ${reg}`), w.range);

  const op = OPCODES[bare.toLowerCase()];
  if (op) {
    const md = new vscode.MarkdownString();
    md.appendMarkdown(`**${bare.toLowerCase()}**${mask ? " `." + mask + "`" : ""} — ${op}`);
    if (mask)
      md.appendMarkdown(
        `\n\nThe \`.${mask}\` suffix is the destination mask: only those fields are written, the rest keep their previous value.`
      );
    return new vscode.Hover(md, w.range);
  }
  return undefined;
}

function provideCompletions(doc, position) {
  const line = doc.lineAt(position.line).text.slice(0, position.character);
  const items = [];

  // At the start of a line you are naming an instruction or calling a macro.
  const atLineStart = /^\s*[A-Za-z_]*$/.test(line);

  if (atLineStart) {
    for (const [name, doc1] of Object.entries(OPCODES)) {
      const it = new vscode.CompletionItem(name, vscode.CompletionItemKind.Keyword);
      it.detail = "VU1 instruction";
      it.documentation = new vscode.MarkdownString(doc1);
      items.push(it);
    }
    for (const [name, m] of Object.entries(catalogue.macros)) {
      const it = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
      it.detail = `Tyra macro (${m.params.length} arg${m.params.length === 1 ? "" : "s"})`;
      it.documentation = new vscode.MarkdownString(m.doc || "");
      const args = m.params.map((p, i) => `\${${i + 1}:${p}}`).join(", ");
      it.insertText = new vscode.SnippetString(`${name}{ ${args} }`);
      items.push(it);
    }
  }

  // Anywhere: the address constants and the registers.
  for (const [name, d] of Object.entries(catalogue.defines)) {
    const it = new vscode.CompletionItem(name, vscode.CompletionItemKind.Constant);
    it.detail = `= ${d.value}`;
    it.documentation = new vscode.MarkdownString(d.doc || "");
    items.push(it);
  }
  for (const [name, d] of Object.entries(REGISTERS)) {
    const it = new vscode.CompletionItem(name, vscode.CompletionItemKind.Variable);
    it.detail = "VU register";
    it.documentation = new vscode.MarkdownString(d);
    items.push(it);
  }
  return items;
}

// ---- Diagnostics ------------------------------------------------------------
//
// Deliberately few, and only things the assembler will NOT tell you. A wrong
// opcode is dvp-as's job; these are the traps this codebase has actually been
// bitten by.

function refreshDiagnostics(doc, collection) {
  if (doc.languageId !== LANG) return;
  const out = [];
  const lines = doc.getText().split(/\r?\n/);
  let qPendingLine = -1;

  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const line = raw.replace(/;.*$/, "").replace(/\/\/.*$/, "");
    if (!line.trim()) continue;

    // A div/rsqrt whose Q nothing consumed before the next one. The single
    // hazard that is a programmer mistake rather than a scheduling detail, and
    // the one CalculateTyraEnvStq warns about in prose.
    const writesQ = /^\s*(div|rsqrt|sqrt)\b/i.test(line);
    const readsQ = /\bq\b/i.test(line) && !writesQ;
    if (readsQ) qPendingLine = -1;
    if (writesQ) {
      if (qPendingLine >= 0) {
        out.push(
          new vscode.Diagnostic(
            new vscode.Range(i, 0, i, raw.length),
            "Q is overwritten before anything read it - the previous div/rsqrt result is lost. " +
              "A normalize (rsqrt) must come BEFORE the perspective divide, not after.",
            vscode.DiagnosticSeverity.Warning
          )
        );
      }
      qPendingLine = i;
    }

    // A macro call inside a macro body: vclpp does not expand nested macros, so
    // the line reaches the assembler verbatim.
    if (/^\s*#macro\b/.test(line)) {
      for (let j = i + 1; j < lines.length && !/^\s*#endmacro/.test(lines[j]); j++) {
        const body = lines[j].replace(/;.*$/, "");
        if (/\b[A-Z][A-Za-z0-9_]*\s*\{/.test(body))
          out.push(
            new vscode.Diagnostic(
              new vscode.Range(j, 0, j, lines[j].length),
              "Macros do not nest: vclpp leaves this call unexpanded and it reaches the assembler as-is. " +
                "Inline the instructions instead (the clip programs do).",
              vscode.DiagnosticSeverity.Warning
            )
          );
      }
    }
  }
  collection.set(doc.uri, out);
}

module.exports = {
  LANG,
  OPCODES,
  REGISTERS,
  DIRECTIVES,
  collect,
  loadCatalogue,
  provideHover,
  provideCompletions,
  refreshDiagnostics,
  get catalogue() {
    return catalogue;
  },
};
