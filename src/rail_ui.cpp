// The Project panel's Assets, Audio and Scripts pages (the left rail,
// docs/editor-theme.md "The Project panel") - App:: methods declared in
// app.hpp, own TU (the assetbrowser.cpp precedent).
//
// One skeleton for all three: search and filters on top, a list that takes
// the height, rows that say what USES them (a click goes there), unused and
// broken things flagged with one fix each, and an empty state that says why
// and offers the action. The pages are a pocket view: moving, renaming and
// deleting files stays in the Asset Browser.

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "app.hpp"
#include "app_internal.hpp"
#include "audiopreview.hpp"
#include "extension.hpp"
#include "flowgraph.hpp"
#include "logview.hpp"
#include "templates.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"
#include "wavconvert.hpp"
#include "widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool matches(const std::string& hay, const char* filter) {
    return !filter[0] || lower(hay).find(lower(filter)) != std::string::npos;
}

ImU32 col(const ImVec4& c, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a)); }

// A small rounded label drawn at the cursor's line; advances like text.
void tag(const char* text, const ImVec4& c, bool filled = true) {
    const float fs = ImGui::GetFontSize() * 0.78f;
    const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = fs * 1.45f, w = sz.x + fs;
    const float y = p.y + (ImGui::GetTextLineHeight() - h) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (filled) dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), col(c, 0.18f), h * 0.5f);
    else dl->AddRect(ImVec2(p.x, y), ImVec2(p.x + w, y + h), col(c, 0.5f), h * 0.5f);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + fs * 0.5f, y + (h - fs) * 0.5f), col(c), text);
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
}

