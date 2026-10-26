#pragma once

#include <map>
#include <string>
#include <vector>

// Public script fields (docs/script-api.md, "Public fields"): members of an
// object script marked [[tyra::prop]] that the editor shows on every object the
// script is attached to, and that the generated game fills in before onStart.
//
//   class Spinner : public ObjectScript {
//    public:
//     [[tyra::prop(min = 0, max = 720, tooltip = "Degrees per second")]]
//     float speed = 90.0F;
//     [[tyra::prop]] tyrax::ObjectRef target;
//   };
//
// The scan is TEXTUAL, like the attach list's TYRA_OBJECT_SCRIPT scan: no
// compiler runs in the editor. It finds each registered class's body in the
// same file and reads the declarations that follow a [[tyra::prop ...]]
// attribute. What it cannot read is reported per field, never guessed.
namespace scriptprops {

enum class Kind {
    Unknown,
    Bool,
    Int,
    Enum,   // int with options = "A|B|C"
    Float,
    Vec3,
    Color,  // tyrax::Vec3 with the `color` flag (0..1 RGB)
    Text,   // const char*
    Object, // tyrax::ObjectRef - an object of the same scene
    Sound,
    Music,
    Event,
    Scene,
    SaveValue,
    Prefab,
    Action,
    HudText,
    HudBar,
    Sequence,
    Menu,
};

struct Field {
    std::string name;
    std::string type;          // as written: "float", "tyrax::SoundId", ...
    Kind kind = Kind::Unknown;
    std::string defaultValue;  // canonical value text (see valueOf) or "" = none readable
    bool hasMin = false, hasMax = false;
    float min = 0.0f, max = 0.0f;
    std::string tooltip;
    std::string header;        // header = "..." - a caption above the field
    std::vector<std::string> options;  // Enum labels
    std::string problem;       // why the editor cannot offer it ("" = fine)
};

struct ClassInfo {
    std::string name;
    std::string file;  // path relative to the project
    std::vector<Field> fields;
    // Methods registered with TYRA_SCRIPT_METHOD(Class, method) in the same
    // file - what the Call Script node offers.
    std::vector<std::string> methods;
};

// Every [[tyra::prop]] field of every TYRA_OBJECT_SCRIPT class in `source`.
// `file` is only copied into the result.
std::vector<ClassInfo> scanSource(const std::string& source, const std::string& file);

// The whole src/scripts tree of a project (.cpp, .hpp, .h).
std::vector<ClassInfo> scanProject(const std::string& projectDir);

const ClassInfo* findClass(const std::vector<ClassInfo>& classes, const std::string& name);

// Value text, the one form a value is stored in (project file, UI, codegen):
//   Bool "true"/"false", Int/Enum "3", Float "1.5", Vec3/Color "1 2 3",
//   Text the text itself, Object the object's id, Sound/Music the asset path,
//   every other reference the item's NAME.
// Numbers are written with the shortest round-tripping form.
std::string formatFloat(float v);
bool parseVec3(const std::string& text, float out[3]);

// Short label for the kind ("float", "object", "sound", ...), for the UI.
const char* kindLabel(Kind k);

}  // namespace scriptprops
