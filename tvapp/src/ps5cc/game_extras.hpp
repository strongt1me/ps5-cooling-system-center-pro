// ps5cc - The game extras (Spiele → Bibliothek → Quadrat-Taste → Extras) and the library tools.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's per-game window "LZ4-Asset-Packs & APR-EMU": the AMPR index, APR-EMU and PlayGo in the game, the
// plugins, LZ4 packs and the optimisation by recording, each with the console's own state and refusals. And the
// library's duplicate images and its cover cache. Every action fires at once; jobs run on the console and the window
// may be closed while they do.

#pragma once

#include "core/input.hpp"
#include "core/tween.hpp"
#include "ps5cc/data.hpp"
#include "ui/components.hpp"

#include <string>
#include <vector>

namespace hui::ps5cc
{

class GameExtras
{
  public:
    void init(const ui::Theme &theme);
    void open_game(const Game &game, ui::Feedback &feedback);
    void open_library(ui::Feedback &feedback);
    void absorb(const Snapshot &data);
    bool modal() const;
    bool visible() const;
    void handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw_overlay(ui::Canvas &canvas) const;

  private:
    enum View
    {
        kClosed,
        kMain,
        kVersions,
        kLz4,
        kLibrary,
    };

    void build();
    void build_main();
    void build_versions();
    void build_lz4();
    void build_library();
    void activate(int id, ui::Feedback &feedback);
    void close(ui::Feedback &feedback);
    void ask_plan();
    std::string plan_text() const;
    std::string side_text() const;
    std::string job_line(float *fraction) const;

    ui::Theme theme_;
    ui::Form form_;
    tween::Spring shown_;
    View view_ = kClosed;
    Game game_;
    ExtrasState extras_;
    ExtrasVersions versions_;
    ExtrasLz4Plan plan_;
    LibraryTools library_;
    bool cache_on_ = true;
    std::string lib_; // ampr or playgo, for the versions view
    std::vector<std::string> version_names_;
    std::vector<std::string> profile_ids_; // the choices of the LZ4 mode, "" = automatic
    std::vector<std::string> paths_;       // the duplicate images, by row
    std::vector<bool> aside_;              // and whether each is set aside
    std::string key_;
    float clock_ = 0.0f;
};

} // namespace hui::ps5cc
