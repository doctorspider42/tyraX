#pragma once

#include <map>
#include <string>
#include <vector>

#include "glbparser.hpp"

// Importing animation from ANOTHER model file (docs/animation-import.md).
//
// The problem this solves: a character is rigged once, and its clips arrive
// separately - a Mixamo download is one .fbx per move, each carrying a full
// copy of the skeleton and no useful mesh. Without this, the only way to get
// them onto the character is to merge the files in Blender and re-export.
//
// WHERE THE WORK HAPPENS, and why it is here rather than in fbxparser.cpp.
// The merge operates on the PARSED skeleton (glbparser::Skel), not on a ufbx
// scene, so it is format-agnostic by construction: .fbx clips onto a .glb
// character - the common case, since Blender exports .glb and Mixamo hands out
// .fbx - works with the same code as any other combination, and a second
// format costs nothing here. Host-only, no GL, no ImGui, no project.hpp (the
// treegen/aobake shape), so the whole retarget is exercisable from a small
// harness.
//
// WHAT IT IS NOT. This is a same-skeleton merge, not a full retargeter. Bone
// ROTATIONS are copied verbatim, so the two rigs must agree on their bind
// orientation (two Mixamo rigs do; a Mixamo clip on an arbitrarily authored
// skeleton does not). What makes it safe in practice is the translation
// policy below, not any rotation maths - see `TranslationMode`.
namespace animmerge {

// How a donor's translation channels are treated. The default exists because
// copying them all is what makes a merged clip stretch a character: bone
// lengths live in the translation of each bone, so a donor's arm translation
// applied to a target rebuilds the DONOR's proportions.
enum class TranslationMode {
    // Keep translation only on root bones (the hips) - everywhere else the
    // target's own bind translation stands, which IS its bone lengths. The
    // right answer for essentially every rig, and the default.
    RootBonesOnly = 0,
    // Keep translation only where it actually animates. For rigs that legally
    // translate other bones (a stretchy or IK-baked rig).
    AnimatedOnly = 1,
    // Copy everything, proportions included. For two exports of one rig.
    CopyAll = 2,
};

struct MergeOptions {
    // Explicit donor-bone -> target-bone pairs, authored in the bone-mapping
    // editor (docs/animation-import.md). Consulted BEFORE any name matching,
    // so a hand-made pair always wins - it exists precisely for the bones the
    // name rules get wrong. Donor bones absent from it fall through to the
    // exact/normalized name resolution unchanged.
    std::vector<std::pair<std::string, std::string>> boneMap;
    TranslationMode translation = TranslationMode::RootBonesOnly;
    // Scale channels are noise in most exports and a rig-breaker in a few, so
    // they are dropped by default.
    bool ignoreScale = true;
    // Re-express root-bone motion around the TARGET's rest hip, scaled by the
    // two rigs' hip heights - so a clip authored on a tall rig does not make a
    // short one skate. Off = the donor's raw root positions.
    bool retargetRoot = true;
    // Name matching. "mixamorig:Hips" / "Armature|Hips" -> "Hips", and case is
    // ignored; an EXACT match is always tried first, so a rig that really does
    // have both spellings resolves the literal one.
    bool stripNamespace = true;
    bool caseInsensitive = true;
    // Drop tracks whose target node is not a skinning bone (mesh nodes,
    // cameras, helpers). They cannot deform the character and a donor's mesh
    // node moving the target's mesh node is rarely what anyone wants.
    bool skeletonTracksOnly = true;
};

inline bool operator==(const MergeOptions& a, const MergeOptions& b) {
    return a.boneMap == b.boneMap &&
           a.translation == b.translation && a.ignoreScale == b.ignoreScale &&
           a.retargetRoot == b.retargetRoot &&
           a.stripNamespace == b.stripNamespace &&
           a.caseInsensitive == b.caseInsensitive &&
           a.skeletonTracksOnly == b.skeletonTracksOnly;
}

// One donor file and what to take from it.
struct ImportSpec {
    std::string path;                // absolute path to the donor .fbx/.glb
    std::vector<std::string> clips;  // empty = every clip in the file
    std::string prefix;              // prepended to each imported clip name
    MergeOptions options;
};

// Equality is what lets a per-frame push into the viewport skip the expensive
// re-bake when nothing about the imports changed.
inline bool operator==(const ImportSpec& a, const ImportSpec& b) {
    return a.path == b.path && a.clips == b.clips && a.prefix == b.prefix &&
           a.options == b.options;
}

struct MergeReport {
    int clipsAdded = 0;
    int tracksMatched = 0;
    int tracksDropped = 0;
    int translationStripped = 0;
    int scaleStripped = 0;
    int rootTracksRetargeted = 0;
    float rootMotionScale = 1.0f;
    // Donor bones with no target counterpart, deduped and capped - the list a
    // user needs to see when a merge comes out empty or partial.
    std::vector<std::string> unmatched;
    std::vector<std::string> addedClips;
};
inline constexpr int kMaxReportedUnmatched = 24;

// Fraction of the donor's ANIMATED bones that resolve to a target bone: the
// "will this work" number, shown before anything is imported. 1.0 means every
// animated bone has a home; a rig mismatch shows up as a low fraction rather
// than as a character folded inside out.
float compatibility(const glbparser::Skel& target, const glbparser::Skel& donor,
                    const MergeOptions& options);

// ---------------------------------------------------------------------------
// The bone-mapping editor's backend (Tools > Animation Editor > Map bones).

// The name a donor bone lands on, through the SAME chain the merge uses -
// explicit boneMap first, then exact, then normalized names. -1 = unmatched.
// The mapping editor colours its skeleton view with this, so what it shows
// green is by construction what the merge will match.
int resolveBoneName(const glbparser::Skel& target, const std::string& donorName,
                    const MergeOptions& options);

// A heuristic guess for each donor BONE the name chain leaves unmatched:
// names are cut into tokens (camelCase, separators), side words collapse
// (left -> l, right -> r), and candidates score by token overlap - so
// "mixamorig:LeftUpLeg" finds "UpperLeg_L". Greedy one-to-one by score;
// only pairs scoring >= 0.5 are returned, best first.
//
// Suggestions are NEVER applied by the merge itself - they are offered in the
// mapping editor for a person to confirm, because a fuzzy guess that is wrong
// bends the wrong limb, silently. What the merge consults is the explicit
// boneMap the user accepted.
struct BoneSuggestion {
    std::string donor;
    std::string target;
    float score = 0.0f;  // 0..1
};
// `aliases` (optional): the user's own past decisions - canonicalBoneKey of
// a donor bone -> the target bone NAME it was hand-mapped to before. A hit
// nearly maxes the name signal, which is what makes the same rename resolve
// itself in the next file from the same pack.
std::vector<BoneSuggestion> suggestBoneMap(
    const glbparser::Skel& target, const glbparser::Skel& donor,
    const MergeOptions& options,
    const std::map<std::string, std::string>* aliases = nullptr);

// The alias book's key: namespace stripped, lowercased, tokens canonicalized
// through the synonym table and joined - so "mixamorig:LeftUpLeg" and
// "UpperLeg_L" share a key with themselves across files, not with each other.
std::string canonicalBoneKey(const std::string& name);

// One affix transformation that maps MANY unmatched donor bones onto target
// bones at once - the "two rigs identical modulo a prefix/suffix" case, which
// deserves one reviewable rule instead of thirty fuzzy scores. Either the
// strip pair (donor = prefix + target + suffix) or the add pair (target =
// prefix + donor + suffix) is set, never both.
struct AffixRule {
    std::string stripPrefix, stripSuffix;
    std::string addPrefix, addSuffix;
    int matches = 0;
    // Human-readable form for the button label ("strip 'rigX:'+'X'").
    std::string describe() const;
};
// Finds the most-voted rule over the unmatched donor bones; true when one
// covers at least 3 of them (and at least a third of the unmatched set).
bool detectAffixRule(const glbparser::Skel& target,
                     const glbparser::Skel& donor, const MergeOptions& options,
                     AffixRule& out);
// The pairs that rule produces (only where the transformed name IS a free
// target bone) - handed to the UI as ready-to-accept pairs.
std::vector<std::pair<std::string, std::string>> applyAffixRule(
    const glbparser::Skel& target, const glbparser::Skel& donor,
    const MergeOptions& options, const AffixRule& rule);

// The mapper's "test pose": donor posed at `t` of its clip `donorClip`, and
// the TARGET posed by borrowing the mapped donor rotations - exactly what the
// merge would bake (default policy: rotations travel, the target keeps its
// own translations, the root is retargeted by hip height). A wrong mapping
// shows as a folded limb long before any percentage says so. Outputs are
// global joint positions, 3 floats per node, ready for the canvas.
void posedPreview(const glbparser::Skel& target, const glbparser::Skel& donor,
                  const MergeOptions& options, int donorClip, float t,
                  std::vector<float>& donorXyz, std::vector<float>& targetXyz);

// --- the AI assist (docs/animation-import.md, "Ask AI") --------------------
// Everything a model needs to map the rigs, as one text block: both bone
// hierarchies with depths and normalized bind positions, the pairs already
// resolved (context), the unmatched list (the task), and the output contract
// ({"pairs":[{"s":..,"t":..}]}). Pure text from parsed data, so the harness
// can check it without any backend.
std::string aiMapPrompt(const glbparser::Skel& target,
                        const glbparser::Skel& donor,
                        const MergeOptions& options);
// The reply parsed and VALIDATED: only pairs whose donor name exists and
// whose target names a real target bone survive, one per donor. Lenient
// about fences and prose the way every model-reply parser here is.
std::vector<std::pair<std::string, std::string>> parseAiBoneMap(
    const std::string& reply, const glbparser::Skel& target,
    const glbparser::Skel& donor);

// Bind-pose global position of every node (3 floats each) - what the mapping
// editor projects into its 2D skeleton view.
void bindGlobals(const glbparser::Skel& skel, std::vector<float>& xyz);

// ---------------------------------------------------------------------------
// Parsed-skeleton cache. Parsing is the expensive half of everything above -
// a big .fbx costs seconds in ufbx - and before this existed every consumer
// (the panel's probe, the mapping editor, the model info, the preview) parsed
// the same files again, so one click could stall the editor three parses
// deep. One instance lives on the App and every UI-thread consumer goes
// through it; entries revalidate by file size + mtime, so a re-imported
// asset is picked up without anyone clearing anything.
//
// NOT thread-safe by design - the viewport's background bake parses without
// it rather than putting a mutex on the UI thread's hot path.
class SkelCache {
   public:
    // Parsed skeleton for `path`, or nullptr with `error` set. The pointer is
    // valid until the next get() - copy the Skel if it must outlive that.
    const glbparser::Skel* get(const std::string& path, std::string& error);

