// ps5cc - The Protokoll page: the app's own log and the console's kernel log, live.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

enum View
{
    kApp = 0,
    kKernel = 1,
};

constexpr float kListTop = kTop + 10.0f;
constexpr float kListW = 1824.0f - kLeftX;

Color level_color(const std::string &level)
{
    if (level == "ERROR")
        return kCritical;
    if (level == "WARN")
        return kWarning;
    return kGood;
}

const char *level_text(const std::string &level)
{
    if (level == "ERROR")
        return "FEHLER";
    if (level == "WARN")
        return "WARNUNG";
    return "INFO";
}

class LogsPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        style_subtabs(views_, theme, 2);
        views_.set_tabs({{"App-Protokoll"}, {"Kernel-Log live"}});
        views_.set_active(kApp, true);

        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.row_height = 36.0f;
        list_.style.gap = 2.0f;
        list_.style.padding = 16.0f;
        list_.style.title_size = 19.0f;
        list_.style.entrance_step = 0.0f;
        list_.style.focus_shift = 0.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.content = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int index, float focus)
        {
            (void)index;
            (void)focus;
            draw_line(canvas, row, item.tag);
        };
        list_.set_bounds({kLeftX, kListTop, kListW, kBottom - kListTop});
    }

    void absorb(const Snapshot &data) override
    {
        tz_ = data.config.tz_offset_min;
        lost_ = data.klog_lost;
        record_ = data.klog_record;
        const bool app_changed = data.log.size() != log_.size() ||
                                 (!data.log.empty() && data.log.back().t_ms != log_.back().t_ms);
        const bool kernel_changed = data.klog.size() != klog_.size() ||
                                    (!data.klog.empty() && data.klog.back().n != klog_.back().n);
        log_ = data.log;
        klog_ = data.klog;
        if ((view_ == kApp && app_changed) || (view_ == kKernel && kernel_changed))
            rebuild();
    }

    void enter() override
    {
        follow_ = true;
        rebuild();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            if (views_.step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) == ui::Event::changed)
            {
                view_ = views_.active();
                follow_ = true;
                rebuild();
            }
            return;
        }
        // Triangle in the kernel log: record it into a file, as the web app's "Aufnahme" (download it there)
        if (input.is_pressed(Action::north) && view_ == kKernel)
        {
            feedback.play(audio::Cue::select);
            source().klog_record(!record_.recording);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            follow_ = true;
            list_.set_focus(last(), false);
            feedback.play(audio::Cue::focus);
            return;
        }
        list_.handle(input, feedback);
        // Reading further up stops the list from running on; back at the last line it follows again.
        follow_ = list_.focus() >= last();
    }

    void update(float dt) override
    {
        views_.update(dt);
        list_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        views_.draw(canvas);
        draw_subtab_glyphs(canvas, views_);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const std::size_t count = view_ == kApp ? log_.size() : klog_.size();
        char text[96];
        std::snprintf(text, sizeof(text), "%zu Zeilen · %s", count, follow_ ? "läuft mit" : "angehalten");
        const float baseline = kTabY + kTabH * 0.5f + 8.0f;
        ui::text(list, fonts.regular, text, 1824.0f, baseline, 21.0f, follow_ ? kMuted : kWarning,
                 gfx::Align::right);
        // Starts after the R2 glyph: right-aligned, a long file name ran back over it. The file loses its fixed
        // "aufnahme-" and ".log" so the line stays clear of the line count on the right.
        const Rect &tabs = views_.bounds();
        const float status_x = tabs.x + tabs.w + 14.0f + ui::button_width(ui::Button::r2, 38.0f) + 22.0f;
        if (view_ == kKernel && record_.recording)
        {
            std::string file = record_.file;
            if (file.rfind("aufnahme-", 0) == 0)
                file.erase(0, 9);
            if (file.size() > 4 && file.compare(file.size() - 4, 4, ".log") == 0)
                file.resize(file.size() - 4);
            const std::string rec = "Aufnahme läuft · " + format_bytes(record_.bytes) + " · " + file;
            ui::text(list, fonts.semibold, rec, status_x, baseline, 21.0f, kCritical);
        }
        else if (view_ == kKernel && lost_)
            ui::text(list, fonts.semibold, "Zeilen verpasst", status_x, baseline, 21.0f, kWarning);
        list_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        out[0] = {ui::Button::l2, "Ansicht", ui::Button::r2};
        out[1] = {ui::Button::dpad, "Blättern"};
        out[2] = {ui::Button::square, "Zum Ende"};
        if (view_ == kKernel) // at most three: the recording takes the place of the scrolling hint
        {
            out[1] = out[2];
            out[2] = {ui::Button::triangle, record_.recording ? "Aufnahme beenden" : "Aufnahme"};
        }
        return 3;
    }

  private:
    int last() const
    {
        return std::max(static_cast<int>(list_.items().size()) - 1, 0);
    }

    void rebuild()
    {
        std::vector<ui::ListItem> items;
        const std::size_t count = view_ == kApp ? log_.size() : klog_.size();
        items.reserve(std::max<std::size_t>(count, 1));
        for (std::size_t i = 0; i < count; ++i)
        {
            ui::ListItem item;
            item.tag = static_cast<int>(i);
            items.push_back(item);
        }
        if (items.empty())
        {
            ui::ListItem none;
            none.tag = -1;
            items.push_back(none);
        }
        const int keep = list_.focus();
        list_.set_items(items);
        list_.set_focus(follow_ ? last() : std::min(keep, last()));
    }

    void draw_line(ui::Canvas &canvas, const Rect &row, int index) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        ui::Painter paint(list, fonts, theme_);
        const float base = row.y + row.h * 0.5f + 7.0f;
        if (index < 0)
        {
            ui::text(list, fonts.regular,
                     view_ == kApp ? "Noch keine Einträge." : "Noch keine Zeilen – das Kernel-Log wird gelesen.",
                     row.x + 16.0f, base, 19.0f, kMuted);
            return;
        }
        const std::size_t at = static_cast<std::size_t>(index);
        if (view_ == kApp && at < log_.size())
        {
            const LogEntry &e = log_[at];
            ui::text(list, fonts.mono, format_time_of_day(e.t_ms, tz_), row.x + 16.0f, base, 18.0f, kMuted);
            ui::text(list, fonts.semibold, level_text(e.level), row.x + 136.0f, base, 17.0f, level_color(e.level));
            ui::text(list, fonts.regular, ui::fit_label(paint, e.message, 19.0f, row.w - 270.0f), row.x + 250.0f,
                     base, 19.0f, kInk);
            return;
        }
        if (view_ == kKernel && at < klog_.size())
        {
            const KlogLine &l = klog_[at];
            ui::text(list, fonts.mono, format_time_of_day(l.t_ms, tz_), row.x + 16.0f, base, 17.0f, kMuted);
            // The mono face is not the painter's text face: shorten by its own advance (about 0.6 em per char).
            const std::size_t room = static_cast<std::size_t>(std::max(10.0f, (row.w - 150.0f) / (17.0f * 0.6f)));
            std::string line = l.text;
            if (line.size() > room)
            {
                std::size_t cut = room - 1;
                while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0u) == 0x80u)
                    --cut; // never through the middle of a UTF-8 character
                line = line.substr(0, cut) + "…";
            }
            ui::text(list, fonts.mono, line, row.x + 136.0f, base, 17.0f, kInk);
        }
    }

    ui::Theme theme_;
    ui::TabBar views_;
    ui::ListView list_;
    int view_ = kApp;
    bool follow_ = true;
    bool lost_ = false;
    int tz_ = 0;
    std::vector<LogEntry> log_;
    std::vector<KlogLine> klog_;
    KlogRecord record_;
};

} // namespace

std::unique_ptr<Page> make_logs_page()
{
    return std::make_unique<LogsPage>();
}

} // namespace hui::ps5cc
