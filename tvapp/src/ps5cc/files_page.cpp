// ps5cc - The Dateien area: the console's folders and drives, copying, moving, deleting, renaming, a new folder.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's file manager (src/filemgr.c): looking is allowed everywhere, changing only below the drives and
// /data, and the console says which with writable. The X button opens a folder or shows a text file; Square opens what
// an entry allows (pick, copy, cut, rename, delete, measure); Triangle the rest (paste here, new folder, pick all or
// none, the order, cancel the job). Names are typed on the kit's keyboard. Delete fires at once, as everywhere in the
// app. Uploading from a computer and downloading to one stay in the web app.

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

namespace
{

using gfx::Color;
using gfx::Rect;

const Rect kPanel{200.0f, 92.0f, 1520.0f, 864.0f};

constexpr int kPlaceTag = 100;  // + index into the places
constexpr int kUpTag = 99;      // ".." up one folder
constexpr int kEntryTag = 1000; // + index into entries_ (sorted)

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
    de.add_row({wide(ui::KeyKind::shift, 2), wide(ui::KeyKind::layout, 1), wide(ui::KeyKind::space, 3),
                wide(ui::KeyKind::backspace, 2), wide(ui::KeyKind::done, 2)});
    return {ui::KeyboardLayout::letters(), de, ui::KeyboardLayout::symbols()};
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
        list_.set_bounds({kLeftX, kTop + 62.0f, kLeftW, kBottom - kTop - 62.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 110.0f, kRightW - 60.0f, 220.0f});

