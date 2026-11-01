// ps5cc - The Dateien area: the console's folders and drives, copying, moving, deleting, renaming,
// a new folder.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's file manager (src/filemgr.c): looking is allowed everywhere, changing only below
// the drives and /data, and the console says which with writable. The X button opens a folder or
// shows a text file; Square opens what an entry allows (pick, copy, cut, rename, delete, measure);
// Triangle the rest (paste here, new folder, pick all or none, the order, cancel the job). Names
// are typed on the kit's keyboard. Delete fires at once, as everywhere in the app. Uploading from a
// computer and downloading to one stay in the web app.

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/playtime_rules.hpp"
#include "core/tween.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace hui::ps5cc
{

// The keyboard with the German letters on a page of their own.
std::vector<ui::KeyboardLayout> german_layouts()
{
    ui::KeyboardLayout de;
    de.name = "äöü";
    de.columns = 10;
    std::vector<ui::KeyboardKey> row;
    for (const char *c : {"ä", "ö", "ü", "ß", "-", "_", "(", ")", "&", "+"})
    {
        ui::KeyboardKey k;
        k.text = c;
        if (std::string(c) == "ä")
            k.shifted = "Ä";
        else if (std::string(c) == "ö")
            k.shifted = "Ö";
        else if (std::string(c) == "ü")
            k.shifted = "Ü";
        row.push_back(k);
    }
    de.add_row(row);
    const auto wide = [](ui::KeyKind kind, int span)
    {
        ui::KeyboardKey k;
        k.kind = kind;
        k.span = span;
        return k;
    };
    de.add_row({wide(ui::KeyKind::shift, 2), wide(ui::KeyKind::layout, 1),
                wide(ui::KeyKind::space, 3), wide(ui::KeyKind::backspace, 2),
                wide(ui::KeyKind::done, 2)});
    return {ui::KeyboardLayout::letters(), de, ui::KeyboardLayout::symbols()};
}

namespace
{

using gfx::Color;
using gfx::Rect;

const Rect kPanel{200.0f, 92.0f, 1520.0f, 864.0f};

constexpr int kPlaceTag = 100;  // + index into the places
constexpr int kUpTag = 99;      // ".." up one folder
constexpr int kEntryTag = 1000; // + index into entries_ (sorted)
constexpr int kJumpRows = 8;    // L2 / R2: a page of rows at a time
constexpr float kBarH = 50.0f;  // the path bar above the list
// The page has no sub-tabs: it starts level with the sidebar's top (user, 30.10.2026).
constexpr float kFilesTop = kTabY;

const Color kFolderInk = Color::rgb(0x6f9dff);
const Color kFileInk = Color::rgb(0xaab4d4);

enum class Glyph
{
    folder,
    file,
    link,
    up,
    drive,
    picked,
};

// The row's sign, drawn with the kit's shapes like the sidebar's icons (29.10.2026: the list looked
// bare).
void draw_glyph(gfx::DrawList &list, Glyph glyph, float cx, float cy, float s, Color ink)
{
    const float x = cx - s * 0.5f;
    const float y = cy - s * 0.5f;
    const float t = std::max(2.0f, s * 0.08f);
    const Color clear{ink.r, ink.g, ink.b, 0.0f};
    switch (glyph)
    {
    case Glyph::folder:
        list.rounded_rect({x + s * 0.04f, y + s * 0.18f, s * 0.4f, s * 0.2f}, s * 0.06f, ink);
        list.rounded_rect({x + s * 0.04f, y + s * 0.28f, s * 0.92f, s * 0.56f}, s * 0.1f, ink);
        break;
    case Glyph::link:
        list.rounded_rect({x + s * 0.04f, y + s * 0.18f, s * 0.4f, s * 0.2f}, s * 0.06f, ink);
        list.bordered_rect({x + s * 0.04f, y + s * 0.28f, s * 0.92f, s * 0.56f}, s * 0.1f, clear, t,
                           ink);
        list.line(x + s * 0.36f, y + s * 0.7f, x + s * 0.64f, y + s * 0.46f, t, ink);
        list.line(x + s * 0.48f, y + s * 0.46f, x + s * 0.64f, y + s * 0.46f, t, ink);
        list.line(x + s * 0.64f, y + s * 0.46f, x + s * 0.64f, y + s * 0.6f, t, ink);
        break;
    case Glyph::file:
        list.bordered_rect({x + s * 0.18f, y + s * 0.06f, s * 0.64f, s * 0.88f}, s * 0.1f, clear, t,
                           ink);
        for (int i = 0; i < 3; ++i)
        {
            const float ly = y + s * (0.36f + 0.17f * static_cast<float>(i));
            list.line(x + s * 0.34f, ly, x + s * (i == 2 ? 0.54f : 0.66f), ly, t * 0.9f, ink);
        }
        break;
    case Glyph::up:
        list.line(cx, y + s * 0.24f, cx, y + s * 0.86f, t * 1.2f, ink);
        list.line(cx, y + s * 0.2f, x + s * 0.24f, y + s * 0.48f, t * 1.2f, ink);
        list.line(cx, y + s * 0.2f, x + s * 0.76f, y + s * 0.48f, t * 1.2f, ink);
        break;
    case Glyph::drive:
        list.bordered_rect({x + s * 0.04f, y + s * 0.26f, s * 0.92f, s * 0.48f}, s * 0.12f, clear,
                           t, ink);
        list.circle(x + s * 0.76f, cy, s * 0.07f, ink);
        list.line(x + s * 0.2f, cy, x + s * 0.5f, cy, t, ink);
        break;
    case Glyph::picked:
        list.rounded_rect({x + s * 0.06f, y + s * 0.06f, s * 0.88f, s * 0.88f}, s * 0.22f,
                          Color::rgb(0x3880ff));
        list.line(x + s * 0.28f, y + s * 0.52f, x + s * 0.44f, y + s * 0.68f, t * 1.3f,
                  Color::rgb(0xffffff));
        list.line(x + s * 0.44f, y + s * 0.68f, x + s * 0.74f, y + s * 0.34f, t * 1.3f,
                  Color::rgb(0xffffff));
        break;
    }
}

enum MenuTag
{
    kPick = 1,
    kCopy,
    kCut,
    kRename,
    kDelete,
    kMeasure,
    kView,
    kPaste = 20,
    kForget,
    kNewFolder,
    kPickAll,
    kPickNone,
    kSort,
    kCancel,
    kReload,
};

enum Dialog
{
    kNone,
    kName,
    kViewer,
};

std::string json_text(const std::string &s)
{
    std::string out = "\"";
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out + "\"";
}

std::string join(const std::string &folder, const std::string &name)
{
    return folder == "/" ? "/" + name : folder + "/" + name;
}

std::string base_of(const std::string &path)
{
    const std::size_t s = path.find_last_of('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

class FilesPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.row_height = 76.0f;
        list_.style.title_size = 23.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 20.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.badge_column = true;
        list_.style.value_width = 130.0f; // the badges line up
        list_.style.leading_width = 40.0f;
        list_.leading =
            [this](ui::Canvas &canvas, const Rect &box, const ui::ListItem &item, int, float)
        { draw_glyph(canvas.list, glyph_of(item), box.cx(), box.cy(), 34.0f, ink_of(item)); };
        list_.set_bounds(
            {kLeftX, kFilesTop + kBarH + 14.0f, kLeftW, kBottom - kFilesTop - kBarH - 14.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kFilesTop + 158.0f, kRightW - 60.0f,
                          220.0f}); // under the sign and the line

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 560.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});

        keys_.style.theme = theme;
        keys_.style.bindings = ui::KeyboardBindings::standard();
        keys_.style.space_label = "Leer";
        keys_.style.done_label = "Fertig";
        keys_.style.max_length = 120;
        keys_.set_layouts(german_layouts());
        keys_.set_bounds({kPanel.x + 120.0f, kPanel.y + 330.0f, kPanel.w - 240.0f, 420.0f});
        keys_.on_text = [this](std::string_view utf8) { name_.append(utf8); };
        keys_.on_backspace = [this]
        {
            // one UTF-8 character
            while (!name_.empty() && (static_cast<unsigned char>(name_.back()) & 0xC0) == 0x80)
                name_.pop_back();
            if (!name_.empty())
                name_.pop_back();
        };
    }

    void absorb(const Snapshot &data) override
    {
        places_ = data.fs_places;
        job_ = data.fs_job;
        sizes_ = data.fs_sizes;
        tz_ = data.config.tz_offset_min;
        if (data.fs_list.seq != list_seq_)
        {
            list_seq_ = data.fs_list.seq;
            folder_ = data.fs_list;
            if (folder_.path != shown_path_)
            {
                shown_path_ = folder_.path;
                picked_.clear();
                list_.set_focus(0);
            }
            sort_entries();
        }
        if (dialog_ == kViewer && data.fs_view.seq != view_seq_)
        {
            view_seq_ = data.fs_view.seq;
            view_ = data.fs_view;
            view_lines_ = split_lines(view_.text);
            view_top_ = 0;
        }
        if (dialog_ != kViewer)
            view_seq_ = data.fs_view.seq;
        rebuild();
    }

    void enter() override
    {
        list_.enter();
        if (!entered_)
        {
            entered_ = true;
            source().files_open({});
        }
    }

    bool modal() const override
    {
        return menu_.is_open() || dialog_ != kNone;
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (menu_.is_open())
        {
            if (menu_.handle(input, feedback) == ui::Event::activated)
                run(menu_.items()[static_cast<std::size_t>(menu_.focus())].tag, feedback);
            return;
        }
        if (dialog_ == kName)
        {
            keys_.set_length(static_cast<int>(name_.size()));
            const ui::Event event = keys_.handle(input, feedback);
            if (event == ui::Event::cancelled)
            {
                dialog_ = kNone;
                feedback.play(audio::Cue::back);
            }
            else if (event == ui::Event::activated)
                submit_name(feedback);
            return;
        }
        if (dialog_ == kViewer)
        {
            const int page = 18;
            if (input.is_pressed(Action::back) || input.is_pressed(Action::confirm))
            {
                dialog_ = kNone;
                feedback.play(audio::Cue::back);
            }
            else if (input.nav == Direction::down)
                view_top_ = std::min(view_top_ + 1,
                                     std::max(0, static_cast<int>(view_lines_.size()) - page));
            else if (input.nav == Direction::up)
                view_top_ = std::max(0, view_top_ - 1);
            return;
        }
        if (input.is_pressed(Action::back) && !folder_.path.empty())
        {
            go_back(feedback);
            return;
        }
        if (jump(input, feedback))
            return;
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
            refresh_info();
        if (event == ui::Event::activated)
        {
            activate(feedback);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            open_entry_menu(feedback);
            return;
        }
        if (input.is_pressed(Action::north))
            open_page_menu(feedback);
    }

    void update(float dt) override
    {
        clock_ += dt;
        list_.update(dt);
        info_.update(dt);
        menu_.update(dt);
        if (dialog_ == kName)
            keys_.update(dt);
        shown_.target = dialog_ != kNone ? 1.0f : 0.0f;
        shown_.update(dt, 14.0f);
    }

    void draw(ui::Canvas &canvas) const override
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        ui::Painter paint(list, fonts, theme_);
        const std::string where = folder_.path.empty() ? "Orte" : folder_.path;
        draw_path_bar(canvas, paint);
        list_.draw(canvas);

        const Rect card{kRightX, kFilesTop, kRightW, kBottom - kFilesTop};
        paint.panel(card);
        const FsEntry *e = focused_entry();
        const FsPlace *pl = focused_place();
        const std::string title = e != nullptr    ? e->name
                                  : pl != nullptr ? pl->label
                                                  : base_or_root(where);
        // a large sign beside the name, then what it is
        const Glyph sign = e != nullptr    ? entry_glyph(*e)
                           : pl != nullptr ? Glyph::drive
                                           : Glyph::folder;
        const Rect tile{card.x + 30.0f, card.y + 30.0f, 84.0f, 84.0f};
        list.rounded_rect(tile, 20.0f, Color::rgb(0x3880ff, 0.14f));
        draw_glyph(list, sign, tile.cx(), tile.cy(), 50.0f,
                   sign == Glyph::file ? kFileInk : kFolderInk);
        ui::text(list, fonts.display, ui::fit_label(paint, title, 30.0f, card.w - 180.0f),
                 tile.x + tile.w + 24.0f, card.y + 70.0f, 30.0f, kInk);
        ui::text(list, fonts.regular,
                 ui::fit_label(paint, card_subtitle(e, pl, where), 19.0f, card.w - 180.0f),
                 tile.x + tile.w + 24.0f, card.y + 102.0f, 19.0f, kMuted);
        list.line(card.x + 30.0f, card.y + 140.0f, card.x + card.w - 30.0f, card.y + 140.0f, 1.0f,
                  Color::rgb(0x3880ff, 0.18f));
        info_.draw(canvas);

        float y = card.y + 400.0f;
        if (!folder_.path.empty() && !folder_.writable)
            y = note_card(canvas, paint, card, y, "Nur ansehen",
                          "Hier darf nichts geändert werden. Geschützt sind nur /dev, /proc, "
                          "Einhängepunkte und Verknüpfungen.",
                          kWarning);
        if (!clip_paths_.empty())
            y = note_card(canvas, paint, card, y,
                          std::string(clip_move_ ? "Ausgeschnitten: " : "Kopiert: ") +
                              std::to_string(clip_paths_.size()) +
                              (clip_paths_.size() == 1 ? " Eintrag" : " Einträge"),
                          "aus " + clip_from_ + " · Mehr → Hier einfügen", kBlue);
        if (!picked_.empty())
            y = note_card(canvas, paint, card, y, std::to_string(picked_.size()) + " ausgewählt",
                          "Quadrat-Taste: Kopieren, Ausschneiden, Löschen …", kBlue);

        // the job at the bottom
        const float jy = kBottom - 170.0f;
        if (job_.active ||
            (job_.finished_ago_s >= 0 && job_.finished_ago_s < 60 && !job_.kind.empty()))
        {
            list.line(card.x + 30.0f, jy, card.x + card.w - 30.0f, jy, 1.0f,
                      Color::rgb(0x3880ff, 0.18f));
            const char *what = job_.kind == "move"     ? "Verschieben"
                               : job_.kind == "delete" ? "Löschen"
                                                       : "Kopieren";
            if (job_.active)
            {
                ui::text(list, fonts.semibold,
                         std::string(what) + " · " + std::to_string(job_.percent) + " %",
                         card.x + 30.0f, jy + 40.0f, 22.0f, kInk);
                const Rect bar{card.x + 30.0f, jy + 58.0f, card.w - 60.0f, 12.0f};
                list.rounded_rect(bar, 6.0f, Color::rgb(0x3880ff, 0.18f));
                list.rounded_rect(
                    {bar.x, bar.y,
                     bar.w * static_cast<float>(std::clamp(job_.percent, 0, 100)) / 100.0f, bar.h},
                    6.0f, kBlue);
                const std::string amount =
                    job_.bytes_total > 0
                        ? format_bytes(job_.bytes_done) + " von " + format_bytes(job_.bytes_total)
                        : std::to_string(job_.files_done) + " von " +
                              std::to_string(job_.files_total) + " Dateien";
                ui::text(list, fonts.regular,
                         ui::fit_label(paint, amount + " · " + job_.current, 18.0f, card.w - 60.0f),
                         card.x + 30.0f, jy + 100.0f, 18.0f, kMuted);
            }
            else
            {
                const bool ok = job_.state == "done";
                ui::text(list, fonts.semibold,
                         std::string(what) + (ok                          ? " fertig"
                                              : job_.state == "cancelled" ? " abgebrochen"
                                                                          : " fehlgeschlagen"),
                         card.x + 30.0f, jy + 40.0f, 22.0f, ok ? kGood : kWarning);
                paragraph(canvas, !job_.error.empty() ? job_.error : job_.note, card.x + 30.0f,
                          jy + 76.0f, card.w - 60.0f, kMuted, 3);
            }
        }
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        menu_.draw(canvas);
        const float amount = shown_.value;
        if (amount <= 0.01f)
            return;
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, kNavy.with_alpha(0.78f * amount));
        list.push_opacity(amount);
        const Rect panel{kPanel.x, kPanel.y + 24.0f * (1.0f - amount), kPanel.w, kPanel.h};
        ui::Painter paint(list, fonts, theme_);
        solid_floor(list, panel, theme_.radius_card);
        solid_floor(list, panel, theme_.radius_card);
        paint.panel(panel);
        ui::HintLayout layout;
        layout.size = 34.0f;
        layout.text_size = 21.0f;
        layout.cy = panel.y + panel.h - 42.0f;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        if (dialog_ == kName)
        {
            ui::text(list, fonts.display, name_mode_ == kRename ? "Umbenennen" : "Neuer Ordner",
                     panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
            ui::text(list, fonts.regular,
                     ui::fit_label(paint, "in " + folder_.path, 21.0f, panel.w - 80.0f),
                     panel.x + 40.0f, panel.y + 104.0f, 21.0f, kMuted);
            const Rect field{panel.x + 120.0f, panel.y + 160.0f, panel.w - 240.0f, 84.0f};
            list.rounded_rect(field, 14.0f, Color::rgb(0x3880ff, 0.12f));
            const std::string shown = ui::fit_label(paint, name_, 32.0f, field.w - 60.0f);
            ui::text(list, fonts.semibold, shown, field.x + 28.0f, field.y + 54.0f, 32.0f, kInk);
            if (std::fmod(clock_, 1.0f) < 0.6f)
            {
                const float cx = field.x + 30.0f + fonts.semibold.font->measure(shown, 32.0f);
                list.rounded_rect({cx, field.y + 22.0f, 3.0f, 40.0f}, 1.5f, kBlue);
            }
            if (!name_error_.empty())
                ui::text(list, fonts.regular, name_error_, field.x, field.y + field.h + 36.0f,
                         21.0f, kCritical);
            keys_.draw(canvas);
            const ui::Hint hints[] = {{ui::Button::square, "Zeichen löschen"},
                                      {ui::Button::triangle, "Leerzeichen"},
                                      {ui::Button::circle, "Abbrechen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 3, panel.x + 40.0f, false, layout);
        }
        else
        {
            ui::text(list, fonts.display,
                     ui::fit_label(paint, base_of(view_.path), 36.0f, panel.w - 80.0f),
                     panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
            std::string about = view_.path;
            if (view_.truncated)
                about += " · nur der Anfang";
            ui::text(list, fonts.regular, ui::fit_label(paint, about, 21.0f, panel.w - 80.0f),
                     panel.x + 40.0f, panel.y + 104.0f, 21.0f, kMuted);
            if (view_.seq == 0 || (view_.path.empty() && view_.error.empty()))
                ui::text(list, fonts.semibold, "Wird geladen …", panel.x + panel.w * 0.5f,
                         panel.y + 420.0f, 30.0f, kInk, gfx::Align::center);
            else if (!view_.error.empty())
                paragraph(canvas, view_.error, panel.x + 40.0f, panel.y + 200.0f, panel.w - 80.0f,
                          kCritical, 4);
            else
            {
                float y = panel.y + 160.0f;
                for (int i = view_top_;
                     i < static_cast<int>(view_lines_.size()) && y < panel.y + panel.h - 90.0f; ++i)
                {
                    ui::text(list, fonts.regular,
                             ui::fit_label(paint, view_lines_[static_cast<std::size_t>(i)], 20.0f,
                                           panel.w - 80.0f),
                             panel.x + 40.0f, y, 20.0f, kInk.with_alpha(0.9f));
                    y += 30.0f;
                }
            }
            const ui::Hint hints[] = {{ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 1, panel.x + 40.0f, false, layout);
        }
        list.pop_opacity();
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        if (const FsEntry *e = focused_entry())
        {
            if (e->type == "dir" || e->type == "dirlink")
                out[n++] = {ui::Button::cross, "Öffnen"};
            else if (e->type == "file")
                out[n++] = {ui::Button::cross, "Ansehen"};
            out[n++] = {ui::Button::square, "Aktionen"};
        }
        else if (focused_place() != nullptr || focused_tag() == kUpTag)
            out[n++] = {ui::Button::cross, "Öffnen"};
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    // ---------------------------------------------------------------- what is focused

    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1;
        return list_.items()[static_cast<std::size_t>(i)].tag;
    }

    const FsEntry *focused_entry() const
    {
        const int i = focused_tag() - kEntryTag;
        return i >= 0 && i < static_cast<int>(entries_.size())
                   ? &entries_[static_cast<std::size_t>(i)]
                   : nullptr;
    }

    const FsPlace *focused_place() const
    {
        const int i = focused_tag() - kPlaceTag;
        return folder_.path.empty() && i >= 0 && i < static_cast<int>(places_.size())
                   ? &places_[static_cast<std::size_t>(i)]
                   : nullptr;
    }

    // The picked entries, or the focused one when none is picked: what copy, cut and delete act on.
    std::vector<const FsEntry *> targets() const
    {
        std::vector<const FsEntry *> out;
        for (const FsEntry &e : entries_)
        {
            if (picked_.count(e.name))
                out.push_back(&e);
        }
        if (out.empty())
        {
            if (const FsEntry *e = focused_entry())
                out.push_back(e);
        }
        return out;
    }

    static bool pickable(const FsEntry &e)
    {
        return e.type == "dir" || e.type == "file";
    }

    // ---------------------------------------------------------------- actions

    void open(const std::string &path, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        if (history_.size() >= 64)
            history_.erase(history_.begin());
        history_.push_back(folder_.path); // where O leads back to
        source().files_open(path);
    }

    // O: back where the user was before opening this folder (a place opened from Orte goes back to
    // Orte, not to /mnt), with the focus on the folder just left (user, 30.10.2026). Without a way
    // back: up one folder.
    void go_back(ui::Feedback &feedback)
    {
        while (!history_.empty() && history_.back() == folder_.path)
            history_.pop_back();
        std::string target = folder_.parent;
        if (!history_.empty())
        {
            target = history_.back();
            history_.pop_back();
        }
        came_from_ = folder_.path;
        source().files_open(target);
        feedback.play(audio::Cue::back);
    }

    // The row "Übergeordneter Ordner": always the folder above.
    void go_up(ui::Feedback &feedback)
    {
        came_from_ = folder_.path;
        while (!history_.empty() && history_.back() == folder_.parent)
            history_.pop_back();
        source().files_open(folder_.parent);
        feedback.play(audio::Cue::back);
    }

    void activate(ui::Feedback &feedback)
    {
        if (const FsPlace *p = focused_place())
        {
            open(p->path, feedback);
            return;
        }
        if (focused_tag() == kUpTag)
        {
            go_up(feedback);
            return;
        }
        const FsEntry *e = focused_entry();
        if (e != nullptr && (e->type == "dir" || e->type == "dirlink"))
            open(join(folder_.path, e->name), feedback);
        else if (e != nullptr && e->type == "file")
            show(join(folder_.path, e->name), feedback);
        else
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    }

    void show(const std::string &path, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        dialog_ = kViewer;
        view_ = FsView{};
        view_lines_.clear();
        source().files_view(path);
    }

    static ui::MenuItem item(const std::string &label, int tag, bool disabled = false,
                             bool danger = false)
    {
        ui::MenuItem m;
        m.label = label;
        m.tag = tag;
        m.disabled = disabled;
        m.danger = danger;
        return m;
    }

    void open_entry_menu(ui::Feedback &feedback)
    {
        const FsEntry *e = focused_entry();
        if (e == nullptr)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        menu_entry_ = *e;
        const std::vector<const FsEntry *> t = targets();
        const bool all_writable = std::all_of(t.begin(), t.end(), [](const FsEntry *x)
                                              { return x->writable && pickable(*x); });
        const std::string count = t.size() > 1 ? " (" + std::to_string(t.size()) + ")" : "";
        std::vector<ui::MenuItem> items;
        if (pickable(*e))
            items.push_back(item(picked_.count(e->name) ? "Abwählen" : "Auswählen", kPick));
        if (e->type == "file")
            items.push_back(item("Ansehen", kView));
        if (e->type == "dir")
            items.push_back(item("Größe messen", kMeasure));
        items.push_back(
            item("Kopieren" + count, kCopy,
                 !std::all_of(t.begin(), t.end(), [](const FsEntry *x) { return pickable(*x); })));
        items.push_back(item("Ausschneiden" + count, kCut, !all_writable));
        items.push_back(item("Umbenennen", kRename, !e->writable || !pickable(*e)));
        items.push_back(item("Löschen" + count, kDelete, !all_writable || job_.active, true));
        menu_.set_items(items);
        menu_.open({kRightX, kFilesTop, kRightW, 80.0f}, feedback);
    }

    void open_page_menu(ui::Feedback &feedback)
    {
        const bool inside = !folder_.path.empty();
        const char *const orders[] = {"Name", "Größe", "Datum"};
        std::vector<ui::MenuItem> items;
        if (!clip_paths_.empty())
        {
            items.push_back(
                item("Hier einfügen (" + std::to_string(clip_paths_.size()) + ")", kPaste,
                     !inside || !folder_.writable || folder_.path == clip_from_ || job_.active));
            items.push_back(item("Ablage leeren", kForget));
        }
        items.push_back(item("Neuer Ordner", kNewFolder, !inside || !folder_.writable));
        items.push_back(item("Alle auswählen", kPickAll, !inside || entries_.empty()));
        items.push_back(item("Keine auswählen", kPickNone, picked_.empty()));
        items.push_back(item(std::string("Sortieren nach: ") + orders[sort_], kSort, !inside));
        if (job_.active)
            items.push_back(item("Auftrag abbrechen", kCancel, false, true));
        items.push_back(item("Aktualisieren", kReload));
        menu_.set_items(items);
        menu_.open({kRightX, kFilesTop, kRightW, 80.0f}, feedback);
    }

    std::string paths_json(const std::vector<std::string> &paths) const
    {
        std::string out;
        for (const std::string &p : paths)
            out += (out.empty() ? "" : ",") + json_text(p);
        return "[" + out + "]";
    }

    void run(int tag, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        std::vector<std::string> paths;
        for (const FsEntry *e : targets())
            paths.push_back(join(folder_.path, e->name));
        switch (tag)
        {
        case kPick:
            if (!picked_.erase(menu_entry_.name))
                picked_.insert(menu_entry_.name);
            break;
        case kView:
            show(join(folder_.path, menu_entry_.name), feedback);
            break;
        case kMeasure:
            source().files_size(join(folder_.path, menu_entry_.name));
            break;
        case kCopy:
        case kCut:
            clip_move_ = tag == kCut;
            clip_from_ = folder_.path;
            clip_paths_ = paths;
            picked_.clear();
            break;
        case kRename:
            open_name(kRename, feedback);
            break;
        case kDelete:
            source().files_delete(paths);
            picked_.clear();
            break;
        case kPaste:
            source().files_post(clip_move_ ? "move" : "copy",
                                "{\"paths\":" + paths_json(clip_paths_) +
                                    ",\"dest\":" + json_text(folder_.path) + "}");
            // a copy can be pasted again; what was cut is gone from its place
            clip_paths_.clear();
            break;
        case kForget:
            clip_paths_.clear();
            break;
        case kNewFolder:
            open_name(kNewFolder, feedback);
            break;
        case kPickAll:
            for (const FsEntry &e : entries_)
            {
                if (pickable(e))
                    picked_.insert(e.name);
            }
            break;
        case kPickNone:
            picked_.clear();
            break;
        case kSort:
            sort_ = (sort_ + 1) % 3;
            sort_entries();
            break;
        case kCancel:
            source().files_post("job/cancel", "{}");
            break;
        case kReload:
            source().files_open(folder_.path);
            break;
        default:
            break;
        }
        key_.clear();
        rebuild();
    }

    void open_name(int mode, ui::Feedback &feedback)
    {
        (void)feedback;
        name_mode_ = mode;
        name_error_.clear();
        name_ = mode == kRename ? menu_entry_.name : "Neuer Ordner";
        keys_.set_layout(0);
        keys_.set_length(static_cast<int>(name_.size()));
        keys_.set_active(true);
        keys_.enter();
        dialog_ = kName;
    }

    void submit_name(ui::Feedback &feedback)
    {
        if (name_.empty() || name_ == "." || name_ == ".." ||
            name_.find('/') != std::string::npos || name_.find('\\') != std::string::npos)
        {
            name_error_ = "Ein Name darf nicht leer sein und kein / oder \\ enthalten.";
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        if (name_mode_ == kRename)
            source().files_post("rename",
                                "{\"path\":" + json_text(join(folder_.path, menu_entry_.name)) +
                                    ",\"name\":" + json_text(name_) + "}");
        else
            source().files_post("mkdir", "{\"path\":" + json_text(folder_.path) +
                                             ",\"name\":" + json_text(name_) + "}");
        dialog_ = kNone;
        feedback.play(audio::Cue::select);
    }

    // ---------------------------------------------------------------- list

    static std::vector<std::string> split_lines(const std::string &text)
    {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start < text.size() && out.size() < 5000)
        {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            for (char &c : line)
                c = c == '\t' ? ' ' : c;
            out.push_back(std::move(line));
            start = end + 1;
        }
        return out;
    }

    // Folders first, then by the chosen order (size and date: the biggest and newest first), as the
    // web page.
    void sort_entries()
    {
        entries_ = folder_.entries;
        const int order = sort_;
        std::stable_sort(entries_.begin(), entries_.end(),
                         [order](const FsEntry &a, const FsEntry &b)
                         {
                             const bool da = a.type == "dir" || a.type == "dirlink";
                             const bool db = b.type == "dir" || b.type == "dirlink";
                             if (da != db)
                                 return da;
                             if (order == 1 && a.size != b.size)
                                 return a.size > b.size;
                             if (order == 2 && a.mtime != b.mtime)
                                 return a.mtime > b.mtime;
                             return false; // the console's own order: by name, case-insensitive
                         });
    }

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        if (folder_.path.empty())
        {
            for (std::size_t i = 0; i < places_.size(); ++i)
            {
                const FsPlace &p = places_[i];
                ui::ListItem item;
                item.title = p.label;
                item.subtitle = p.path + (p.writable ? "" : " · nur ansehen");
                if (p.free_bytes >= 0 && p.writable)
                    item.value = format_bytes(static_cast<std::uint64_t>(p.free_bytes)) + " frei";
                item.tag = kPlaceTag + static_cast<int>(i);
                out.push_back(item);
            }
            if (out.empty())
            {
                ui::ListItem n;
                n.title = "Wird gelesen …";
                n.disabled = true;
                n.tag = -1;
                out.push_back(n);
            }
            return out;
        }
        ui::ListItem up;
        up.title = "Übergeordneter Ordner";
        up.subtitle = folder_.parent.empty() ? "zurück zu den Orten" : folder_.parent;
        up.tag = kUpTag;
        out.push_back(up);
        if (!folder_.error.empty())
        {
            ui::ListItem n;
            n.title = folder_.error;
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
            return out;
        }
        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            const FsEntry &e = entries_[i];
            ui::ListItem item;
            const bool folder = e.type == "dir" || e.type == "dirlink";
            item.title = e.name;
            std::string sub = std::string(folder ? "Ordner · " : "") + play_date(e.mtime, tz_) +
                              ", " + play_clock(e.mtime, tz_) + " Uhr";
            if (e.type == "dirlink")
                sub += " · Verknüpfung zu einem Ordner";
            else if (e.type == "link")
                sub += " · Verknüpfung";
            else if (e.type == "other")
                sub += " · Gerät oder Sonderdatei";
            item.subtitle = sub;
            const std::string full = join(folder_.path, e.name);
            auto s = sizes_.find(full);
            if (e.type == "file")
                item.value = format_bytes(e.size);
            else if (s != sizes_.end())
                item.value = s->second.failed
                                 ? "?"
                                 : format_bytes(s->second.bytes) + (s->second.partial ? "+" : "");
            // picked: the sign turns into a check (glyph_of), no badge
            if (picked_.count(e.name) == 0 && !clip_paths_.empty() &&
                std::find(clip_paths_.begin(), clip_paths_.end(), full) != clip_paths_.end())
                item.badge = clip_move_ ? "AUSGESCHNITTEN" : "KOPIERT";
            item.tag = kEntryTag + static_cast<int>(i);
            out.push_back(item);
        }
        if (entries_.empty())
        {
            ui::ListItem n;
            n.title = "Der Ordner ist leer";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
        }
        if (folder_.truncated)
        {
            ui::ListItem n;
            n.title = "Nur die ersten " + std::to_string(entries_.size()) + " von " +
                      std::to_string(folder_.total);
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
        }
        return out;
    }

    void rebuild()
    {
        std::vector<ui::ListItem> next = items();
        std::string key = folder_.path;
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge;
        for (const std::string &name : picked_)
            key += "\n+" + name;
        if (key != key_)
        {
            key_ = std::move(key);
            const int keep = list_.focus();
            list_.set_items(next);
            list_.set_focus(std::clamp(keep, 0, std::max(static_cast<int>(next.size()) - 1, 0)));
            focus_came_from();
        }
        refresh_info();
    }

    // After going up: the row of the folder we came out of, once the parent's list is here.
    void focus_came_from()
    {
        if (came_from_.empty() || came_from_ == folder_.path)
            return;
        const std::vector<ui::ListItem> &rows = list_.items();
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const int tag = rows[i].tag;
            std::string path;
            if (tag >= kEntryTag && tag - kEntryTag < static_cast<int>(entries_.size()))
                path = join(folder_.path, entries_[static_cast<std::size_t>(tag - kEntryTag)].name);
            else if (tag >= kPlaceTag && tag < kEntryTag &&
                     tag - kPlaceTag < static_cast<int>(places_.size()))
                path = places_[static_cast<std::size_t>(tag - kPlaceTag)].path;
            if (!path.empty() && path == came_from_)
            {
                list_.set_focus(static_cast<int>(i));
                came_from_.clear();
                return;
            }
        }
        if (folder_.path.empty() ? !places_.empty() : !entries_.empty())
            came_from_.clear(); // the list is here and the folder is not in it
    }

    void refresh_info()
    {
        std::vector<ui::DetailItem> rows;
        if (const FsEntry *e = focused_entry())
        {
            const char *kind = e->type == "dir"       ? "Ordner"
                               : e->type == "dirlink" ? "Verknüpfung zu einem Ordner"
                               : e->type == "link"    ? "Verknüpfung"
                               : e->type == "other"   ? "Gerät oder Sonderdatei"
                                                      : "Datei";
            rows.push_back({"Art", kind});
            const std::string full = join(folder_.path, e->name);
            auto s = sizes_.find(full);
            if (e->type == "file")
                rows.push_back({"Größe", format_bytes(e->size)});
            else if (s != sizes_.end() && !s->second.failed)
                rows.push_back({"Größe", (s->second.partial ? "mindestens " : "") +
                                             format_bytes(s->second.bytes) + " · " +
                                             std::to_string(s->second.files) + " Dateien"});
            rows.push_back(
                {"Geändert", play_date(e->mtime, tz_) + ", " + play_clock(e->mtime, tz_) + " Uhr"});
            rows.push_back({"Ändern", e->writable ? "erlaubt" : "nur ansehen"});
        }
        else if (const FsPlace *p = focused_place())
        {
            rows.push_back({"Ordner", p->path});
            if (p->free_bytes >= 0)
                rows.push_back({"Frei", format_bytes(static_cast<std::uint64_t>(p->free_bytes))});
            rows.push_back({"Ändern", p->writable ? "erlaubt" : "nur ansehen"});
        }
        else if (!folder_.path.empty())
        {
            rows.push_back({"Einträge", std::to_string(folder_.entries.size())});
            if (folder_.free_bytes >= 0)
                rows.push_back(
                    {"Frei", format_bytes(static_cast<std::uint64_t>(folder_.free_bytes))});
            rows.push_back({"Ändern", folder_.writable ? "erlaubt" : "nur ansehen"});
        }
        std::string key;
        for (const ui::DetailItem &r : rows)
            key += r.label + '\t' + r.value + '\n';
        if (key == info_key_)
            return;
        info_key_ = key;
        info_.set_items(rows);
    }

    static Glyph entry_glyph(const FsEntry &e)
    {
        if (e.type == "dir")
            return Glyph::folder;
        if (e.type == "dirlink" || e.type == "link")
            return Glyph::link;
        return Glyph::file;
    }

    Glyph glyph_of(const ui::ListItem &item) const
    {
        if (item.tag == kUpTag)
            return Glyph::up;
        if (item.tag >= kPlaceTag && item.tag < kEntryTag)
            return Glyph::drive;
        const int i = item.tag - kEntryTag;
        if (i >= 0 && i < static_cast<int>(entries_.size()))
        {
            const FsEntry &e = entries_[static_cast<std::size_t>(i)];
            return picked_.count(e.name) != 0 ? Glyph::picked : entry_glyph(e);
        }
        return Glyph::file;
    }

    Color ink_of(const ui::ListItem &item) const
    {
        const Glyph g = glyph_of(item);
        if (item.tag < 0)
            return {0.0f, 0.0f, 0.0f, 0.0f}; // "Wird gelesen", "leer": no sign
        return g == Glyph::file || g == Glyph::up ? kFileInk : kFolderInk;
    }

    static std::string base_or_root(const std::string &path)
    {
        const std::size_t cut = path.find_last_of('/');
        if (cut == std::string::npos || cut + 1 >= path.size())
            return path;
        return path.substr(cut + 1);
    }

    std::string card_subtitle(const FsEntry *e, const FsPlace *pl, const std::string &where) const
    {
        if (e != nullptr)
            return join(folder_.path, e->name);
        if (pl != nullptr)
            return pl->path;
        if (focused_tag() == kUpTag)
            return folder_.parent.empty() ? "zurück zu den Orten" : "hoch nach " + folder_.parent;
        return where;
    }

    // Where we are: the folders from the root, the last one bright; on the right how many and in
    // which order, and that L2 and R2 turn the pages.
    void draw_path_bar(ui::Canvas &canvas, ui::Painter &paint) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect bar{kLeftX, kFilesTop, kLeftW, kBarH};
        list.rounded_rect(bar, bar.h * 0.5f, Color::rgb(0x0b1430, 0.72f));
        list.bordered_rect(bar, bar.h * 0.5f, {0.0f, 0.0f, 0.0f, 0.0f}, 1.0f,
                           Color::rgb(0x3880ff, 0.28f));
        const float base = bar.y + bar.h * 0.5f + 7.0f;
        draw_glyph(list, folder_.path.empty() ? Glyph::drive : Glyph::folder, bar.x + 32.0f,
                   bar.y + bar.h * 0.5f, 24.0f, kFolderInk);

        // right: count · order, then the L2 R2 glyphs
        const char *order = sort_ == 1 ? "Größe" : sort_ == 2 ? "Datum" : "Name";
        const std::string right =
            folder_.path.empty() ? std::to_string(places_.size()) + " Orte"
                                 : std::to_string(entries_.size()) + " Einträge · nach " + order;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        const float r2w = ui::button_width(ui::Button::r2, 30.0f);
        const float l2w = ui::button_width(ui::Button::l2, 30.0f);
        float rx = bar.x + bar.w - 22.0f;
        const float jump_w = fonts.regular.measure("Blättern", 18.0f);
        ui::text(list, fonts.regular, "Blättern", rx, base - 1.0f, 18.0f, kMuted,
                 gfx::Align::right);
        rx -= jump_w + 10.0f + r2w;
        ui::draw_button(list, fonts, glyphs, ui::Button::r2, rx, bar.y + bar.h * 0.5f, 30.0f);
        rx -= 6.0f + l2w;
        ui::draw_button(list, fonts, glyphs, ui::Button::l2, rx, bar.y + bar.h * 0.5f, 30.0f);
        rx -= 26.0f;
        ui::text(list, fonts.regular, right, rx, base - 1.0f, 18.0f, kMuted, gfx::Align::right);
        const float right_w = fonts.regular.measure(right, 18.0f);

        // left: the crumbs, dropped from the front with "…" when they do not fit
        std::vector<std::string> parts;
        if (folder_.path.empty())
            parts.push_back("Orte");
        else
        {
            parts.push_back("Orte");
            std::size_t from = 1;
            while (from < folder_.path.size())
            {
                const std::size_t to = folder_.path.find('/', from);
                const std::string part = folder_.path.substr(
                    from, to == std::string::npos ? std::string::npos : to - from);
                if (!part.empty())
                    parts.push_back(part);
                if (to == std::string::npos)
                    break;
                from = to + 1;
            }
        }
        const float left = bar.x + 58.0f;
        const float room = rx - right_w - 30.0f - left;
        const auto width_of = [&](std::size_t first)
        {
            float w = first > 0 ? fonts.regular.measure("…", 20.0f) + 30.0f : 0.0f;
            for (std::size_t i = first; i < parts.size(); ++i)
                w +=
                    fonts.semibold.measure(parts[i], 20.0f) + (i + 1 < parts.size() ? 30.0f : 0.0f);
            return w;
        };
        std::size_t first = 0;
        while (first + 1 < parts.size() && width_of(first) > room)
            ++first;
        float x = left;
        const auto chevron = [&](float at)
        {
            const float cy = bar.y + bar.h * 0.5f;
            list.line(at + 10.0f, cy - 6.0f, at + 16.0f, cy, 2.0f, kMuted);
            list.line(at + 16.0f, cy, at + 10.0f, cy + 6.0f, 2.0f, kMuted);
        };
        if (first > 0)
        {
            ui::text(list, fonts.regular, "…", x, base, 20.0f, kMuted);
            x += fonts.regular.measure("…", 20.0f);
            chevron(x);
            x += 30.0f;
        }
        for (std::size_t i = first; i < parts.size(); ++i)
        {
            const bool last = i + 1 == parts.size();
            const std::string shown =
                last ? ui::fit_label(paint, parts[i], 20.0f, std::max(room - (x - left), 40.0f))
                     : parts[i];
            ui::text(list, last ? fonts.semibold : fonts.regular, shown, x, base, 20.0f,
                     last ? kInk : kMuted);
            x += (last ? fonts.semibold : fonts.regular).measure(shown, 20.0f);
            if (!last)
            {
                chevron(x);
                x += 30.0f;
            }
        }
    }

    // A small framed note on the right card: a bold line and a quiet one.
    float note_card(ui::Canvas &canvas, ui::Painter &paint, const Rect &card, float y,
                    const std::string &title, const std::string &text, Color accent) const
    {
        gfx::DrawList &list = canvas.list;
        const Rect box{card.x + 30.0f, y, card.w - 60.0f, 92.0f};
        list.rounded_rect(box, 16.0f, accent.with_alpha(0.12f));
        list.rounded_rect({box.x, box.y + 14.0f, 4.0f, box.h - 28.0f}, 2.0f, accent);
        ui::text(list, canvas.fonts.semibold, ui::fit_label(paint, title, 22.0f, box.w - 48.0f),
                 box.x + 24.0f, box.y + 38.0f, 22.0f, kInk);
        ui::text(list, canvas.fonts.regular, ui::fit_label(paint, text, 18.0f, box.w - 48.0f),
                 box.x + 24.0f, box.y + 70.0f, 18.0f, kMuted);
        return box.y + box.h + 14.0f;
    }

    // L2 / R2: a page of rows up or down; held, it goes on after a moment, like the D-pad.
    bool jump(const InputFrame &input, ui::Feedback &feedback)
    {
        const bool down = input.is_held(Action::jump_next);
        const bool up = input.is_held(Action::jump_prev);
        if (!down && !up)
        {
            jump_since_ = -1.0f;
            return false;
        }
        const bool fresh =
            input.is_pressed(Action::jump_next) || input.is_pressed(Action::jump_prev);
        if (fresh)
        {
            jump_since_ = clock_;
            jump_next_ = clock_ + 0.35f;
        }
        else if (jump_since_ < 0.0f || clock_ < jump_next_)
            return true;
        else
            jump_next_ = clock_ + 0.09f;
        const int count = static_cast<int>(list_.items().size());
        const int from = list_.focus();
        const int to =
            std::clamp(from + (down ? kJumpRows : -kJumpRows), 0, std::max(count - 1, 0));
        if (to == from)
        {
            if (fresh)
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.4f);
            return true;
        }
        list_.set_focus(to, false);
        feedback.play(audio::Cue::focus);
        refresh_info();
        return true;
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

    ui::Theme theme_;
    ui::ListView list_;
    ui::DetailList info_;
    ui::Menu menu_;
    ui::Keyboard keys_;
    tween::Spring shown_;
    float clock_ = 0.0f;
    float jump_since_ = -1.0f; // L2 / R2 held since, -1 when neither
    float jump_next_ = 0.0f;
    std::string key_;
    std::string info_key_ = "\x01";
    bool entered_ = false;

    std::vector<FsPlace> places_;
    FsList folder_;
    std::uint64_t list_seq_ = 0;
    std::string shown_path_ = "\x01";
    std::vector<FsEntry> entries_; // folder_.entries in the chosen order
    FsJob job_;
    std::map<std::string, FsSize> sizes_;
    int tz_ = 0;
    int sort_ = 0;                 // name, size, date
    std::set<std::string> picked_; // names in this folder

    bool clip_move_ = false;
    std::string clip_from_;
    std::vector<std::string> clip_paths_;
    std::string came_from_;            // the folder just left with O, until its row has the focus
    std::vector<std::string> history_; // the folders opened from, for O ("" = Orte)

    Dialog dialog_ = kNone;
    FsEntry menu_entry_;
    int name_mode_ = kNewFolder;
    std::string name_;
    std::string name_error_;
    FsView view_;
    std::uint64_t view_seq_ = 0;
    std::vector<std::string> view_lines_;
    int view_top_ = 0;
};

} // namespace

std::unique_ptr<Page> make_files_page()
{
    return std::make_unique<FilesPage>();
}

} // namespace hui::ps5cc
