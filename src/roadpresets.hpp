#pragma once

#include <string>
#include <vector>

#include "roadfurniture.hpp"
#include "roadtex.hpp"

struct SceneObject;
namespace json {
struct Value;
}

// Road presets (docs/roads.md "Road presets"): one choice that sets EVERY
// authored field of a Road - width, the generated surface and junction
// materials (lanes are in the texture), kerbs, pavement and its material,
// details, the street-furniture lines, markings, kind and tracks, rank, grip,
// spill and edge fade. The Draw road tool applies one to each road it makes,
// and Properties applies one to the selected roads.
//
// Presets are DATA: the built-in table lives in roadpresets.cpp, and a project
// can add its own (ProjectSettings::roadPresets, format v105) - captured from a
// road with fromRoad(). Host-only: no GL, no ImGui, and no project.hpp in this
// header (project.hpp includes it for the settings list).
namespace roadpresets {

struct Preset {
    std::string key;   // a stable id the CLI and the AI tool name ("city-street")
    std::string name;  // what the UI shows ("City street")
    std::string help;  // one or two sentences for the tooltip
    // Every Road field a preset owns (SceneObject::road*). Paths are
    // project-relative .mtl files; a preset's generated materials live in
    // res/materials/roads and are written on demand (ensureMaterials).
    float width = 7.5f;
    float sampleStep = 1.0f;
    float grip = 1.0f;
    int rank = 1;
    float spill = 1.5f;
    float edgeFade = 0.0f;
    int markings = 1;
    bool kerb = false;
    float kerbHeight = 0.15f, kerbWidth = 0.25f;
    float pavement = 0.0f;
    std::string surface, intersection, pavementMaterial;
    int kind = 0;      // roadrail::Kind
    int tracks = 1;
    float gauge = 0.0f;  // 0 = standard gauge in the project's units (1.435 x units per metre)
    float details = 0.0f;
    int detailSeed = 0;
    roadfurn::Settings furniture;
    bool operator==(const Preset&) const = default;
};

// The built-in table (City street, Avenue with tram, Boulevard, Country road,
// Dirt track, Railway, Highway, Alley), in menu order.
const std::vector<Preset>& builtins();
// A built-in or project preset by key or (case-insensitive) name; nullptr when
// none. Project presets win over built-ins of the same key.
const Preset* find(const std::vector<Preset>& project, const std::string& keyOrName);
// Every preset the UI lists: the built-ins, then the project's.
std::vector<const Preset*> all(const std::vector<Preset>& project);

// The generator recipe of every material a preset may name, by file stem:
// roadtex's seeded presets plus the ones only presets use (road-country,
// road-highway, road-dirt-junction). False for a stem with no recipe.
bool materialRecipe(const std::string& stem, roadtex::RoadTexParams* out);

// Writes every material the preset names that is MISSING from the project and
// has a recipe here (never overwrites one: a repainted or re-generated
// material stays). Returns the .mtl paths it wrote; *err on a write failure.
std::vector<std::string> ensureMaterials(const std::string& projectDir, const Preset& p,
                                         std::string* err = nullptr);

// Sets every field the preset owns on a Road. Points, heights, the bridge
// flag, the name and the object's id are left alone. `unitsPerMeter` scales a
// railway's standard gauge (and its bed) to the project.
void apply(const Preset& p, SceneObject& road, float unitsPerMeter);
// The inverse: a preset holding the road's fields ("Save preset").
Preset fromRoad(const SceneObject& road, const std::string& name);
// True when the road's fields are exactly what apply() would set (the
// Properties panel marks the matching preset).
bool matches(const Preset& p, const SceneObject& road, float unitsPerMeter);

// A road switched to Kind = Railway in Properties: the Railway preset's bed
// for the road's own track count (full = false only resizes the bed after a
// track-count change and swaps its single/double material).
void applyRailway(SceneObject& road, const std::string& projectDir, float unitsPerMeter,
                  bool full = true);

// A file-name-safe key from a display name ("My Street!" -> "my-street").
std::string keyOf(const std::string& name);

// The project list as JSON (ProjectSettings::roadPresets, format v105):
// "" when empty, else a JSON array; every key written only off its default.
std::string toJson(const std::vector<Preset>& list);
void fromJson(const json::Value& v, std::vector<Preset>& out);

}  // namespace roadpresets
