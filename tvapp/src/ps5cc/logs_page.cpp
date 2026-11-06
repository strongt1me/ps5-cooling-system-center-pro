// ps5cc - The Protokoll page: the app's own log, the console's kernel log live, and what of it is
// kept on the console (recordings and excerpts).
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
    kSaved = 2,
};

// Rows of the Gespeichert view that are not a file.
constexpr int kRowSave = -2;
constexpr int kRowClear = -3;
constexpr int kRowEmpty = -4;

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
        style_subtabs(views_, theme, 3);
        views_.set_tabs({{"App-Protokoll"}, {"Kernel-Log live"}, {"Gespeicherte"}});
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
        list_.content = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item,
                               int index, float focus)
        {
            (void)index;
            (void)focus;
            draw_line(canvas, row, item.tag);
        };
        list_.set_bounds({kLeftX, kListTop, kListW, kBottom - kListTop});
        ask_.init(theme);
    }

    bool modal() const override
    {
        return ask_.is_open();
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
        std::string files_key = data.klog_files_loaded ? "1" : "0";
        for (const KlogFile &f : data.klog_files)
            files_key += '\n' + f.name + '\t' + std::to_string(f.size) + (f.active ? "*" : "");
        const bool files_changed = files_key != files_key_;
        files_key_ = files_key;
        files_ = data.klog_files;
        files_loaded_ = data.klog_files_loaded;
        log_ = data.log;
        klog_ = data.klog;
        if ((view_ == kApp && app_changed) || (view_ == kKernel && kernel_changed) ||
            (view_ == kSaved && (files_changed || kernel_changed)))
            rebuild();
    }

    void enter() override
    {
        follow_ = true;
        rebuild();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (ask_.is_open())
        {
            // only "Löschen" in the question deletes the saved files (06.11.2026)
            if (ask_.handle(input, feedback))
                source().klog_files_clear();
            return;
        }
        if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            if (views_.step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) ==
                ui::Event::changed)
            {
                view_ = views_.active();
                follow_ = true;
                rebuild();
                if (view_ == kSaved)
                    list_.set_focus(0); // on "Auszug speichern", never first on "Ordner leeren"
            }
            return;
        }
        // Triangle in the kernel log: record it into a file, as the web app's "Aufnahme" (download
        // it there)
        if (input.is_pressed(Action::north) && view_ != kApp)
        {
            feedback.play(audio::Cue::select);
            source().klog_record(!record_.recording);
            return;
        }
        if (view_ == kSaved)
        {
            const ui::Event event = list_.handle(input, feedback);
            if (event != ui::Event::activated)
                return;
            const int tag = focused_tag();
            if (tag == kRowSave)
            {
                const std::string text = excerpt();
                if (text.empty())
                {
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                    return;
                }
                feedback.play(audio::Cue::select);
                source().klog_save(text);
            }
            else if (tag == kRowClear)
            {
                // asks first (06.11.2026); the recording that runs stays
                feedback.play(audio::Cue::select);
                ask_.open("Dateien löschen?", {}, {}, feedback,
                          "Alle gespeicherten Kernel-Log-Dateien werden gelöscht.",
                          "Das lässt sich nicht rückgängig machen.\n"
                          "Die laufende Aufnahme bleibt.");
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
        // Reading further up stops the list from running on; back at the last line it follows
        // again.
        follow_ = list_.focus() >= last();
    }

    void update(float dt) override
    {
        views_.update(dt);
        list_.update(dt);
        ask_.update(dt);
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        ask_.draw(canvas);
    }

    void draw(ui::Canvas &canvas) const override
    {
        views_.draw(canvas);
        draw_subtab_glyphs(canvas, views_);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const std::size_t count = view_ == kApp ? log_.size() : klog_.size();
        char text[96];
        if (view_ == kSaved)
        {
            std::uint64_t bytes = 0;
            for (const KlogFile &f : files_)
                bytes += f.size;
            std::snprintf(text, sizeof(text), "%zu %s · %s", files_.size(),
                          files_.size() == 1 ? "Datei" : "Dateien", format_bytes(bytes).c_str());
        }
        else
            std::snprintf(text, sizeof(text), "%zu Zeilen · %s", count,
                          follow_ ? "läuft mit" : "angehalten");
        const float baseline = kTabY + kTabH * 0.5f + 8.0f;
        ui::text(list, fonts.regular, text, 1824.0f, baseline, 21.0f, follow_ ? kMuted : kWarning,
                 gfx::Align::right);
        // Starts after the R2 glyph: right-aligned, a long file name ran back over it. The file
        // loses its fixed "aufnahme-" and ".log" so the line stays clear of the line count on the
        // right.
        const Rect &tabs = views_.bounds();
        const float status_x =
            tabs.x + tabs.w + 14.0f + ui::button_width(ui::Button::r2, 38.0f) + 22.0f;
        if (view_ != kApp && record_.recording)
        {
            std::string file = record_.file;
            if (file.rfind("aufnahme-", 0) == 0)
                file.erase(0, 9);
            if (file.size() > 4 && file.compare(file.size() - 4, 4, ".log") == 0)
                file.resize(file.size() - 4);
            const std::string rec =
                "Aufnahme läuft · " + format_bytes(record_.bytes) + " · " + file;
            ui::text(list, fonts.semibold, rec, status_x, baseline, 21.0f, kCritical);
        }
        else if (view_ == kKernel && lost_)
            ui::text(list, fonts.semibold, "Zeilen verpasst", status_x, baseline, 21.0f, kWarning);
        list_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        out[0] = {ui::Button::l2, "Ansicht", ui::Button::r2};
        if (view_ == kSaved)
        {
            int n = 1;
            const int tag = focused_tag();
            if (tag == kRowSave)
                out[n++] = {ui::Button::cross, "Auszug speichern"};
            else if (tag == kRowClear)
                out[n++] = {ui::Button::cross, "Ordner leeren"};
            out[n++] = {ui::Button::triangle, record_.recording ? "Aufnahme beenden" : "Aufnahme"};
            return n;
        }
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

    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1000;
        return list_.items()[static_cast<std::size_t>(i)].tag;
    }

    // The kernel log as it is on screen, one line each with the local time, as the web page's
    // excerpt (there with its filter).
    std::string excerpt() const
    {
        if (klog_.empty())
            return {};
        std::string out = "# PS5 Kernel-Log, Auszug aus der Vollbild-App\n";
        for (const KlogLine &l : klog_)
            out += format_time_of_day(l.t_ms, tz_) + "  " + l.text + "\n";
        return out;
    }

    void rebuild_saved()
    {
        std::vector<ui::ListItem> items;
        const auto row = [&](int tag)
        {
            ui::ListItem item;
            item.tag = tag;
            items.push_back(item);
        };
        row(kRowSave);
        for (std::size_t i = 0; i < files_.size(); ++i)
            row(static_cast<int>(i));
        if (files_.empty())
            row(kRowEmpty);
        row(kRowClear);
        const int keep = list_.focus();
        list_.set_items(items);
        list_.set_focus(std::clamp(keep, 0, last()));
    }

    void rebuild()
    {
        if (view_ == kSaved)
        {
            rebuild_saved();
            return;
        }
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
        if (view_ == kSaved)
        {
            if (index == kRowSave)
            {
                const std::string text =
                    "Auszug speichern · die " + std::to_string(klog_.size()) +
                    " Zeilen des Kernel-Logs live, im Ordner klog-live-log der Konsole";
                ui::text(list, fonts.semibold, ui::fit_label(paint, text, 19.0f, row.w - 40.0f),
                         row.x + 16.0f, base, 19.0f, kBlue);
            }
            else if (index == kRowClear)
                ui::text(list, fonts.semibold,
                         "Ordner leeren · alles außer einer laufenden Aufnahme", row.x + 16.0f,
                         base, 19.0f, kCritical);
            else if (index == kRowEmpty)
                ui::text(list, fonts.regular,
                         files_loaded_ ? "Der Ordner ist leer." : "Wird gelesen …", row.x + 16.0f,
                         base, 19.0f, kMuted);
            else if (static_cast<std::size_t>(index) < files_.size())
            {
                const KlogFile &f = files_[static_cast<std::size_t>(index)];
                ui::text(list, fonts.mono, ui::fit_label(paint, f.name, 18.0f, row.w * 0.5f),
                         row.x + 16.0f, base, 18.0f, kInk);
                std::string what = std::string(f.kind == "rec" ? "Aufnahme" : "Auszug") + " · " +
                                   format_date(f.mtime);
                if (f.active)
                    what += " · läuft";
                ui::text(list, fonts.regular, what, row.x + row.w * 0.55f, base, 18.0f,
                         f.active ? kWarning : kMuted);
                ui::text(list, fonts.regular, format_bytes(f.size), row.x + row.w - 16.0f, base,
                         18.0f, kMuted, gfx::Align::right);
            }
            return;
        }
        if (index < 0)
        {
            ui::text(list, fonts.regular,
                     view_ == kApp ? "Noch keine Einträge."
                                   : "Noch keine Zeilen – das Kernel-Log wird gelesen.",
                     row.x + 16.0f, base, 19.0f, kMuted);
            return;
        }
        const std::size_t at = static_cast<std::size_t>(index);
        if (view_ == kApp && at < log_.size())
        {
            const LogEntry &e = log_[at];
            ui::text(list, fonts.mono, format_time_of_day(e.t_ms, tz_), row.x + 16.0f, base, 18.0f,
                     kMuted);
            ui::text(list, fonts.semibold, level_text(e.level), row.x + 136.0f, base, 17.0f,
                     level_color(e.level));
            ui::text(list, fonts.regular, ui::fit_label(paint, e.message, 19.0f, row.w - 270.0f),
                     row.x + 250.0f, base, 19.0f, kInk);
            return;
        }
        if (view_ == kKernel && at < klog_.size())
        {
            const KlogLine &l = klog_[at];
            ui::text(list, fonts.mono, format_time_of_day(l.t_ms, tz_), row.x + 16.0f, base, 17.0f,
                     kMuted);
            // The mono face is not the painter's text face: shorten by its own advance (about 0.6
            // em per char).
            const std::size_t room =
                static_cast<std::size_t>(std::max(10.0f, (row.w - 150.0f) / (17.0f * 0.6f)));
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
    DeleteAsk ask_; // asked before the saved kernel-log files are deleted
    int view_ = kApp;
    bool follow_ = true;
    bool lost_ = false;
    int tz_ = 0;
    std::vector<LogEntry> log_;
    std::vector<KlogLine> klog_;
    KlogRecord record_;
    std::vector<KlogFile> files_;
    bool files_loaded_ = false;
    std::string files_key_ = "\x01";
};

} // namespace

std::unique_ptr<Page> make_logs_page()
{
    return std::make_unique<LogsPage>();
}

} // namespace hui::ps5cc
