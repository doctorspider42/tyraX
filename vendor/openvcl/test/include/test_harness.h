// Minimal test harness for openvcl.
//
// Auto-registers each TEST_CASE via a static initializer; the runner in
// test_main.cpp iterates the registry and reports per-case status plus a
// final summary.  Exit status is 0 if all assertions pass, 1 otherwise.
//
// Why not doctest / Catch2 / GoogleTest?  doctest tripped over a known
// libc++ + AppleClang 14 issue around 'using_if_exists' on the C math
// functions; the others add build-system complexity we don't need yet.
// A handful of macros plus auto-registration is enough for the kind of
// deterministic unit testing openvcl needs.

#ifndef OPENVCL_TEST_HARNESS_H
#define OPENVCL_TEST_HARNESS_H

#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace test
{
    struct Case
    {
        const char* name;
        void (*fn)();
    };

    inline std::vector<Case>& registry()
    {
        static std::vector<Case> r;
        return r;
    }

    inline int& failures_in_current_test()
    {
        static int f = 0;
        return f;
    }

    inline int& total_checks()
    {
        static int c = 0;
        return c;
    }

    inline const char*& current_test_name()
    {
        static const char* n = "?";
        return n;
    }

    // When true, the runner treats the current test as a "known-broken" entry:
    // failures inside the test do NOT bump the overall exit code, but a test
    // that unexpectedly passes is flagged so the developer knows to remove the
    // marker (the underlying bug was fixed).
    inline bool& current_test_expects_failure()
    {
        static bool b = false;
        return b;
    }

    struct AutoRegister
    {
        AutoRegister(const char* name, void (*fn)())
        {
            Case c = { name, fn };
            registry().push_back(c);
        }
    };

    inline bool approx_eq(double a, double b, double eps = 1e-6)
    {
        double d = a - b;
        if (d < 0) d = -d;
        return d < eps;
    }
}

#define OPENVCL_TEST_CONCAT_(a, b) a##b
#define OPENVCL_TEST_CONCAT(a, b) OPENVCL_TEST_CONCAT_(a, b)

#define TEST_CASE(name)                                                                    \
    static void OPENVCL_TEST_CONCAT(openvcl_test_fn_, __LINE__)();                         \
    static ::test::AutoRegister OPENVCL_TEST_CONCAT(openvcl_test_reg_, __LINE__)(          \
        name, &OPENVCL_TEST_CONCAT(openvcl_test_fn_, __LINE__));                           \
    static void OPENVCL_TEST_CONCAT(openvcl_test_fn_, __LINE__)()

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++::test::total_checks();                                                          \
        if (!(cond)) {                                                                     \
            ++::test::failures_in_current_test();                                          \
            fprintf(stderr, "  FAIL [%s] %s:%d: CHECK(%s)\n",                              \
                    ::test::current_test_name(), __FILE__, __LINE__, #cond);               \
        }                                                                                  \
    } while (0)

#define REQUIRE(cond)                                                                      \
    do {                                                                                   \
        ++::test::total_checks();                                                          \
        if (!(cond)) {                                                                     \
            ++::test::failures_in_current_test();                                          \
            fprintf(stderr, "  FAIL [%s] %s:%d: REQUIRE(%s)  -- aborting test\n",          \
                    ::test::current_test_name(), __FILE__, __LINE__, #cond);               \
            return;                                                                        \
        }                                                                                  \
    } while (0)

// Mark the current test as "known-broken with reason".  Failures inside this
// test don't fail the run; instead they're reported as expected.  If the test
// somehow passes anyway, the runner flags it so we know to drop the marker.
#define EXPECTED_FAIL(reason)                                                              \
    do {                                                                                   \
        ::test::current_test_expects_failure() = true;                                     \
        fprintf(stderr, "  [known issue] %s: %s\n",                                        \
                ::test::current_test_name(), reason);                                      \
    } while (0)

#define CHECK_APPROX(a, b)                                                                 \
    do {                                                                                   \
        double a_val_ = (double)(a);                                                       \
        double b_val_ = (double)(b);                                                       \
        ++::test::total_checks();                                                          \
        if (!::test::approx_eq(a_val_, b_val_)) {                                          \
            ++::test::failures_in_current_test();                                          \
            fprintf(stderr, "  FAIL [%s] %s:%d: CHECK_APPROX(%s, %s)  got %g vs %g\n",     \
                    ::test::current_test_name(), __FILE__, __LINE__,                       \
                    #a, #b, a_val_, b_val_);                                               \
        }                                                                                  \
    } while (0)

#endif // OPENVCL_TEST_HARNESS_H
