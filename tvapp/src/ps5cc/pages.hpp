// ps5cc - The pages after cooling, games and jobs: settings, payloads, logs.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each page owns its components and its own overlay (a confirmation); the screen
// (src/concepts/ps5cc.cpp) owns the header, the tab bar, the hint row and the menu.

#pragma once

#include "core/input.hpp"
#include "ps5cc/data.hpp"
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

std::unique_ptr<Page> make_settings_page();
std::unique_ptr<Page> make_payloads_page();
std::unique_ptr<Page> make_logs_page();

} // namespace hui::ps5cc
