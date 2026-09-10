// Helper for integration tests: spawn ./openvcl as a child process with
// a chosen argv and stdin payload, capture stdout/stderr/exit-code.
//
// Uses fork + execv + a pair of pipes for stdin/stdout.  stderr is merged
// into the captured stderr buffer.  No timeouts or signal handling — the
// child is expected to terminate on its own; if it hangs the test will
// hang.  That's acceptable for a deterministic, hermetic tool like
// openvcl.
//
// The path to the openvcl binary is provided at compile time via the
// OPENVCL_BIN macro (set by CMake) so tests can find the executable
// regardless of the caller's working directory.

#ifndef OPENVCL_TEST_RUNNER_H
#define OPENVCL_TEST_RUNNER_H

#include <string>
#include <vector>

namespace test
{
    struct RunResult
    {
        int          exit_code; // child exit status (0 == EXIT_SUCCESS)
        bool         signaled;  // true if child died on a signal
        int          signal;    // signal number when signaled, else 0
        std::string  stdout_data;
        std::string  stderr_data;
    };

    // Run the openvcl binary with the given arguments and stdin payload.
    // `args` is the full argv vector starting at argv[1] (the binary path
    // itself is filled in by the runner).  Returns the captured output.
    //
    // If launching the binary itself fails (e.g. not built), exit_code is
    // -1 and stderr_data contains a description of the failure.
    RunResult run_openvcl(const std::vector<std::string>& args,
                          const std::string& stdin_data);
}

#endif // OPENVCL_TEST_RUNNER_H
