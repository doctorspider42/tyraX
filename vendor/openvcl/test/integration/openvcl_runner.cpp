// POSIX implementation of run_openvcl().  Spawns the openvcl binary, pipes
// the test's stdin data to it, drains stdout and stderr into strings, waits
// for exit.  Not portable to Windows, but openvcl itself is a POSIX-shaped
// tool so that's an acceptable constraint for the harness.

#include "openvcl_runner.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sstream>
#include <vector>

#ifndef OPENVCL_BIN
#error "OPENVCL_BIN must be defined by the test build (path to the openvcl executable)"
#endif

namespace
{
    // Read everything currently readable from fd until EOF, append to out.
    void drain_fd(int fd, std::string& out)
    {
        char buf[4096];
        for (;;) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                return;
            } else if (errno == EINTR) {
                continue;
            } else {
                return; // unrecoverable read error — give up on this fd
            }
        }
    }
}

namespace test
{

RunResult run_openvcl(const std::vector<std::string>& args,
                      const std::string& stdin_data)
{
    RunResult result;
    result.exit_code = -1;
    result.signaled  = false;
    result.signal    = 0;

    // Pipes: one for stdin (parent writes), one for stdout, one for stderr.
    int in_pipe[2]  = { -1, -1 };
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };

    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        result.stderr_data = "pipe() failed";
        return result;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        result.stderr_data = "fork() failed";
        return result;
    }

    if (pid == 0) {
        // Child: hook up the pipes and exec openvcl.
        ::dup2(in_pipe[0],  STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);

        // Close every pipe end we no longer need so the parent can detect EOF.
        ::close(in_pipe[0]);  ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        ::close(err_pipe[0]); ::close(err_pipe[1]);

        // Build argv: binary path + caller-supplied args.
        std::vector<char*> argv;
        std::vector<std::string> owned;
        owned.push_back(OPENVCL_BIN);
        for (size_t i = 0; i < args.size(); ++i)
            owned.push_back(args[i]);
        for (size_t i = 0; i < owned.size(); ++i)
            argv.push_back(const_cast<char*>(owned[i].c_str()));
        argv.push_back(NULL);

        ::execv(OPENVCL_BIN, argv.data());

        // If execv returns, it failed.  Report via stderr (now the pipe) and exit.
        fprintf(stderr, "execv(%s) failed: %s\n", OPENVCL_BIN, strerror(errno));
        _exit(127);
    }

    // Parent: close child-side fds, write stdin payload, drain stdout/stderr.
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    if (!stdin_data.empty()) {
        const char* p = stdin_data.data();
        size_t left = stdin_data.size();
        while (left > 0) {
            ssize_t n = ::write(in_pipe[1], p, left);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
    }
    ::close(in_pipe[1]); // signal EOF to child

    drain_fd(out_pipe[0], result.stdout_data);
    drain_fd(err_pipe[0], result.stderr_data);

    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            result.stderr_data += "\nwaitpid() failed";
            return result;
        }
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.signaled  = true;
        result.signal    = WTERMSIG(status);
        result.exit_code = 128 + result.signal;
    }

    return result;
}

} // namespace test
