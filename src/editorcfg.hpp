// The machine-global editor config, for code that must not pull in the GUI.
//
// editor.ini (%LOCALAPPDATA%\tyra-editor\editor.ini) is parsed in app.cpp - it
// is the App's own state - but two things in it are useful outside a window:
// where the file is, and which projects were opened most recently. The CLI's
// --debug-state uses them to answer "what is this machine working on right
// now" without a project path being typed (see docs/devkit.md).
#pragma once

#include <string>
#include <vector>

namespace editorcfg {

/** Absolute path of editor.ini. Empty if the environment has no home. */
std::string configPath();

/** Project folders from editor.ini, MOST RECENT FIRST. The list is rewritten
 * the moment a project is opened, so entry 0 is the last project opened on
 * this machine - by whichever editor instance opened it last. */
std::vector<std::string> recentProjects();

/** Where "New project" proposes to put things: the editor.ini setting, or
 * ~/TyraProjects when it is unset. Projects made there but never opened by
 * this installation are missing from recentProjects(), so a search for live
 * work has to look here too. */
std::string defaultProjectsDir();

}  // namespace editorcfg
