#include "project.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"
#include "templates.hpp"

namespace fs = std::filesystem;

const char* primitiveTypeName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box: return "box";
        case PrimitiveType::Sphere: return "sphere";
        case PrimitiveType::Cylinder: return "cylinder";
        case PrimitiveType::Cone: return "cone";
    }
    return "box";
}

static PrimitiveType primitiveTypeFromName(const std::string& s) {
    if (s == "sphere") return PrimitiveType::Sphere;
    if (s == "cylinder") return PrimitiveType::Cylinder;
    if (s == "cone") return PrimitiveType::Cone;
    return PrimitiveType::Box;
}

namespace project {

static std::string writeFile(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) return "Cannot write file: " + path.string();
    f << content;
    return "";
}

static std::string fmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", (double)v);
    return buf;
}

static std::string fmtVec3(const float* v) {
    return "[" + fmtFloat(v[0]) + ", " + fmtFloat(v[1]) + ", " + fmtFloat(v[2]) + "]";
}

std::string save(const Project& p) {
    std::ostringstream json;
    json << "{\n"
         << "  \"name\": \"" << p.name << "\",\n"
         << "  \"terrain\": {\n"
         << "    \"width\": " << p.terrain.width << ",\n"
         << "    \"depth\": " << p.terrain.depth << "\n"
         << "  },\n"
         << "  \"scenes\": [";
    for (size_t i = 0; i < p.scenes.size(); ++i) {
        if (i) json << ", ";
        json << "\"" << p.scenes[i] << "\"";
    }
    json << "],\n"
         << "  \"objects\": [";
    for (size_t i = 0; i < p.objects.size(); ++i) {
        const SceneObject& o = p.objects[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << o.name << "\", \"type\": \""
             << primitiveTypeName(o.type) << "\", \"position\": " << fmtVec3(o.position)
             << ", \"rotation\": " << fmtVec3(o.rotation)
             << ", \"scale\": " << fmtVec3(o.scale) << ", \"color\": " << fmtVec3(o.color)
             << " }";
    }
    json << (p.objects.empty() ? "]\n" : "\n  ]\n") << "}\n";
    return writeFile(fs::path(p.dir) / "project.json", json.str());
}

std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain) {
    if (name.empty()) return "Project name is empty";
    for (char c : name) {
        if (!isalnum((unsigned char)c) && c != '-' && c != '_')
            return "Project name may contain only letters, digits, '-' and '_'";
    }
    if (parentDir.empty()) return "Location is empty";

    fs::path root = fs::path(parentDir) / name;
    std::error_code ec;
    if (fs::exists(root, ec) && !fs::is_empty(root, ec))
        return "Directory already exists and is not empty: " + root.string();
    fs::create_directories(root, ec);
    if (ec) return "Cannot create directory: " + root.string() + " (" + ec.message() + ")";

    out = Project{};
    out.name = name;
    out.dir = root.string();
    out.terrain = terrain;

    for (const auto& f : templates::generate(out)) {
        if (auto err = writeFile(root / f.relativePath, f.content); !err.empty()) return err;
    }
    return save(out);
}

static void readVec3(const json::Value* v, float* out) {
    if (!v || v->type != json::Value::Type::Array || v->arr.size() < 3) return;
    for (int i = 0; i < 3; ++i) out[i] = (float)v->arr[i].numberOr(out[i]);
}

std::string load(Project& out, const std::string& projectDir) {
    fs::path jsonPath = fs::path(projectDir) / "project.json";
    std::ifstream f(jsonPath, std::ios::binary);
    if (!f) return "Not a tyra-editor project (missing project.json): " + projectDir;
    std::stringstream ss;
    ss << f.rdbuf();

    json::Value root;
    if (!json::parse(ss.str(), root) || root.type != json::Value::Type::Object)
        return "project.json is malformed";

    out = Project{};
    out.dir = fs::path(projectDir).string();
    if (const auto* v = root.find("name")) out.name = v->stringOr("");
    if (out.name.empty()) return "project.json is malformed (no name)";

    if (const auto* terrain = root.find("terrain")) {
        if (const auto* v = terrain->find("width")) out.terrain.width = (int)v->numberOr(64);
        if (const auto* v = terrain->find("depth")) out.terrain.depth = (int)v->numberOr(64);
    }

    if (const auto* scenes = root.find("scenes");
        scenes && scenes->type == json::Value::Type::Array && !scenes->arr.empty()) {
        out.scenes.clear();
        for (const auto& s : scenes->arr) out.scenes.push_back(s.stringOr("main"));
    }

    if (const auto* objects = root.find("objects");
        objects && objects->type == json::Value::Type::Array) {
        for (const auto& jo : objects->arr) {
            if (jo.type != json::Value::Type::Object) continue;
            SceneObject o;
            if (const auto* v = jo.find("name")) o.name = v->stringOr("object");
            if (const auto* v = jo.find("type"))
                o.type = primitiveTypeFromName(v->stringOr("box"));
            readVec3(jo.find("position"), o.position);
            readVec3(jo.find("rotation"), o.rotation);
            readVec3(jo.find("scale"), o.scale);
            readVec3(jo.find("color"), o.color);
            out.objects.push_back(std::move(o));
        }
    }
    return "";
}

std::string refreshGenerated(const Project& p) {
    for (const auto& f : templates::generate(p)) {
        const fs::path path = fs::path(p.dir) / f.relativePath;

        bool write = false;
        if (f.relativePath == "Dockerfile" || f.relativePath == "docker-compose.yml" ||
            f.relativePath == "inc\\terrain_config.hpp" ||
            f.relativePath == "inc\\scene_data.hpp") {
            write = true;  // editor-owned, always in sync with project data
        } else if (f.relativePath == "src\\terrain_game.cpp" ||
                   f.relativePath == "inc\\terrain_game.hpp") {
            // Regenerate while the ownership marker is present, or when the
            // file is byte-identical to an old template (never user-edited).
            std::ifstream existing(path, std::ios::binary);
            if (!existing) {
                write = true;
            } else {
                std::stringstream content;
                content << existing.rdbuf();
                std::string firstLine = content.str().substr(0, content.str().find('\n'));
                write = firstLine.find("Generated by tyra-editor") != std::string::npos ||
                        templates::matchesLegacy(p, f.relativePath, content.str());
            }
        }

        if (write) {
            if (auto err = writeFile(path, f.content); !err.empty()) return err;
        }
    }
    return "";
}

}  // namespace project