   private:
    struct Entry {
        long long size = -1;
        long long mtime = 0;  // file_clock ticks - compared, never interpreted
        bool ok = false;
        std::string error;
        glbparser::Skel skel;
    };
    std::map<std::string, Entry> entries_;
};

// Appends `donor`'s selected clips to `target`, rebound onto the target's own
// nodes. A clip whose name is taken gains a "_1" suffix, so importing twice
// cannot silently replace anything. Returns false only when nothing at all was
// merged (with `error` set); a partially matched clip is a success with a
// populated report.
bool merge(glbparser::Skel& target, const glbparser::Skel& donor,
           const ImportSpec& spec, MergeReport& report, std::string& error);

// Recomputes Skel::min/max as the union over EVERY clip, sampled sparsely -
// the same conservative box parseSkel builds from a file's own clips.
//
// This is not housekeeping: those bounds are what the console frustum-culls
// and box-collides the model with, and they were computed from the target's
// own clips alone. An imported clip that reaches further - a jump, a lunge, a
// clip that travels - would otherwise be culled while still on screen.
void refreshPoseBounds(glbparser::Skel& skel);

// Loads each donor and merges it into `target`, in order; returns true when
// anything landed. Failures are reported into `warnings` and skipped rather
// than failing the build - a missing donor file must not stop a game from
// being built, and the warning names the file.
//
// It does NOT refresh the pose bounds: that is a second full skinning pass,
// and only the .tskl bake needs it (the preview re-skins every frame anyway).
// Call refreshPoseBounds yourself when the result is going to the console.
// `cache` (optional) supplies parsed donors without re-reading their files -
// pass the App's on the UI thread, nullptr from a worker.
bool applyImports(const std::vector<ImportSpec>& imports,
                  glbparser::Skel& target,
                  std::vector<std::string>* warnings,
                  SkelCache* cache = nullptr);

// ---------------------------------------------------------------------------
// The preview half.
//
// Everything that DRAWS an animated model in the editor consumes morph frames
// (glbparser::Baked), which the format parsers produce by posing the source
// scene - a path that by definition knows nothing about clips merged in
// afterwards. So imported clips are baked from the merged SKELETON instead:
// evaluate the channels, skin the bind-pose mesh, emit the same frames.
//
// That the Skel is also exactly what ships makes this preview MORE faithful
// than the parser's own bake, not less - it is the .tskl being drawn.
bool skelToBaked(const glbparser::Skel& skel, float fps, glbparser::Baked& out,
                 std::string& error);

// What every preview site calls: parse `modelPath`, fold in `imports`, and
// return the morph-frame bake.
//
// With no imports this is a straight `animimport::bake` - the long-standing,
// proven path, byte for byte - so a model nobody has imported into cannot be
// affected by any of the above. Only a model that HAS imports goes through
// parseSkel + merge + skelToBaked.
bool bakedWithImports(const std::string& modelPath,
                      const std::vector<ImportSpec>& imports, float fps,
                      glbparser::Baked& out, std::string& error,
                      SkelCache* cache = nullptr);

// The MERGED skeleton alone - every parse cache-assisted, no skinning. What
// the model-info path wants: clip names, bounds and materials are all here,
// and the morph-frame bake it used to pay for is the single most expensive
// thing the editor can do to a model.
bool mergedSkel(const std::string& modelPath,
                const std::vector<ImportSpec>& imports, glbparser::Skel& out,
                std::string& error, SkelCache* cache = nullptr);

}  // namespace animmerge
