#pragma once

#include <map>
#include <string>
#include <vector>

#include "charanim.hpp"
#include "glbparser.hpp"

// Procedural character generator (Tools > Character Generator,
// docs/character-generator.md). Host-only, no GL, no Project dependency - the
// treegen/matbake pattern, so the whole thing is exercisable from the
// --chargen command line instead of by clicking the GUI.
//
// Everything it reads is the CHARACTER KIT (resources/chargen-kit.bin), a
// single file built offline by tools/chargen-kit/ from CC0 sources and embedded
// into the editor. Nothing is fetched and nothing is read from disk at runtime.
// The kit holds:
//   - a 1701-vertex game body (MakeHuman's CC0 female1605 topology plus
//     low-poly eyeballs) on a PS2 texture atlas where the face gets the texels
//   - ~500 morph targets (MakeHuman's macro corners, face and body details)
//     already projected onto those vertices, with their effect on the rig
//   - skin weights for the 35-bone Mixamo-named rig
//   - texture layers in the atlas: 18 skins, AO, eyes, 12 eyebrows, lashes
//   - the wardrobe and hair (meshes bound to the body + their textures)
//   - motion-captured clips (Quaternius' CC0 Universal Animation Library)
//     already retargeted onto the rig
// A build blends targets, composes one texture, assembles the parts and
// writes a plain .glb - where the editor's animated-model chain begins.
namespace chargen {

struct Rgb {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

// One worn item: a kit garment id plus how it is dyed.
struct Wear {
    std::string id;
    // The dye. A negative red means "as made": the garment keeps its own
    // colours. Otherwise the garment is RECOLOURED - its shading (luminance
    // against its own average) is kept and the hue comes from here.
    Rgb color{-1, -1, -1};
    Rgb color2{1, 1, 1};   // the pattern's second colour
    int pattern = 0;       // 0 = none, others: see patterns()
    bool operator==(const Wear& o) const {
        return id == o.id && color == o.color && color2 == o.color2 && pattern == o.pattern;
    }
};

struct Params {
    // ---- macro body: MakeHuman's own scales, 0..1 ----
    float gender = 0.5f;   // 0 female .. 1 male
    // 0 = baby (1 year), 0.1875 = child (10), 0.5 = young adult (25), 1 = old (90).
    float age = 0.5f;
    float muscle = 0.5f;   // 0 min .. 0.5 average .. 1 max
    float weight = 0.5f;   // 0 min .. 0.5 average .. 1 max
    // A 3-way mix, normalized inside the generator.
    float african = 1.0f / 3.0f;
    float asian = 1.0f / 3.0f;
    float caucasian = 1.0f / 3.0f;
    // The finished body is scaled to stand this tall, feet on y = 0.
    float heightMeters = 1.75f;
    // How strongly gender reads, on top of MakeHuman's own macro: its average
    // man and woman are quite alike in the face. 0 = MakeHuman as is; at 1 a
    // man gains jaw, brow ridge, chin and neck and a woman fuller lips, a
    // softer jaw and larger eyes. Scaled down for children.
    float dimorphism = 0.6f;
    // Which body the character is built on: 0 crowd (MakeHuman's proxy741,
    // ~1.5k triangles, one for both sexes), 1 standard (female1605 /
    // male1591, ~3.3k), 2 hero (those subdivided over head, torso and limbs,
    // ~9.5k). Every slider, garment and clip works on all three.
    int detail = 1;
    // MakeHuman's breast macro (women; men keep the Bust slider): cup size
    // and firmness, 0..1 with 0.5 = average - the base body.
    float breastSize = 0.5f;
    float breastFirmness = 0.5f;

    // ---- detail sliders: slider id (see sliders()) -> -1..1 ----
    std::map<std::string, float> shape;

