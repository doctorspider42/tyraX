// Shared widgets and faces for the editor's redesigned chrome
// (docs/editor-theme.md).
//
// theme.hpp says what a colour MEANS; this says what a recurring control LOOKS
// like, so the same inspector field is the same field in every panel. Pure
// ImGui like theme.cpp - no App, no Project - so a widget can be read and
// changed in one place.
//
// Every widget keeps ImGui's own contract: the label is the ID (and the name
// --ui-script finds it by), the return value is "changed this frame", and
// IsItemDeactivatedAfterEdit() right after it reports the end of an edit.
#pragma once

#include <imgui.h>

namespace ui {

// --- faces -------------------------------------------------------------------
// Set once by App::loadUiFont. Either may stay null (no matching system face):
// the getters then return null, and PushFont(nullptr, 0) keeps the current
// face - so callers never branch on whether a face was found.
void setBoldFont(ImFont* f);
void setMonoFont(ImFont* f);
ImFont* boldFont();  // the interface family's heavier cut: titles, headings
ImFont* monoFont();  // fixed-width: numbers that change, logs, code

/** RAII PushFont at the current size; a null face keeps the current one. */
struct FontScope {
    explicit FontScope(ImFont* f) { ImGui::PushFont(f, 0.0f); }
    ~FontScope() { ImGui::PopFont(); }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

// --- marks -------------------------------------------------------------------

/** The editor's mark: the four face-button glyphs (triangle, circle, cross,
 * square) in a rounded tile `size` px square at `p`, in the theme's ok /
 * danger / accent / special colours. Draw-only, no item. */
void LogoMark(ImDrawList* dl, ImVec2 p, float size);

// --- fields ------------------------------------------------------------------

/** DragFloat3 with the axis named INSIDE each field - a small X/Y/Z in the
 * gizmo's colour at the field's left edge - and the value in the fixed-width
 * face, so digits do not jitter while dragging. Same signature, ID, return
 * value and group semantics as ImGui::DragFloat3 (the label is drawn after
 * the three fields), so it is a drop-in replacement. */
bool AxisDrag3(const char* label, float v[3], float speed = 1.0f, float vmin = 0.0f,
               float vmax = 0.0f, const char* fmt = "%.3f");

}  // namespace ui
