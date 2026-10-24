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
        if (ui::SecondaryButton("Open in Asset Browser")) {
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

    // --- the SPU2 budget ------------------------------------------------------
    // What is resident at once (docs/sound.md, "SPU2 sample RAM"): the shared
    // sounds, plus the scene whose own sounds weigh the most.
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
        templates::SoundResidency res = templates::soundResidency(project_);
        soundResShared_ = std::move(res.shared);
        soundResScene_ = std::move(res.scene);
        soundResSerial_ = modelEditSerial_;
    }
    auto sumOf = [&](const std::vector<int>& slots) {
        uintmax_t b = 0;
        for (const int s : slots) if (s >= 0 && s < (int)rows.size()) b += rows[s].adpcm;
        return b;
    };
    const uintmax_t shared = sumOf(soundResShared_);
    int heavy = -1;
    uintmax_t heavyBytes = 0;
    for (int si = 0; si < (int)soundResScene_.size(); ++si)
        if (const uintmax_t b = sumOf(soundResScene_[si]); b > heavyBytes) heavy = si, heavyBytes = b;
    {
        const uintmax_t total = shared + heavyBytes;
        const double mb = total / (1024.0 * 1024.0);
        const double cap = kSpu2SampleBudgetBytes / (1024.0 * 1024.0);
        ImGui::TextUnformatted("SPU2 sample RAM");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("0.00 / 0.00 MB").x);
        if (total > kSpu2SampleBudgetBytes) ImGui::TextColored(sem.danger, "%.2f / %.2f MB", mb, cap);
        else ImGui::Text("%.2f / %.2f MB", mb, cap);
        // One segment per resident sound, shared ones first; an oversized one
        // in amber.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = scaled(8);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(sem.surface), h * 0.5f);
        float x = p.x;
        auto segments = [&](const std::vector<int>& slots, float alpha) {
            for (size_t i = 0; i < slots.size(); ++i) {
                if (slots[i] < 0 || slots[i] >= (int)rows.size()) continue;
                const uintmax_t b = rows[slots[i]].adpcm;
                const float sw = (float)((double)b / ImMax<double>(kSpu2SampleBudgetBytes, total) * w);
                if (sw < 0.5f) continue;
                const bool big = b > kSpu2SampleBudgetBytes / 4;
                const ImVec4 c = big ? sem.warn
                                     : ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, alpha - 0.08f * (i % 4));
                dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + sw - 1.0f, p.y + h), col(c));
                x += sw;
            }
        };
        segments(soundResShared_, 1.0f);
        if (heavy >= 0) segments(soundResScene_[heavy], 0.6f);
        ImGui::Dummy(ImVec2(w, h));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Reverb rooms take up to 192 KB more from the top of the same RAM.");
        std::string split = "Shared " + bytesText(shared);
        if (heavy >= 0)
            split += "  +  " + project_.scenes[heavy].name + " " + bytesText(heavyBytes) +
                     (soundResScene_.size() > 1 ? "  (heaviest scene)" : "");
        ImGui::TextDisabled("%s", split.c_str());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Shared sounds load at boot. A scene's own load when it starts and are "
                            "freed when it ends. Music streams and takes no SPU2 RAM.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

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
    auto rightText = [&](const std::string& t, const ImVec4& c) {
        const float tw = ImGui::CalcTextSize(t.c_str()).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
        ImGui::TextColored(c, "%s", t.c_str());
    };

    ImGui::BeginChild("##railaudio", ImVec2(0, ImMax(scaled(120), ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() - scaled(4))));
    // --- Music -----------------------------------------------------------------
    ui::Caption("Music", std::to_string(project_.music.size()).c_str());
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
        ImGui::TextDisabled("No music. Import a WAV, or make a track in the Drone Generator.");
    }

    // --- Sounds ------------------------------------------------------------------
    ImGui::Dummy(ImVec2(0, scaled(4)));
    ui::Caption("Sounds", std::to_string(project_.sounds.size()).c_str());
    for (int i = 0; i < (int)rows.size(); ++i) {
        const SoundRow& r = rows[i];
        const fs::path abs = fs::path(project_.dir) / r.rel;
        const std::string name = abs.filename().string();
        ImGui::PushID(i + 1000);
        const bool selRow = railAudioSel_ == r.rel;
        playButton(r.rel);
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Selectable(name.c_str(), selRow, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(ImGui::GetContentRegionAvail().x - scaled(70), 0)))
            railAudioSel_ = selRow ? std::string() : r.rel;
        const std::string& issue = wavIssue(r.rel, true);
        const bool tooBig = r.adpcm > kSpu2SampleBudgetBytes;
        if (tooBig) {
            ImGui::TextColored(sem.danger, "~%s - larger than SPU2, plays nothing", bytesText(r.adpcm).c_str());
        } else if (!issue.empty()) {
            ImGui::TextColored(sem.warn, "%s", issue.c_str());
        } else {
            char len[16];
            std::snprintf(len, sizeof len, "%.1f s ", wavSeconds(abs));
            ImGui::TextDisabled("%s", len);
            ImGui::SameLine(0, 0);
            usedLine(r.rel);
            if (std::find(soundResShared_.begin(), soundResShared_.end(), i) != soundResShared_.end()) {
                ImGui::SameLine();
                ImGui::TextDisabled("- shared");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("In SPU2 RAM for the whole game: always loaded, every scene\n"
                                      "plays it, or a prefab can spawn it anywhere.");
            }
        }
        ImGui::EndGroup();
        rightText(bytesText(r.adpcm), tooBig ? sem.danger : sem.textDim);
        if (!issue.empty() && !tooBig) {
            if (ImGui::SmallButton("Convert")) {
                std::string err;
                statusMessage_ = wavconvert::convertTo16(abs, 22050, err, true)
                                     ? name + " converted to mono 16-bit PCM 22050 Hz"
                                     : name + ": conversion failed - " + err;
                wavIssueCache_.clear();
            }
        } else if (tooBig) {
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
                    project_.sounds.erase(std::remove(project_.sounds.begin(), project_.sounds.end(), r.rel),
                                          project_.sounds.end());
                    project_.music.push_back("res/audio/" + abs.filename().string());
                    assetsChanged();
                    saveAll(("Moved " + name + " to Music").c_str());
                }
            }
            ImGui::EndDisabled();
            if (used && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Something plays it as a sound - change that first.");
        }
        if (selRow) {
            ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            if (const AssetUsage* use = assetUsageFor(r.rel); use && !use->lines.empty())
                ui::UsedBy("Used by", use->lines);
            auto& pinned = project_.soundsAlwaysLoaded;
            const auto pin = std::find(pinned.begin(), pinned.end(), r.rel);
            bool always = pin != pinned.end();
            if (ImGui::Checkbox("Always loaded", &always)) {
                if (always) pinned.push_back(r.rel);
                else pinned.erase(pin);
                commitChange();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Keep it in SPU2 RAM in every scene - for a sound played from\n"
                                  "your own C++, which the scene loading cannot see.");
            if (ImGui::SmallButton("Delete sound..."))
                requestAssetDelete(PendingAssetDelete::Sound, r.rel, name);
            ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
        }
        ImGui::PopID();
    }
    if (project_.sounds.empty())
        ImGui::TextDisabled("No sounds. Import a WAV: effects, footsteps, engine loops.");
    if (const std::string err = clipPlayer_->error(); !err.empty()) ImGui::TextColored(sem.danger, "%s", err.c_str());
    ImGui::EndChild();
    ImGui::TextDisabled("Imports convert to what the PS2 plays.");
}