        menu_.style.theme = theme;
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
                view_top_ = std::min(view_top_ + 1, std::max(0, static_cast<int>(view_lines_.size()) - page));
            else if (input.nav == Direction::up)
                view_top_ = std::max(0, view_top_ - 1);
            return;
        }
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
        // where we are, above the list
        const std::string where = folder_.path.empty() ? "Orte" : folder_.path;
        ui::text(list, fonts.semibold, ui::fit_label(paint, where, 24.0f, kLeftW - 20.0f), kLeftX + 10.0f, kTop + 40.0f,
                 24.0f, kInk);
        list_.draw(canvas);

        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        paint.panel(card);
        const FsEntry *e = focused_entry();
        const FsPlace *pl = focused_place();
        const std::string title = e != nullptr ? e->name : pl != nullptr ? pl->label : where;
        ui::text(list, fonts.display, ui::fit_label(paint, title, 30.0f, card.w - 60.0f), card.x + 30.0f, card.y + 62.0f,
                 30.0f, kInk);
        info_.draw(canvas);

        float y = card.y + 360.0f;
        if (!folder_.path.empty() && !folder_.writable)
            y = paragraph(canvas, "Nur ansehen: Hier darf nichts geändert werden. Ändern geht auf den Laufwerken und in /data.",
                          card.x + 30.0f, y, card.w - 60.0f, kWarning, 3) +
                10.0f;
        if (!clip_paths_.empty())
            y = paragraph(canvas,
                          std::string(clip_move_ ? "Ausgeschnitten: " : "Kopiert: ") + std::to_string(clip_paths_.size()) +
                              (clip_paths_.size() == 1 ? " Eintrag aus " : " Einträge aus ") + clip_from_ +
                              ". Mehr → Hier einfügen.",
                          card.x + 30.0f, y, card.w - 60.0f, kInk, 3) +
                10.0f;
        if (!picked_.empty())
            y = paragraph(canvas, std::to_string(picked_.size()) + " ausgewählt", card.x + 30.0f, y, card.w - 60.0f,
                          kBlue, 1) +
                10.0f;

        // the job at the bottom
        const float jy = kBottom - 170.0f;
        if (job_.active || (job_.finished_ago_s >= 0 && job_.finished_ago_s < 60 && !job_.kind.empty()))
        {
            list.line(card.x + 30.0f, jy, card.x + card.w - 30.0f, jy, 1.0f, Color::rgb(0x3880ff, 0.18f));
            const char *what = job_.kind == "move" ? "Verschieben" : job_.kind == "delete" ? "Löschen" : "Kopieren";
            if (job_.active)
            {
                ui::text(list, fonts.semibold, std::string(what) + " · " + std::to_string(job_.percent) + " %",
                         card.x + 30.0f, jy + 40.0f, 22.0f, kInk);
                const Rect bar{card.x + 30.0f, jy + 58.0f, card.w - 60.0f, 12.0f};
                list.rounded_rect(bar, 6.0f, Color::rgb(0x3880ff, 0.18f));
                list.rounded_rect(
                    {bar.x, bar.y, bar.w * static_cast<float>(std::clamp(job_.percent, 0, 100)) / 100.0f, bar.h}, 6.0f,
                    kBlue);
                const std::string amount = job_.bytes_total > 0 ? format_bytes(job_.bytes_done) + " von " +
                                                                      format_bytes(job_.bytes_total)
                                                                : std::to_string(job_.files_done) + " von " +
                                                                      std::to_string(job_.files_total) + " Dateien";
                ui::text(list, fonts.regular, ui::fit_label(paint, amount + " · " + job_.current, 18.0f, card.w - 60.0f),
                         card.x + 30.0f, jy + 100.0f, 18.0f, kMuted);
            }
            else
            {
                const bool ok = job_.state == "done";
                ui::text(list, fonts.semibold,
                         std::string(what) + (ok ? " fertig" : job_.state == "cancelled" ? " abgebrochen" : " fehlgeschlagen"),
                         card.x + 30.0f, jy + 40.0f, 22.0f, ok ? kGood : kWarning);
                paragraph(canvas, !job_.error.empty() ? job_.error : job_.note, card.x + 30.0f, jy + 76.0f,
                          card.w - 60.0f, kMuted, 3);
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
        paint.panel(panel);
        ui::HintLayout layout;
        layout.size = 34.0f;
        layout.text_size = 21.0f;
        layout.cy = panel.y + panel.h - 42.0f;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        if (dialog_ == kName)
        {
            ui::text(list, fonts.display, name_mode_ == kRename ? "Umbenennen" : "Neuer Ordner", panel.x + 40.0f,
                     panel.y + 66.0f, 36.0f, kInk);
            ui::text(list, fonts.regular, ui::fit_label(paint, "in " + folder_.path, 21.0f, panel.w - 80.0f), panel.x + 40.0f,
                     panel.y + 104.0f, 21.0f, kMuted);
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
                ui::text(list, fonts.regular, name_error_, field.x, field.y + field.h + 36.0f, 21.0f, kCritical);
            keys_.draw(canvas);
            const ui::Hint hints[] = {{ui::Button::square, "Zeichen löschen"},
                                      {ui::Button::triangle, "Leerzeichen"},
                                      {ui::Button::circle, "Abbrechen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 3, panel.x + 40.0f, false, layout);
        }
        else
        {
            ui::text(list, fonts.display, ui::fit_label(paint, base_of(view_.path), 36.0f, panel.w - 80.0f),
                     panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
            std::string about = view_.path;
            if (view_.truncated)
                about += " · nur der Anfang";
            ui::text(list, fonts.regular, ui::fit_label(paint, about, 21.0f, panel.w - 80.0f), panel.x + 40.0f,
                     panel.y + 104.0f, 21.0f, kMuted);
            if (view_.seq == 0 || (view_.path.empty() && view_.error.empty()))
                ui::text(list, fonts.semibold, "Wird geladen …", panel.x + panel.w * 0.5f, panel.y + 420.0f, 30.0f, kInk,
                         gfx::Align::center);
            else if (!view_.error.empty())
                paragraph(canvas, view_.error, panel.x + 40.0f, panel.y + 200.0f, panel.w - 80.0f, kCritical, 4);
            else
            {
                float y = panel.y + 160.0f;
                for (int i = view_top_; i < static_cast<int>(view_lines_.size()) && y < panel.y + panel.h - 90.0f; ++i)
                {
                    ui::text(list, fonts.regular,
                             ui::fit_label(paint, view_lines_[static_cast<std::size_t>(i)], 20.0f, panel.w - 80.0f),
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
        return i >= 0 && i < static_cast<int>(entries_.size()) ? &entries_[static_cast<std::size_t>(i)] : nullptr;
    }

    const FsPlace *focused_place() const
    {
        const int i = focused_tag() - kPlaceTag;
        return folder_.path.empty() && i >= 0 && i < static_cast<int>(places_.size()) ? &places_[static_cast<std::size_t>(i)]
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
        source().files_open(path);
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
            open(folder_.parent, feedback);
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

    static ui::MenuItem item(const std::string &label, int tag, bool disabled = false, bool danger = false)
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
        const bool all_writable =
            std::all_of(t.begin(), t.end(), [](const FsEntry *x) { return x->writable && pickable(*x); });
        const std::string count = t.size() > 1 ? " (" + std::to_string(t.size()) + ")" : "";
        std::vector<ui::MenuItem> items;
        if (pickable(*e))
            items.push_back(item(picked_.count(e->name) ? "Abwählen" : "Auswählen", kPick));
        if (e->type == "file")
            items.push_back(item("Ansehen", kView));
        if (e->type == "dir")
            items.push_back(item("Größe messen", kMeasure));
        items.push_back(item("Kopieren" + count, kCopy, !std::all_of(t.begin(), t.end(), [](const FsEntry *x) { return pickable(*x); })));
        items.push_back(item("Ausschneiden" + count, kCut, !all_writable));
        items.push_back(item("Umbenennen", kRename, !e->writable || !pickable(*e)));
        items.push_back(item("Löschen" + count, kDelete, !all_writable || job_.active, true));
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void open_page_menu(ui::Feedback &feedback)
    {
        const bool inside = !folder_.path.empty();
        const char *const orders[] = {"Name", "Größe", "Datum"};
        std::vector<ui::MenuItem> items;
        if (!clip_paths_.empty())
        {
            items.push_back(item("Hier einfügen (" + std::to_string(clip_paths_.size()) + ")", kPaste,
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
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
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
                                "{\"paths\":" + paths_json(clip_paths_) + ",\"dest\":" + json_text(folder_.path) + "}");
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
        if (name_.empty() || name_ == "." || name_ == ".." || name_.find('/') != std::string::npos ||
            name_.find('\\') != std::string::npos)
        {
            name_error_ = "Ein Name darf nicht leer sein und kein / oder \\ enthalten.";
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        if (name_mode_ == kRename)
            source().files_post("rename", "{\"path\":" + json_text(join(folder_.path, menu_entry_.name)) + ",\"name\":" +
                                              json_text(name_) + "}");
        else
            source().files_post("mkdir", "{\"path\":" + json_text(folder_.path) + ",\"name\":" + json_text(name_) + "}");
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

    // Folders first, then by the chosen order (size and date: the biggest and newest first), as the web page.
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
        up.title = "..";
        up.subtitle = folder_.parent.empty() ? "zurück zu den Orten" : "hoch zu " + folder_.parent;
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
            std::string sub = std::string(folder ? "Ordner · " : "") + play_date(e.mtime, tz_) + ", " +
                              play_clock(e.mtime, tz_) + " Uhr";
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
                item.value = s->second.failed ? "?" : format_bytes(s->second.bytes) + (s->second.partial ? "+" : "");
            if (picked_.count(e.name))
                item.badge = "AUSGEWÄHLT";
            else if (!clip_paths_.empty() && std::find(clip_paths_.begin(), clip_paths_.end(), full) != clip_paths_.end())
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
            n.title = "Nur die ersten " + std::to_string(entries_.size()) + " von " + std::to_string(folder_.total);
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
                rows.push_back({"Größe", (s->second.partial ? "mindestens " : "") + format_bytes(s->second.bytes) + " · " +
                                             std::to_string(s->second.files) + " Dateien"});
            rows.push_back({"Geändert", play_date(e->mtime, tz_) + ", " + play_clock(e->mtime, tz_) + " Uhr"});
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
                rows.push_back({"Frei", format_bytes(static_cast<std::uint64_t>(folder_.free_bytes))});
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

    float paragraph(ui::Canvas &canvas, const std::string &text, float x, float y, float width, Color ink,
                    int max_lines) const
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
    int sort_ = 0; // name, size, date
    std::set<std::string> picked_; // names in this folder

    bool clip_move_ = false;
    std::string clip_from_;
    std::vector<std::string> clip_paths_;

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
