// Runner for openvcl unit tests.  Iterates the registry that TEST_CASE
// macros populate at static-initialization time, prints per-test status
// and a final summary, returns non-zero exit if anything failed.

#include "test_harness.h"

int main(int /*argc*/, char** /*argv*/)
{
    int passed = 0;
    int failed = 0;
    int known_fail = 0;       // expected-fail tests that failed as expected
    int unexpected_pass = 0;  // expected-fail tests that passed — the bug got fixed!

    printf("[==========] Running %zu test case(s)\n",
           ::test::registry().size());

    for (size_t i = 0; i < ::test::registry().size(); ++i) {
        const ::test::Case& c = ::test::registry()[i];
        ::test::current_test_name() = c.name;
        ::test::current_test_expects_failure() = false;
        int before = ::test::failures_in_current_test();
        printf("[ RUN      ] %s\n", c.name);
        c.fn();
        int delta = ::test::failures_in_current_test() - before;
        bool expects_fail = ::test::current_test_expects_failure();

        if (delta == 0 && !expects_fail) {
            printf("[       OK ] %s\n", c.name);
            ++passed;
        } else if (delta == 0 && expects_fail) {
            // Known-broken test that didn't actually fail — the bug seems
            // to have been fixed.  Flag loudly so the marker gets removed.
            printf("[ XPASS!!  ] %s  (expected-fail test passed; drop EXPECTED_FAIL!)\n",
                   c.name);
            ++unexpected_pass;
            ++failed;
        } else if (delta > 0 && expects_fail) {
            printf("[ XFAIL    ] %s (%d expected failure(s))\n", c.name, delta);
            ++known_fail;
            // Subtract from total failures so the global counter only tracks
            // real failures.
            ::test::failures_in_current_test() = before;
        } else {
            printf("[  FAILED  ] %s (%d failed assertion(s))\n", c.name, delta);
            ++failed;
        }
    }

    printf("[==========] %d passed, %d failed, %d known issue(s), %d assertion(s) total\n",
           passed, failed, known_fail, ::test::total_checks());

    if (unexpected_pass > 0)
        printf("[!] %d test(s) marked EXPECTED_FAIL passed unexpectedly — remove the marker.\n",
               unexpected_pass);

    return failed > 0 ? 1 : 0;
}
