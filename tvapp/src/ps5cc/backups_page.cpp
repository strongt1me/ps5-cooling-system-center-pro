// ps5cc - The Sicherungen page (Spiele → Sicherungen): the app's own backups of games (folder
// PS5-Sicherung/Spiele).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's tab: every backup with its drive, size, date and whether it is finished and has
// checksums. Square deletes it at once (the app plans it first and refuses when it must; the
// deletion then runs as a job under Aufträge). The saves are not part of these backups and are
// never touched here.

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/playtime_rules.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr int kDelete = 1;
constexpr int kReload = 2;

class BackupsPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.row_height = 84.0f;
        list_.style.title_size = 24.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 21.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.badge_column = true;
        list_.style.value_width = 130.0f; // the badges line up
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 110.0f, kRightW - 60.0f, 300.0f});

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 560.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});
    }

    void absorb(const Snapshot &data) override
    {
        backups_ = data.game_backups;
        tz_ = data.config.tz_offset_min;
        deleting_ = data.jobs[static_cast<std::size_t>(JobKind::deletion)].active;
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
                if (tag == kDelete)
                    source().backup_delete(menu_path_);
                else if (tag == kReload)
                    source().reload();
            }
            return;
        }
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
            refresh_info();
        const GameBackup *b = focused();
        if (event == ui::Event::activated || input.is_pressed(Action::west))
        {
            if (b == nullptr)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            menu_path_ = b->path;
            std::vector<ui::MenuItem> items;
            ui::MenuItem m;
            m.label = "Sicherung löschen";
            m.tag = kDelete;
            m.danger = true;
            m.disabled = deleting_;
            items.push_back(m);
            menu_.set_items(items);
            menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
            return;
        }
        if (input.is_pressed(Action::north))
        {
            std::vector<ui::MenuItem> items;
            ui::MenuItem m;
            m.label = "Neu einlesen";
            m.tag = kReload;
            items.push_back(m);
            menu_.set_items(items);
            menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
        }
    }

    void update(float dt) override
    {
        list_.update(dt);
        info_.update(dt);
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
        const GameBackup *b = focused();
        ui::text(
            list, fonts.display,
            ui::fit_label(paint, b != nullptr ? b->name : "Sicherungen", 32.0f, card.w - 60.0f),
            card.x + 30.0f, card.y + 66.0f, 32.0f, kInk);
        float y = card.y + 120.0f;
        if (b != nullptr)
        {
            info_.draw(canvas);
            y = card.y + 450.0f;
            if (b->unfinished)
                y = paragraph(canvas,
                              "Unfertig: Die Sicherung wurde nicht zu Ende geschrieben. Zum "
                              "Zurückkopieren taugt sie "
                              "nicht; löschen und neu sichern.",
                              card.x + 30.0f, y, card.w - 60.0f, kWarning, 4) +
                    12.0f;
        }
        paragraph(canvas,
                  "Sicherungen der App (Ordner PS5-Sicherung/Spiele). Die Quadrat-Taste löscht "
                  "eine sofort; die "
                  "Spielstände bleiben erhalten. Das Löschen läuft unter Aufträge.",
                  card.x + 30.0f, y, card.w - 60.0f, kMuted, 5);
        std::uint64_t total = 0;
        for (const GameBackup &x : backups_.backups)
            total += x.size_bytes;
        const std::string foot = !backups_.loaded ? "Wird eingelesen …"
                                 : !backups_.error.empty()
                                     ? backups_.error
                                     : std::to_string(backups_.backups.size()) +
                                           " Sicherungen, zusammen " + format_bytes(total);
        ui::text(list, fonts.semibold, ui::fit_label(paint, foot, 21.0f, card.w - 60.0f),
                 card.x + 30.0f, kBottom - 56.0f, 21.0f,
                 backups_.error.empty() ? kMuted : kWarning);
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        menu_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        if (focused() != nullptr)
            out[n++] = {ui::Button::square, "Aktionen"};
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    const GameBackup *focused() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return nullptr;
        const int tag = list_.items()[static_cast<std::size_t>(i)].tag;
        return tag >= 0 && tag < static_cast<int>(backups_.backups.size())
                   ? &backups_.backups[static_cast<std::size_t>(tag)]
                   : nullptr;
    }

    float paragraph(ui::Canvas &canvas, const std::string &text, float x, float y, float width,
                    Color ink, int max_lines) const
    {
        int lines = 0;
        for (const std::string &line : canvas.fonts.regular.font->wrap(text, 21.0f, width))
        {
            if (++lines > max_lines)
                break;
            ui::text(canvas.list, canvas.fonts.regular, line, x, y, 21.0f, ink);
            y += 29.0f;
        }
        return y;
    }

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        for (std::size_t i = 0; i < backups_.backups.size(); ++i)
        {
            const GameBackup &b = backups_.backups[i];
            ui::ListItem item;
            item.title = b.name;
            item.subtitle = b.type + " · " + b.drive + " · " + play_date(b.modified, tz_);
            item.value = format_bytes(b.size_bytes);
            item.badge = b.unfinished ? "UNFERTIG" : b.sums ? "MIT PRÜFSUMME" : "";
            item.tag = static_cast<int>(i);
            out.push_back(item);
        }
        if (out.empty())
        {
            ui::ListItem n;
            n.title = !backups_.loaded         ? "Wird eingelesen …"
                      : backups_.error.empty() ? "Keine Sicherungen gefunden"
                                               : backups_.error;
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
        }
        return out;
    }

    void rebuild()
    {
        std::vector<ui::ListItem> next = items();
        std::string key;
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge;
        if (key != key_)
        {
            key_ = std::move(key);
            const int keep = list_.focus();
            list_.set_items(next);
            list_.set_focus(std::clamp(keep, 0, std::max(static_cast<int>(next.size()) - 1, 0)));
        }
        refresh_info();
    }

    void refresh_info()
    {
        std::vector<ui::DetailItem> rows;
        if (const GameBackup *b = focused())
        {
            rows.push_back({"Laufwerk", b->drive});
            rows.push_back({"Art", b->type});
            rows.push_back({"Größe", format_bytes(b->size_bytes)});
            rows.push_back({"Stand", play_date(b->modified, tz_) + ", " +
                                         play_clock(b->modified, tz_) + " Uhr"});
            rows.push_back({"Prüfsumme", b->sums ? "ja (.sha256)" : "nein"});
            rows.push_back({"Ordner", b->path});
        }
        std::string key;
        for (const ui::DetailItem &r : rows)
            key += r.label + '\t' + r.value + '\n';
        if (key == info_key_)
            return;
        info_key_ = key;
        info_.set_items(rows);
    }

    ui::Theme theme_;
    ui::ListView list_;
    ui::DetailList info_;
    ui::Menu menu_;
    std::string key_;
    std::string info_key_ = "\x01";
    GameBackups backups_;
    int tz_ = 0;
    bool deleting_ = false;
    std::string menu_path_;
};

} // namespace

std::unique_ptr<Page> make_backups_page()
{
    return std::make_unique<BackupsPage>();
}

} // namespace hui::ps5cc
