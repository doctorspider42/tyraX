// Tokenizer unit tests.
//
// The Tokenizer turns a textual Line into a stream of Tokens by:
//   1. stripping comments (`;`, `//`, `/* */`),
//   2. splitting label / mnemonic / fields / bit-flags / arguments,
//   3. matching the mnemonic against the operand-template list,
//   4. queuing the result into tokens().
//
// We feed the Tokenizer a hand-crafted minimal operand list rather than
// pulling in Parser::setupOperands() — the tokenizer is generic over the
// template list, so a synthetic 4-entry list is sufficient to exercise
// every code path tested here.  Errors raised by the tokenizer are
// counted via Error::HasErrors() (reset before each parse).

#include "test_harness.h"

#include "../../src/Tokenizer.h"
#include "../../src/Line.h"
#include "../../src/File.h"
#include "../../src/Operand.h"
#include "../../src/Token.h"
#include "../../src/Error.h"

#include <list>
#include <string>

using namespace vcl;

namespace
{
    // Synthetic operand list — minimal, sufficient to exercise every
    // tokenizer code path in this file.
    std::list<Operand> make_operands()
    {
        std::list<Operand> ops;
        // 0-arg, no flags — exercises bare-mnemonic path.
        ops.push_back(Operand("NOP", 0, 0, "", Operand::INVALID, 1, 4));
        // 0-arg + DEST — lets us test field parsing (`.x`, `.xy`, ...).
        // We give DEST without XYZ so the "must be xyz" check at
        // handleToken doesn't fire.
        ops.push_back(Operand("TEST", 0, Operand::DEST, ""));
        // 1-arg preprocessor with a permissive pattern — used for the
        // case-insensitive lookup test (if openvcl ever switches that)
        // and as a sanity-check of the operand-not-found path.
        ops.push_back(Operand(".global", 1, Operand::PREPROCESSOR, "imm"));
        return ops;
    }

    // Parse `text` as line N=1 of a synthetic file; returns the
    // tokenizer's parse() result.  Resets the global error count
    // beforehand so each test can independently observe Error::HasErrors().
    bool parse_one(Tokenizer& t, const std::string& text)
    {
        static File f("<test>");
        Line line(f, 1, 1, text);
        Error::ResetErrorCount();
        return t.parse(line);
    }
}

// --- defaults & toggles ----------------------------------------------

TEST_CASE("Tokenizer: default availableFloats and availableIntegers are zero")
{
    Tokenizer t;
    CHECK(t.availableFloats()   == 0u);
    CHECK(t.availableIntegers() == 0u);
}

TEST_CASE("Tokenizer: newSyntax setter is read back")
{
    Tokenizer t;
    t.setNewSyntax(true);
    CHECK(t.newSyntax() == true);
    t.setNewSyntax(false);
    CHECK(t.newSyntax() == false);
}

// --- comment and empty handling --------------------------------------

TEST_CASE("Tokenizer: empty line produces no tokens")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    CHECK(parse_one(t, ""));
    CHECK(t.tokens().size() == 0u);
    CHECK(Error::HasErrors() == false);
}

TEST_CASE("Tokenizer: whitespace-only line produces no tokens")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    CHECK(parse_one(t, "    \t   "));
    CHECK(t.tokens().size() == 0u);
    CHECK(Error::HasErrors() == false);
}

TEST_CASE("Tokenizer: semicolon line-comment produces no tokens")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    CHECK(parse_one(t, "; entirely a comment"));
    CHECK(t.tokens().size() == 0u);
    CHECK(Error::HasErrors() == false);
}

TEST_CASE("Tokenizer: double-slash line-comment produces no tokens")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    CHECK(parse_one(t, "// entirely a comment"));
    CHECK(t.tokens().size() == 0u);
    CHECK(Error::HasErrors() == false);
}

// --- basic mnemonic identification -----------------------------------

TEST_CASE("Tokenizer: bare NOP is identified")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "NOP"));
    REQUIRE(t.tokens().size() == 1u);
    const Token& tok = t.tokens().front();
    CHECK(tok.name()  == "NOP");
    CHECK(tok.label() == "");
    CHECK(tok.fields() == 0u);
    CHECK(tok.operand() != 0);
}

TEST_CASE("Tokenizer: surrounding whitespace does not change the mnemonic")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "   NOP   "));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().name() == "NOP");
}

TEST_CASE("Tokenizer: mnemonic lookup is case-insensitive")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    // identifyToken uses casecompare, so a lower-cased "nop" must
    // resolve to the same operand as "NOP".  Sony's VCL convention.
    REQUIRE(parse_one(t, "nop"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().operand() != 0);
    CHECK(Error::HasErrors() == false);
}

TEST_CASE("Tokenizer: unknown mnemonic is rejected")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    CHECK(parse_one(t, "WIDGET") == false);
    CHECK(Error::HasErrors() == true);
    CHECK(t.tokens().size() == 0u);
}

// --- label parsing ---------------------------------------------------