    // ---- skin and face paint ----
    float skinTone = 0.0f;    // -1 paler .. +1 darker, on top of the ethnicity mix
    float skinWarmth = 0.0f;  // -1 rosier .. +1 more golden
    float aging = 0.0f;       // extra skin age (wrinkles) on top of the age slider, 0..1
    int brows = 2;            // index into browList(), -1 = none
    float browDensity = 1.0f;
    int lashes = 0;           // index into lashList(), -1 = none
    Rgb hairColor{0.23f, 0.15f, 0.09f};  // hair, brows and stubble
    Rgb eyeColor{0.33f, 0.22f, 0.12f};
    float stubble = 0.0f;     // 0..1
    float lipstick = 0.0f;    // 0..1 strength of lipColor over the lips
    Rgb lipColor{0.65f, 0.12f, 0.16f};
    float eyeShadow = 0.0f;   // 0..1
    Rgb eyeShadowColor{0.25f, 0.18f, 0.30f};
    float blush = 0.0f;       // 0..1
    int textureSize = 256;    // 128 / 256 / 512 (the atlas; see docs for VRAM)

    // ---- outfit ----
    std::vector<Wear> outfit;
    std::string hair;         // kit hair id, "" = bald
    // A hairstyle you modelled (.glb / .obj, on the reference body that
    // exportReferenceBodies writes) and, optionally, its texture (else the
    // model's own). Replaces `hair`. Relative paths resolve against
    // setAssetRoot (the project). docs/character-generator.md, "Your own hair".
    std::string customHair;
    std::string customHairTexture;
    // Clothes you modelled the same way (docs/character-generator.md, "Your
    // own clothes"): each takes its slot from the kit's outfit (a "full" one
    // replaces top and bottom, and the other way round).
    struct CustomWear {
        std::string mesh, texture;
        // full / top / bottom / feet / head / face / hands - or "over": worn
        // over the top (a vest, a jacket), replacing nothing
        std::string slot = "top";
        Rgb color{-1.0f, -1.0f, -1.0f};  // r < 0 = as made, else a recolour
        bool operator==(const CustomWear& o) const {
            return mesh == o.mesh && texture == o.texture && slot == o.slot && color.r == o.color.r &&
                   color.g == o.color.g && color.b == o.color.b;
        }
    };
    std::vector<CustomWear> customWear;
    // In-game creator options: extra hair / head / face item ids built as
    // SEPARATE parts the game can switch (docs/character-generator.md,
    // "In-game character creator"). A worn item in a slot that has options
    // becomes that slot's default choice. Empty = an ordinary character.
    std::vector<std::string> options;
    // The creator's Body row: a second model of the same person in the other
    // sex (altParams), written beside this one as "<name>-alt.glb" - the game
    // swaps between them, loading the other in the background
    // (docs/character-generator.md, "Man or woman").
    bool bodyChoice = false;

    // ---- animation ----
    // Kit clip names to include ("" list = the default locomotion set). The
    // locomotion clips are renamed idle / walk / run / sprint / jump, the names
    // the generated game's third-person player looks for.
    std::vector<std::string> clips;
    bool defaultClips = true;  // when true `clips` is ignored and the default set is used
    float animFps = 15.0f;     // keys per second the clips are resampled to
    // How the kit's clips move: -1 masculine .. 0 the clips as captured ..
    // +1 feminine - hip sway, a narrower step, elbows in, a smaller arm
    // swing (docs/character-generator.md, "Movement style"). Auto follows
    // Gender (motionStyleFor); a set value is written to the recipe.
    bool motionStyleAuto = true;
    float motionStyle = 0.0f;
    // A .glb/.fbx (Mixamo-named rig) or .tmocap whose clips are retargeted onto
    // the rig INSTEAD of the kit's. "" = kit clips.
    std::string animSource;
    charanim::RetargetOptions retarget;

    std::string name = "character";

