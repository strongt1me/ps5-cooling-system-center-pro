// ps5cc - The pages after cooling, games and jobs: settings, packages, payloads, logs.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each page owns its components and its own overlay (a confirmation); the screen
// (src/concepts/ps5cc.cpp) owns the header, the tab bar, the hint row and the menu.

#pragma once

#include "core/input.hpp"
#include "ps5cc/data.hpp"
#include "ps5cc/look.hpp"
#include "ui/components.hpp"
#include "ui/glyphs.hpp"

#include <memory>

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
    // True while the page shows an overlay of its own: it then gets the input first and the frame its glass.
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
    // The page's own hints for the hint row (at most three); the screen adds L1/R1 before and Options after.
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
    ui::draw_button(canvas.list, canvas.fonts, glyphs, ui::Button::r2, r.x + r.w + 14.0f, cy, 38.0f);
}

std::unique_ptr<Page> make_settings_page();
std::unique_ptr<Page> make_payloads_page();
std::unique_ptr<Page> make_packages_page();
std::unique_ptr<Page> make_logs_page();

} // namespace hui::ps5cc
