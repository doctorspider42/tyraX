#include "menulayout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include <stb_image.h>  // implementation lives in app.cpp

namespace menulayout {

namespace {

// The PS2 texture axis cap, and the GS words a texture costs (docs/gs-vram.md:
// every allocation carries 1024 * 2 words of padding on top of its pixels).
constexpr int kMaxAxis = 512;
constexpr int kTexPadWords = 2048;
constexpr int kTextureHeapWords = 282000;

int pow2AtLeast(int v) {
    int r = 64;
    while (r < v && r < kMaxAxis) r *= 2;
    return r;
}

int clampPanelW(int w) {
    if (w == 128 || w == 512) return w;
    if (w == kMaxAxis) return kMaxAxis;
    return 256;
}

// Fitted (drawn) size of every menu image, from PNG headers only - the full
// decode happens at bake time. Moved here from menubake.cpp: it is geometry,
// and the baker must not have a second opinion about it.
struct Fitted {
    int w = 0, h = 0;
};

std::vector<Fitted> fitImages(const GameMenu& menu, const std::string& projectDir,
                              int panelW) {
    std::vector<Fitted> out(menu.images.size());
    if (projectDir.empty()) return out;
    for (size_t i = 0; i < menu.images.size(); ++i) {
        const MenuImage& mi = menu.images[i];
        if (mi.path.empty()) continue;
        int w = 0, h = 0, comp = 0;
        const std::string full =
            (std::filesystem::path(projectDir) / mi.path).string();
        if (!stbi_info(full.c_str(), &w, &h, &comp) || w <= 0 || h <= 0) continue;
        if (mi.slot == MenuImage::Background) {
            out[i] = {panelW, 0};  // stretched over the content at bake time
            continue;
        }
        const float maxW =
            (float)(panelW - (mi.slot == MenuImage::Overlay ? 0 : 32));
        float s = 1.0f;
        if (w > maxW) s = maxW / w;
        s *= (mi.scale > 0.05f ? mi.scale : 0.05f);
        float fw = w * s, fh = h * s;
        if (fw > panelW) {
            fh *= panelW / fw;
            fw = (float)panelW;
        }
        if (fh > 320.0f) {
            fw *= 320.0f / fh;
            fh = 320.0f;
        }
        out[i] = {(int)fw, (int)fh};
    }
    return out;
}

int bitsForQuant(int quant) {
    switch (quant) {
        case 1: return 4;
        case 2: return 8;
        default: return 32;
    }
}

}  // namespace

int Texture::words() const {
    if (w <= 0 || h <= 0) return 0;
    const long long bytes = (long long)w * h * bits / 8;
    // Palettized textures also carry their CLUT (16 or 256 entries, 32bpp).
    const long long clut = bits == 4 ? 16 * 4 : bits == 8 ? 256 * 4 : 0;
    return (int)((bytes + clut + 3) / 4) + kTexPadWords;
}

int Layout::words() const {
    int total = 0;
    for (const Texture& t : textures) total += t.words();
    return total;
}

float Layout::vramFraction() const {
    return (float)words() / (float)kTextureHeapWords;
}

const menustyle::Sheet& sheetFor(const GameMenu& menu) {
    return menustyle::find(menu.style);
}

menustyle::Computed baseFor(const GameMenu& menu, menustyle::Elem elem) {
    menustyle::Computed c = menustyle::defaults(elem);
    auto b255 = [](float v) {
        const int k = (int)(v * 255.0f + 0.5f);
        return (unsigned char)(k < 0 ? 0 : k > 255 ? 255 : k);
    };
    const menustyle::Color accent{b255(menu.accent[0]), b255(menu.accent[1]),
                                  b255(menu.accent[2]), 255};
    switch (elem) {
        case menustyle::Elem::Panel:
            c.width = (float)clampPanelW(menu.panelW);
            c.borderColor = accent;
            c.font = menu.font;
            break;
        case menustyle::Elem::Title:
            c.fontSize = (float)menu.titleSize;
            c.color = accent;
            c.font = menu.font;
            break;
        case menustyle::Elem::Row:
        case menustyle::Elem::Value:
            c.fontSize = (float)menu.entrySize;
            c.font = menu.font;
            break;
        case menustyle::Elem::Marker:
            c.color = accent;
            break;
        case menustyle::Elem::Hint:
            c.font = menu.font;
            // "OK" would be a lie on a panel where Cross saves and Circle
            // loads, so a save menu says what its buttons do. It is the base,
            // not a hard rule: `hint { content: "..." }` still overrides it.
            if (menu.saveMenu) c.content = "X SAVE   O LOAD   \xE2\x96\xB2 BACK";
            break;
        default:
            c.font = menu.font;
            break;
    }
    return c;
}

std::string sanitizeName(const std::string& name, const char* fallback) {
    std::string s;
    for (char c : name) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            s += (char)tolower((unsigned char)c);
        else
            s += '-';
    }
    if (s.empty()) s = fallback;
    return s;
}