    bool operator==(const Params& o) const;
    bool operator!=(const Params& o) const { return !(*this == o); }
};

// The detail sliders the kit carries, in display order.
struct Slider {
    std::string id, label, group;
};
const std::vector<Slider>& sliders();

// Eyebrow / eyelash styles (display names).
const std::vector<std::string>& browList();
const std::vector<std::string>& lashList();

// Wardrobe and hair in the kit.
struct Item {
    std::string id, label;
    std::string slot;      // "top", "bottom", "full", "feet", "head", "face", "hands", "hair"
    bool dyeable = true;
    bool twoTone = false;  // carries a secondary dye region
    Rgb color{0.5f, 0.5f, 0.5f};  // its own average colour (where a recolour starts)
    // "f" / "m" for items cut for one body, "" for either. Nothing stops a man
    // wearing a dress; Randomize just does not do it for him.
    std::string sex;
};
const std::vector<Item>& wardrobe();
const std::vector<Item>& hairstyles();
const std::vector<std::string>& patterns();

// Clips in the kit (source names) and whether each loops.
struct ClipInfo {
    std::string name;
    bool loop = false;
    float seconds = 0.0f;
};
const std::vector<ClipInfo>& kitClips();
// The default set and the names they are written under.
const std::vector<std::pair<std::string, std::string>>& defaultClipSet();
// The movement style a recipe builds with: its own, or (auto) from Gender -
// a woman +0.8, a man -0.8 - and toward 0 for children.
float motionStyleFor(const Params& p);
// The other body of a Body-row character: the same recipe in the other sex -
// height, muscle and the sex-specific paint and macros adjusted, everything
// worn kept. "<x>.glb" -> "<x>-alt.glb" is where it is written.
Params altParams(const Params& p);
std::string altModelPath(const std::string& glbPath);
// Writes (or, without bodyChoice, removes a stale) "<x>-alt.glb" beside
// `glbPath`, with the same number of palette variants. False + error on failure.
bool writeBodyChoice(const Params& p, const std::string& glbPath, int variants,
                     std::string& error);

// The rig's bone names in palette order (Mixamo naming, "mixamorig:Hips", ...).
const std::vector<std::string>& boneNames();

// True when the embedded kit parsed. (It always should; false means a broken
// build, and build() reports why.)
bool kitAvailable();

// Builds a character. Returns false with `error` set when the kit is unusable;
// `warnings` collects non-fatal notes.
//
// Deterministic: identical Params produce identical bytes.
bool build(const Params& p, glbparser::Skel& out, std::vector<std::string>& warnings,
           std::string& error);

// Writes a built character into <projectDir>/res/models/characters/<name>.glb,
// plus <name>.chargen.json - the Params it came from, so the character can be
// reopened in the generator and edited. On success `outRelPath` holds the
// project-relative .glb path with forward slashes.
bool writeAsset(const std::string& projectDir, const std::string& name,
                const glbparser::Skel& skel, const Params& p, std::string* outRelPath,
                std::string* outError);

// Which parts of a generated model draw when nothing has chosen otherwise -
// ordinary parts and the creator options worn as built ("optd-"), the
// hairstyle's pressed "opth-" twin instead of it under a hat worn as built.
// The editor's twin of the game's applyLook with an untouched look; by
// material name, so it works on any baked .glb.
std::vector<bool> partsShownAsBuilt(const std::vector<std::string>& materials);

// Where relative Params paths (customHair, its texture) resolve from: the
// project directory, set by the editor when a project opens.
void setAssetRoot(const std::string& dir);

// Writes the bodies custom hair is modelled on - the average woman and man
// at 1.75 m, as .glb - into `dir`: reference-female.glb / -male.glb (the
// standard bodies), reference-hero-female/-male.glb and reference-crowd.glb.
// Model on the one whose body you will use (Detail, and gender < / >= 0.5).
bool exportReferenceBodies(const std::string& dir, std::string& error);

// Params <-> JSON (the .chargen.json sidecar, the --chargen command line).
std::string toJson(const Params& p);
bool fromJson(const std::string& text, Params& p, std::string& error);

// Tuned starting points for the UI (index 0 = the default).
struct Preset {
    const char* name;
    Params params;
};
const std::vector<Preset>& presets();

// Random but plausible: a body, a face, colours and an outfit from a seed.
Params randomize(unsigned seed, const Params& keep);

// Crowds (docs/character-generator.md, "Crowds"): the same person in other
// colours. Only what lives in the TEXTURES changes - skin tone and warmth,
// hair and eye colour, every worn item's dye - never a shape, an item or a
// clip, so a variant shares the base's mesh, rig, pose and texel layout and
// costs the game nothing but a palette. Deterministic in `seed`.
Params paletteVariant(const Params& base, unsigned seed);

// Writes variant `k`'s textures beside the base's .glb as
// "<glb stem>_<image>.v<k>.png" - the names texbake turns into the game's
// "<texture>.v<k>.pal" palettes, fitted to the base's own quantization.
bool writeVariantTextures(const Params& variant, const std::string& glbPath, int k,
                          std::string& error);

}  // namespace chargen