// ============================================================================
// Scripts
// ============================================================================

void App::drawScriptsSection() {
    const theme::Semantics& sem = theme::semantics();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##railscriptsearch", "Search scripts, classes, objects", railScriptSearch_,
                             sizeof railScriptSearch_);

    // --- gather -------------------------------------------------------------------
    const SceneData& scene = project_.active();
    std::vector<int> graphs;
    for (int i = 0; i < (int)scene.objects.size(); ++i)
        if (!scene.objects[i].flowGraph.nodes.empty()) graphs.push_back(i);

    struct CppFile { std::string rel; ScriptFileInfo info; };
    std::vector<CppFile> cpp;
    std::set<std::string> registered;
    {
        const fs::path dir = fs::path(project_.dir) / "src" / "scripts";
        std::error_code ec;
        if (fs::exists(dir, ec))
            for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".cpp") continue;
                const std::string fn = it->path().filename().string();
                if (fn.size() > 8 && fn.compare(fn.size() - 8, 8, ".gen.cpp") == 0) continue;
                CppFile f;
                f.rel = fs::relative(it->path(), dir, ec).generic_string();
                f.info = scanScriptFile(it->path());
                for (const std::string& c : f.info.objectClasses) registered.insert(c);
                cpp.push_back(std::move(f));
            }
        std::sort(cpp.begin(), cpp.end(), [](const CppFile& a, const CppFile& b) { return a.rel < b.rel; });
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

    {
        std::string flowL = "Flow graphs " + std::to_string(graphs.size()) + "##sf";
        std::string cppL = "C++ " + std::to_string(cpp.size()) + "##sf";
        std::string vuL = "VU " + std::to_string(vu.size()) + "##sf";
        const char* filters[] = {"All##sf", flowL.c_str(), cppL.c_str(), vuL.c_str()};
        ui::Segmented("##scriptfilter", filters, 4, &railScriptFilter_);
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

    const bool showFlow = railScriptFilter_ == 0 || railScriptFilter_ == 1;
    const bool showCpp = railScriptFilter_ == 0 || railScriptFilter_ == 2;
    const bool showVu = railScriptFilter_ == 0 || railScriptFilter_ == 3;
    ImGui::BeginChild("##railscripts", ImVec2(0, ImMax(scaled(120), ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() - scaled(4))));
    auto objChip = [&](int si, int oi) {
        const SceneObject& o = project_.scenes[si].objects[oi];
        const std::string l = o.name + "##chip" + std::to_string(si) + "_" + std::to_string(oi);
        if (ImGui::SmallButton(l.c_str())) {
            if (si == project_.activeScene) selectOnly(oi);
            openGraphOf(si, oi);
        }
        ImGui::SameLine();
    };

    if (showFlow) {
        ui::Caption("Flow graphs", std::to_string(graphs.size()).c_str());
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

    if (showCpp) {
        ImGui::Dummy(ImVec2(0, scaled(4)));
        ui::Caption("C++ scripts", std::to_string(cpp.size()).c_str());
        for (const CppFile& f : cpp) {
            bool hit = matches(f.rel, railScriptSearch_);
            for (const std::string& c : f.info.objectClasses) hit |= matches(c, railScriptSearch_);
            if (!hit) continue;
            ImGui::PushID(f.rel.c_str());
            const std::string label = f.rel + "##cpp";
            const char* sub = f.info.objectClasses.empty() && f.info.globalClasses.empty() ? "no script class"
                              : !f.info.globalClasses.empty() ? "global - runs every frame"
                                                              : "object script";
            if (ui::ListRow(label.c_str(), sub, false, ui::Glyph::Code)) openInVSCode("src/scripts/" + f.rel);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Open in VS Code");
            ImGui::Indent(scaled(30));
            for (const std::string& g : f.info.globalClasses) {
                tag(g.c_str(), ImVec4(0.95f, 0.56f, 0.88f, 1.0f));
                ImGui::SameLine();
                tag("global", ImVec4(0.95f, 0.56f, 0.88f, 1.0f), false);
            }
            for (const std::string& c : f.info.objectClasses) {
                tag(c.c_str(), sem.accent);
                ImGui::SameLine();
                auto it = attached.find(c);
                if (it == attached.end()) {
                    ImGui::TextDisabled("not attached to any object");
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Attach...##" + c).c_str())) {
                        if (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size()) {
                            project_.objects()[selectedObject_].scripts.push_back(c);
                            commitChange();
                            statusMessage_ = "Attached " + c + " to " + project_.objects()[selectedObject_].name;
                        } else {
                            statusMessage_ = "Select an object first, then Attach";
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Attaches it to the selected object.");
                    ImGui::NewLine();
                } else {
                    ImGui::TextDisabled("on");
                    ImGui::SameLine();
                    int n = 0;
                    for (const auto& [si, oi] : it->second) {
                        if (n++ >= 4) break;
                        objChip(si, oi);
                    }
                    if ((int)it->second.size() > 4) ImGui::TextDisabled("+%d", (int)it->second.size() - 4);
                    else ImGui::NewLine();
                }
            }
            ImGui::Unindent(scaled(30));
            ImGui::PopID();
        }
        // Attached classes nobody registers: the object would get no script.
        for (const std::string& c : missing) {
            if (!matches(c, railScriptSearch_)) continue;
            ImGui::PushID(c.c_str());
            const std::string label = c + " (missing class)##miss";
            ui::ListRow(label.c_str(), "attached, but no TYRA_OBJECT_SCRIPT registers it", false, ui::Glyph::Flag);
            ImGui::Indent(scaled(30));
            int n = 0;
            for (const auto& [si, oi] : attached[c]) {
                if (n++ >= 4) break;
                objChip(si, oi);
            }
            if (ImGui::SmallButton("Create")) {
                openNewScriptPopup_ = true;
                newScriptError_.clear();
                newScriptAttachTo_ = -1;
                std::snprintf(newScriptName_, sizeof newScriptName_, "%s", c.c_str());
            }
            ImGui::Unindent(scaled(30));
            ImGui::PopID();
        }
        if (cpp.empty() && missing.empty()) {
            if (ui::EmptyState(ui::Glyph::Code, "No C++ scripts",
                               "Object scripts run on the objects they are attached to; a global script runs every frame.",
                               "New script...##railempty")) {
                openNewScriptPopup_ = true;
                newScriptError_.clear();
                newScriptAttachTo_ = -1;
            }
        }
    }

    if (showVu && !vu.empty()) {
        ImGui::Dummy(ImVec2(0, scaled(4)));
        ui::Caption("VU programs", std::to_string(vu.size()).c_str());
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
    ImGui::EndChild();
    ImGui::TextDisabled("A C++ row opens VS Code; a graph row opens the Flow Graph.");
}
