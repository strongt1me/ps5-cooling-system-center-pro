// ps5cc - The Spielzeit page (Spiele → Spielzeit): what was played, how long, and how warm it got.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's tab: on the left the game running now, the most played games and the last
// sessions; on the right today, the last 7 and 30 days and everything, and the last fourteen days
// as bars. Triangle: delete the history (at once, as everywhere in the app). The table export stays
// in the web app: a file to save belongs to a computer.

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/playtime_rules.hpp"

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

constexpr int kRankTag = 1000;    // + index into the ranking
constexpr int kSessionTag = 2000; // + index into the sessions, newest first
constexpr int kLiveTag = 999;
constexpr int kMoreTag = 998;
constexpr int kReset = 1;
constexpr int kReload = 2;

class PlaytimePage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.header_rule = true; // title and line, as the forms (user, 29.10.2026)
        list_.style.row_height = 84.0f;
        list_.style.title_size = 24.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 21.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.badge_column = true;
        list_.style.value_width = 190.0f; // the badges line up
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 560.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});
    }

    void absorb(const Snapshot &data) override
    {
        play_ = data.playtime;
        tz_ = data.config.tz_offset_min;
        totals_ = play_totals(play_, tz_);
        days_ = play_days(play_, tz_);
        rank_ = play_ranking(play_);
        rebuild();
    }

    void enter() override
    {
        list_.enter();
    }

    bool modal() const override
    {
        return menu_.is_open();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (menu_.is_open())
        {
            if (menu_.handle(input, feedback) == ui::Event::activated)
            {
                const int tag = menu_.items()[static_cast<std::size_t>(menu_.focus())].tag;
                feedback.play(audio::Cue::select);
                if (tag == kReset)
                    source().playtime_reset();
                else if (tag == kReload)
                    source().reload();
            }
            return;
        }
        const ui::Event event = list_.handle(input, feedback);
        if (event == ui::Event::activated)
        {
            if (focused_tag() == kMoreTag)
            {
                feedback.play(audio::Cue::select);
                shown_ += 30;
                rebuild();
            }
            else if (focused_tag() == kRankTag - 1)
            {
                feedback.play(audio::Cue::select);
                all_ranks_ = !all_ranks_;
                rebuild();
            }
            else
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        if (input.is_pressed(Action::north))
        {
            std::vector<ui::MenuItem> items;
            ui::MenuItem m;
            m.label = "Verlauf löschen";
            m.tag = kReset;
            m.danger = true;
            m.disabled = play_.sessions.empty();
            items.push_back(m);
            m = ui::MenuItem{};
            m.label = "Neu laden";
            m.tag = kReload;
            items.push_back(m);
            menu_.set_items(items);
            menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
        }
    }

    void update(float dt) override
    {
        list_.update(dt);
        menu_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);

        // four numbers, two by two
        const char *const labels[4] = {"Heute", "Letzte 7 Tage", "Letzte 30 Tage", "Insgesamt"};
        const int secs[4] = {totals_.today_s, totals_.week_s, totals_.month_s, totals_.all_s};
        const int counts[4] = {totals_.today_n, totals_.week_n, totals_.month_n, totals_.all_n};
        const float tw = (card.w - 60.0f - 20.0f) * 0.5f, th = 118.0f;
        for (int i = 0; i < 4; ++i)
        {
            const Rect tile{card.x + 30.0f + static_cast<float>(i % 2) * (tw + 20.0f),
                            card.y + 30.0f + static_cast<float>(i / 2) * (th + 18.0f), tw, th};
            list.rounded_rect(tile, 16.0f, Color::rgb(0x3880ff, 0.10f));
            ui::text(list, fonts.semibold, labels[i], tile.x + 20.0f, tile.y + 34.0f, 19.0f,
                     kMuted);
            ui::text(list, fonts.display, play_hours(secs[i]), tile.x + 20.0f, tile.y + 76.0f,
                     30.0f, kInk);
            ui::text(list, fonts.regular,
                     std::to_string(counts[i]) + (counts[i] == 1 ? " Sitzung" : " Sitzungen"),
                     tile.x + 20.0f, tile.y + 104.0f, 18.0f, kMuted);
        }

        // the last fourteen days
        const float top = card.y + 330.0f;
        ui::text(list, fonts.semibold, "Die letzten 14 Tage", card.x + 30.0f, top, 21.0f, kInk);
        const Rect area{card.x + 30.0f, top + 24.0f, card.w - 60.0f, 300.0f};
        int peak = 3600;
        for (int s : days_)
            peak = std::max(peak, s);
        const float slot = area.w / 14.0f, bar_w = slot * 0.62f;
        const std::int64_t today = play_day(play_.now, tz_);
        for (int i = 0; i < 14; ++i)
        {
            const int s = days_[static_cast<std::size_t>(i)];
            const float h = (area.h - 56.0f) * static_cast<float>(s) / static_cast<float>(peak);
            const float x = area.x + slot * static_cast<float>(i) + (slot - bar_w) * 0.5f;
            const float base = area.y + area.h - 40.0f;
            list.rounded_rect({x, area.y, bar_w, base - area.y}, 6.0f, Color::rgb(0x3880ff, 0.06f));
            if (h > 0.5f)
                list.rounded_rect({x, base - h, bar_w, h}, 6.0f,
                                  i == 13 ? kBlue : Color::rgb(0x3880ff, 0.55f));
            if (s >= 60)
            {
                char text[16];
                if (s >= 3600)
                {
                    // a German decimal comma
                    std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(s) / 3600.0);
                    for (char &c : text)
                        c = c == '.' ? ',' : c;
                }
                else
                    std::snprintf(text, sizeof(text), "%d'", s / 60);
                ui::text(list, fonts.regular, text, x + bar_w * 0.5f, base - h - 8.0f, 15.0f,
                         kMuted, gfx::Align::center);
            }
            // every other day named, and today always
            if (i % 2 == 1 || i == 13)
            {
                const std::string label = play_day_label(today - 13 + i);
                ui::text(list, fonts.regular, label.substr(0, label.find(' ')), x + bar_w * 0.5f,
                         base + 22.0f, 15.0f, i == 13 ? kInk : kMuted, gfx::Align::center);
            }
        }
        std::string foot = std::to_string(play_.total) + " Sitzungen";
        if (!play_.sessions.empty())
            foot += " seit dem " + play_date(play_.sessions.front().start, tz_);
        if (play_.kept > 0 && play_.total >= play_.kept)
            foot += " · es bleiben die letzten " + std::to_string(play_.kept);
        if (!play_.enabled)
            foot = "Die Spielerkennung ist ausgeschaltet: neue Sitzungen werden nicht gezählt.";
        ui::text(
            list, fonts.semibold,
            ui::fit_label(paint, play_.loaded ? foot : "Wird geladen …", 21.0f, card.w - 60.0f),
            card.x + 30.0f, kBottom - 56.0f, 21.0f, play_.enabled ? kMuted : kWarning);
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        menu_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        const int tag = focused_tag();
        if (tag == kMoreTag)
            out[n++] = {ui::Button::cross, "Mehr anzeigen"};
        else if (tag == kRankTag - 1)
            out[n++] = {ui::Button::cross, all_ranks_ ? "Weniger anzeigen" : "Alle anzeigen"};
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1;
        return list_.items()[static_cast<std::size_t>(i)].tag;
    }

    static ui::ListItem header(std::string title)
    {
        ui::ListItem h;
        h.title = std::move(title);
        h.header = true;
        h.tag = -1;
        return h;
    }

    std::string temps(const PlaySession &s) const
    {
        std::string t;
        if (s.cpu_max >= 0)
            t += "CPU bis " + std::to_string(s.cpu_max) + " °C";
        if (s.soc_max >= 0)
            t += std::string(t.empty() ? "" : " · ") + "SoC bis " + std::to_string(s.soc_max) +
                 " °C";
        return t;
    }

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        if (!play_.loaded)
        {
            ui::ListItem n;
            n.title = "Wird geladen …";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
            return out;
        }
        if (play_.live)
        {
            out.push_back(header("LÄUFT GERADE"));
            ui::ListItem live;
            live.title = play_.current.name;
            const char *where = play_.live_state == "back"
                                    ? "pausiert, der Startbildschirm ist vorn"
                                : play_.live_state == "gone" ? "wird beendet …"
                                                             : "im Vordergrund";
            live.subtitle =
                std::string(where) + " · seit " + play_clock(play_.current.start, tz_) + " Uhr";
            const std::string t = temps(play_.current);
            if (!t.empty())
                live.subtitle += " · " + t;
            live.value = play_duration(play_.current.play);
            live.badge = "LÄUFT";
            live.tag = kLiveTag;
            out.push_back(live);
        }
        if (rank_.empty() && play_.sessions.empty())
        {
            ui::ListItem n;
            n.title = "Noch keine Sitzungen";
            n.subtitle = "Sobald ein Spiel läuft, zählt die App mit.";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
            return out;
        }
        out.push_back(header("MEISTGESPIELT"));
        int total = 0;
        for (const PlayRank &r : rank_)
            total += r.seconds;
        const std::size_t ranks =
            all_ranks_ ? rank_.size() : std::min<std::size_t>(rank_.size(), 8);
        for (std::size_t i = 0; i < ranks; ++i)
        {
            const PlayRank &r = rank_[i];
            ui::ListItem item;
            item.title = r.name;
            std::string sub = std::to_string(r.sessions) +
                              (r.sessions == 1 ? " Sitzung" : " Sitzungen") + " · im Schnitt " +
                              play_duration(r.seconds / std::max(1, r.sessions)) + " · zuletzt " +
                              play_date(r.last, tz_);
            if (r.temp_max >= 0)
                sub += " · bis " + std::to_string(r.temp_max) + " °C";
            item.subtitle = sub;
            item.value = play_duration(r.seconds);
            if (total > 0)
                item.badge = std::to_string((r.seconds * 100 + total / 2) / total) + " %";
            item.tag = kRankTag + static_cast<int>(i);
            out.push_back(item);
        }
        if (rank_.size() > 8)
        {
            ui::ListItem more;
            more.title = all_ranks_ ? "Weniger anzeigen"
                                    : "Alle " + std::to_string(rank_.size()) + " anzeigen";
            more.tag = kRankTag - 1;
            out.push_back(more);
        }
        out.push_back(header("LETZTE SITZUNGEN"));
        const int count = static_cast<int>(play_.sessions.size());
        for (int i = 0; i < std::min(count, shown_); ++i)
        {
            const PlaySession &s = play_.sessions[static_cast<std::size_t>(count - 1 - i)];
            ui::ListItem item;
            item.title = s.name;
            std::string sub = play_range(s.start, s.end, tz_);
            const std::string t = temps(s);
            if (!t.empty())
                sub += " · " + t;
            item.subtitle = sub;
            item.value = play_duration(s.play);
            item.badge = (s.flags & 1) != 0   ? "NOTFALL"
                         : (s.flags & 2) != 0 ? "WARNUNG"
                         : (s.flags & 4) != 0 ? "GESCHÄTZT"
                                              : "";
            item.tag = kSessionTag + i;
            out.push_back(item);
        }
        if (count > shown_)
        {
            ui::ListItem more;
            more.title = "Mehr anzeigen (" + std::to_string(count - shown_) + " weitere)";
            more.tag = kMoreTag;
            out.push_back(more);
        }
        return out;
    }

    // Rebuilt only when what the list shows changed: a refill restarts its animation.
    void rebuild()
    {
        std::vector<ui::ListItem> next = items();
        std::string key;
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge;
        if (key == key_)
            return;
        key_ = std::move(key);
        const int count = static_cast<int>(next.size());
        int focus = std::clamp(list_.focus(), 0, std::max(count - 1, 0));
        while (focus < count && next[static_cast<std::size_t>(focus)].header)
            ++focus;
        list_.set_items(next);
        list_.set_focus(std::min(focus, std::max(count - 1, 0)));
    }

    ui::Theme theme_;
    ui::ListView list_;
    ui::Menu menu_;
    std::string key_;
    Playtime play_;
    int tz_ = 0;
    PlayTotals totals_;
    std::array<int, 14> days_{};
    std::vector<PlayRank> rank_;
    int shown_ = 15;
    bool all_ranks_ = false;
};

} // namespace

std::unique_ptr<Page> make_playtime_page()
{
    return std::make_unique<PlaytimePage>();
}

} // namespace hui::ps5cc