TEST_CASE("Tokenizer: label-only line yields a label-bearing token")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "loop:"));
    // handleToken returns true for label-only tokens so the parser can
    // attach the label to the next instruction.
    REQUIRE(t.tokens().size() == 1u);
    const Token& tok = t.tokens().front();
    CHECK(tok.label() == "loop");
    CHECK(tok.name()  == "");
    CHECK(Error::HasErrors() == false);
}

TEST_CASE("Tokenizer: label and mnemonic on the same line produce one token")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "loop: NOP"));
    REQUIRE(t.tokens().size() == 1u);
    const Token& tok = t.tokens().front();
    CHECK(tok.label() == "loop");
    CHECK(tok.name()  == "NOP");
    CHECK(Error::HasErrors() == false);
}

// --- comments next to instructions -----------------------------------

TEST_CASE("Tokenizer: trailing semicolon comment is dropped")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "NOP ; trailing comment"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().name() == "NOP");
    CHECK(Error::HasErrors() == false);
}

TEST_CASE("Tokenizer: trailing double-slash comment is dropped")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "NOP // trailing comment"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().name() == "NOP");
    CHECK(Error::HasErrors() == false);
}

// --- token accumulation ----------------------------------------------

TEST_CASE("Tokenizer: tokens accumulate across multiple parse calls")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "NOP"));
    REQUIRE(parse_one(t, "NOP"));
    REQUIRE(parse_one(t, "NOP"));
    CHECK(t.tokens().size() == 3u);
}

// --- destination-field parsing ---------------------------------------
//
// Field bits live on the Token; the Token::X / ::Y / ::Z / ::W flags
// are anonymous-enum values matching the mask in Token.h.

TEST_CASE("Tokenizer: single .x field sets only the X bit")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST.x"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().fields() == (unsigned)Token::X);
}

TEST_CASE("Tokenizer: .xy field sets X|Y")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST.xy"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().fields() == (unsigned)(Token::X | Token::Y));
}

TEST_CASE("Tokenizer: .xyzw on a DEST operand normalizes fields to zero")
{
    // Sony VCL convention: an absent destination-field specifier
    // implicitly means "all four lanes", so writing `.xyzw` explicitly
    // is semantically identical.  Token::process collapses the explicit
    // form to fields()==0 so downstream code-gen treats both forms the
    // same.  This test pins that normalization.
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST.xyzw"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().fields() == 0u);
}

TEST_CASE("Tokenizer: partial fields like .xyz are preserved verbatim")
{
    // Only the full X|Y|Z|W case is normalized.  A genuine subset like
    // .xyz keeps the bits the user specified.
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST.xyz"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK(t.tokens().front().fields()
          == (unsigned)(Token::X | Token::Y | Token::Z));
}

TEST_CASE("Tokenizer: unknown field letter is rejected")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    // 'q' is not a valid field letter; identifyToken bails out and the
    // line is rejected.
    CHECK(parse_one(t, "TEST.q") == false);
    CHECK(Error::HasErrors() == true);
}

// --- bit-flag parsing  [E] [D] [T] -----------------------------------

TEST_CASE("Tokenizer: [E] bit-flag sets the E flag on the token")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST[E]"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK((t.tokens().front().flags() & (unsigned)Token::E) != 0u);
}

TEST_CASE("Tokenizer: [D] and [T] each set their respective flags")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST[D]"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK((t.tokens().front().flags() & (unsigned)Token::D) != 0u);

    // Fresh tokenizer for the T case so the tokens list is clean.
    Tokenizer t2;
    std::list<Operand> ops2 = make_operands();
    t2.setOperands(ops2);
    REQUIRE(parse_one(t2, "TEST[T]"));
    REQUIRE(t2.tokens().size() == 1u);
    CHECK((t2.tokens().front().flags() & (unsigned)Token::T) != 0u);
}

TEST_CASE("Tokenizer: combined [ED] sets both E and D")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST[ED]"));
    REQUIRE(t.tokens().size() == 1u);
    unsigned f = t.tokens().front().flags();
    CHECK((f & (unsigned)Token::E) != 0u);
    CHECK((f & (unsigned)Token::D) != 0u);
}

TEST_CASE("Tokenizer: bit-flags are case-insensitive ([e] == [E])")
{
    // identifyToken upper-cases the inner chars before matching, so
    // lower-case bit-flag letters should resolve to the same Token flag.
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    REQUIRE(parse_one(t, "TEST[e]"));
    REQUIRE(t.tokens().size() == 1u);
    CHECK((t.tokens().front().flags() & (unsigned)Token::E) != 0u);
}

TEST_CASE("Tokenizer: unknown bit-flag letter is rejected")
{
    Tokenizer t;
    std::list<Operand> ops = make_operands();
    t.setOperands(ops);
    // 'X' (or any non-E/D/T letter) trips the "Invalid bit-flag" error.
    CHECK(parse_one(t, "TEST[X]") == false);
    CHECK(Error::HasErrors() == true);
}
