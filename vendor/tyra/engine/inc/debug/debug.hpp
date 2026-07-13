/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

// Modified by tyra-editor: TyraDebug::trap() no longer takes over the whole
// screen with the kernel debug console on a failed assertion. It keeps writing
// the assertion to the console / host log.txt (the editor's Debug window tails
// that and turns it into a copyable error dialog) and then halts quietly. The
// upstream full-screen dump is gated behind Tyra::Info::drawAssertScreen (off
// by default) for standalone hardware debugging.

#pragma once

#ifdef NDEBUG
#define TYRA_LOG(...) ((void)0)
#define TYRA_WARN(...) ((void)0)
#define TYRA_ERROR(...) ((void)0)
#define TYRA_TRAP(...) ((void)0)
#define TYRA_SOFT_ERROR(...) ((void)0)
#define TYRA_ASSERT(condition, ...) ((void)0)

#else  // IF Debug

#include <stdio.h>
#include <debug.h>
#include <kernel.h>  // SleepThread - quiet halt after an assertion (tyra-editor)
#include <string>
#include <sstream>
#include <fstream>
#include <utility>
#include <memory>

#include "file/file_utils.hpp"
#include "info/info.hpp"

#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\n")
#define TYRA_WARN(...) TyraDebug::writeLines("==WARN: ", ##__VA_ARGS__, "\n")
#define TYRA_ERROR(...) TyraDebug::writeLines("====ERR: ", ##__VA_ARGS__, "\n")
#define TYRA_TRAP(...) TyraDebug::trap(__FILE__, __LINE__, ##__VA_ARGS__)
#define TYRA_BREAKPOINT() TyraDebug::trap(__FILE__, __LINE__, "Breakpoint")
#define TYRA_ASSERT(condition, ...) \
  if (!(condition)) TyraDebug::trap(__FILE__, __LINE__, ##__VA_ARGS__)
// Modified by tyra-editor: a NON-FATAL error. Logs the same delimited block a
// failed assertion does (so the editor's Debug window / error dialog surfaces
// it identically), but returns instead of halting - the caller has already
// recovered (a missing texture -> placeholder, a missing sound/model -> skip),
// so the game keeps running.
#define TYRA_SOFT_ERROR(...) \
  TyraDebug::softError(__FILE__, __LINE__, ##__VA_ARGS__)

class TyraDebug {
 public:
  template <typename Arg, typename... Args>
  static void writeLines(Arg&& arg, Args&&... args) {
    std::stringstream ss;

    ss << std::forward<Arg>(arg);
    using expander = int[];
    (void)expander{0, (void(ss << std::forward<Args>(args)), 0)...};

    if (Tyra::Info::writeLogsToFile) {
      writeInLogFile(&ss);
    } else {
      printf("%s", ss.str().c_str());
    }
  }

  template <typename... Args>
  static void trap(const char* file, int line, Args... args) {
    std::stringstream ss1;
    ss1 << "\n";
    ss1 << "==============  TYRA  ==============\n";
    ss1 << "| Assertion failed!\n";
    ss1 << "|\n";

    if (Tyra::Info::writeLogsToFile) {
      writeInLogFile(&ss1);
    } else {
      printf("%s", ss1.str().c_str());
    }

    writeAssertLines(args...);

    std::stringstream ss2;
    ss2 << "|\n";
    ss2 << "| File : " << file << ":" << line << "\n";
    ss2 << "====================================\n\n";

    if (Tyra::Info::writeLogsToFile) {
      writeInLogFile(&ss2);
    } else {
      printf("%s", ss2.str().c_str());
    }

    // Modified by tyra-editor: the assertion is already on the console / log
    // above. Only seize the screen with the kernel debug console when explicitly
    // opted in (a standalone build on real hardware with nothing watching the
    // console); otherwise halt quietly so the last frame stays on screen and the
    // editor surfaces the error instead of a full-screen dump.
    if (Tyra::Info::drawAssertScreen) {
      init_scr();
      for (;;) {
        scr_setXY(20, 10);
        scr_printf(ss1.str().c_str());
        writeAssertLinesInScreen(args...);
        scr_printf(ss2.str().c_str());
      }
    }
    for (;;) SleepThread();  // dead game - idle the EE, keep the last frame up
  }

  // Modified by tyra-editor: a non-fatal error. Same delimited block as trap()
  // (so the editor parses it the same way) but a distinct header and NO halt /
  // screen takeover - the caller recovered, the game keeps running.
  template <typename... Args>
  static void softError(const char* file, int line, Args... args) {
    std::stringstream ss1;
    ss1 << "\n";
    ss1 << "==============  TYRA  ==============\n";
    ss1 << "| Non-fatal error (game keeps running)!\n";
    ss1 << "|\n";

    if (Tyra::Info::writeLogsToFile) {
      writeInLogFile(&ss1);
    } else {
      printf("%s", ss1.str().c_str());
    }

    writeAssertLines(args...);

    std::stringstream ss2;
    ss2 << "|\n";
    ss2 << "| File : " << file << ":" << line << "\n";
    ss2 << "====================================\n\n";

    if (Tyra::Info::writeLogsToFile) {
      writeInLogFile(&ss2);
    } else {
      printf("%s", ss2.str().c_str());
    }
  }

 private:
  static void writeInLogFile(std::stringstream* ss);

  template <typename Arg, typename... Args>
  static void writeAssertLines(Arg&& arg, Args&&... args) {
    std::stringstream ss;

    ss << "| " << std::forward<Arg>(arg) << "\n";
    using expander = int[];
    (void)expander{
        0, (void(ss << "| " << std::forward<Args>(args) << "\n"), 0)...};

    if (Tyra::Info::writeLogsToFile) {
      writeInLogFile(&ss);
    } else {
      printf("%s", ss.str().c_str());
    }
  }

  template <typename Arg, typename... Args>
  static void writeAssertLinesInScreen(Arg&& arg, Args&&... args) {
    std::stringstream ss;

    ss << "| " << std::forward<Arg>(arg) << "\n";
    using expander = int[];
    (void)expander{
        0, (void(ss << "| " << std::forward<Args>(args) << "\n"), 0)...};

    scr_printf(ss.str().c_str());
  }
};

#endif  // NDEBUG