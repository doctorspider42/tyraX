#pragma once

#include <string>
#include <vector>

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

// The skeleton half of a take, without any frames: joint names, the tree, and a
// rest pose already decomposed into position + rotation. This is what the LIVE
// link receives once when a phone connects - a stream cannot afford to resend
// it - and what `load` builds internally from the file's matrices.
//
// Sharing it is the point: a pose arriving over a socket and a pose read out of
// a file become the SAME source Skel, so charanim::prepareLive cannot tell them
// apart and neither can anything downstream. `restPos` is jointCount * 3,
// `restRot` jointCount * 4 (x, y, z, w).
bool buildSource(const std::vector<std::string>& jointNames, const std::vector<int>& parents,
                 const float* restPos, const float* restRot, glbparser::Skel& out,
                 std::string& error);

// ARKit's name for a joint -> the Mixamo name the generated rig uses, or null
// when this rig has no bone for it (fingers, face, toes past the ball). The
// phone streams ARKit's own names, so the translation lives here, once.
const char* mixamoName(const std::string& arkitJoint);

// Writes a take - the editor's half of the format the phone app writes on
// device. Recording a LIVE stream produces one of these rather than a clip
// straight onto a character, so a session leaves a reusable artefact that the
// Character Generator imports like any other take, and so there is exactly one
// path from motion to animation.
//
// `restPos`/`restRot` are the skeleton's rest pose (joints*3, joints*4).
// `times` is one entry per frame; `rot` is frames*joints*4 and `hips`
// frames*3. Per-joint translation comes from the rest pose - a body's bones do
// not change length mid-take, and the reader decomposes what it is given.
bool writeTake(const std::string& path, const std::vector<std::string>& jointNames,
               const std::vector<int>& parents, const float* restPos, const float* restRot,
               const std::vector<float>& times, const std::vector<float>& rot,
               const std::vector<float>& hips, std::string& error);

}  // namespace mocap