std::string panelFileName(const std::string& menuName) {
    return sanitizeName(menuName, "menu") + ".png";
}

std::string valueStripFileName(const std::string& menuName) {
    return sanitizeName(menuName, "menu") + "-values.png";
}

std::string listFileName(const std::string& menuName) {
    return sanitizeName(menuName, "menu") + "-list.png";
}

std::string stateAtlasFileName(const std::string& menuName) {
    return sanitizeName(menuName, "menu") + "-rows.png";
}

std::string descAtlasFileName(const std::string& menuName) {
    return sanitizeName(menuName, "menu") + "-desc.png";
}

// The row list of a menu as the bake sees it: the save menu's rows ARE its save
// slots, so it bakes that many BLANK rows and the game draws "SLOT n" into the
// geometry at runtime (a baked label could never page). Routing it through here
// means the panel, the preview and the generated metrics count rows the same way.
GameMenu asBaked(const GameMenu& menu, const Project& p) {
    if (!menu.saveMenu) return menu;
    GameMenu c = menu;
    int rows = p.saveSlotsPerPage;
    if (rows < 1) rows = 1;
    if (rows > kMaxRows) rows = kMaxRows;
    c.entries.assign((size_t)rows, MenuEntry{});
    for (MenuEntry& e : c.entries) e.label.clear();
    return c;
}