std::string countNoun(size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string bytesText(uintmax_t b) {
    char buf[32];
    if (b >= 1024ull * 1024ull) std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    return buf;
}

std::string clockText(double s) {
    char buf[16];
    const int t = (int)(s + 0.5);
    std::snprintf(buf, sizeof buf, "%d:%02d", t / 60, t % 60);
    return buf;
}

// Length of a WAV from its header (cached per path + size).
double wavSeconds(const fs::path& p) {
    static std::map<std::string, std::pair<uintmax_t, double>> cache;
    std::error_code ec;
    const uintmax_t size = fs::file_size(p, ec);
    if (ec) return 0.0;
    auto it = cache.find(p.string());
    if (it != cache.end() && it->second.first == size) return it->second.second;
    double secs = 0.0;
    std::ifstream f(p, std::ios::binary);
    char hdr[12];
    if (f.read(hdr, 12) && std::memcmp(hdr, "RIFF", 4) == 0) {
        uint32_t rate = 0, data = 0;
        uint16_t ch = 0, bits = 0;
        char ck[8];
        while (f.read(ck, 8)) {
            uint32_t n = 0;
            std::memcpy(&n, ck + 4, 4);
            if (std::memcmp(ck, "fmt ", 4) == 0 && n >= 16) {
                char fmt[16];
                f.read(fmt, 16);
                std::memcpy(&ch, fmt + 2, 2);
                std::memcpy(&rate, fmt + 4, 4);
                std::memcpy(&bits, fmt + 14, 2);
                f.seekg(n - 16 + (n & 1), std::ios::cur);
            } else if (std::memcmp(ck, "data", 4) == 0) {
                data = n;
                break;
            } else {
                f.seekg(n + (n & 1), std::ios::cur);
            }
        }
        if (rate && ch && bits >= 8) secs = (double)data / (ch * (bits / 8)) / rate;
    }
    cache[p.string()] = {size, secs};
    return secs;
}

// The classes a script file registers: TYRA_OBJECT_SCRIPT(Name) and the
// global TYRA_SCRIPT(Name), cached by write time.
struct ScriptFileInfo {
    fs::file_time_type mtime;
    std::vector<std::string> objectClasses, globalClasses;
};
const ScriptFileInfo& scanScriptFile(const fs::path& p) {
    static std::map<std::string, ScriptFileInfo> cache;
    std::error_code ec;
    const auto mtime = fs::last_write_time(p, ec);
    auto it = cache.find(p.string());
    if (it != cache.end() && it->second.mtime == mtime) return it->second;
    ScriptFileInfo info;
    info.mtime = mtime;
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();
    auto grab = [&](const std::string& macro, std::vector<std::string>& out) {
        for (size_t pos = src.find(macro); pos != std::string::npos; pos = src.find(macro, pos + 1)) {
            // TYRA_SCRIPT( must not match inside TYRA_OBJECT_SCRIPT(
            if (pos > 0 && (std::isalnum((unsigned char)src[pos - 1]) || src[pos - 1] == '_')) continue;
            const size_t end = src.find(')', pos + macro.size());
            if (end == std::string::npos) break;
            std::string n = src.substr(pos + macro.size(), end - pos - macro.size());
            while (!n.empty() && std::isspace((unsigned char)n.front())) n.erase(n.begin());
            while (!n.empty() && std::isspace((unsigned char)n.back())) n.pop_back();
            if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
    };
    grab("TYRA_OBJECT_SCRIPT(", info.objectClasses);
    grab("TYRA_SCRIPT(", info.globalClasses);
    return cache[p.string()] = std::move(info);
}

}  // namespace

// ============================================================================
// Assets
// ============================================================================

void App::drawAssetsSection() {
    const theme::Semantics& sem = theme::semantics();
    if (assetScanTime_ < 0.0 || ImGui::GetTime() - assetScanTime_ > 3.0) scanAssetTree();
    if (assetUsageSerial_ != modelEditSerial_) rebuildAssetUsage();

    // Kinds the page shows: audio has its own page, generated files are the
    // build's (the Asset Browser's "Show generated").
    struct Kind { const char* label; AssetKind a, b; };
    static const Kind kKinds[] = {{"All", AssetKind::Other, AssetKind::Other},
                                  {"Models", AssetKind::Model, AssetKind::Model},
                                  {"Animated", AssetKind::AnimModel, AssetKind::AnimModel},
                                  {"Materials", AssetKind::Material, AssetKind::Material},
                                  {"Textures", AssetKind::Texture, AssetKind::Texture},
                                  {"Fonts", AssetKind::Font, AssetKind::Font}};
    auto shownKind = [](AssetKind k) {
        return k == AssetKind::Model || k == AssetKind::AnimModel || k == AssetKind::Material ||
               k == AssetKind::Texture || k == AssetKind::Font;
    };
    auto isUnused = [&](const AssetItem& item) {
        const AssetUsage* use = assetUsageFor(item.rel);
        if (use && use->total() > 0) return false;
        return assetWavefrontUsers(item.rel).empty();
    };
    int counts[6] = {};
    int unused = 0;
    std::vector<const AssetItem*> pool;
    for (const AssetItem& item : assetItems_) {
        if (item.generated || !shownKind(item.kind)) continue;
        if (!railAssetFolder_.empty() && item.rel.rfind(railAssetFolder_ + "/", 0) != 0) continue;
        // The button prompts and HUD sprites every project gets: noise in a
        // pocket view unless their folder is picked on purpose.
        if (railAssetFolder_.empty() && item.rel.rfind("res/hud/", 0) == 0) continue;
        ++counts[0];
        for (int k = 1; k < 6; ++k) counts[k] += item.kind == kKinds[k].a ? 1 : 0;
        pool.push_back(&item);
    }

    // --- search, type chips, folder, Unused, Grid | List ----------------------
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##railassetsearch", "Search assets", railAssetSearch_, sizeof railAssetSearch_);
    for (int k = 0; k < 6; ++k) {
        if (k > 0 && counts[k] == 0) continue;
        ui::ChipFlow(ui::FilterChipWidth(kKinds[k].label, counts[k]), k == 0);
        if (ui::FilterChip(kKinds[k].label, railAssetKind_ == k, counts[k])) railAssetKind_ = k;
    }
    std::vector<const AssetItem*> shown;
    for (const AssetItem* item : pool) {
        if (railAssetKind_ > 0 && item->kind != kKinds[railAssetKind_].a) continue;
        if (!matches(item->name, railAssetSearch_)) continue;
        const bool un = isUnused(*item);
        unused += un ? 1 : 0;
        if (railAssetUnused_ && !un) continue;
        shown.push_back(item);
    }
    // Models first (what gets placed), then materials, textures, fonts.
    std::stable_sort(shown.begin(), shown.end(), [](const AssetItem* a, const AssetItem* b) {
        auto rank = [](AssetKind k) {
            return k == AssetKind::Model ? 0 : k == AssetKind::AnimModel ? 1 : k == AssetKind::Material ? 2
                 : k == AssetKind::Texture ? 3 : 4;
        };
        if (rank(a->kind) != rank(b->kind)) return rank(a->kind) < rank(b->kind);
        return a->name < b->name;
    });
    {
        // Folder: res, or one of its sub-folders (deep).
        const float listW = ImGui::GetFrameHeight() * 2.6f;
        const float unW = ImGui::CalcTextSize("Unused 000").x + ImGui::GetStyle().FramePadding.x * 3.0f;
        ImGui::SetNextItemWidth(ImMax(scaled(80), ImGui::GetContentRegionAvail().x - listW - unW - scaled(12)));
        const std::string cur = railAssetFolder_.empty() ? "res /" : railAssetFolder_;
        if (ImGui::BeginCombo("##railassetfolder", cur.c_str())) {
            if (ImGui::Selectable("res /  (everything)", railAssetFolder_.empty())) railAssetFolder_.clear();
            for (const auto& [rel, dir] : assetDirs_) {
                if (rel == "res" || dir.filesDeep == 0) continue;
                char lbl[256];
                std::snprintf(lbl, sizeof lbl, "%s  (%d)", rel.c_str(), dir.filesDeep);
                if (ImGui::Selectable(lbl, rel == railAssetFolder_)) railAssetFolder_ = rel;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        char un[32];
        std::snprintf(un, sizeof un, "Unused %d###railunused", unused);
        if (ui::FilterChip(un, railAssetUnused_)) railAssetUnused_ = !railAssetUnused_;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Nothing in the project names these files: no object, node,\n"
                              "material library, menu or vehicle. They still ship if they\n"
                              "sit in res/ - review them in the Asset Browser.");
        ImGui::SameLine();
        const char* views[] = {"Grid##railview", "List##railview"};
        int view = railAssetList_ ? 1 : 0;
        if (ui::Segmented("##railview", views, 2, &view)) railAssetList_ = view == 1;
    }
    if (railAssetUnused_ && unused > 0)
        ImGui::TextColored(sem.warn, "%d file%s nothing uses. Review them before shipping.", unused,
                           unused == 1 ? "" : "s");

    // --- the list ---------------------------------------------------------------
    const float drawerH = railAssetSel_.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing() * 6.2f;
    const float footH = ImGui::GetTextLineHeightWithSpacing() + scaled(4);
    ImGui::BeginChild("##railassetlist", ImVec2(0, ImMax(scaled(120), ImGui::GetContentRegionAvail().y - drawerH - footH)));
    if (assetItems_.empty()) {
        if (ui::EmptyState(ui::Glyph::Box, "No assets yet",
                           "Models, materials, textures and fonts live in res/. Import one, or drop files on the editor.",
                           "Import model...##railempty"))
            importModelAsset();
    } else if (shown.empty()) {
        ImGui::TextDisabled(railAssetUnused_ ? "Everything here is used." : "Nothing matches.");
    }
    int thumbBudget = 3;
    auto thumbOf = [&](const AssetItem& item) -> uint32_t {
        if (item.kind == AssetKind::Font) return 0;
        uint32_t t = viewport_.assetThumb(item.rel, false);
        if (!t && thumbBudget > 0) {
            t = viewport_.assetThumb(item.rel, true);
            --thumbBudget;
        }
        return t;
    };
    auto usesOf = [&](const AssetItem& item) {
        const AssetUsage* use = assetUsageFor(item.rel);
        return (use ? use->total() : 0) + (int)assetWavefrontUsers(item.rel).size();
    };
    auto kindTag = [](AssetKind k) {
        switch (k) {
            case AssetKind::AnimModel: return "anim";
            case AssetKind::Material: return "mtl";
            case AssetKind::Texture: return "png";
            case AssetKind::Font: return "ttf";
            default: return "";
        }
    };
    // Drag a model to the viewport, double-click to add it at the view centre.
    auto interact = [&](const AssetItem& item) {
        if (ImGui::IsItemClicked()) railAssetSel_ = item.rel;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) activateAsset(item.rel);
        if (ImGui::BeginDragDropSource()) {
            const bool model = item.kind == AssetKind::Model || item.kind == AssetKind::AnimModel;
            ImGui::SetDragDropPayload(model ? "TYRAX_ASSET_MODEL" : "TYRAX_ASSET_FILE", item.rel.c_str(),
                                      item.rel.size() + 1);
            ImGui::TextUnformatted(item.name.c_str());
            if (model) ImGui::TextDisabled("drop on the viewport to place it");
            ImGui::EndDragDropSource();
        }
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    if (!railAssetList_) {
        const int cols = 3;
        const float gap = scaled(8);
        const float tw = IM_TRUNC((ImGui::GetContentRegionAvail().x - gap * (cols - 1)) / cols);
        const float th = tw * 0.8f;
        for (size_t i = 0; i < shown.size(); ++i) {
            const AssetItem& item = *shown[i];
            if (i % cols) ImGui::SameLine(0.0f, gap);
            ImGui::PushID(item.rel.c_str());
            ImGui::BeginGroup();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(item.name.c_str(), ImVec2(tw, th + fs * 2.4f));
            const bool sel = item.rel == railAssetSel_;
            uiscript::markLastItemChecked(sel);
            interact(item);
            const bool hov = ImGui::IsItemHovered();
            const float r = ImGui::GetStyle().FrameRounding;
            dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + th), col(sem.field), r);
            if (sel || hov) dl->AddRect(p, ImVec2(p.x + tw, p.y + th), col(sel ? sem.accent : sem.border), r, 0, sel ? 2.0f : 1.0f);
            if (const uint32_t t = thumbOf(item)) {
                const float s = th - scaled(6);
                const ImVec2 a(p.x + (tw - s) * 0.5f, p.y + scaled(3));
                const bool flip = item.kind != AssetKind::Texture;  // rendered thumbs come out upside down
                dl->AddImage((ImTextureID)(intptr_t)t, a, ImVec2(a.x + s, a.y + s), ImVec2(0, flip ? 1.0f : 0.0f),
                             ImVec2(1, flip ? 0.0f : 1.0f));
            } else if (item.kind == AssetKind::Font) {
                ui::FontScope bold(ui::boldFont());
                const float big = th * 0.4f;
                const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0, "Aa");
                dl->AddText(ImGui::GetFont(), big, ImVec2(p.x + (tw - ts.x) * 0.5f, p.y + (th - ts.y) * 0.5f),
                            col(sem.text), "Aa");
            }
            // Used-by badge (amber 0) and the kind in the corner.
            const int uses = usesOf(item);
            char ub[16];
            std::snprintf(ub, sizeof ub, "%d", uses);
            const float bfs = fs * 0.75f;
            const float bw = ImGui::GetFont()->CalcTextSizeA(bfs, FLT_MAX, 0, ub).x + bfs;
            const ImVec2 b0(p.x + tw - bw - scaled(4), p.y + scaled(4));
            dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bfs * 1.4f), IM_COL32(8, 9, 12, 190), bfs * 0.7f);
            dl->AddText(ImGui::GetFont(), bfs, ImVec2(b0.x + bfs * 0.5f, b0.y + bfs * 0.2f),
                        col(uses == 0 ? sem.warn : sem.text), ub);
            if (const char* kt = kindTag(item.kind); kt[0]) {
                const float kw = ImGui::GetFont()->CalcTextSizeA(bfs, FLT_MAX, 0, kt).x + bfs;
                const ImVec2 k0(p.x + scaled(4), p.y + th - bfs * 1.4f - scaled(4));
                dl->AddRectFilled(k0, ImVec2(k0.x + kw, k0.y + bfs * 1.4f), IM_COL32(8, 9, 12, 190), scaled(3));
                dl->AddText(ImGui::GetFont(), bfs, ImVec2(k0.x + bfs * 0.5f, k0.y + bfs * 0.2f), col(sem.textDim), kt);
            }
            dl->PushClipRect(ImVec2(p.x, p.y + th), ImVec2(p.x + tw, p.y + th + fs * 2.4f), true);
            const std::string stem = fs::path(item.name).stem().string();
            dl->AddText(ImVec2(p.x + scaled(2), p.y + th + scaled(3)), col(sel ? sem.accent : sem.text), stem.c_str());
            dl->AddText(ImGui::GetFont(), fs * 0.82f, ImVec2(p.x + scaled(2), p.y + th + fs * 1.3f), col(sem.textDim),
                        bytesText(item.bytes).c_str());
            dl->PopClipRect();
            ImGui::EndGroup();
            ImGui::PopID();
        }
    } else {
        for (const AssetItem* ip : shown) {
            const AssetItem& item = *ip;
            const int uses = usesOf(item);
            char sub[96];
            std::snprintf(sub, sizeof sub, "%s - %s - %s", assetKindName(item.kind), bytesText(item.bytes).c_str(),
                          uses == 0 ? "unused" : (std::to_string(uses) + " use" + (uses == 1 ? "" : "s")).c_str());
            const std::string label = item.name + "##" + item.rel;
            if (ui::ListRow(label.c_str(), sub, item.rel == railAssetSel_, ui::Glyph::Box)) railAssetSel_ = item.rel;
            interact(item);
        }
    }
    ImGui::EndChild();

    // --- the selection drawer -----------------------------------------------------
    const AssetItem* sel = nullptr;
    for (const AssetItem& item : assetItems_)
        if (item.rel == railAssetSel_) sel = &item;
    if (!sel) railAssetSel_.clear();
    if (sel) {
        ImGui::Separator();
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::TextUnformatted(sel->name.c_str());
        }
        tag(assetKindName(sel->kind), sem.accent);
        ImGui::SameLine();
        tag(bytesText(sel->bytes).c_str(), sem.textDim, false);
        std::vector<std::string> users;
        if (const AssetUsage* use = assetUsageFor(sel->rel)) users = use->lines;
        for (const std::string& w : assetWavefrontUsers(sel->rel)) users.push_back(w);
        if (users.empty()) ImGui::TextColored(sem.warn, "Nothing uses it yet.");
        else ui::UsedBy("Used by", users);
        const bool model = sel->kind == AssetKind::Model || sel->kind == AssetKind::AnimModel;
        if (model && ui::PrimaryButton("Add to scene")) activateAsset(sel->rel);
        else if (sel->kind == AssetKind::Material && ui::PrimaryButton("Edit material")) openMaterialEditor(sel->rel);
        ImGui::SameLine();
        if (ui::SecondaryButton("Open in Asset Browser##selection")) {
            showAssetBrowser_ = true;
            assetFolder_ = sel->rel.substr(0, sel->rel.rfind('/'));
            assetSelection_ = {sel->rel};
        }
    }
    ImGui::TextDisabled("Drag a model onto the viewport to place it.");
}

// ============================================================================
// Audio
// ============================================================================

