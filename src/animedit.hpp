#pragma once

#include <string>
#include <vector>

#include "glbparser.hpp"
#include "project.hpp"

// Non-destructive animation-clip editing (Tools > Animation Editor).
//
// The source .glb/.fbx is never rewritten. The user's per-clip edits live in
// Project::animClipEdits and are folded into the parsed skeleton on its way
// into the .tskl at build time (templates.cpp bakeAnimAssets), so the console
// receives clips that are already trimmed, retimed and renamed and pays
// nothing at runtime. The editor's viewport preview applies the same numbers
// through the sampling helpers below, which is what keeps "what you scrub is
// what ships" true - change the math here and both sides move together.
//
// Time is in SECONDS everywhere (glTF/FBX keyframes are seconds; the .tskl
// stores seconds; SkelInstance::advance takes seconds). "Source seconds" mean
// positions in the clip as authored - trim values are always source seconds,
// never post-scale ones, so changing the speed never moves the trim handles.
namespace animedit {

// Project-wide playback SPEED multiplier from the animation-fps pair:
// animPlayFps / animSourceFps. A clip of N frames exported from a 24 fps
// Blender scene arrives N/24 seconds long; if it was authored for 30 fps it
// should run N/30, i.e. 30/24 = 1.25x faster. Equal values give exactly 1.0f.
// Every multiplier in this header is a speed, so an output duration is always
// the input duration DIVIDED by it.
float projectTimeScale(const ProjectSettings& st);

// The user's entry for one source clip of one model, or nullptr when the clip
// has never been touched (which means "bake it exactly as authored").
const AnimClipEdit* findEdit(const Project& p, const std::string& modelRel,
                             const std::string& sourceClip);

// Total playback multiplier for a source clip: the project fps ratio times
// the clip's own time scale. 2.0 = plays twice as fast / half as long.
float totalTimeScale(const Project& p, const std::string& modelRel,
                     const std::string& sourceClip);

// Resolves an entry's trim into a usable [start, end] window in source
// seconds, clamped to `duration`. A zero/!inverted/degenerate trim yields the
// whole clip, so a bad entry can never produce an empty clip.
void trimWindow(const AnimClipEdit* e, float duration, float& start,
                float& end);

// The name the GAME sees for a source clip (the rename when set, else the
// source name). Every clip name the editor displays or stores in a reference
// - SceneObject::animClip, the Player locomotion fields, the Animation flow
// node's Clip param - is an effective name.
std::string effectiveName(const Project& p, const std::string& modelRel,
                          const std::string& sourceClip);

// The reverse lookup: which source clip an effective name refers to. Falls
// back to the name itself when no entry renames to it, so an unedited project
// resolves every name to itself.
std::string sourceName(const Project& p, const std::string& modelRel,
                       const std::string& effective);

// Applies every edit belonging to `modelRel` to a parsed skeleton, in place:
// per clip, trim (inserting interpolated boundary keys and rebasing to 0),
// then scale time, then rename. Clips with no entry still get the project fps
// ratio applied. Safe to call on a model with no edits at all.
void applyClipEdits(const Project& p, const std::string& modelRel,
                    glbparser::Skel& skel);

}  // namespace animedit
