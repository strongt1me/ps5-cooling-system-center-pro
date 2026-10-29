// ps5cc - The game tools of Spiele → Bibliothek: the action menu (□) and the plan dialog of each tool.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every tool first asks the console app for its plan, shows the choices the web app offers with the same rules
// (ps5cc/plans.hpp) and starts the job with the same POST body. The job then runs on the console; the screen
// shows it under Spiele → Aufträge.

#pragma once

#include "core/input.hpp"
#include "core/tween.hpp"
#include "ps5cc/data.hpp"
#include "ps5cc/plans.hpp"
#include "ui/components.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ps5cc
{

class GameTools
{
  public:
    // A short message of the tools' own (a refusal before anything was asked).
    std::function<void(ui::StatusKind kind, const std::string &title, const std::string &body)> toast;
    // The menu's last two entries (Phase 6, 29.10.2026): the game's extras and the library's tools.
    std::function<void(const Game &game, ui::Feedback &feedback)> on_extras;
    std::function<void(ui::Feedback &feedback)> on_library;

    void init(const ui::Theme &theme);
    // □ in the library: the menu of what this game allows. `running` refuses the delete at once.
    void open_menu(const Game &game, bool running, ui::Feedback &feedback);
    // Opens a tool's dialog directly (the menu's choice; the tour).
    void open_tool(Tool tool, ui::Feedback &feedback);
    // The plan the dialog waits for, the running game.
    void absorb(const Snapshot &data);
    // Menu, dialog or question open: they get the input first, and the frame its glass.
    bool modal() const;
    bool visible() const;
    void handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw_overlay(ui::Canvas &canvas) const;

  private:
    void open_dialog(Tool tool, ui::Feedback &feedback);
    void close_dialog(ui::Feedback &feedback);
    void build_form();
    void refresh();
    void press_start(ui::Feedback &feedback);
    void start();
    void answer_question();
    std::string title() const;
    std::string start_label() const;
    void draw_side(ui::Canvas &canvas, const gfx::Rect &box) const;
    float draw_paragraph(ui::Canvas &canvas, const std::string &text, float x, float y, float width, float size,
                         gfx::Color ink, int max_lines = 6) const;

    ui::Theme theme_;
    ui::Menu menu_;
    ui::Form form_;
    std::vector<Tool> menu_tools_;

    Game game_;
    std::string running_id_;
    Tool tool_ = Tool::copy;
    bool open_ = false;
    std::uint64_t awaiting_ = 0; // the ticket of the plan the dialog waits for; 0 when it has it
    Plan plan_;
    PlanChoice choice_;
    std::vector<int> op_index_; // the op choice's options, as indexes into kOpKeys
    float clock_ = 0.0f;
    tween::Spring shown_;
};

} // namespace hui::ps5cc
