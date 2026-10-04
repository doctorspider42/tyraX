// Language features for a PROJECT'S OWN VU PROGRAM - `src/vu/*.cpp`.
//
// That file is where a VU1 program is actually WRITTEN (docs/vu-authoring.md).
// It is ordinary C++, so the `.vclpp` support in vu.js does not apply to it at
// all - and `.vclpp` is the generated OUTPUT, which is the one thing nobody
// types.
//
// WHAT THIS DELIBERATELY DOES NOT DO: repeat the headers. Once `vugen/` is on
// the project's includePath, cpptools already hovers every `vu::Ctx` and
// `vugen::Vu` member with its declaration and its doc comment, straight out of
// `vugen/vushader.hpp`. Copying those descriptions here would be a second
// source of truth for exactly the thing the VU framework exists to stop
// duplicating - and it would rot the first time a method changed.
//
// What is left is the two things a header CANNOT say:
//
//   1. Rules that hold BETWEEN declarations. `movesGeometry()` is a promise
//      about `slot()`; `classes()` is constrained by `movesGeometry()`. No
//      single doc comment can check that, and each of these rules was learned
//      from a Docker build or a console run - which is minutes and a TV away
//      from the person typing.
//   2. What a builder call becomes on the machine. `b.mulInto` is `mul`,
//      `b.sineApprox` is seventeen instructions, `b.divQ` writes Q. The
//      instruction catalogue for that already exists in vu.js and is reused
//      here rather than restated.
//
// Plain JS, no build step, same as the rest of this extension.
"use strict";

let vscode;
try {
  vscode = require("vscode");
} catch (e) {
  // Same stub shape vu.js uses, so the pure logic runs under a plain node
  // harness - which is how the diagnostics below are checked against the real
  // examples/vu-lab scripts without launching VS Code.
  vscode = {
    MarkdownString: class {
      constructor(v) { this.value = v || ""; }
      appendMarkdown(v) { this.value += v; return this; }
      appendCodeblock(v) { this.value += "\n" + v + "\n"; return this; }
    },
    Hover: class { constructor(c, r) { this.contents = c; this.range = r; } },
    CompletionItem: class { constructor(l, k) { this.label = l; this.kind = k; } },
    CompletionItemKind: { Snippet: 14, Keyword: 0 },
    SnippetString: class { constructor(v) { this.value = v; } },
    Diagnostic: class { constructor(r, m, s) { this.range = r; this.message = m; this.severity = s; } },
    DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2 },
    Range: class { constructor(a, b, c, d) { this.a = a; this.b = b; this.c = c; this.d = d; } },
  };
}

const vu = require("./vu");

/** The document this applies to: a C++ file under a project's `src/vu`. Scoped
 * by PATH and not by language, because every other .cpp in the project is
 * ordinary game code and none of the rules below hold for it. */
const SELECTOR = [{ language: "cpp", scheme: "file", pattern: "**/src/vu/*.cpp" }];

/** True for a document this module should touch. The selector does the real
 * filtering inside VS Code; this is for the diagnostics pass, which is driven
 * by document events rather than by a provider registration. */
function applies(doc) {
  const p = (doc.uri && (doc.uri.fsPath || doc.uri.path)) || doc.fileName || "";
  return /[\\/]src[\\/]vu[\\/][^\\/]+\.cpp$/.test(p.replace(/\\/g, "/")) ||
         /[\\/]src\/vu\/[^\\/]+\.cpp$/.test(p);
}

// ---- builder call -> the instruction it emits --------------------------------
//
// Only the mapping lives here; the DESCRIPTION comes from vu.js's opcode table,
// which is the same one the .vclpp hover uses. One catalogue, two languages.

const EMITS = {
  add: "add", addInto: "add", sub: "sub", subInto: "sub",
  mul: "mul", mulInto: "mul", mulQ: "mul", mulQInto: "mul",
  mulAcc: "mula", maddAcc: "madda", maddInto: "madd", msubInto: "msub",
  minimum: "mini", minimumInto: "mini", minimumI: "mini", minimumIInto: "mini",
  maximum: "max", maximumInto: "max",
  move: "move", moveInto: "move", absInto: "abs",
  ftoi4: "ftoi4", ftoi4Into: "ftoi4", ftoi0: "ftoi0", ftoi0Into: "ftoi0",
  loadI: "loi", addI: "add",
  divQ: "div", rsqrtQ: "rsqrt",
  clipwInto: "clipw", fcandInto: "fcand",
  iadd: "iadd", iaddInto: "iadd", iaddiu: "iaddiu", iaddiuInto: "iaddiu",
  ior: "ior", iorInto: "ior", iand: "iand", iandInto: "iand",
  isubInto: "isub", mtir: "mtir",
  lq: "lq", lqConst: "lq", sq: "sq", ilw: "ilw", isw: "isw",
  branchIfEq: "ibeq", branchIfNotEq: "ibne", branchIfLez: "iblez",
  branchIfGtz: "ibgtz", branchIfLtz: "ibltz", branch: "b",
  xtop: "xtop", xgkick: "xgkick",
};

