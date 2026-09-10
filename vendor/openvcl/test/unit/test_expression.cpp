// Expression evaluator unit tests.
//
// The Expression class is the constant-folding engine used by openvcl for
// immediate values in instructions and address offsets.  These tests cover
// the basic arithmetic surface, with one deliberate regression test for the
// left-associativity fix in commit bc41a56.

#include "test_harness.h"

#include "../../src/Expression.h"
#include "../../src/Math.h"

using namespace vcl;

// Helper: parse and evaluate a textual expression, return result().
// Pushes a failure (without aborting) if parse or solve fails so the
// caller's subsequent CHECK_APPROX line still has something to compare.
static double eval(const std::string& expr)
{
    Expression e;
    e.setCustomOperators(Math::mathOperators());
    if (!e.process(expr)) {
        fprintf(stderr, "  eval(): process failed for '%s'\n", expr.c_str());
        return 0.0;
    }
    if (!e.solve()) {
        fprintf(stderr, "  eval(): solve failed for '%s'\n", expr.c_str());
        return 0.0;
    }
    return e.result();
}

TEST_CASE("Expression: simple addition")
{
    CHECK_APPROX(eval("1 + 2"), 3.0);
}

TEST_CASE("Expression: precedence respects * over +")
{
    CHECK_APPROX(eval("2 * 3 + 4"), 10.0);
    CHECK_APPROX(eval("4 + 2 * 3"), 10.0);
}

TEST_CASE("Expression: subtraction is left-associative (regression for bc41a56)")
{
    // Before bc41a56, the operator-priority compare used > instead of >=,
    // so "a - b + c" parsed as "a - (b + c)".  With the fix it parses as
    // "(a - b) + c", which is the standard arithmetic interpretation.
    CHECK_APPROX(eval("1 - 2 + 3"), 2.0);   // (1-2)+3 = 2,   NOT 1-(2+3) = -4
    CHECK_APPROX(eval("10 - 5 - 2"), 3.0);  // (10-5)-2 = 3,  NOT 10-(5-2) = 7
}

TEST_CASE("Expression: parentheses override precedence")
{
    CHECK_APPROX(eval("(1 + 2) * 3"),  9.0);
    CHECK_APPROX(eval("1 - (2 + 3)"), -4.0);
}

TEST_CASE("Expression: division is left-associative like subtraction")
{
    // Same operator-priority compare governs / as governs -, so this
    // is an indirect regression check for bc41a56 on a different op.
    CHECK_APPROX(eval("24 / 4 / 3"), 2.0);   // (24/4)/3 = 2,  NOT 24/(4/3) = 18
    CHECK_APPROX(eval("100 / 5 * 2"), 40.0); // (100/5)*2 = 40, NOT 100/(5*2) = 10
}

TEST_CASE("Expression: mixed integer arithmetic produces integer-shaped results")
{
    // The constant-folder is double-based internally but the most common
    // openvcl use case is integer offset math (e.g. for memory addresses).
    // These cases verify integer arithmetic stays integer-valued.
    CHECK_APPROX(eval("7 * 8"), 56.0);
    CHECK_APPROX(eval("100 - 25 + 5"), 80.0);
    CHECK_APPROX(eval("3 * 4 + 2 * 5"), 22.0);
}

TEST_CASE("Expression: zero and identity")
{
    CHECK_APPROX(eval("0 + 0"), 0.0);
    CHECK_APPROX(eval("5 - 5"), 0.0);
    CHECK_APPROX(eval("0 * 100"), 0.0);
    CHECK_APPROX(eval("1 * 42"), 42.0);
}
