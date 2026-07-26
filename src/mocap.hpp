#pragma once

#include <string>

#include "glbparser.hpp"

// Reader for `.tmocap` takes - ARKit body-tracking recordings made by the
// TyraX Mocap phone app (github.com/doctorspider42/tyrax-mocap; the format is
// one page, in that repo's PROTOCOL.md). Host-only, no GL, no Project.
//
// A take arrives as a skeleton + its rest pose + a local transform per joint
// per frame, which is exactly the shape `glbparser::Skel` already holds. So
// this module does the smallest possible job - decode, decompose the matrices,
// rename ARKit's joints to the Mixamo names the rig uses - and then
// `charanim::retarget` moves it onto a character with no idea it came from a
// phone rather than from Mixamo. That is the whole reason retargeting was
// written against bone NAMES and an arbitrary source bind pose.
//
// **Keep the layout in step with the app's PROTOCOL.md** - it lives in two
// repositories, which is a worse place for it than two files, so the reader
// validates every field it can and says which one was wrong.
namespace mocap {

// True when the path looks like a take (by extension only - `load` is what
// actually decides).
bool isTakePath(const std::string& path);

// Decodes a take into a source Skel: one node per joint (named the way the
// generated rig names bones, where a correspondence exists), a bind pose from
// the recording's rest pose, and one clip named after the file. There is no
// mesh - nothing downstream needs one, retargeting reads rotations.
bool load(const std::string& path, glbparser::Skel& out, std::string& error);

}  // namespace mocap