Layout compute(const GameMenu& menuIn, const Project& p) {
    const GameMenu menu = asBaked(menuIn, p);
    const menustyle::Sheet& sheet = sheetFor(menu);
    Layout L;

    auto resolve = [&](menustyle::Elem e, const std::string& cls, int state) {
        menustyle::Computed base = baseFor(menu, e);
        menustyle::Computed normal =
            menustyle::compute(sheet, menu.name, e, cls, menustyle::StateNormal, base);
        if (state == menustyle::StateNormal) return normal;
        return menustyle::compute(sheet, menu.name, e, cls, state, normal);
    };

    L.panel = resolve(menustyle::Elem::Panel, "", menustyle::StateNormal);
    L.title = resolve(menustyle::Elem::Title, "", menustyle::StateNormal);
    L.list = resolve(menustyle::Elem::List, "", menustyle::StateNormal);
    L.hint = resolve(menustyle::Elem::Hint, "", menustyle::StateNormal);
    L.desc = resolve(menustyle::Elem::Description, "", menustyle::StateNormal);
    L.value = resolve(menustyle::Elem::Value, "", menustyle::StateNormal);
    L.marker = resolve(menustyle::Elem::Marker, "", menustyle::StateNormal);

    // `width: screen` parses to -1: the GS framebuffer is 512 wide, so a
    // full-screen panel is a 512px texture drawn 1:1 (docs/ps2-viewport.md).
    L.panelW = L.panel.width < 0    ? kMaxAxis
               : L.panel.width == 0 ? clampPanelW(menu.panelW)
                                    : clampPanelW((int)L.panel.width);

    const auto fit = fitImages(menu, p.dir, L.panelW);
    auto flowBlock = [&](int slot) {
        int h = 0;
        for (size_t i = 0; i < menu.images.size(); ++i)
            if (menu.images[i].slot == slot && fit[i].h > 0)
                h += fit[i].h + (int)L.panel.gap;
        return h;
    };

    int entries = (int)menu.entries.size();
    if (entries < 0) entries = 0;
    if (entries > kMaxRows) entries = kMaxRows;

    // --- rows: resolve every row's style first, the pitch comes out of it ----
    L.rows.resize((size_t)entries);
    int maxRowH = 0;
    for (int i = 0; i < entries; ++i) {
        Row& r = L.rows[(size_t)i];
        r.entry = i;
        r.cls = menu.entries[(size_t)i].styleClass;
        // A cell is only worth baking for a state the row can actually BE in,
        // and both halves of that were wrong at first (measured on a 6-row menu:
        // 71% of the texture heap, down to 13%):
        //  * `row:disabled` needs something to gate the row - an `enabledWhen`
        //    naming a save value that EXISTS, since the runtime resolves it to
        //    an index and a name that resolves to nothing can never be 0.
        //  * `row:selected` needs the row to be selectable at all: a header or
        //    a spacer never takes the cursor.
        const MenuEntry& en = menu.entries[(size_t)i];
        bool gateable = false;
        for (const SaveValue& sv : p.saveValues)
            gateable |= (!en.enabledWhen.empty() && sv.name == en.enabledWhen);
        r.style[menustyle::StateNormal] =
            resolve(menustyle::Elem::Row, r.cls, menustyle::StateNormal);
        r.selectable = r.style[menustyle::StateNormal].selectable &&
                       en.action != MenuEntry::Label;
        for (int s = 1; s < menustyle::StateCount; ++s) {
            r.style[s] = resolve(menustyle::Elem::Row, r.cls, s);
            const bool possible = s == menustyle::StateDisabled
                                      ? gateable
                                      : r.selectable;
            r.paints[s] =
                possible && menustyle::statePaints(sheet, menu.name, r.cls, s);
        }
        const menustyle::Computed& n = r.style[menustyle::StateNormal];
        const int h = n.height > 0
                          ? (int)n.height
                          : (int)(n.padding.t + n.fontSize + n.padding.b);
        r.box.h = h;
        if (h > maxRowH) maxRowH = h;
    }
    // The cursor arithmetic in the generated game is row0Y + index * rowH, so
    // the pitch stays uniform: the tallest row wins and every row gets it. A
    // per-row pitch would need the runtime to carry a table it cannot index
    // before the row is known.
    if (entries > 0) {
        L.rowH = maxRowH;
    } else {
        // An empty menu still has a pitch: the cursor geometry and the value
        // strip's cell height are baked from it, and a menu gains rows later.
        const menustyle::Computed r = resolve(menustyle::Elem::Row, "", menustyle::StateNormal);
        L.rowH = (int)(r.height > 0 ? r.height : r.padding.t + r.fontSize + r.padding.b);
    }
    if (L.rowH <= 0) L.rowH = menu.entrySize + 9;

    // --- vertical flow ------------------------------------------------------
    int y = (int)L.panel.padding.t;

    for (size_t i = 0; i < menu.images.size(); ++i) {
        if (menu.images[i].slot != MenuImage::AboveTitle || fit[i].h <= 0) continue;
        ImagePlace ip;
        ip.image = (int)i;
        ip.slot = MenuImage::AboveTitle;
        ip.box = Box{(L.panelW - fit[i].w) / 2 + (int)menu.images[i].offset[0],
                     y + (int)menu.images[i].offset[1], fit[i].w, fit[i].h};
        L.images.push_back(ip);
        y += fit[i].h + (int)L.panel.gap;
    }

    if (menu.showTitle) {
        const int th = L.title.height > 0 ? (int)L.title.height : (int)L.title.fontSize;
        L.titleBox = Box{0, y, L.panelW, th};
        int after = y + th + (int)L.title.padding.b;
        if (L.title.ruleBelow > 0) {
            L.titleRule = Box{(int)L.title.margin.l, after,
                              L.panelW - (int)L.title.margin.l - (int)L.title.margin.r,
                              (int)L.title.ruleBelow};
            after += (int)L.title.ruleBelow;
        } else {
            L.titleRule = Box{0, after, 0, 0};
        }
        y = after + (int)L.title.margin.b;
    } else {
        L.titleBox = Box{0, y, L.panelW, 0};
        L.titleRule = Box{0, y, 0, 0};
        y += 2;  // the classic no-title gap
    }

    for (size_t i = 0; i < menu.images.size(); ++i) {
        if (menu.images[i].slot != MenuImage::AboveEntries || fit[i].h <= 0) continue;
        ImagePlace ip;
        ip.image = (int)i;
        ip.slot = MenuImage::AboveEntries;
        ip.box = Box{(L.panelW - fit[i].w) / 2 + (int)menu.images[i].offset[0],
                     y + (int)menu.images[i].offset[1], fit[i].w, fit[i].h};
        L.images.push_back(ip);
        y += fit[i].h + (int)L.panel.gap;
    }

    L.row0Y = y;
    L.rowsVisible = L.list.rowsVisible > 0
                        ? std::min(L.list.rowsVisible, entries)
                        : entries;
    if (L.rowsVisible <= 0) L.rowsVisible = entries;
    L.scrolls = L.rowsVisible < entries;

    for (int i = 0; i < entries; ++i) {
        Row& r = L.rows[(size_t)i];
        r.box = Box{0, L.row0Y + i * L.rowH, L.panelW, L.rowH};
        const menustyle::Computed& n = r.style[menustyle::StateNormal];
        if (n.iconSize > 0) {
            const int side = (int)n.iconSize;
            r.icon = Box{(int)n.padding.l - side - 6,
                         r.box.y + (L.rowH - side) / 2, side, side};
            if (r.icon.x < 2) r.icon.x = 2;
        }
    }
    L.listBox = Box{0, L.row0Y, L.panelW, L.rowsVisible * L.rowH};

    int afterRows = L.row0Y + L.rowsVisible * L.rowH;

    const int belowBlock = flowBlock(MenuImage::BelowEntries);
    {
        int by = afterRows + 4;  // the classic 4px nudge before the block
        for (size_t i = 0; i < menu.images.size(); ++i) {
            if (menu.images[i].slot != MenuImage::BelowEntries || fit[i].h <= 0)
                continue;
            ImagePlace ip;
            ip.image = (int)i;
            ip.slot = MenuImage::BelowEntries;
            ip.box = Box{(L.panelW - fit[i].w) / 2 + (int)menu.images[i].offset[0],
                         by + (int)menu.images[i].offset[1], fit[i].w, fit[i].h};
            L.images.push_back(ip);
            by += fit[i].h + (int)L.panel.gap;
        }
    }
    afterRows += belowBlock;

    // The description pane: below the rows (its own strip) or to the right of
    // them (a column beside the list, for wide panels).
    const int descH = (int)L.desc.height;
    if (descH > 0) {
        if (L.desc.area == 1) {
            const int half = L.panelW / 2;
            L.descBox = Box{half, L.row0Y, L.panelW - half,
                            std::max(descH, L.rowsVisible * L.rowH)};
        } else {
            L.descBox = Box{0, afterRows, L.panelW, descH};
            afterRows += descH;
        }
    }

    // Background (stretched under everything) and Overlay (freeform, in front)
    // are not part of the flow, but they are still placements - the baker must
    // not compute a rect of its own.
    for (size_t i = 0; i < menu.images.size(); ++i) {
        const MenuImage& mi = menu.images[i];
        if (mi.slot == MenuImage::Background && fit[i].w > 0) {
            ImagePlace ip;
            ip.image = (int)i;
            ip.slot = MenuImage::Background;
            ip.stretchBackground = true;
            L.images.push_back(ip);  // box filled in below, once contentH is known
        } else if (mi.slot == MenuImage::Overlay && fit[i].h > 0) {
            ImagePlace ip;
            ip.image = (int)i;
            ip.slot = MenuImage::Overlay;
            ip.box = Box{(int)mi.offset[0], (int)mi.offset[1], fit[i].w, fit[i].h};
            L.images.push_back(ip);
        }
    }

    const int hintH = L.hint.height > 0 ? (int)L.hint.height : 0;
    L.hintBox = Box{0, afterRows, L.panelW, hintH};

    L.contentH = afterRows + hintH;
    if (L.contentH > kMaxAxis) {
        L.contentH = kMaxAxis;
        L.clipped = true;
    }
    L.canvasH = pow2AtLeast(L.contentH);
    for (ImagePlace& ip : L.images)
        if (ip.stretchBackground) ip.box = Box{0, 0, L.panelW, L.contentH};

    // --- the scrolling strip ------------------------------------------------
    // Rows leave the panel when the list scrolls: they go into their own
    // texture and the game shows a window of it. 512px of strip is the cap, so
    // a very long list is reported clipped rather than silently cut.
    if (L.scrolls) {
        const int need = entries * L.rowH;
        L.listCanvasH = pow2AtLeast(need);
        if (need > kMaxAxis) L.listClipped = true;
    }

    // --- the state atlas ----------------------------------------------------
    // A cell is a full-width row strip: the panel's own background at that row,
    // plus the state's plate and label. That is what lets it be drawn OVER the
    // baked normal row and cover it completely.
    L.stateCellW = L.panelW;
    L.stateCellH = L.rowH;
    L.statePitch = L.rowH + 8;  // transparent gap - bilinear bleed guard
    int cell = 0;
    // Selected first: if the atlas runs out, losing a disabled tint is far less
    // damaging than losing the selection highlight.
    for (int pass = 0; pass < 2; ++pass) {
        const int state = pass == 0 ? menustyle::StateSelected : menustyle::StateDisabled;
        for (int i = 0; i < entries; ++i) {
            Row& r = L.rows[(size_t)i];
            if (!r.paints[state]) continue;
            if ((cell + 1) * L.statePitch > kMaxAxis) {
                L.stateClipped = true;
                continue;
            }
            r.stateCell[state] = cell++;
        }
    }
    L.stateCells = cell;
    L.stateCanvasH = cell > 0 ? pow2AtLeast(cell * L.statePitch) : 0;

    // --- the description atlas ---------------------------------------------
    if (descH > 0) {
        L.descCellW = pow2AtLeast(L.descBox.w);
        if (L.descCellW > L.panelW) L.descCellW = L.panelW;
        L.descCellH = L.descBox.h;
        L.descPitch = L.descCellH + 8;
        int dcell = 0;
        for (int i = 0; i < entries; ++i) {
            if (menu.entries[(size_t)i].description.empty()) continue;
            if ((dcell + 1) * L.descPitch > kMaxAxis) continue;
            L.rows[(size_t)i].descCell = dcell++;
        }
        L.descCells = dcell;
        L.descCanvasH = dcell > 0 ? pow2AtLeast(dcell * L.descPitch) : 0;
    }

    // --- cost ---------------------------------------------------------------
    const int panelBits = bitsForQuant(L.panel.quant);
    L.textures.push_back(
        Texture{panelFileName(menu.name), L.panelW, L.canvasH, panelBits});
    if (L.scrolls)
        L.textures.push_back(Texture{listFileName(menu.name), L.panelW,
                                     L.listCanvasH, panelBits});
    if (L.stateCells > 0)
        L.textures.push_back(Texture{stateAtlasFileName(menu.name), L.stateCellW,
                                     L.stateCanvasH, panelBits});
    if (L.descCells > 0)
        L.textures.push_back(Texture{descAtlasFileName(menu.name), L.descCellW,
                                     L.descCanvasH, panelBits});
    // Worst case per frame: panel + the selected row's cell + one description +
    // one value cell per row + the cursor. Grouped by texture at draw time.
    L.spritesPerFrame = 1 + (L.stateCells > 0 ? 1 : 0) + (L.descCells > 0 ? 1 : 0) +
                        entries + 1;
    return L;
}

}  // namespace menulayout