/** Composite helpers - not one instruction, and the number is what an author
 * actually needs (the budget, not the mnemonic). */
const COSTS = {
  sineApprox: "**17 instructions**, and it costs the same on one masked field as on three - so ask for one. Phase `0.5` is a sine, `0.75` a cosine; adding pi/2 by hand is NOT a quarter turn in float.",
  transform: "**4 instructions** - the `mula`/`madd` chain that is one matrix multiply.",
  constants: "**8 instructions**, and they belong in `prepare()`. Four literals in one register beat a `loi` per use.",
  spotLight: "**~20 instructions**, on the OBJECT-space vertex - it must come before the MVP multiply.",
  envStq: "**~13 instructions**, and it WRITES Q (an rsqrt), so it has to precede the perspective divide.",
  persCorrect: "the perspective divide. **WRITES Q** - a script must not (see the diagnostics).",
  scaleToGsFormat: "**3 instructions** - NDC to the GS 12.4 screen format.",
  fixColor: "**4 instructions** - clamp to 0..255 and convert to integer bits.",
  truncate: "**2 instructions** - `ftoi0` then `itof0`.",
};

const MASKS = {
  MX: "the **x** field only", MY: "the **y** field only",
  MZ: "the **z** field only", MW: "the **w** field only",
  MXYZ: "**x, y and z** - the usual one for a position, leaving w (the clip-space distance) alone",
  MALL: "**all four** fields",
};

// ---- the rules a header cannot state ----------------------------------------

/** Blank out comments and string literals, KEEPING the offsets, so a rule does
 * not fire on the word `movesGeometry` inside the paragraph explaining it -
 * which every one of the example scripts contains. */
function blankNonCode(text) {
  const out = text.split("");
  let i = 0;
  const n = text.length;
  const blank = (from, to) => {
    for (let k = from; k < to && k < n; k++) if (out[k] !== "\n") out[k] = " ";
  };
  while (i < n) {
    const c = text[i], d = text[i + 1];
    if (c === "/" && d === "/") {
      let j = text.indexOf("\n", i);
      if (j < 0) j = n;
      blank(i, j);
      i = j;
    } else if (c === "/" && d === "*") {
      let j = text.indexOf("*/", i + 2);
      j = j < 0 ? n : j + 2;
      blank(i, j);
      i = j;
    } else if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < n && text[j] !== c) j += text[j] === "\\" ? 2 : 1;
      blank(i, Math.min(j + 1, n));
      i = j + 1;
    } else {
      i++;
    }
  }
  return out.join("");
}

function posOf(text, index) {
  let line = 0, last = 0;
  for (let i = 0; i < index; i++) if (text[i] === "\n") { line++; last = i + 1; }
  return { line, ch: index - last };
}

function rangeAt(text, index, length) {
  const a = posOf(text, index);
  return new vscode.Range(a.line, a.ch, a.line, a.ch + length);
}

