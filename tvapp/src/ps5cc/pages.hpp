// ps5cc - The pages after cooling, games and jobs: settings, packages, payloads, logs, storage.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each page owns its components and its own overlay (a confirmation); the screen
// (src/concepts/ps5cc.cpp) owns the header, the tab bar, the hint row and the menu.

#pragma once

#include "core/input.hpp"
#include "ps5cc/data.hpp"
#include "ps5cc/delete_ask.hpp"
#include "ps5cc/look.hpp"
#include "ui/components.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace hui::ps5cc
{

class Page
{
  public:
    virtual ~Page() = default;
    virtual void init(const ui::Theme &theme) = 0;
    // New data from the source (every second or so): rebuild only what changed.
    virtual void absorb(const Snapshot &data) = 0;
    // The page was switched to.
    virtual void enter()
    {
    }
    // True while the page shows an overlay of its own: it then gets the input first and the frame
    // its glass.
    virtual bool modal() const
    {
        return false;
    }
    virtual void handle(const InputFrame &input, ui::Feedback &feedback) = 0;
    virtual void update(float dt) = 0;
    virtual void draw(ui::Canvas &canvas) const = 0;
    virtual void draw_overlay(ui::Canvas &canvas) const
    {
        (void)canvas;
    }
    // The page's own hints for the hint row (at most four: the screen has room for six in all); the
    // screen adds L1/R1 before and Options after.
    virtual int hints(ui::Hint *out) const = 0;
};

// The sub-tabs of a page (L2/R2): one look for every page, in the row above the content.
inline void style_subtabs(ui::TabBar &tabs, const ui::Theme &theme, int count)
{
    tabs.style.theme = theme;
    tabs.style.kind = ui::TabKind::segmented;
    tabs.style.width = ui::TabWidth::fill;
    tabs.style.text_size = 22.0f;
    tabs.style.height = 52.0f;
    tabs.set_bounds({kLeftX + 56.0f, kTabY, 280.0f * static_cast<float>(count), kTabH});
}

// The L2 and R2 glyphs beside the sub-tabs.
inline void draw_subtab_glyphs(ui::Canvas &canvas, const ui::TabBar &tabs)
{
    const gfx::Rect &r = tabs.bounds();
    const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
    const float cy = r.y + r.h * 0.5f;
    ui::draw_button(canvas.list, canvas.fonts, glyphs, ui::Button::l2,
                    r.x - 14.0f - ui::button_width(ui::Button::l2, 38.0f), cy, 38.0f);
    // R2 follows the last tab, not the bounds: a bar sized by its labels (TabWidth::fit) ends
    // before them
    float right = r.x + r.w;
    if (!tabs.tabs().empty())
    {
        const gfx::Rect last =
            tabs.tab_rect(canvas.fonts, static_cast<int>(tabs.tabs().size()) - 1);
        right = std::min(right, last.x + last.w);
    }
    ui::draw_button(canvas.list, canvas.fonts, glyphs, ui::Button::r2, right + 14.0f, cy, 38.0f);
}

// Where the focus goes when a list is refilled: on the same row (same tag and title) if it is still
// there, so rows inserted or removed above it cannot slide another action under X; otherwise the
// same place, clamped.
inline int refocus(const ui::ListView &list, const std::vector<ui::ListItem> &next)
{
    const int keep = list.focus();
    const std::vector<ui::ListItem> &old = list.items();
    if (keep >= 0 && keep < static_cast<int>(old.size()))
    {
        const ui::ListItem &was = old[static_cast<std::size_t>(keep)];
        for (std::size_t i = 0; i < next.size(); ++i)
            if (next[i].tag == was.tag && next[i].title == was.title)
                return static_cast<int>(i);
    }
    return std::clamp(keep, 0, std::max(static_cast<int>(next.size()) - 1, 0));
}

// The on-screen keyboard's pages: letters, the German ones (ä ö ü ß), symbols (files_page.cpp).
std::vector<ui::KeyboardLayout> german_layouts();

std::unique_ptr<Page> make_settings_page();
std::unique_ptr<Page> make_payloads_page();
std::unique_ptr<Page> make_packages_page();
std::unique_ptr<Page> make_ps4mount_page();
std::unique_ptr<Page> make_disc_page();
std::unique_ptr<Page> make_storage_page();
// System (Phase 7): Konsole, Speicher (the storage page inside it), Kühlleistung, Zusatzabfragen,
// Experte.
std::unique_ptr<Page> make_system_page();
std::unique_ptr<Page> make_playtime_page();
std::unique_ptr<Page> make_saves_page();
std::unique_ptr<Page> make_backups_page();
std::unique_ptr<Page> make_files_page();
std::unique_ptr<Page> make_logs_page();
std::unique_ptr<Page> make_profile_page();

} // namespace hui::ps5cc