void App::drawAudioPage() {
    const theme::Semantics& sem = theme::semantics();
    if (!clipPlayer_) clipPlayer_ = std::make_unique<audiopreview::ClipPlayer>();
    if (assetUsageSerial_ != modelEditSerial_) rebuildAssetUsage();
    const std::string playing = clipPlayer_->current();

    // Where each sound lives in SPU2 RAM (docs/sound.md, "SPU2 sample RAM").
    // The page is organised by it: the list groups sounds by what loads them,
    // the card on top says how much can be loaded at once.
    struct SoundRow { std::string rel; uintmax_t wav = 0, adpcm = 0; };
    std::vector<SoundRow> rows;
    for (const std::string& s : project_.sounds) {
        std::error_code ec;
        SoundRow r;
        r.rel = s;
        r.wav = fs::file_size(fs::path(project_.dir) / s, ec);
        r.adpcm = ec ? 0 : estimateAdpcmBytes(r.wav);
        rows.push_back(r);
    }
    if (soundResSerial_ != modelEditSerial_) {
        std::vector<uintmax_t> bytes;
        for (const SoundRow& r : rows) bytes.push_back(r.adpcm);
        const templates::SoundResidency res = templates::soundResidency(project_);
        soundResShared_ = res.shared;
        soundResBank_ = res.bank;
        soundResBase_ = res.scene;
        soundResLayer_ = res.layer;
        soundResScene_.clear();
        for (int si = 0; si < (int)project_.scenes.size(); ++si)
            soundResScene_.push_back(templates::soundWorstCase(project_, res, si, bytes));
        soundResSerial_ = modelEditSerial_;
    }
    const int sceneCount = (int)project_.scenes.size();
    const int S = audioScene_ >= 0 && audioScene_ < sceneCount ? audioScene_ : project_.activeScene;
    auto has = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    auto sumOf = [&](const std::vector<int>& slots) {
        uintmax_t b = 0;
        for (const int s : slots) if (s >= 0 && s < (int)rows.size()) b += rows[s].adpcm;
        return b;
    };
    // The four kinds of residency, one colour each - the card's bar, its
    // legend, the group headers and the inspector's chips all use these.
    const ImVec4 kAlways = sem.accent, kScene = sem.ok, kLayer = sem.special, kBank = sem.warn;
    const float fs = ImGui::GetFontSize();
    // A small square in a residency colour at the cursor, hollow for "none".
    auto swatch = [&](const ImVec4& c, bool hollow) {
        const float d = fs * 0.55f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float y = p.y + (ImGui::GetTextLineHeight() - d) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hollow) dl->AddRect(ImVec2(p.x, y), ImVec2(p.x + d, y + d), col(sem.textDim), scaled(2));
        else dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + d, y + d), col(c), scaled(2));
        ImGui::Dummy(ImVec2(d, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0, fs * 0.5f);
    };
    auto smallCaps = [&](const char* text) {
        ImGui::PushFont(ui::boldFont(), fs * 0.78f);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        std::string up = text;
        for (char& c : up) c = (char)std::toupper((unsigned char)c);
        ImGui::TextUnformatted(up.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    };
    auto rightText = [&](const std::string& t, const ImVec4& c) {
        const float tw = ImGui::CalcTextSize(t.c_str()).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
        ImGui::TextColored(c, "%s", t.c_str());
    };

    // --- How much can be loaded at once ----------------------------------------
    {
        const std::vector<int> none;
        const std::vector<int>& worst = S < (int)soundResScene_.size() ? soundResScene_[S] : none;
        uintmax_t bScene = 0, bLayer = 0, bBank = 0;
        for (const int x : worst) {
            if (x < 0 || x >= (int)rows.size()) continue;
            bool inLayer = false;
            if (S < (int)soundResLayer_.size())
                for (const auto& l : soundResLayer_[S]) inLayer |= has(l, x);
            if (S < (int)soundResBase_.size() && has(soundResBase_[S], x)) bScene += rows[x].adpcm;
            else if (inLayer) bLayer += rows[x].adpcm;
            else bBank += rows[x].adpcm;
        }
        const uintmax_t bAlways = sumOf(soundResShared_);
        const uintmax_t total = bAlways + bScene + bLayer + bBank;
        if (ui::BeginCard("##audioram")) {
            smallCaps("Sound RAM at most");
            if (sceneCount > 0) {
                const std::string pv = "Scene: " + project_.scenes[S].name;
                const float w = ImGui::CalcTextSize(pv.c_str()).x + ImGui::GetFrameHeight() * 1.6f;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - w);
                ImGui::SetNextItemWidth(w);
                if (ImGui::BeginCombo("##audioscene", pv.c_str())) {
                    for (int si = 0; si < sceneCount; ++si) {
                        const std::string item = project_.scenes[si].name + "  " +
                                                 bytesText(bAlways + sumOf(soundResScene_[si]));
                        if (ImGui::Selectable(item.c_str(), si == S))
                            audioScene_ = si == project_.activeScene ? -1 : si;
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Which scene to count and group by - each has its own worst moment.");
            }
            ImGui::PushFont(ui::boldFont(), fs * 1.85f);
            ImGui::TextUnformatted(bytesText(total).c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("of %.2f MB", kSpu2SampleBudgetBytes / (1024.0 * 1024.0));
            {
                const double share = (double)total / (double)kSpu2SampleBudgetBytes;
                char verdict[48];
                if (total > kSpu2SampleBudgetBytes)
                    std::snprintf(verdict, sizeof verdict, "over by %s",
                                  bytesText(total - kSpu2SampleBudgetBytes).c_str());
                else
                    std::snprintf(verdict, sizeof verdict, "%.0f%% - %s", share * 100.0,
                                  share > 0.85 ? "tight" : "fits");
                rightText(verdict, total > kSpu2SampleBudgetBytes ? sem.danger
                                   : share > 0.85                 ? sem.warn
                                                                  : sem.ok);
            }
            // The bar: one run per kind, against the budget (or the total when over).
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = scaled(9);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(sem.field), h * 0.5f);
            float x = p.x;
            const double denom = (double)ImMax<uintmax_t>(kSpu2SampleBudgetBytes, total);
            for (const auto& [b, c] : {std::pair<uintmax_t, ImVec4>{bAlways, kAlways}, {bScene, kScene},
                                       {bLayer, kLayer}, {bBank, kBank}}) {
                const float sw = (float)(b / denom * w);
                if (sw < 0.5f) continue;
                dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + ImMax(sw - 1.0f, 1.5f), p.y + h), col(c),
                                  scaled(2));
                x += sw;
            }
            ImGui::Dummy(ImVec2(w, h));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("The worst moment of each scene, shared sounds included:");
                for (int si = 0; si < sceneCount; ++si)
                    ImGui::Text("  %s  %s", project_.scenes[si].name.c_str(),
                                bytesText(bAlways + sumOf(soundResScene_[si])).c_str());
                ImGui::TextDisabled("Counts every layer that can be in at once and every bank the\n"
                                    "scene loads. Reverb rooms take up to 192 KB more.");
                ImGui::EndTooltip();
            }
            if (ImGui::BeginTable("##audiolegend", 2)) {
                const std::pair<const char*, std::pair<uintmax_t, ImVec4>> legend[] = {
                    {"Always", {bAlways, kAlways}}, {"Scene", {bScene, kScene}},
                    {"Layers", {bLayer, kLayer}}, {"Banks", {bBank, kBank}}};
                for (const auto& [label, v] : legend) {
                    ImGui::TableNextColumn();
                    swatch(v.second, false);
                    ImGui::TextUnformatted(label);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", bytesText(v.first).c_str());
                }
                ImGui::EndTable();
            }
            // Another scene worse than this one is what a build would trip on.
            int heavy = -1;
            uintmax_t heavyBytes = 0;
            for (int si = 0; si < sceneCount; ++si)
                if (const uintmax_t b = sumOf(soundResScene_[si]); b > heavyBytes) heavy = si, heavyBytes = b;
            if (heavy >= 0 && heavy != S && bAlways + heavyBytes > total)
                ImGui::TextColored(sem.textDim, "Heaviest: %s, %s", project_.scenes[heavy].name.c_str(),
                                   bytesText(bAlways + heavyBytes).c_str());
        }
        ui::EndCard();
    }

    // --- Sounds | Music ---------------------------------------------------------
    {
        const std::string l0 = "Sounds  " + std::to_string(project_.sounds.size()) + "###audtab_s";
        const std::string l1 = "Music  " + std::to_string(project_.music.size()) + "###audtab_m";
        const char* labels[] = {l0.c_str(), l1.c_str()};
        ImGui::Dummy(ImVec2(0, scaled(2)));
        ui::Segmented("##audiotabs", labels, 2, &audioTab_, ImGui::GetContentRegionAvail().x);
    }

    // One playable row: round play/stop, name, a line under it, a right column.
    auto playButton = [&](const std::string& rel) {
        const bool on = playing == rel;
        const float d = ImGui::GetFrameHeight();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const std::string id = std::string(on ? "Stop##" : "Play##") + rel;
        if (ImGui::InvisibleButton(id.c_str(), ImVec2(d, d))) {
            if (on) clipPlayer_->stop();
            else clipPlayer_->play((fs::path(project_.dir) / rel).string(), rel);
        }
        uiscript::markLastItemChecked(on);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
        dl->AddCircleFilled(c, d * 0.5f, col(on ? sem.ok : sem.surface), 20);
        dl->AddCircle(c, d * 0.5f, col(on ? sem.ok : sem.border), 20);
        const ImU32 ink = on ? IM_COL32(6, 20, 12, 255) : col(sem.text);
        const float s = d * 0.2f;
        if (on) dl->AddRectFilled(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), ink);
        else dl->AddTriangleFilled(ImVec2(c.x - s * 0.8f, c.y - s * 1.2f), ImVec2(c.x - s * 0.8f, c.y + s * 1.2f),
                                   ImVec2(c.x + s * 1.3f, c.y), ink);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip(on ? "Stop" : "Play");
    };
    auto usedLine = [&](const std::string& rel) {
        const AssetUsage* use = assetUsageFor(rel);
        if (!use || use->total() == 0) {
            ImGui::TextColored(sem.warn, "unused");
            return;
        }
        std::string s = use->lines.empty() ? std::to_string(use->total()) + " uses" : use->lines.front();
        if (use->total() > 1 && !use->lines.empty()) s += "  +" + std::to_string(use->total() - 1);
        ImGui::TextDisabled("%s", s.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !use->lines.empty()) {
            ImGui::BeginTooltip();
            for (const std::string& l : use->lines) ImGui::TextUnformatted(l.c_str());
            ImGui::EndTooltip();
        }
        if (ImGui::IsItemClicked() && !use->objectRefs.empty()) {
            const auto [si, oi] = use->objectRefs.front();
            openGraphOf(si, oi);
        }
    };

    ImGui::BeginChild("##railaudio", ImVec2(0, ImMax(scaled(120), ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - scaled(6))));
    if (audioTab_ == 1) {
        for (int i = 0; i < (int)project_.music.size(); ++i) {
            const std::string& rel = project_.music[i];
            const fs::path abs = fs::path(project_.dir) / rel;
            const std::string name = abs.filename().string();
            ImGui::PushID(i);
            const bool selRow = railAudioSel_ == rel;
            playButton(rel);
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::Selectable(name.c_str(), selRow, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x - scaled(70), 0)))
                railAudioSel_ = selRow ? std::string() : rel;
            fs::path patch = abs;
            patch.replace_extension(".drone");
            std::error_code pec;
            const bool drone = fs::exists(patch, pec);
            const std::string& issue = wavIssue(rel, false);
            if (playing == rel) {
                const double len = clipPlayer_->length();
                const float t = len > 0 ? (float)(clipPlayer_->position() / len) : 0.0f;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, sem.ok);
                ImGui::ProgressBar(t, ImVec2(ImGui::GetContentRegionAvail().x - scaled(80), scaled(4)), "");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextDisabled("%s / %s", clockText(clipPlayer_->position()).c_str(),
                                    clockText(len > 0 ? len : wavSeconds(abs)).c_str());
            } else if (!issue.empty()) {
                ImGui::TextColored(sem.warn, "%s", issue.c_str());
            } else {
                ImGui::TextDisabled("%s ", clockText(wavSeconds(abs)).c_str());
                ImGui::SameLine(0, 0);
                usedLine(rel);
            }
            ImGui::EndGroup();
            std::error_code ec;
            rightText(bytesText(fs::file_size(abs, ec)), sem.textDim);
            // One fix per row: Convert an unplayable WAV, Edit a Drone track.
            if (!issue.empty()) {
                if (ImGui::SmallButton("Convert")) {
                    std::string err;
                    statusMessage_ = wavconvert::convertTo16(abs, 22050, err)
                                         ? name + " converted to 16-bit PCM 22050 Hz"
                                         : name + ": conversion failed - " + err;
                    wavIssueCache_.clear();
                }
            } else if (drone) {
                if (ImGui::SmallButton("Edit in Drone Generator")) {
                    fs::path r(rel);
                    r.replace_extension(".drone");
                    droneLoadPatch(r.generic_string());
                }
            }
            if (selRow) {
                // Build knobs only on the selected track (docs/sound.md): the bin\
                // copy is converted after every build, the res\ source never.
                ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
                auto it = project_.musicBuild.find(rel);
                Project::MusicBuildOpt opt = it != project_.musicBuild.end() ? it->second : Project::MusicBuildOpt{};
                int rate = opt.rate == 48000 ? 1 : opt.rate == 32000 ? 2 : opt.rate == 22050 ? 3 : 0;
                const char* rates[] = {"Keep", "48k", "32k", "22k"};
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("PS2 build");
                ImGui::SameLine();
                bool edited = ui::Segmented("##mbrate", rates, 4, &rate);
                ImGui::SameLine();
                edited |= ui::Switch("Mono##mb", &opt.mono);
                ImGui::TextDisabled("48k is SPU2-native: the IOP skips resampling.");
                if (edited) {
                    opt.rate = rate == 1 ? 48000 : rate == 2 ? 32000 : rate == 3 ? 22050 : 0;
                    if (opt.rate == 0 && !opt.mono) project_.musicBuild.erase(rel);
                    else project_.musicBuild[rel] = opt;
                    commitChange();
                    statusMessage_ = "Music build settings changed - rebuild to apply";
                }
                if (const AssetUsage* use = assetUsageFor(rel); use && !use->lines.empty())
                    ui::UsedBy("Used by", use->lines);
                if (ImGui::SmallButton("Delete track..."))
                    requestAssetDelete(PendingAssetDelete::Music, rel, name);
                ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            }
            ImGui::PopID();
        }
        if (project_.music.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("No music. Import a WAV, or make a track in the Drone Generator.");
            ImGui::PopTextWrapPos();
        }

    } else {
        // Which groups each sound shows in: `here` = this scene can load it,
        // `anywhere` = something in the project can.
        std::vector<char> here(rows.size(), 0), anywhere(rows.size(), 0);
        auto markAll = [&](const std::vector<int>& v, bool inScene) {
            for (const int x : v)
                if (x >= 0 && x < (int)rows.size()) anywhere[x] = 1, here[x] |= inScene ? 1 : 0;
        };
        markAll(soundResShared_, true);
        for (int si = 0; si < (int)soundResBase_.size(); ++si) markAll(soundResBase_[si], si == S);
        for (int si = 0; si < (int)soundResLayer_.size(); ++si)
            for (const auto& l : soundResLayer_[si]) markAll(l, si == S);
        for (const auto& b : soundResBank_) markAll(b, true);

        // "Loaded by N nodes": Set Sound Bank Loaded nodes naming a bank with
        // their load input wired.
        auto bankNodes = [&](const std::string& name) {
            int n = 0;
            for (const SceneData& sc : project_.scenes)
                for (const SceneObject& o : sc.objects)
                    for (const FlowNode& fn : o.flowGraph.nodes) {
                        if (fn.type != "SetSoundBankLoaded" || fn.str != name) continue;
                        bool loads = false;
                        for (const FlowLink& l : o.flowGraph.links)
                            loads |= l.kind == FlowLinkExec && l.toNode == fn.id && l.toPin == 0;
                        n += loads;
                    }
            return n;
        };
        // A group's total, only where it adds something to its rows' sizes.
        auto groupSize = [&](const std::vector<int>& v) {
            return v.empty() ? std::string("empty") : v.size() > 1 ? bytesText(sumOf(v)) : std::string();
        };
        auto groupHeader = [&](const ImVec4& c, bool hollow, const char* kind, const std::string& name,
                               const std::string& note, const std::string& right, const char* tip) {
            ImGui::Dummy(ImVec2(0, scaled(2)));
            swatch(c, hollow);
            ImGui::PushFont(ui::boldFont(), 0.0f);
            ImGui::TextUnformatted(kind);
            ImGui::PopFont();
            if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip);
            if (!name.empty()) {
                ImGui::SameLine();
                ImGui::TextUnformatted(name.c_str());
            }
            if (!note.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", note.c_str());
            }
            if (!right.empty()) rightText(right, sem.textDim);
        };
        auto divider = [&] {
            ImGui::Dummy(ImVec2(0, scaled(2)));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                                                col(sem.border));
            ImGui::Dummy(ImVec2(0, scaled(2)));
        };
        // A rounded tag in a residency colour ("Layer building-a").
        bool pillFirst = true;
        auto pill = [&](const std::string& text, const ImVec4& c) {
            const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            const float d = fs * 0.45f, padX = fs * 0.6f, h = ImGui::GetTextLineHeight() + scaled(6);
            const float w = padX * 2 + d + fs * 0.4f + ts.x;
            ui::ChipFlow(w, pillFirst);
            pillFirst = false;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(ImVec4(c.x, c.y, c.z, 0.16f)), h * 0.5f);
            const float cy = p.y + h * 0.5f;
            dl->AddRectFilled(ImVec2(p.x + padX, cy - d * 0.5f), ImVec2(p.x + padX + d, cy + d * 0.5f), col(c),
                              scaled(1.5f));
            dl->AddText(ImVec2(p.x + padX + d + fs * 0.4f, cy - ts.y * 0.5f), col(c), text.c_str());
            ImGui::Dummy(ImVec2(w, h));
        };

        // The inspector under the selected sound's first row.
        bool inspected = false;
        auto inspector = [&](int i) {
            const SoundRow& r = rows[i];
            const fs::path abs = fs::path(project_.dir) / r.rel;
            const std::string name = abs.filename().string();
            ImGui::PushID("insp");
            if (ui::BeginCard("##soundinspect")) {
                const std::string& issue = wavIssue(r.rel, true);
                const bool tooBig = r.adpcm > kSpu2SampleBudgetBytes;
                if (tooBig) {
                    ImGui::TextColored(sem.danger, "~%s - larger than SPU2, plays nothing",
                                       bytesText(r.adpcm).c_str());
                    // A sound too long for SPU2 is a music track: move it to res/audio
                    // (only while nothing names it as a sound - a reference would break).
                    const AssetUsage* use = assetUsageFor(r.rel);
                    const bool used = use && use->total() > 0;
                    ImGui::BeginDisabled(used);
                    if (ImGui::SmallButton("Make music")) {
                        const fs::path dst = fs::path(project_.dir) / "res" / "audio" / abs.filename();
                        std::error_code ec;
                        fs::create_directories(dst.parent_path(), ec);
                        fs::rename(abs, dst, ec);
                        if (ec) {
                            statusMessage_ = "Could not move " + name + ": " + ec.message();
                        } else {
                            project_.sounds.erase(
                                std::remove(project_.sounds.begin(), project_.sounds.end(), r.rel),
                                project_.sounds.end());
                            project_.music.push_back("res/audio/" + abs.filename().string());
                            assetsChanged();
                            saveAll(("Moved " + name + " to Music").c_str());
                        }
                    }
                    ImGui::EndDisabled();
                    if (used && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Something plays it as a sound - change that first.");
                } else if (!issue.empty()) {
                    ImGui::TextColored(sem.warn, "%s", issue.c_str());
                    if (ImGui::SmallButton("Convert")) {
                        std::string err;
                        statusMessage_ = wavconvert::convertTo16(abs, 22050, err, true)
                                             ? name + " converted to mono 16-bit PCM 22050 Hz"
                                             : name + ": conversion failed - " + err;
                        wavIssueCache_.clear();
                    }
                }

                // Loads with: every place that keeps it loaded, across scenes.
                smallCaps("Loads with");
                pillFirst = true;
                const char* why = nullptr;
                if (has(soundResShared_, i)) {
                    pill("Always", kAlways);
                    why = "In SPU2 RAM for the whole game.";
                }
                for (int si = 0; si < (int)soundResBase_.size(); ++si)
                    if (has(soundResBase_[si], i)) {
                        pill("Scene " + project_.scenes[si].name, kScene);
                        if (!why) why = "Loads when the scene starts, freed when it ends.";
                    }
                for (int si = 0; si < (int)soundResLayer_.size(); ++si)
                    for (int l = 0; l < (int)soundResLayer_[si].size(); ++l)
                        if (has(soundResLayer_[si][l], i)) {
                            std::string t = "Layer " + project_.scenes[si].layers[l].name;
                            if (si != S) t += " (" + project_.scenes[si].name + ")";
                            pill(t, kLayer);
                            if (!why) why = "Loads when its layer streams in, freed when it streams out.";
                        }
                for (int b = 0; b < (int)soundResBank_.size(); ++b)
                    if (has(soundResBank_[b], i)) {
                        pill("Bank " + project_.soundBanks[b].name, kBank);
                        if (!why)
                            why = "Loads while a Set Sound Bank Loaded node has loaded the bank. "
                                  "Played before then, it is silent.";
                    }
                if (!why) {
                    pill("Nothing", sem.textDim);
                    why = "Nothing plays it, so it is never loaded.";
                }
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", why);
                ImGui::PopTextWrapPos();

                ImGui::Dummy(ImVec2(0, scaled(4)));
                if (const AssetUsage* use = assetUsageFor(r.rel)) {
                    const int hit = ui::UsedBy("Played by", use->lines);
                    if (hit >= 0 && hit < (int)use->objectRefs.size())
                        openGraphOf(use->objectRefs[hit].first, use->objectRefs[hit].second);
                }

                ImGui::Dummy(ImVec2(0, scaled(4)));
                smallCaps("In banks");
                for (int b = 0; b < (int)project_.soundBanks.size(); ++b) {
                    auto& members = project_.soundBanks[b].sounds;
                    const auto it = std::find(members.begin(), members.end(), r.rel);
                    if (it == members.end()) continue;
                    ImGui::PushID(b);
                    const std::string lbl = project_.soundBanks[b].name + "  x";
                    ui::ChipFlow(ImGui::CalcTextSize(lbl.c_str()).x + ImGui::GetStyle().FramePadding.x * 2, false);
                    if (ImGui::SmallButton(lbl.c_str())) {
                        members.erase(it);
                        commitChange();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Take it out of %s", project_.soundBanks[b].name.c_str());
                    ImGui::PopID();
                }
                ui::ChipFlow(ImGui::CalcTextSize("+ Add to bank").x + ImGui::GetStyle().FramePadding.x * 2, false);
                if (ImGui::SmallButton("+ Add to bank")) ImGui::OpenPopup("##addtobank");
                if (ImGui::BeginPopup("##addtobank")) {
                    for (int b = 0; b < (int)project_.soundBanks.size(); ++b) {
                        auto& members = project_.soundBanks[b].sounds;
                        if (std::find(members.begin(), members.end(), r.rel) != members.end()) continue;
                        ImGui::PushID(b);
                        if (ImGui::Selectable(project_.soundBanks[b].name.c_str())) {
                            members.push_back(r.rel);
                            commitChange();
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Selectable("New bank")) {
                        std::string bn;
                        for (int k = 1; bn.empty(); ++k) {
                            const std::string c = "Bank " + std::to_string(k);
                            bool used = false;
                            for (const Project::SoundBank& b : project_.soundBanks) used |= b.name == c;
                            if (!used) bn = c;
                        }
                        project_.soundBanks.push_back({bn, {r.rel}});
                        commitChange();
                    }
                    ImGui::EndPopup();
                }

                ImGui::Dummy(ImVec2(0, scaled(4)));
                auto& pinned = project_.soundsAlwaysLoaded;
                const auto pin = std::find(pinned.begin(), pinned.end(), r.rel);
                bool always = pin != pinned.end();
                if (ui::Switch("Always loaded (for a sound your own C++ plays)", &always)) {
                    if (always) pinned.push_back(r.rel);
                    else pinned.erase(pin);
                    commitChange();
                }

                ImGui::Dummy(ImVec2(0, scaled(2)));
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                const char* del = "Delete sound...";
                ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(del).x -
                                     ImGui::GetStyle().FramePadding.x * 2);
                if (ImGui::SmallButton(del)) requestAssetDelete(PendingAssetDelete::Sound, r.rel, name);
                ImGui::PopStyleColor();
            }
            ui::EndCard();
            ImGui::PopID();
        };

        // One sound row: play, name over its length and first use, size.
        auto soundRow = [&](int i, const char* group) {
            const SoundRow& r = rows[i];
            const fs::path abs = fs::path(project_.dir) / r.rel;
            const std::string name = abs.filename().string();
            ImGui::PushID(group);
            ImGui::PushID(i);
            const bool selRow = railAudioSel_ == r.rel;
            playButton(r.rel);
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::Selectable(name.c_str(), selRow, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x - scaled(70), 0)))
                railAudioSel_ = selRow ? std::string() : r.rel;
            const bool tooBig = r.adpcm > kSpu2SampleBudgetBytes;
            const std::string& issue = wavIssue(r.rel, true);
            if (tooBig) {
                ImGui::TextColored(sem.danger, "larger than SPU2 - plays nothing");
            } else if (!issue.empty()) {
                ImGui::TextColored(sem.warn, "needs converting");
            } else {
                const bool loop = name.size() > 9 && name.compare(name.size() - 9, 9, "-loop.wav") == 0;
                ImGui::TextDisabled("%.1f s%s - ", wavSeconds(abs), loop ? " loop" : "");
                ImGui::SameLine(0, 0);
                usedLine(r.rel);
            }
            ImGui::EndGroup();
            rightText(bytesText(r.adpcm), tooBig ? sem.danger : sem.textDim);
            if (selRow && !inspected) {
                inspected = true;
                inspector(i);
            }
            ImGui::PopID();
            ImGui::PopID();
        };

        const std::string sceneName = sceneCount > 0 ? project_.scenes[S].name : std::string();
        // Always loaded
        groupHeader(kAlways, false, "Always loaded", "", "every scene",
                    groupSize(soundResShared_),
                    "Ticked Always loaded, played by a prefab, or loaded by every scene.");
        for (const int x : soundResShared_) soundRow(x, "always");
        // Scene
        const std::vector<int> noneV;
        const std::vector<int>& base = S < (int)soundResBase_.size() ? soundResBase_[S] : noneV;
        groupHeader(kScene, false, "Scene", sceneName, "", groupSize(base),
                    "Emitters and vehicles outside any layer, and Play Sound nodes - loaded\n"
                    "when the scene starts, freed when it ends.");
        for (const int x : base) soundRow(x, "scene");
        // Layers of this scene that carry sounds
        if (S < (int)soundResLayer_.size()) {
            bool first = true;
            for (int l = 0; l < (int)soundResLayer_[S].size(); ++l) {
                const auto& v = soundResLayer_[S][l];
                if (v.empty()) continue;
                if (first) divider();
                first = false;
                ImGui::PushID(l);
                groupHeader(kLayer, false, "Layer", project_.scenes[S].layers[l].name, "", groupSize(v),
                            "The emitters and vehicles of this streaming layer - loaded while\n"
                            "the layer is in.");
                for (const int x : v) soundRow(x, "layer");
                ImGui::PopID();
            }
        }
        // Banks (project-wide)
        if (!project_.soundBanks.empty()) divider();
        for (int b = 0; b < (int)project_.soundBanks.size(); ++b) {
            Project::SoundBank& bank = project_.soundBanks[b];
            ImGui::PushID(b + 5000);
            const int nodes = bankNodes(bank.name);
            const std::vector<int>& v = b < (int)soundResBank_.size() ? soundResBank_[b] : noneV;
            const std::string note = nodes == 0 ? "- no node loads it"
                                                : "- loaded by " + std::to_string(nodes) +
                                                      (nodes == 1 ? " node" : " nodes");
            groupHeader(kBank, false, "Bank", bank.name, note, "", nullptr);
            // Size, then the bank's menu, at the right end.
            const float mb = ImGui::GetTextLineHeight() + scaled(4);
            const std::string size = groupSize(v);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - mb - ImGui::CalcTextSize(size.c_str()).x - fs * 0.5f);
            ImGui::TextDisabled("%s", size.c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - mb);
            if (ui::MoreButton("Bank options", mb)) ImGui::OpenPopup("##bankpop");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Rename or delete the bank");
            bool deleted = false;
            if (ImGui::BeginPopup("##bankpop")) {
                char nm[64];
                std::snprintf(nm, sizeof nm, "%s", bank.name.c_str());
                ImGui::SetNextItemWidth(fs * 12.0f);
                if (ImGui::IsWindowAppearing()) soundBankRenameFrom_.clear();
                if (ImGui::InputText("Name##bankname", nm, sizeof nm)) {
                    if (soundBankRenameFrom_.empty()) soundBankRenameFrom_ = bank.name;
                    bank.name = nm;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    // A Set Sound Bank Loaded node names its bank; keep them pointing at it.
                    const std::string from = soundBankRenameFrom_;
                    soundBankRenameFrom_.clear();
                    if (bank.name.empty()) bank.name = from;
                    for (SceneData& sc : project_.scenes)
                        for (SceneObject& o : sc.objects)
                            for (FlowNode& n : o.flowGraph.nodes)
                                if (n.type == "SetSoundBankLoaded" && n.str == from && !from.empty())
                                    n.str = bank.name;
                    commitChange();
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                if (ImGui::MenuItem("Delete bank")) deleted = true;
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            if (deleted) {
                project_.soundBanks.erase(project_.soundBanks.begin() + b);
                commitChange();
                ImGui::PopID();
                break;
            }
            for (const int x : v) soundRow(x, "bank");
            // A sound that is in the bank but always loaded anyway still shows.
            for (const std::string& snd : bank.sounds)
                for (int i = 0; i < (int)rows.size(); ++i)
                    if (rows[i].rel == snd && !has(v, i)) soundRow(i, "bankshared");
            const std::string add = "+ Add a sound to " + bank.name;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            if (ui::GhostButton(add.c_str())) ImGui::OpenPopup("##addsound");
            if (ImGui::BeginPopup("##addsound")) {
                int offered = 0;
                for (const SoundRow& r : rows) {
                    if (std::find(bank.sounds.begin(), bank.sounds.end(), r.rel) != bank.sounds.end()) continue;
                    if (ImGui::Selectable(fs::path(r.rel).filename().string().c_str())) {
                        bank.sounds.push_back(r.rel);
                        commitChange();
                    }
                    ++offered;
                }
                if (!offered) ImGui::TextDisabled("Every sound is in it already.");
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        // Sounds the other scenes load, and sounds nothing loads.
        std::vector<int> other, never;
        for (int i = 0; i < (int)rows.size(); ++i) {
            if (!anywhere[i]) never.push_back(i);
            else if (!here[i]) other.push_back(i);
        }
        if (!other.empty()) {
            divider();
            groupHeader(sem.textDim, false, "Other scenes", "", "", groupSize(other),
                        "Loaded only by other scenes - pick one in the Scene menu above.");
            for (const int x : other) soundRow(x, "other");
        }
        divider();
        groupHeader(sem.textDim, true, "Never loaded", "", "nothing plays it",
                    groupSize(never),
                    "No emitter, vehicle, Play Sound node or bank uses it.");
        for (const int x : never) soundRow(x, "never");
        if (project_.sounds.empty())
            ImGui::TextDisabled("No sounds. Import a WAV: effects, footsteps, engine loops.");
    }
    if (const std::string err = clipPlayer_->error(); !err.empty()) ImGui::TextColored(sem.danger, "%s", err.c_str());
    ImGui::EndChild();

    if (ImGui::Button("+ New bank")) {
        std::string name;
        for (int k = 1; name.empty(); ++k) {
            const std::string c = "Bank " + std::to_string(k);
            bool used = false;
            for (const Project::SoundBank& b : project_.soundBanks) used |= b.name == c;
            if (!used) name = c;
        }
        project_.soundBanks.push_back({name, {}});
        commitChange();
        audioTab_ = 0;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A named set of sounds a Set Sound Bank Loaded node loads and\n"
                          "unloads - for sounds no layer or emitter brings, like a mission's.");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    rightText("Imports convert to what the PS2 plays", sem.textDim);
}

// ============================================================================
// Scripts
// ============================================================================

// Project > Scripts (ScriptsPage design): everything that runs code, as
// collapsible sections - Object scripts (grouped by file, an extension being
// one group), Global scripts, Flow graphs, VU programs, Extensions - under a
// search field and filter chips. An object chip selects that object and
// frames it in the viewport; a class name opens its file at the class.
void App::drawScriptsSection() {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##railscriptsearch", "Search classes, files, objects", railScriptSearch_,
                             sizeof railScriptSearch_);
    uiscript::nameItem(ImGui::GetItemID(), "Search classes, files, objects");

    // --- gather -------------------------------------------------------------------
    const SceneData& scene = project_.active();
    std::vector<int> graphs;
    for (int i = 0; i < (int)scene.objects.size(); ++i)
        if (!scene.objects[i].flowGraph.nodes.empty()) graphs.push_back(i);

    // One group per src/scripts file, one per extension (all its files).
    struct ClassRow { std::string cls, file; };
    struct Group {
        std::string title;    // "cookbook/motion_math.cpp" or "extensions/essentials"
        std::string open;     // what the group's VS Code button opens
        std::string version;  // extension manifest version
        bool extension = false;
        std::vector<ClassRow> classes;
    };
    std::vector<Group> groups;
    struct GlobalRow { std::string cls, file; };
    std::vector<GlobalRow> globals;
    std::set<std::string> registered;
    std::map<std::string, std::string> extVersion;
    for (const extension::Info& e : extension::list(project_.dir)) extVersion[e.folder] = e.version;
    {
        const fs::path root(project_.dir);
        for (const std::string& d : extension::scriptDirs(project_.dir)) {
            const fs::path dir(d);
            std::error_code ec;
            if (!fs::exists(dir, ec)) continue;
            std::vector<fs::path> files;
            for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".cpp") continue;
                const std::string fn = it->path().filename().string();
                if (fn.size() > 8 && fn.compare(fn.size() - 8, 8, ".gen.cpp") == 0) continue;
                files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (const fs::path& p : files) {
                const std::string rel = fs::relative(p, root, ec).generic_string();  // src/...
                const ScriptFileInfo& info = scanScriptFile(p);
                for (const std::string& c : info.objectClasses) registered.insert(c);
                for (const std::string& g : info.globalClasses) globals.push_back({g, rel});
                static const std::string kExt = "src/extensions/";
                Group* g = nullptr;
                if (rel.rfind(kExt, 0) == 0) {
                    const std::string folder = rel.substr(kExt.size(), rel.find('/', kExt.size()) - kExt.size());
                    const std::string title = "extensions/" + folder;
                    for (Group& e : groups) if (e.title == title) g = &e;
                    if (!g) {
                        groups.push_back({title, kExt + folder + "/extension.json", extVersion[folder], true, {}});
                        g = &groups.back();
                    }
                } else {
                    const std::string title = rel.rfind("src/scripts/", 0) == 0 ? rel.substr(12) : rel;
                    groups.push_back({title, rel, "", false, {}});
                    g = &groups.back();
                }
                for (const std::string& c : info.objectClasses) g->classes.push_back({c, rel});
            }
        }
        // Project files first, extensions after; each block by name.
        std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
            return a.extension != b.extension ? !a.extension : a.title < b.title;
        });
        groups.erase(std::remove_if(groups.begin(), groups.end(),
                                    [](const Group& g) { return g.classes.empty() && g.extension; }),
                     groups.end());
    }
    std::vector<std::string> vu;
    {
        const fs::path dir = fs::path(project_.dir) / "src" / "vu";
        std::error_code ec;
        if (fs::exists(dir, ec))
            for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".cpp")
                    vu.push_back(it->path().filename().string());
        std::sort(vu.begin(), vu.end());
    }
    // Which objects (any scene) attach a class, and classes nobody registers.
    std::map<std::string, std::vector<std::pair<int, int>>> attached;
    for (int si = 0; si < (int)project_.scenes.size(); ++si)
        for (int oi = 0; oi < (int)project_.scenes[si].objects.size(); ++oi)
            for (const std::string& c : project_.scenes[si].objects[oi].scripts) attached[c].push_back({si, oi});
    std::vector<std::string> missing;
    for (const auto& [c, refs] : attached)
        if (!registered.count(c)) missing.push_back(c);
    int nClasses = 0, nUnused = 0;
    for (const Group& g : groups)
        for (const ClassRow& r : g.classes) {
            ++nClasses;
            nUnused += attached.count(r.cls) ? 0 : 1;
        }
    objectScriptNames();  // fills the class cache (fields, lines) scriptClassInfo reads

    // --- filter chips -----------------------------------------------------------------
    // 0 All, 1 Object scripts, 2 Global, 3 Graphs, 4 VU, 5 Unused.
    {
        const std::string labels[] = {"All", "Object scripts " + std::to_string(nClasses),
                                      "Global " + std::to_string(globals.size()),
                                      "Graphs " + std::to_string(graphs.size()),
                                      "VU " + std::to_string(vu.size()), "Unused " + std::to_string(nUnused)};
        if (railScriptFilter_ < 0 || railScriptFilter_ > 5) railScriptFilter_ = 0;
        bool first = true;
        for (int c = 0; c < 6; ++c) {
            if (c == 4 && vu.empty()) continue;
            if (c == 5 && nUnused == 0) continue;
            const float w = ui::ChipButtonWidth(labels[c].c_str());
            if (!first) {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
            }
            first = false;
            if (ui::ChipButton(labels[c].c_str(), railScriptFilter_ == c)) railScriptFilter_ = c;
        }
    }

    // The last build's first error in a script, where you look for it.
    {
        const LogView& lv = logOut_;
        for (const logview::Line& l : lv.lines) {
            if (l.level != logview::Level::Error || l.cont) continue;
            const std::string text = lv.text.substr(l.begin, l.end - l.begin);
            const size_t at = text.find("scripts/") != std::string::npos ? text.find("scripts/")
                                                                           : text.find("scripts\\");
            if (at == std::string::npos) continue;
            const size_t colon = text.find(':', at);
            const std::string file = colon == std::string::npos ? std::string() : text.substr(at + 8, colon - at - 8);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(sem.danger.x, sem.danger.y, sem.danger.z, 0.14f));
            ImGui::BeginChild("##scripterr", ImVec2(0, ImGui::GetFrameHeight() * 2.4f), ImGuiChildFlags_Borders);
            ImGui::TextColored(sem.danger, "Last build: an error in a script");
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - scaled(70));
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::TextDisabled("%s", text.substr(at).c_str());
            }
            ImGui::PopTextWrapPos();
            if (!file.empty()) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - scaled(64));
                if (ImGui::SmallButton("Open file")) openInVSCode("src/scripts/" + file);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            break;
        }
    }

    const int filter = railScriptFilter_;
    const bool showCpp = filter == 0 || filter == 1 || filter == 5;
    const bool showGlobal = filter == 0 || filter == 2;
    const bool showFlow = filter == 0 || filter == 3;
    const bool showVu = filter == 0 || filter == 4;
    const bool searching = railScriptSearch_[0] != '\0';
    // Show in Scripts page (Properties): open the way to that class once.
    const std::string focus = scriptsPageFocus_;
    if (!focus.empty() && (filter == 2 || filter == 3 || filter == 4)) railScriptFilter_ = 0;
    static std::string flashClass;
    static double flashUntil = 0.0;

    ImGui::BeginChild("##railscripts", ImVec2(0, ImMax(scaled(120), ImGui::GetContentRegionAvail().y -
                                                                     ImGui::GetTextLineHeightWithSpacing() * 2.2f)));
    ImGuiStorage* st = ImGui::GetStateStorage();
    // A fold row: chevron, title (bold for a section, mono for a file group),
    // a dim summary at the right end. `label` is the --ui-script name.
    auto foldRow = [&](const std::string& label, const std::string& title, const std::string& summary,
                       bool section, bool defOpen, bool forceOpen, float rightReserve,
                       const char* pill = nullptr) {
        const ImGuiID key = ImGui::GetID((label + "#open").c_str());
        bool open = st->GetBool(key, defOpen);
        if (forceOpen && !open) st->SetBool(key, open = true);
        if (section) {
            ImGui::Dummy(ImVec2(0, scaled(2)));
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y),
                                                col(sem.border));
            ImGui::Dummy(ImVec2(0, scaled(3)));
        }
        const float h = ImGui::GetFrameHeight();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImMax(h, ImGui::GetContentRegionAvail().x - rightReserve);
        if (ImGui::InvisibleButton(label.c_str(), ImVec2(w, h))) st->SetBool(key, open = !open);
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!section && (hov || open))
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(sem.surface, hov ? 1.0f : 0.6f),
                              ImGui::GetStyle().FrameRounding);
        const float cy = p.y + h * 0.5f;
        ui::DrawRtIcon(dl, open ? ui::RtIcon::ChevronDown : ui::RtIcon::ChevronRight, ImVec2(p.x + h * 0.4f, cy),
                       h * 0.42f, col(hov ? sem.text : sem.textDim));
        float x = p.x + h * 0.8f;
        // Right end: summary, then (extensions) the version pill left of it.
        const float sfs = fs * 0.85f;
        const ImVec2 ss = ImGui::GetFont()->CalcTextSizeA(sfs, FLT_MAX, 0.0f, summary.c_str());
        float rx = p.x + w - scaled(6);
        dl->AddText(ImGui::GetFont(), sfs, ImVec2(rx - ss.x, cy - ss.y * 0.5f), col(sem.textDim), summary.c_str());
        rx -= ss.x + scaled(8);
        if (pill && pill[0]) {
            const ImVec2 ps = ImGui::GetFont()->CalcTextSizeA(sfs * 0.95f, FLT_MAX, 0.0f, pill);
            const float ph = sfs + 4.0f;
            dl->AddRectFilled(ImVec2(rx - ps.x - sfs, cy - ph * 0.5f), ImVec2(rx, cy + ph * 0.5f), col(sem.field),
                              ph * 0.5f);
            dl->AddText(ImGui::GetFont(), sfs * 0.95f, ImVec2(rx - ps.x - sfs * 0.5f, cy - ps.y * 0.5f),
                        col(sem.textDim), pill);
            rx -= ps.x + sfs + scaled(6);
        }
        ImFont* f = section ? (ui::boldFont() ? ui::boldFont() : ImGui::GetFont())
                            : (ui::monoFont() ? ui::monoFont() : ImGui::GetFont());
        const float tfs = section ? fs : fs * 0.9f;
        const ImVec2 ts = f->CalcTextSizeA(tfs, FLT_MAX, 0.0f, title.c_str());
        ImGui::PushFont(f, tfs);
        ImGui::RenderTextEllipsis(dl, ImVec2(x, cy - ts.y * 0.5f), ImVec2(rx, cy + ts.y * 0.5f + 1.0f), rx,
                                  title.c_str(), nullptr, &ts);
        ImGui::PopFont();
        return open;
    };
    // An object chip: select + frame (switching scene first when it lives in
    // another one).
    auto objChip = [&](int si, int oi, bool* first) {
        const SceneObject& o = project_.scenes[si].objects[oi];
        const std::string l = o.name + "##chip" + std::to_string(si) + "_" + std::to_string(oi);
        const float w = ui::ChipButtonWidth(o.name.c_str());
        if (!*first) {
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
        }
        *first = false;
        const bool sel = si == project_.activeScene && isSelected(oi);
        if (ui::ChipButton(l.c_str(), sel)) {
            if (si != project_.activeScene) selectScene(si);
            focusObject(oi);
        }
        uiscript::nameItem(ImGui::GetItemID(), o.name.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(si == project_.activeScene ? "Select %s and frame it in the viewport"
                                                         : "Switch to %s's scene, select it and frame it",
                              o.name.c_str());
    };
    auto onCount = [&](const std::string& cls) {
        const auto it = attached.find(cls);
        return it == attached.end() ? 0 : (int)it->second.size();
    };
    const float rowIndent = scaled(22);

    // --- Object scripts -------------------------------------------------------------
    if (showCpp) {
        std::string sum = countNoun(nClasses, "class", "classes") + " \xC2\xB7 " +
                          countNoun((size_t)std::count_if(groups.begin(), groups.end(),
                                                          [](const Group& g) { return !g.classes.empty(); }),
                                    "file", "files");
        if (foldRow("Object scripts section", "Object scripts", sum, true, true, !focus.empty(), 0.0f)) {
            for (const Group& g : groups) {
                // Search hits a group through its path, a class or an object name.
                std::vector<const ClassRow*> rows;
                for (const ClassRow& r : g.classes) {
                    if (filter == 5 && onCount(r.cls) > 0) continue;
                    bool hit = !searching || matches(g.title, railScriptSearch_) || matches(r.cls, railScriptSearch_);
                    if (!hit)
                        if (auto it = attached.find(r.cls); it != attached.end())
                            for (const auto& [si, oi] : it->second)
                                hit |= matches(project_.scenes[si].objects[oi].name, railScriptSearch_);
                    if (hit) rows.push_back(&r);
                }
                if (rows.empty()) continue;
                // (a file with no object class: its globals list under Global scripts)
                int on = 0;
                for (const ClassRow& r : g.classes) on += onCount(r.cls);
                std::string gsum = g.classes.empty() ? std::string("no object script")
                                   : countNoun(g.classes.size(), "class", "classes") + " \xC2\xB7 " +
                                         (on ? "on " + std::to_string(on) : std::string("unused"));
                bool hasFocus = false;
                for (const ClassRow* r : rows) hasFocus |= r->cls == focus;
                ImGui::PushID(g.title.c_str());
                const float btn = ImGui::GetFrameHeight();
                const bool open = foldRow(g.title, g.title, gsum, false, groups.size() <= 2 || searching,
                                          hasFocus || (searching && !rows.empty()), btn + scaled(4),
                                          g.version.empty() ? nullptr : ("v" + g.version).c_str());
                ImGui::SameLine(0.0f, scaled(4));
                if (ui::GlyphButton(("Open " + g.title + " in VS Code").c_str(), ui::Glyph::Code, btn,
                                    g.extension ? "Open the extension's manifest in VS Code" : "Open in VS Code"))
                    openInVSCode(g.open);
                if (open) {
                    for (const ClassRow* r : rows) {
                        ImGui::PushID(r->cls.c_str());
                        const scriptprops::ClassInfo* ci = scriptClassInfo(r->cls);
                        const int n = onCount(r->cls);
                        ImGui::Indent(rowIndent);
                        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                        if (r->cls == focus) {
                            ImGui::SetScrollHereY(0.3f);
                            flashClass = focus;
                            flashUntil = ImGui::GetTime() + 1.6;
                            scriptsPageFocus_.clear();
                        }
                        // Name: opens the file at the class.
                        ImGui::AlignTextToFramePadding();
                        {
                            ui::FontScope bold(ui::boldFont() ? ui::boldFont() : ImGui::GetFont());
                            ImGui::PushStyleColor(ImGuiCol_Text, n ? sem.text : sem.textDim);
                            ImGui::TextUnformatted(r->cls.c_str());
                            ImGui::PopStyleColor();
                        }
                        const bool nameHov = ImGui::IsItemHovered();
                        if (nameHov) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), b, col(sem.textDim));
                            ImGui::SetTooltip("Open %s:%d in VS Code", ci ? ci->file.c_str() : r->file.c_str(),
                                              ci ? ci->line : 0);
                        }
                        if (nameHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            openInVSCode(ci ? ci->file : r->file, ci ? ci->line : 0);
                        if (ci) {
                            ImGui::SameLine();
                            ImGui::TextColored(sem.textDim, "%s", countNoun(ci->fields.size(), "field", "fields").c_str());
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ci->description.empty())
                                ImGui::SetTooltip("%s", ci->description.c_str());
                        }
                        // Right end: "on N objects" / "unused" (+ Attach to selected).
                        const std::string pill = n ? "on " + countNoun(n, "object", "objects") : std::string("unused");
                        const char* attachL = "Attach to selected";
                        const float attachW = n ? 0.0f : ImGui::CalcTextSize(attachL).x +
                                                             ImGui::GetStyle().FramePadding.x * 2.0f + scaled(6);
                        const float pw = ui::PillWidth(pill.c_str());
                        const float avail = ImGui::GetContentRegionMax().x;
                        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + scaled(6), avail - pw - attachW));
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);  // centre on the row
                        ui::Pill(pill.c_str(), n ? 0 : 1);
                        if (!n) {
                            ImGui::SameLine();
                            const bool canAttach = selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size();
                            if (ui::SecondaryButton((std::string(attachL) + "##" + r->cls).c_str(), 0.0f, canAttach)) {
                                project_.objects()[selectedObject_].scripts.push_back(r->cls);
                                commitChange();
                                statusMessage_ = "Attached " + r->cls + " to " + project_.objects()[selectedObject_].name;
                            }
                            uiscript::nameItem(ImGui::GetItemID(), ("Attach " + r->cls + " to selected").c_str());
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
                                ImGui::SetTooltip(canAttach ? "Attach to %s" : "Select an object first",
                                                  canAttach ? project_.objects()[selectedObject_].name.c_str() : "");
                        }
                        // Object chips, wrapping; the first 8, then "+N more".
                        if (n) {
                            const ImGuiID moreKey = ImGui::GetID("more");
                            const bool all = st->GetBool(moreKey, false);
                            bool first = true;
                            int shown = 0;
                            for (const auto& [si, oi] : attached[r->cls]) {
                                if (!all && shown >= 8) break;
                                objChip(si, oi, &first);
                                ++shown;
                            }
                            if (n > 8) {
                                const std::string m = all ? "Fewer" : "+" + std::to_string(n - 8) + " more";
                                ImGui::SameLine();
                                if (ImGui::GetContentRegionAvail().x < ui::ChipButtonWidth(m.c_str())) ImGui::NewLine();
                                if (ui::ChipButton(m.c_str())) st->SetBool(moreKey, !all);
                            }
                        }
                        if (flashClass == r->cls && ImGui::GetTime() < flashUntil) {
                            const float a = (float)(flashUntil - ImGui::GetTime()) / 1.6f;
                            ImGui::GetWindowDrawList()->AddRectFilled(
                                ImVec2(rowStart.x - scaled(6), rowStart.y - scaled(2)),
                                ImVec2(rowStart.x + ImGui::GetContentRegionAvail().x, ImGui::GetCursorScreenPos().y),
                                col(sem.accent, 0.18f * a), ImGui::GetStyle().FrameRounding);
                        }
                        ImGui::Unindent(rowIndent);
                        ImGui::Dummy(ImVec2(0, scaled(3)));
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
            }
            // Attached classes nobody registers: the object would get no script.
            for (const std::string& c : missing) {
                if (filter == 5 || !matches(c, railScriptSearch_)) continue;
                ImGui::PushID(c.c_str());
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(sem.warn, "%s", c.c_str());
                ImGui::SameLine();
                ImGui::TextColored(sem.textDim, "attached, but no class registers it");
                const float cw = ImGui::CalcTextSize("Create").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SameLine(ImMax(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - cw));
                if (ui::SecondaryButton("Create")) {
                    openNewScriptPopup_ = true;
                    newScriptKind_ = 0;
                    newScriptError_.clear();
                    newScriptAttachTo_ = -1;
                    std::snprintf(newScriptName_, sizeof newScriptName_, "%s", c.c_str());
                }
                ImGui::Indent(rowIndent);
                bool first = true;
                int shown = 0;
                for (const auto& [si, oi] : attached[c])
                    if (shown++ < 8) objChip(si, oi, &first);
                ImGui::Unindent(rowIndent);
                ImGui::PopID();
            }
            if (groups.empty() && missing.empty()) {
                if (ui::EmptyState(ui::Glyph::Code, "No object scripts",
                                   "An object script runs on every object it is attached to (Properties > Scripts).",
                                   "New script...##railempty")) {
                    openNewScriptPopup_ = true;
                    newScriptKind_ = 0;
                    newScriptError_.clear();
                    newScriptAttachTo_ = -1;
                }
            }
        }
    }

    // --- Global scripts -------------------------------------------------------------
    if (showGlobal && (!globals.empty() || filter == 2)) {
        if (foldRow("Global scripts section", "Global scripts", "run every frame, every scene", true, true, false, 0.0f)) {
            for (const GlobalRow& g : globals) {
                if (!matches(g.cls, railScriptSearch_) && !matches(g.file, railScriptSearch_)) continue;
                ImGui::PushID(g.cls.c_str());
                ImGui::Indent(rowIndent);
                ImGui::AlignTextToFramePadding();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + scaled(4), p.y + ImGui::GetFrameHeight() * 0.5f),
                                                            scaled(3.5f), col(ImVec4(0.95f, 0.56f, 0.88f, 1.0f)));
                ImGui::Dummy(ImVec2(scaled(10), 0));
                ImGui::SameLine();
                // The name without its namespace (the tooltip has it all);
                // the file only where it fits before the button.
                const size_t ns = g.cls.rfind("::");
                const std::string shortName = ns == std::string::npos ? g.cls : g.cls.substr(ns + 2);
                const float btnX = ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight();
                {
                    ui::FontScope bold(ui::boldFont() ? ui::boldFont() : ImGui::GetFont());
                    ImGui::TextUnformatted(shortName.c_str());
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s\n%s", g.cls.c_str(), g.file.c_str());
                const std::string file = fs::path(g.file).filename().string();
                ImGui::SameLine();
                {
                    ui::FontScope mono(ui::monoFont());
                    if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(file.c_str()).x < btnX - scaled(6))
                        ImGui::TextColored(sem.textDim, "%s", file.c_str());
                    else
                        ImGui::NewLine();
                }
                ImGui::SameLine(btnX);
                if (ui::GlyphButton(("Open " + g.cls + " in VS Code").c_str(), ui::Glyph::Code, 0.0f, "Open in VS Code"))
                    openInVSCode(g.file);
                ImGui::Unindent(rowIndent);
                ImGui::PopID();
            }
            if (globals.empty()) ImGui::TextDisabled("No TYRA_GLOBAL_SCRIPT class yet.");
        }
    }

    // --- Flow graphs ----------------------------------------------------------------
    if (showFlow) {
        std::string sum = std::to_string(graphs.size());
        if (graphs.size() == 1) sum += " \xC2\xB7 " + scene.objects[graphs[0]].name;
        if (foldRow("Flow graphs section", "Flow graphs", sum, true, filter == 3, false, 0.0f)) {
            for (int oi : graphs) {
                const SceneObject& o = scene.objects[oi];
                std::string events;
                for (const FlowNode& n : o.flowGraph.nodes)
                    if (const FlowNodeType* t = flowNodeType(n.type); t && std::strcmp(t->category, "Triggers") == 0) {
                        if (events.find(t->title) != std::string::npos) continue;
                        if (!events.empty()) events += ", ";
                        events += t->title;
                    }
                if (!matches(o.name, railScriptSearch_) && !matches(events, railScriptSearch_)) continue;
                char sub[256];
                std::snprintf(sub, sizeof sub, "%d nodes%s%s", (int)o.flowGraph.nodes.size(), events.empty() ? "" : " - ",
                              events.c_str());
                const std::string label = o.name + "##graph" + std::to_string(oi);
                if (ui::ListRow(label.c_str(), sub, false, ui::Glyph::Nodes)) {
                    selectOnly(oi);
                    openGraphOf(project_.activeScene, oi);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Open in the Flow Graph");
            }
            if (graphs.empty()) ImGui::TextDisabled("No object in this scene has a flow graph yet.");
        }
    }

    // --- VU programs ----------------------------------------------------------------
    if (showVu && !vu.empty()) {
        if (foldRow("VU programs section", "VU programs", std::to_string(vu.size()) + " \xC2\xB7 build time", true,
                    filter == 4, false, 0.0f)) {
            for (const std::string& f : vu) {
                if (!matches(f, railScriptSearch_)) continue;
                const std::string label = f + "##vu";
                if (ui::ListRow(label.c_str(), "host - build time: leaves a VU1 microprogram", false, ui::Glyph::Code))
                    openInVSCode("src/vu/" + f);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("C++ compiled and RUN on the host at build time; it replaces the\n"
                                      "engine's program for the material classes it claims\n"
                                      "(docs/vu-authoring.md). Click to open in VS Code.");
            }
        }
    }

    // --- Extensions -----------------------------------------------------------------
    if (filter == 0 || filter == 1) {
        const size_t nExt = extVersion.size();
        if (foldRow("Extensions section", "Extensions", nExt ? std::to_string(nExt) + " installed" : "none installed",
                    true, nExt == 0, false, 0.0f))
            drawExtensionsSection();
    }
    ImGui::EndChild();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Click an object to select it and frame it in the viewport. A class name opens its file "
                        "in VS Code at the class.");
    ImGui::PopTextWrapPos();
}

