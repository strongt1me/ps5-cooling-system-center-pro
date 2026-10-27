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

constexpr float kListTop = kTop + 82.0f;
constexpr float kListW = 1824.0f - kMargin;

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
        views_.style.theme = theme;
        views_.style.kind = ui::TabKind::segmented;
        views_.style.width = ui::TabWidth::fill;
        views_.style.text_size = 22.0f;
        views_.style.height = 52.0f;
        views_.set_tabs({{"App-Protokoll"}, {"Kernel-Log live"}});
        views_.set_bounds({kMargin, kTop + 8.0f, 760.0f, 56.0f});
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
        list_.set_bounds({kMargin, kListTop, kListW, kBottom - kListTop});
    }

    void absorb(const Snapshot &data) override
    {
        tz_ = data.config.tz_offset_min;
        lost_ = data.klog_lost;
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
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const std::size_t count = view_ == kApp ? log_.size() : klog_.size();
        char text[96];
        std::snprintf(text, sizeof(text), "%zu Zeilen · %s", count, follow_ ? "läuft mit" : "angehalten");
        ui::text(list, fonts.regular, text, 1824.0f, kTop + 46.0f, 21.0f, follow_ ? kMuted : kWarning,
                 gfx::Align::right);
        if (view_ == kKernel && lost_)
            ui::text(list, fonts.semibold, "Zeilen verpasst", 1824.0f - 330.0f, kTop + 46.0f, 21.0f, kWarning,
                     gfx::Align::right);
        list_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        out[0] = {ui::Button::l2, "Ansicht", ui::Button::r2};
        out[1] = {ui::Button::dpad, "Blättern"};
        out[2] = {ui::Button::square, "Zum Ende"};
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
};

} // namespace

std::unique_ptr<Page> make_logs_page()
{
    return std::make_unique<LogsPage>();
}

} // namespace hui::ps5cc
