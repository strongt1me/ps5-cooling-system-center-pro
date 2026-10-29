// ps5cc - Colours and page geometry shared by the screen and its pages.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "ui/theme.hpp"

namespace hui::ps5cc
{

inline const gfx::Color kNavy = gfx::Color::rgb(0x070d1f);
inline const gfx::Color kNavyHigh = gfx::Color::rgb(0x0d1732);
inline const gfx::Color kBlue = gfx::Color::rgb(0x3880ff);
inline const gfx::Color kGood = gfx::Color::rgb(0x0ca30c);
inline const gfx::Color kWarning = gfx::Color::rgb(0xfab219);
inline const gfx::Color kCritical = gfx::Color::rgb(0xd03b3b);
inline const gfx::Color kInk = gfx::Color::rgb(0xe8eefc);
inline const gfx::Color kMuted = gfx::Color::rgb(0x93a1c4);

// The screen in the 1920 x 1080 canvas: the title line and the hint row run from kMargin to 1824; the sidebar
// (collapsed to its icons) stands at kMargin, the content beside it from kLeftX. The sub-tabs of a page sit in the
// row at kTabY above the content area kTop .. kBottom.
constexpr float kMargin = 96.0f;
constexpr float kRailW = 92.0f;
constexpr float kLeftX = kMargin + kRailW + 24.0f;
constexpr float kTabY = 82.0f;
constexpr float kTabH = 56.0f;
constexpr float kTop = 160.0f;
constexpr float kBottom = 960.0f;
constexpr float kRightX = 1180.0f;
constexpr float kRightW = 1824.0f - kRightX;
constexpr float kLeftW = kRightX - 24.0f - kLeftX;

// What lies over a page (a window, a menu, a message) covers it: the page behind and what it shows never shine
// through (user, 29.10.2026). Only the page's own panels let the area's picture through.
inline ui::Theme solid_theme(ui::Theme theme)
{
    theme.surface.a = 1.0f;
    theme.surface_high.a = 1.0f;
    return theme;
}

// The floor under a window's panel, so its surface reads as solid however see-through the theme is.
inline void solid_floor(gfx::DrawList &list, const gfx::Rect &panel, float radius)
{
    list.rounded_rect(panel, radius, kNavyHigh);
}

} // namespace hui::ps5cc