// Script extensions (docs/script-extensions.md): folders under src/extensions
// that carry scripts and flow nodes from project to project. Listed with their
// manifest; New writes a skeleton, Install copies a folder in, Remove deletes
// one after a confirmation.
void App::drawExtensionsSection() {
    // The list reads every file of every extension: once every two seconds
    // and after any action here, not every frame.
    static std::vector<extension::Info> cached;
    static double scannedAt = -1.0;
    static std::string scannedFor;
    auto rescan = [&] { scannedAt = -1.0; };
    if (scannedAt < 0.0 || scannedFor != project_.dir || ImGui::GetTime() - scannedAt > 2.0) {
        cached = extension::list(project_.dir);
        scannedAt = ImGui::GetTime();
        scannedFor = project_.dir;
    }

    // (The page's Extensions section header carries the title and count.)
    for (const extension::Info& e : cached) {
        ImGui::PushID(e.folder.c_str());
        std::string sub = e.problem.empty()
                              ? (e.version.empty() ? "" : "v" + e.version + " - ") +
                                    std::to_string(e.objectScripts.size()) +
                                    (e.objectScripts.size() == 1 ? " object script, " : " object scripts, ") +
                                    std::to_string(e.flowNodes) + (e.flowNodes == 1 ? " flow node" : " flow nodes")
                              : e.problem;
        const std::string label = e.name + "##ext";
        if (ui::ListRow(label.c_str(), sub.c_str(), false, e.problem.empty() ? ui::Glyph::Code : ui::Glyph::Flag))
            openInVSCode("src/extensions/" + e.folder + "/extension.json");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s%sOpens its manifest in VS Code. Folder: src/extensions/%s",
                              e.description.c_str(), e.description.empty() ? "" : "\n",
                              e.folder.c_str());
        ImGui::Indent(scaled(30));
        // The class tags wrap at the panel edge - one line of nine pushed the
        // Remove button past it, out of reach.
        for (size_t k = 0; k < e.objectScripts.size(); ++k) {
            const float w = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * 0.78f, FLT_MAX, 0,
                                                            e.objectScripts[k].c_str()).x +
                            ImGui::GetFontSize() * 0.78f;
            if (k > 0) {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
            }
            tag(e.objectScripts[k].c_str(), theme::semantics().accent);
        }
        if (extensionRemovePending_ == e.folder) {
            ImGui::TextColored(theme::semantics().danger, "Delete the folder?");
            ImGui::SameLine();
            const bool del = ImGui::SmallButton("Delete");
            uiscript::nameItem(ImGui::GetItemID(), ("Delete " + e.folder).c_str());
            if (del) {
                const std::string err = extension::remove(project_.dir, e.folder);
                statusMessage_ = err.empty() ? "Removed extension " + e.name : err;
                extensionRemovePending_.clear();
                flownode::loadForProject(project_.dir);
                rescan();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Keep")) extensionRemovePending_.clear();
        } else {
            const bool rm = ImGui::SmallButton("Remove");
            uiscript::nameItem(ImGui::GetItemID(), ("Remove " + e.folder).c_str());
            if (rm) extensionRemovePending_ = e.folder;
        }
        ImGui::Unindent(scaled(30));
        ImGui::PopID();
    }
    if (cached.empty())
        ImGui::TextDisabled("Reusable scripts and nodes, shared between projects as a folder.");

    ImGui::SetNextItemWidth(scaled(140));
    ImGui::InputText("##newextname", newExtensionName_, sizeof newExtensionName_);
    uiscript::nameItem(ImGui::GetItemID(), "New extension name");
    ImGui::SameLine();
    if (ImGui::SmallButton("New extension")) {
        const std::string err = extension::scaffold(project_.dir, newExtensionName_);
        const std::string folder = extension::folderFor(newExtensionName_);
        statusMessage_ = err.empty() ? "Created src/extensions/" + folder : err;
        if (err.empty()) {
            flownode::loadForProject(project_.dir);
            openInVSCode("src/extensions/" + folder + "/" + folder + ".cpp");
        }
        rescan();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A skeleton: manifest, one object script, one flow node.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Install from folder...")) {
        const std::string picked = pickFolder();
        if (!picked.empty()) {
            std::string folder;
            const std::string err = extension::install(project_.dir, picked, &folder);
            statusMessage_ = err.empty() ? "Installed extension " + folder : err;
            if (err.empty()) flownode::loadForProject(project_.dir);
            rescan();
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Copies a folder holding extension.json into src/extensions.");
}