/** What the file DECLARES about itself - the overrides the framework reads. */
function readOverrides(code) {
  const one = (re) => {
    const m = re.exec(code);
    return m ? { value: m[1], index: m.index, text: m[0] } : null;
  };
  return {
    slot: one(/\bslot\s*\(\s*\)[^;{]*\{[^}]*?\bSlot::(\w+)/),
    moves: one(/\bmovesGeometry\s*\(\s*\)[^;{]*\{[^}]*?\breturn\s+(true|false)/),
    classes: one(/\bclasses\s*\(\s*\)[^;{]*\{[^}]*?\breturn\s+([^;]+);/),
  };
}

const GEOMETRY_SLOTS = ["ObjectSpace", "ClipSpace"];

function refreshDiagnostics(doc, collection) {
  if (!applies(doc)) return;
  const text = doc.getText();
  const code = blankNonCode(text);
  const out = [];
  const add = (index, length, message, severity) =>
    out.push(new vscode.Diagnostic(rangeAt(text, index, length), message,
                                   severity === undefined ? vscode.DiagnosticSeverity.Warning : severity));

  // 1. A SCRIPT MUST NOT WRITE Q. The build refuses it too (vugen reports it),
  //    but that is a container away; and the host simulator CANNOT show the
  //    symptom, because it runs in order and models no latency. On the console
  //    it is grey stippled patches and z-fighting on arithmetic that looks
  //    perfect.
  const qRe = /\b(divQ|rsqrtQ|persCorrect|envStq)\s*\(/g;
  for (let m; (m = qRe.exec(code)); )
    add(m.index, m[1].length,
        "A script must not write Q. Q carries the perspective divide and has a latency the " +
        "assembler schedules around, so this gets moved into that window and the vertex lands " +
        "in the wrong place - grey stippled patches on the console, with nothing wrong on the " +
        "host. Compute it without a divide (docs/vu-authoring.md).",
        vscode.DiagnosticSeverity.Error);

  // 2. There are FOUR scratch registers. scratch() clamps rather than handing
  //    back an unnamed register, so the failure is aliasing instead of silence
  //    - but it is still not what was written.
  const sRe = /\bscratch\s*\(\s*(\d+)\s*\)/g;
  for (let m; (m = sRe.exec(code)); ) {
    const i = parseInt(m[1], 10);
    if (i > 3)
      add(m.index, m[0].length,
          `There are four scratch registers (vu::Ctx::kScratchCount), so ${i} clamps to 3 and ` +
          "silently aliases another value. VF pressure is invisible on the host - a fifth " +
          "register is not free, it is someone else's.");
  }

  const ov = readOverrides(code);

  // 3. A geometry slot is a PROMISE the game has to keep. Without the flag the
  //    EE clipper cuts the mesh before this program runs and a vertex moved
  //    afterwards is moved past a cut computed without it.
  if (ov.slot && GEOMETRY_SLOTS.indexOf(ov.slot.value) >= 0 &&
      (!ov.moves || ov.moves.value !== "true"))
    add(ov.slot.index, ov.slot.text.length,
        `Slot::${ov.slot.value} moves geometry, so this program should override ` +
        "`bool movesGeometry() const override { return true; }`. Without it the EE clipper " +
        "cuts the mesh BEFORE the program runs and every prop touching the edge of the screen " +
        "tears (under VU1 clipping the clip program handles it, but the flag still has to be " +
        "right for the other mode).");

  // 4. Ndc does not need it, and declaring it is not free: the game starts
  //    submitting its props unclipped to compensate for a problem this slot
  //    does not have.
  if (ov.slot && ov.slot.value === "Ndc" && ov.moves && ov.moves.value === "true")
    add(ov.moves.index, ov.moves.text.length,
        "Slot::Ndc does not need movesGeometry(): the divide has already happened and a nudge " +
        "of a few pixels stays inside the guard band. Declaring it makes the game submit its " +
        "props whole for nothing.",
        vscode.DiagnosticSeverity.Information);

  // 5. A DISPLACEMENT MAY NOT CLAIM A SUBSET. An object often draws several
  //    passes over the same vertices in different classes - a reflective ball
  //    has a base pass and a matcap pass, a baked lightmap is a TEXTURED bag
  //    even on an untextured mesh. Move one and not the other and the copies
  //    separate; on the vu-lab ball that was a grey wedge from underneath.
  if (ov.moves && ov.moves.value === "true" && ov.classes &&
      !/\bkAll\b|0x1F/i.test(ov.classes.value))
    add(ov.classes.index, ov.classes.text.length,
        "A program that MOVES geometry has to claim every class (`return vu::kAll;`). An object " +
        "draws several passes over the same vertices in different classes - a reflective ball " +
        "has a base pass and a matcap pass, and a baked lightmap is a TEXTURED bag even on an " +
        "untextured mesh. Displace one and not the other and the copies separate, each showing " +
        "through the other.");

  // 6. The colour and texture slots run AFTER the position has been converted
  //    to the GS 12.4 integer format, so writing it there is not a subtle
  //    mistake - the value being written is not a position any more.
  if (ov.slot && (ov.slot.value === "Color" || ov.slot.value === "Texture")) {
    const wRe = /\b\w+\.position\s*=|Into\s*\(\s*\w+\.position\b/g;
    for (let m; (m = wRe.exec(code)); )
      add(m.index, m[0].length,
          `Writing the position at Slot::${ov.slot.value} does nothing useful: by then it has ` +
          "already been scaled and converted to the GS 12.4 integer format. Move the body to " +
          "Slot::ObjectSpace (the mesh's own units), Slot::ClipSpace or Slot::Ndc.");
  }

  collection.set(doc.uri, out);
}

// ---- hover ------------------------------------------------------------------

function wordAt(doc, position) {
  const line = doc.lineAt(position.line).text;
  let a = position.character, b = position.character;
  while (a > 0 && /[A-Za-z0-9_]/.test(line[a - 1])) a--;
  while (b < line.length && /[A-Za-z0-9_]/.test(line[b])) b++;
  return { word: line.slice(a, b), range: new vscode.Range(position.line, a, position.line, b) };
}

function provideHover(doc, position) {
  if (!applies(doc)) return null;
  const { word, range } = wordAt(doc, position);
  if (!word) return null;
  const md = new vscode.MarkdownString();

  if (COSTS[word]) {
    md.appendMarkdown(`\`Vu::${word}\` — ${COSTS[word]}`);
    return new vscode.Hover(md, range);
  }
  if (EMITS[word]) {
    const op = EMITS[word];
    const desc = vu.OPCODES[op];
    md.appendMarkdown(`\`Vu::${word}\` emits **\`${op}\`**`);
    if (desc) md.appendMarkdown(` — ${desc}`);
    if (/Into$/.test(word))
      md.appendMarkdown(
        "\n\nThe `…Into` form writes an EXISTING register instead of minting one. " +
        "That is not a style choice: VCL allocates 31 VF registers and a value-returning " +
        "call per vertex multiplies the pressure by three, invisibly on the host.");
    return new vscode.Hover(md, range);
  }
  if (MASKS[word]) {
    md.appendMarkdown(`\`vuir::${word}\` — write ${MASKS[word]}.`);
    md.appendMarkdown("\n\nA masked write leaves the other fields of the destination untouched, " +
                      "which is how one register carries several unrelated values at once.");
    return new vscode.Hover(md, range);
  }
  return null;
}

// ---- completion -------------------------------------------------------------

const SCAFFOLD =
  "// ${1:What this program does, and why it is worth the micro memory.}\n" +
  "#include \"vushader.hpp\"\n\n" +
  "namespace {\n\n" +
  "struct ${2:MyLook} : vu::Program {\n" +
  "    const char* name() const override { return \"${3:My look}\"; }\n\n" +
  "    // Every material class this replaces. A DISPLACEMENT must claim them all.\n" +
  "    unsigned classes() const override { return ${4|vu::kColour,vu::kAll|}; }\n\n" +
  "    // Where in the pipeline the body runs.\n" +
  "    vu::Slot slot() const override { return vu::Slot::${5|Color,ObjectSpace,ClipSpace,Ndc,Texture|}; }\n\n" +
  "    // Built ONCE per buffer - constants belong here, not in vertex().\n" +
  "    void prepare(vu::Ctx& c) override { ${6:k_ = vu::splat(c, 1.0F);} }\n" +
  "    vu::Vec k_;\n\n" +
  "    // Once per VERTEX. No divides: a script must not write Q.\n" +
  "    void vertex(vu::Ctx& c) override {\n        $0\n    }\n" +
  "};\n\n" +
  "}  // namespace\n\n" +
  "VU_PROGRAM(${2:MyLook});\n";

/** The scaffold is a WHOLE FILE, so it belongs only where a whole file could
 * start. Offering it everywhere put it in the one place it can never be wanted:
 * the list after `vugen::`, where it sat next to nothing else and read as "this
 * is all the editor knows about vugen" - which is exactly the wrong message
 * when the real answer is that cpptools has not resolved the header yet. A
 * completion provider that fires unconditionally does not just add noise, it
 * MASKS the absence of everything else. */
function scaffoldFits(doc, position) {
  const line = doc.lineAt(position.line).text.slice(0, position.character);
  // Not in a qualified name or a member access.
  if (/(::|\.|->)\s*\w*$/.test(line)) return false;
  // Not inside a string or a comment, and not mid-expression.
  if (/\/\/|["']/.test(line)) return false;
  return /^\s*\w*$/.test(line);
}

function provideCompletions(doc, position) {
  if (!applies(doc)) return [];
  if (!scaffoldFits(doc, position)) return [];
  const item = new vscode.CompletionItem("vuprogram", vscode.CompletionItemKind.Snippet);
  item.detail = "TyraX: a whole vu::Program skeleton";
  item.documentation = new vscode.MarkdownString(
    "The five overrides the framework reads, in the order they matter, with the two rules " +
    "that are easy to get wrong already spelled out: constants go in `prepare()`, and a " +
    "displacement claims every class.");
  item.insertText = new vscode.SnippetString(SCAFFOLD);
  return [item];
}

module.exports = {
  SELECTOR,
  applies,
  blankNonCode,
  readOverrides,
  refreshDiagnostics,
  provideHover,
  provideCompletions,
  scaffoldFits,
  EMITS,
  COSTS,
  MASKS,
};
