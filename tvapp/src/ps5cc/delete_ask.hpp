// ps5cc - The question every delete asks first: it names what goes, and "Abbrechen" has the focus.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Since 06.11.2026 nothing is deleted at one press: the file manager, the saved data, the backups,
// the payload files and profiles, the disc images, the dumps and the histories all ask through this
// class. The page owns one, opens it with what it is about, gives it the input while it is open
// (handle() is true in the frame "Löschen" is chosen) and draws it last. Every line is short enough
// not to wrap, because the translation looks a line up as it is drawn.

#pragma once

#include "core/input.hpp"
#include "ps5cc/look.hpp"
#include "ui/components.hpp"

#include <string>
#include <vector>

namespace hui::ps5cc
{

class DeleteAsk
{
  public:
    void init(const ui::Theme &theme)
    {
        dialog_.style.theme = solid_theme(theme);
        dialog_.style.frosted = false;
        dialog_.style.scrim = 0.72f;
        dialog_.style.width = 900.0f;
        dialog_.style.body_lines = 12; // up to five names, the folder, the warnings
    }

    // The question: a title ("Datei löschen?"), what goes (names with their kind or size) and
    // notes below (where they lie, what stays). `lead` is the sentence above the names; `warn` the
    // last line, "" for none (a delete that is backed up first is not "unwiderruflich").
    void open(const std::string &title, const std::vector<std::string> &names,
              const std::vector<std::string> &notes, ui::Feedback &feedback,
              const std::string &lead = "Das wird endgültig gelöscht:",
              const std::string &warn = "Das lässt sich nicht rückgängig machen.")
    {
        std::string body = lead;
        if (!names.empty())
            body += "\n";
        for (const std::string &n : names)
            body += "\n" + n;
        if (!notes.empty())
            body += "\n";
        for (const std::string &n : notes)
            body += "\n" + n;
        if (!warn.empty())
            body += "\n\n" + warn;
        ui::DialogContent content;
        content.icon = ui::StatusKind::danger;
        content.title = title;
        content.body = body;
        content.buttons = {{"Abbrechen"}, {"Löschen", ui::ButtonKind::primary, true}};
        dialog_.open(content, feedback);
    }

    bool is_open() const
    {
        return dialog_.is_open();
    }

    // True in the frame "Löschen" (the second button) is chosen. Back, "Abbrechen" and anything
    // else leave everything as it was.
    bool handle(const InputFrame &input, ui::Feedback &feedback)
    {
        const ui::Event event = dialog_.handle(input, feedback);
        return event == ui::Event::activated && dialog_.choice() == 1;
    }

    void update(float dt)
    {
        dialog_.update(dt);
    }

    void draw(ui::Canvas &canvas) const
    {
        dialog_.draw(canvas);
    }

    // A name from the console as one line of the question: „name“ · what it is. Too long a name
    // keeps its beginning and its end (the ending tells what it is) with "…" between.
    static std::string line(const std::string &name, const std::string &what = {})
    {
        return "\xE2\x80\x9E" + shorten(name) + "\xE2\x80\x9C" +
               (what.empty() ? "" : " \xC2\xB7 " + what);
    }

    static std::string shorten(const std::string &name)
    {
        constexpr std::size_t kMax = 56;
        if (name.size() <= kMax)
            return name;
        std::size_t head = 34;
        std::size_t tail = name.size() - 18;
        // never cut inside a UTF-8 character
        while (head > 0 && (static_cast<unsigned char>(name[head]) & 0xC0) == 0x80)
            --head;
        while (tail < name.size() && (static_cast<unsigned char>(name[tail]) & 0xC0) == 0x80)
            ++tail;
        return name.substr(0, head) + "\xE2\x80\xA6" + name.substr(tail);
    }

  private:
    ui::Dialog dialog_;
};

} // namespace hui::ps5cc
