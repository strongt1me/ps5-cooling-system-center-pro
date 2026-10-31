// ps5cc - The Payloads page: own payloads to start, copy and delete, the running ones, the payload
// store, and payload profiles (sequences with pauses, a startup profile).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/covers.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/payload_match.hpp"
#include "ps5cc/payload_tile.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Rect;

enum View
{
    kFiles = 0,
    kRunning = 1,
    kStore = 2,
    kProfiles = 3,
    kViews = 4,
};

constexpr float kListTop = kTop + 10.0f;

// Tags of the profile view and the editor that are not an index into a list.
constexpr int kNewProfile = -2;
constexpr int kEditAddPayload = -3;
constexpr int kEditAddPause = -4;
constexpr int kEditName = -5;
constexpr int kEditSave = -6;
constexpr int kEditDiscard = -7;

// Menu tags.
enum MenuTag
{
    kMenuRun = 1,
    kMenuEdit,
    kMenuStartup,
    kMenuDelete,
    kMenuUp,
    kMenuDown,
    kMenuRemove,
    kMenuPause1,
    kMenuPause2,
    kMenuPause5,
    kMenuPause10,
    kMenuFileBase = 100, // + index into the internal payload files
};

const Rect kDialog{360.0f, 150.0f, 1200.0f, 760.0f};

// "!2000" -> 2000, -1 when the entry is a file.
int pause_ms(const std::string &item)
{
    if (item.size() < 2 || item[0] != '!')
        return -1;
    return std::atoi(item.c_str() + 1);
}

std::string pause_text(int ms)
{
    if (ms % 1000 == 0)
        return "Pause " + std::to_string(ms / 1000) + " s";
    char text[32];
    std::snprintf(text, sizeof(text), "Pause %.1f s", static_cast<double>(ms) / 1000.0);
    return text;
}

std::string item_text(const std::string &item)
{
    const int ms = pause_ms(item);
    return ms >= 0 ? pause_text(ms) : item;
}

// "a.elf → Pause 2 s → b.elf"
std::string profile_summary(const PayloadProfile &p)
{
    if (p.items.empty())
        return "leer";
    std::string out;
    for (const std::string &item : p.items)
    {
        if (!out.empty())
            out += " → ";
        out += item_text(item);
    }
    return out;
}

// An id from the name, as the web page makes it (a-z0-9-_), unique among the others.
std::string make_id(const std::string &name, const std::vector<PayloadProfile> &taken)
{
    std::string base;
    for (const char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            base += static_cast<char>(std::tolower(u));
        else if (!base.empty() && base.back() != '-')
            base += '-';
        if (base.size() >= 24)
            break;
    }
    while (!base.empty() && base.back() == '-')
        base.pop_back();
    if (base.empty())
        base = "profil";
    std::string id = base;
    for (int n = 2;; ++n)
    {
        const bool used = std::any_of(taken.begin(), taken.end(),
                                      [&](const PayloadProfile &p) { return p.id == id; });
        if (!used)
            return id;
        id = base + "-" + std::to_string(n);
    }
}

class PayloadsPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        style_subtabs(views_, theme, 4);
        views_.set_tabs({{"Meine Payloads"}, {"Bereits geladen"}, {"Payload-Store"}, {"Profile"}});
        views_.set_active(kFiles, true);

        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.header_rule = true; // title and line, as the forms (user, 29.10.2026)
        list_.style.row_height = 76.0f;
        list_.style.title_size = 24.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 20.0f;
        list_.style.header_size = 18.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.badge_column = true;
        list_.style.value_width = 110.0f; // the badges line up
        list_.style.leading_width = 72.0f;
        list_.leading = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item,
                               int index, float focus)
        {
            (void)index;
            (void)focus;
            PayloadTile tile;
            if (!tile_of(item, tile))
                return;
            const float side = std::min(52.0f, row.h - 20.0f);
            const Rect box{row.x + 16.0f, row.y + (row.h - side) * 0.5f, side, side};
            draw_icon(canvas, box, item, tile, item.disabled ? 0.45f : 1.0f);
            // every payload carries a dot on its picture's corner: softly glowing green when it is
            // loaded, a dark one that does not glow when it is not (user, 29.10.2026; Meine
            // Payloads, Bereits geladen, Payload-Store) a store entry not yet downloaded has none:
            // there is nothing on the console that could run
            const bool none = view_ == kStore && item.tag >= 0 &&
                              static_cast<std::size_t>(item.tag) < store_.size() &&
                              store_[static_cast<std::size_t>(item.tag)].installed_file.empty();
            if (item.tag >= 0 && !none)
            {
                const float cx = box.x + box.w - 3.0f, cy = box.y + box.h - 3.0f;
                canvas.list.circle(cx, cy, 8.5f, kNavy);
                if (loaded(item))
                {
                    const float pulse = 0.5f + 0.5f * std::sin(canvas.time * 2.6f +
                                                               static_cast<float>(item.tag) * 0.7f);
                    canvas.list.circle(cx, cy, 13.0f + 3.0f * pulse,
                                       kGood.with_alpha(0.18f + 0.22f * pulse));
                    canvas.list.circle(cx, cy, 8.5f, kNavy);
                    canvas.list.circle(cx, cy, 6.5f, gfx::Color::rgb(0x2fd44a));
                }
                else
                    canvas.list.circle(cx, cy, 6.0f, gfx::Color::rgb(0x4a5677));
            }
        };
        list_.set_bounds({kLeftX, kListTop, kLeftW, kBottom - kListTop});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 42.0f;
        info_.style.max_lines = 1;

        bar_.style.theme = theme;
        bar_.style.height = 14.0f;
        bar_.style.sheen = true;
        bar_.style.text_size = 20.0f;
        bar_.set_bounds({kRightX + 30.0f, kBottom - 150.0f, kRightW - 60.0f, 48.0f});

        // Under at most two lines of title and four of text.
        info_.set_bounds(
            {kRightX + 30.0f, kTop + 300.0f, kRightW - 60.0f, kBottom - kTop - 480.0f});

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 560.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});

        keys_.style.theme = theme;
        keys_.style.bindings = ui::KeyboardBindings::standard();
        keys_.style.space_label = "Leer";
        keys_.style.done_label = "Fertig";
        keys_.style.max_length = 60;
        keys_.set_layouts(german_layouts());
        keys_.set_bounds({kDialog.x + 60.0f, kDialog.y + 250.0f, kDialog.w - 120.0f, 400.0f});
        keys_.on_text = [this](std::string_view utf8)
        {
            if (name_.size() + utf8.size() <= 60)
                name_.append(utf8);
        };
        keys_.on_backspace = [this]
        {
            while (!name_.empty() && (static_cast<unsigned char>(name_.back()) & 0xC0) == 0x80)
                name_.pop_back();
            if (!name_.empty())
                name_.pop_back();
        };
    }

    bool modal() const override
    {
        return menu_.is_open() || naming_ || editing_;
    }

    void absorb(const Snapshot &data) override
    {
        files_ = data.payload_files;
        files_loaded_ = data.payload_files_loaded;
        running_ = data.running;
        store_ = data.store;
        store_loaded_ = data.store_loaded;
        store_fetching_ = data.store_fetching;
        store_error_ = data.store_error;
        job_ = data.store_job;
        profiles_ = data.payload_profiles;
        startup_ = data.profile_startup;
        profiles_loaded_ = data.profiles_loaded;
        run_ = data.profile_run;
        rebuild(false);
        const bool known = job_.total > 0.0;
        bar_.style.mode = job_.state == "running" && !known ? ui::ProgressMode::indeterminate
                                                            : ui::ProgressMode::determinate;
        bar_.style.percent = known;
        bar_.style.status = job_.state == "failed"    ? ui::Status::danger
                            : job_.state == "running" ? ui::Status::accent
                                                      : ui::Status::success;
        bar_.label =
            job_.state == "running" ? "„" + job_.name + "“ wird geladen"
            : job_.state == "failed"
                ? "„" + job_.name + "“: " + (job_.error.empty() ? "gescheitert" : job_.error)
            : job_.state == "done" ? "„" + job_.name + "“ geladen"
                                   : "";
        bar_.set_value(known ? static_cast<float>(std::clamp(job_.done / job_.total, 0.0, 1.0))
                             : 0.0f);
    }

    void enter() override
    {
        list_.enter();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (menu_.is_open())
        {
            if (menu_.handle(input, feedback) == ui::Event::activated)
                run_menu(menu_.items()[static_cast<std::size_t>(menu_.focus())].tag, feedback);
            return;
        }
        if (naming_)
        {
            keys_.set_length(static_cast<int>(name_.size()));
            const ui::Event event = keys_.handle(input, feedback);
            if (event == ui::Event::cancelled)
            {
                naming_ = false;
                feedback.play(audio::Cue::back);
            }
            else if (event == ui::Event::activated)
            {
                std::string trimmed = name_;
                while (!trimmed.empty() && trimmed.back() == ' ')
                    trimmed.pop_back();
                if (trimmed.empty())
                {
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                    return;
                }
                draft_.name = trimmed;
                naming_ = false;
                feedback.play(audio::Cue::select);
                rebuild(true);
            }
            return;
        }
        if (editing_)
        {
            handle_editor(input, feedback);
            return;
        }
        if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            focus_[static_cast<std::size_t>(view_)] = list_.focus();
            if (views_.step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) ==
                ui::Event::changed)
            {
                view_ = views_.active();
                rebuild(true);
                list_.enter();
            }
            return;
        }
        const ui::Event event = list_.handle(input, feedback);
        const ListItemRef item = focused();
        if (event == ui::Event::activated)
        {
            activate(item, feedback);
            return;
        }
        if (view_ == kProfiles && input.is_pressed(Action::north))
        {
            const PayloadProfile *prof = focused_profile();
            if (prof == nullptr)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            menu_profile_ = prof->id;
            const bool runs = run_.running && run_.id == prof->id;
            std::vector<ui::MenuItem> items;
            items.push_back(menu_item(runs ? "Anhalten" : "Ausführen", kMenuRun));
            items.push_back(menu_item("Bearbeiten …", kMenuEdit));
            items.push_back(menu_item(startup_ == prof->id ? "Nicht mehr beim Start ausführen"
                                                           : "Beim Start der App ausführen",
                                      kMenuStartup));
            ui::MenuItem line;
            line.separator = true;
            items.push_back(line);
            ui::MenuItem del = menu_item("Profil löschen", kMenuDelete);
            del.danger = true;
            items.push_back(del);
            menu_.set_items(items);
            menu_.open({kRightX + 360.0f, kBottom - 80.0f, 140.0f, 44.0f}, feedback);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            if (view_ == kStore)
            {
                source().store_action("refresh", {});
                return;
            }
            if (view_ == kFiles && item.file != nullptr)
            {
                // at once, nothing asked twice: an internal file goes, a USB file is copied in
                // (never over one of the same name)
                if (item.file->source == "internal")
                {
                    feedback.play(audio::Cue::select);
                    source().delete_payload_file(*item.file);
                }
                else if (internal_has(item.file->name))
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                else
                {
                    feedback.play(audio::Cue::select);
                    source().copy_payload_file(*item.file);
                }
                return;
            }
            if (view_ == kProfiles)
            {
                if (const PayloadProfile *prof = focused_profile())
                {
                    feedback.play(audio::Cue::select);
                    open_editor(*prof);
                }
                return;
            }
            if (view_ == kRunning && item.running != nullptr)
            {
                if (item.running->self)
                {
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                    return;
                }
                // □ ends it at once – nothing is asked twice (user, 27.10.2026).
                feedback.play(audio::Cue::select);
                source().kill_payload(item.running->pid);
            }
        }
    }

    void update(float dt) override
    {
        views_.update(dt);
        list_.update(dt);
        menu_.update(dt);
        clock_ += dt;
        if (naming_)
            keys_.update(dt);
        shown_.target = naming_ ? 1.0f : 0.0f;
        shown_.update(dt, 14.0f);
        if (list_.focus() != info_for_)
            refresh_info();
        info_.update(dt);
        bar_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        views_.draw(canvas);
        draw_subtab_glyphs(canvas, views_);
        list_.draw(canvas);

        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);
        const ListItemRef item = focused();
        std::string title;
        std::string text;
        const PayloadProfile *prof = view_ == kProfiles && !editing_ ? focused_profile() : nullptr;
        if (editing_)
        {
            title = draft_.name.empty() ? "Neues Profil" : draft_.name;
            text = "Die Einträge laufen der Reihe nach, Pausen warten so lange. Nur Payloads aus "
                   "dem internen Ordner. Die Dreieck-Taste ordnet oder entfernt einen Eintrag; "
                   "Speichern schreibt "
                   "alle Profile.";
        }
        else if (prof != nullptr)
        {
            title = prof->name;
            const bool runs = run_.running && run_.id == prof->id;
            text = runs ? "Läuft: Schritt " + std::to_string(run_.step) + " von " +
                              std::to_string(run_.total) + "."
                        : profile_summary(*prof);
            if (startup_ == prof->id)
                text += " Wird kurz nach dem Start der App ausgeführt.";
        }
        else if (view_ == kProfiles)
        {
            title = "Payload-Profile";
            text = "Mehrere Payloads nacheinander starten, mit Pausen dazwischen. Ein Profil kann "
                   "beim Start der App von selbst laufen. Laden und Sichern als Datei geht in der "
                   "Browser-App.";
        }
        else if (item.file != nullptr)
        {
            title = item.file->name;
            text = item.file->valid ? "Wird beim Starten an den ELF-Loader (Port 9021) gesendet."
                                    : item.file->why;
            if (item.file->source == "internal")
                text += " Die Quadrat-Taste löscht die Datei sofort.";
            else if (internal_has(item.file->name))
                text += " Im internen Ordner gibt es sie schon.";
            else
                text += " Die Quadrat-Taste kopiert sie in den internen Ordner.";
        }
        else if (item.running != nullptr)
        {
            title = item.running->name;
            text = item.running->self ? "Diese App selbst; sie lässt sich hier nicht beenden."
                                      : "Läuft gerade auf der Konsole.";
        }
        else if (item.store != nullptr)
        {
            title = item.store->name;
            text = item.store->description;
        }
        else
        {
            title = view_ == kStore     ? "Payload-Store"
                    : view_ == kRunning ? "Bereits geladene Payloads"
                                        : "Meine Payloads";
            text = view_ == kFiles
                       ? "Payloads aus /data/PS5-Cooling-Center/payloads und von USB-Sticks."
                   : view_ == kStore ? "Liste: itsPLK ps5-payloads-mirror."
                                     : "";
        }
        float title_w = card.w - 60.0f;
        PayloadTile tile;
        const bool has_tile = list_.focus() >= 0 &&
                              list_.focus() < static_cast<int>(list_.items().size()) &&
                              tile_of(list_.items()[static_cast<std::size_t>(list_.focus())], tile);
        if (has_tile)
        {
            draw_icon(canvas, {card.x + card.w - 30.0f - 96.0f, card.y + 30.0f, 96.0f, 96.0f},
                      list_.items()[static_cast<std::size_t>(list_.focus())], tile, 1.0f);
            title_w -= 116.0f;
        }
        float y = card.y + 64.0f;
        int lines = 0;
        for (const std::string &line : fonts.display.font->wrap(title, 32.0f, title_w))
        {
            if (++lines > 2)
                break;
            ui::text(list, fonts.display, line, card.x + 30.0f, y, 32.0f, kInk);
            y += 42.0f;
        }
        y += 4.0f;
        lines = 0;
        // Beside the tile while the text is still level with it.
        if (has_tile && y < card.y + 30.0f + 96.0f + 10.0f)
            y = card.y + 30.0f + 96.0f + 34.0f;
        for (const std::string &line : fonts.regular.font->wrap(text, 21.0f, card.w - 60.0f))
        {
            if (++lines > 4)
                break;
            ui::text(list, fonts.regular, line, card.x + 30.0f, y, 21.0f, kMuted);
            y += 29.0f;
        }
        info_.draw(canvas);
        if (view_ == kStore && job_.state != "idle")
            bar_.draw(canvas);
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
        const Rect panel{kDialog.x, kDialog.y + 24.0f * (1.0f - amount), kDialog.w, kDialog.h};
        ui::Painter paint(list, fonts, theme_);
        solid_floor(list, panel, theme_.radius_card);
        paint.panel(panel);
        ui::text(list, fonts.display, "Name des Profils", panel.x + 40.0f, panel.y + 66.0f, 36.0f,
                 kInk);
        const Rect field{panel.x + 60.0f, panel.y + 120.0f, panel.w - 120.0f, 84.0f};
        list.rounded_rect(field, 14.0f, gfx::Color::rgb(0x3880ff, 0.12f));
        const std::string shown = ui::fit_label(paint, name_, 32.0f, field.w - 60.0f);
        ui::text(list, fonts.semibold, shown, field.x + 28.0f, field.y + 54.0f, 32.0f, kInk);
        if (std::fmod(clock_, 1.0f) < 0.6f)
        {
            const float cx = field.x + 30.0f + fonts.semibold.font->measure(shown, 32.0f);
            list.rounded_rect({cx, field.y + 22.0f, 3.0f, 40.0f}, 1.5f, kBlue);
        }
        keys_.draw(canvas);
        list.pop_opacity();
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        if (editing_)
        {
            const int tag = focused_tag();
            if (tag >= 0)
                out[n++] = {ui::Button::triangle, "Ordnen, entfernen"};
            else if (tag == kEditSave || tag == kEditDiscard || tag == kEditName ||
                     tag == kEditAddPayload || tag == kEditAddPause)
                out[n++] = {ui::Button::cross, "Auswählen"};
            out[n++] = {ui::Button::circle, "Verwerfen"};
            return n;
        }
        out[n++] = {ui::Button::l2, "Ansicht", ui::Button::r2};
        if (view_ == kProfiles)
        {
            const PayloadProfile *prof = focused_profile();
            if (prof != nullptr)
            {
                out[n++] = {ui::Button::cross,
                            run_.running && run_.id == prof->id ? "Anhalten" : "Ausführen"};
                out[n++] = {ui::Button::square, "Bearbeiten"};
            }
            else if (focused_tag() == kNewProfile)
                out[n++] = {ui::Button::cross, "Neues Profil"};
            return n;
        }
        const ListItemRef item = focused();
        if (view_ == kFiles && item.file != nullptr)
        {
            if (item.file->valid)
                out[n++] = {ui::Button::cross, "Starten"};
            if (item.file->source == "internal")
                out[n++] = {ui::Button::square, "Löschen"};
            else if (!internal_has(item.file->name))
                out[n++] = {ui::Button::square, "Intern kopieren"};
            return n;
        }
        if (view_ == kFiles && item.file != nullptr && item.file->valid)
            out[n++] = {ui::Button::cross, "Starten"};
        if (view_ == kRunning && item.running != nullptr && !item.running->self)
            out[n++] = {ui::Button::square, "Beenden"};
        if (view_ == kStore)
        {
            if (item.store != nullptr)
                out[n++] = {ui::Button::cross, store_verb(*item.store)};
            out[n++] = {ui::Button::square, "Liste neu laden"};
        }
        return n;
    }

  private:
    // What the focused row stands for: at most one of the three.
    struct ListItemRef
    {
        const PayloadFile *file = nullptr;
        const RunningPayload *running = nullptr;
        const StoreItem *store = nullptr;
    };

    // The GitHub account whose picture the console app has for this row: a store item's own, or for
    // a file or a running payload the one of the store item with the same name stem. "" when there
    // is none.
    std::string owner_of(const ui::ListItem &item) const
    {
        if (item.header || item.tag < 0)
            return {};
        const std::size_t at = static_cast<std::size_t>(item.tag);
        if (view_ == kStore)
            return at < store_.size() && store_[at].avatar ? store_[at].owner : std::string();
        std::string stem;
        if (view_ == kFiles && at < files_.size())
            stem = payload_stem(files_[at].name);
        else if (view_ == kRunning && at < running_.size())
            stem = payload_stem(running_[at].name);
        if (stem.empty())
            return {};
        for (const StoreItem &s : store_)
            if (s.avatar && !s.owner.empty() &&
                (payload_stem(s.filename) == stem || payload_stem(s.name) == stem))
                return s.owner;
        return {};
    }

    // The developer's picture once it is loaded (faded in over the tile), the coloured tile until
    // then or without one.
    void draw_icon(ui::Canvas &canvas, const Rect &box, const ui::ListItem &item,
                   const PayloadTile &tile, float alpha) const
    {
        const std::string owner = owner_of(item);
        const CoverShown pic =
            owner.empty() ? CoverShown{} : covers().texture_of(avatar_key(owner));
        const float shown = pic.texture == 0 ? 0.0f : pic.alpha;
        if (shown < 1.0f)
            draw_payload_tile(canvas, box, tile, alpha);
        if (shown > 0.0f)
            canvas.list.image(pic.texture, box, gfx::kFullUv,
                              gfx::Color{1.0f, 1.0f, 1.0f, shown * alpha}, box.w * 0.22f);
    }

    // The row's payload as a tile; false for section titles and notes.
    bool tile_of(const ui::ListItem &item, PayloadTile &out) const
    {
        if (item.header || item.tag < 0)
            return false;
        const std::size_t at = static_cast<std::size_t>(item.tag);
        if (view_ == kFiles && at < files_.size())
            out = payload_tile(files_[at].name);
        else if (view_ == kRunning && at < running_.size())
            out = payload_tile(running_[at].name);
        else if (view_ == kStore && at < store_.size())
            out = payload_tile(store_[at].filename.empty() ? store_[at].name : store_[at].filename,
                               store_[at].category);
        else
            return false;
        return true;
    }

    static const char *store_verb(const StoreItem &s)
    {
        if (s.installed_version.empty())
            return "Laden";
        return s.update ? "Aktualisieren" : "Starten";
    }

    ListItemRef focused() const
    {
        ListItemRef ref;
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return ref;
        const ui::ListItem &item = list_.items()[static_cast<std::size_t>(i)];
        if (item.header || item.tag < 0)
            return ref;
        const std::size_t at = static_cast<std::size_t>(item.tag);
        if (view_ == kFiles && at < files_.size())
            ref.file = &files_[at];
        else if (view_ == kRunning && at < running_.size())
            ref.running = &running_[at];
        else if (view_ == kStore && at < store_.size())
            ref.store = &store_[at];
        return ref;
    }

    void activate(const ListItemRef &item, ui::Feedback &feedback)
    {
        if (view_ == kProfiles)
        {
            if (focused_tag() == kNewProfile)
            {
                if (profiles_.size() >= 24)
                {
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                    return;
                }
                PayloadProfile fresh;
                fresh.name = "Neues Profil";
                open_editor(fresh);
                naming_ = true;
                name_ = fresh.name;
                keys_.set_layout(0);
                keys_.set_length(static_cast<int>(name_.size()));
                keys_.set_active(true);
                keys_.enter();
                return;
            }
            if (const PayloadProfile *prof = focused_profile())
            {
                feedback.play(audio::Cue::select);
                if (run_.running && run_.id == prof->id)
                    source().stop_payload_profile();
                else if (run_.running)
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                else
                    source().run_payload_profile(prof->id);
                return;
            }
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        if (item.file != nullptr && item.file->valid)
        {
            source().start_payload(*item.file);
            return;
        }
        if (item.store != nullptr)
        {
            if (!item.store->installed_version.empty() && !item.store->update)
                source().store_action("start", item.store->id);
            else if (job_.state == "running")
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            else
                source().store_action("install", item.store->id);
            return;
        }
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    }

    static ui::ListItem header(std::string title)
    {
        ui::ListItem h;
        h.title = std::move(title);
        h.header = true;
        h.tag = -1;
        return h;
    }

    static ui::ListItem note(std::string title, std::string subtitle)
    {
        ui::ListItem n;
        n.title = std::move(title);
        n.subtitle = std::move(subtitle);
        n.disabled = true;
        n.tag = -1;
        return n;
    }

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        if (editing_)
        {
            out.push_back(header("EINTRÄGE (" + std::to_string(draft_.items.size()) + " von 64)"));
            for (std::size_t i = 0; i < draft_.items.size(); ++i)
            {
                ui::ListItem row;
                row.title = std::to_string(i + 1) + ". " + item_text(draft_.items[i]);
                const int ms = pause_ms(draft_.items[i]);
                row.subtitle = ms >= 0 ? "wartet, bevor der nächste Eintrag startet"
                               : internal_has(draft_.items[i]) ? "aus dem internen Ordner"
                                                               : "fehlt im internen Ordner";
                row.tag = static_cast<int>(i);
                out.push_back(row);
            }
            if (draft_.items.empty())
                out.push_back(note("Noch keine Einträge", "Füge unten Payloads und Pausen hinzu."));
            out.push_back(header("BEARBEITEN"));
            const auto action = [&](const char *title, const char *sub, int tag)
            {
                ui::ListItem row;
                row.title = title;
                row.subtitle = sub;
                row.tag = tag;
                out.push_back(row);
            };
            action("Payload hinzufügen …", "aus dem internen Ordner", kEditAddPayload);
            action("Pause hinzufügen", "2 Sekunden; die Dreieck-Taste ändert die Dauer",
                   kEditAddPause);
            action("Name ändern …", "", kEditName);
            action("Speichern", "", kEditSave);
            action("Verwerfen", "Die Änderungen gehen verloren.", kEditDiscard);
            return out;
        }
        if (view_ == kProfiles)
        {
            if (!profiles_loaded_)
                return {note("Wird geladen …", "Die App liest die Profile.")};
            if (run_.running)
                out.push_back(header("LÄUFT: " + run_.name + " (Schritt " +
                                     std::to_string(run_.step) + " von " +
                                     std::to_string(run_.total) + ")"));
            out.push_back(header("PROFILE (" + std::to_string(profiles_.size()) + ")"));
            for (std::size_t i = 0; i < profiles_.size(); ++i)
            {
                const PayloadProfile &prof = profiles_[i];
                ui::ListItem row;
                row.title = prof.name;
                row.subtitle = profile_summary(prof);
                if (startup_ == prof.id)
                    row.badge = "BEIM START";
                if (run_.running && run_.id == prof.id)
                    row.value = "läuft";
                row.tag = static_cast<int>(i);
                out.push_back(row);
            }
            ui::ListItem add;
            add.title = "Neues Profil …";
            add.subtitle =
                profiles_.size() >= 24 ? "Höchstens 24 Profile." : "Name, dann Payloads und Pausen";
            add.disabled = profiles_.size() >= 24;
            add.tag = kNewProfile;
            out.push_back(add);
            return out;
        }
        if (view_ == kFiles)
        {
            if (!files_loaded_)
                return {note("Wird geladen …", "Die App liest die Ordner.")};
            std::string where;
            for (std::size_t i = 0; i < files_.size(); ++i)
            {
                const PayloadFile &f = files_[i];
                if (f.where != where)
                {
                    where = f.where;
                    out.push_back(header(where));
                }
                ui::ListItem item;
                item.title = f.name;
                item.subtitle = f.valid ? format_bytes(f.size) + " · " + format_date(f.mtime)
                                        : "ungültig – " + f.why;
                item.disabled = !f.valid;
                item.tag = static_cast<int>(i);
                out.push_back(item);
            }
            if (out.empty())
                out.push_back(note("Keine Payloads gefunden",
                                   "Leg .elf-Dateien in /data/PS5-Cooling-Center/payloads oder auf "
                                   "einen USB-Stick."));
            return out;
        }
        if (view_ == kRunning)
        {
            // a title above the list, as the other two views have their folders and categories
            // (user, 29.10.2026)
            if (!running_.empty())
                out.push_back(
                    header("BEREITS GELADENE PAYLOADS (" + std::to_string(running_.size()) + ")"));
            for (std::size_t i = 0; i < running_.size(); ++i)
            {
                const RunningPayload &p = running_[i];
                ui::ListItem item;
                item.title = p.name;
                item.subtitle =
                    "PID " + std::to_string(p.pid) + " · " + std::to_string(p.memory_mb) + " MB";
                if (p.self)
                    item.badge = "DIESE APP";
                item.tag = static_cast<int>(i);
                out.push_back(item);
            }
            if (out.empty())
                out.push_back(note("Wird geladen …", "Die App fragt die laufenden Prozesse ab."));
            return out;
        }
        if (!store_loaded_)
            return {note(store_fetching_ ? "Liste wird geladen …" : "Liste nicht geladen",
                         store_error_.empty() ? "Die Quadrat-Taste lädt die Liste der Payloads."
                                              : store_error_)};
        std::vector<std::size_t> order(store_.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
                         { return store_[a].category < store_[b].category; });
        std::string category;
        for (std::size_t i : order)
        {
            const StoreItem &s = store_[i];
            if (s.category != category)
            {
                category = s.category;
                out.push_back(header(category));
            }
            ui::ListItem item;
            item.title = s.name + "  " + s.version;
            item.subtitle = s.description;
            // "gespeichert", not "geladen": loaded means running now, which the dot shows
            // (29.10.2026)
            item.value = s.installed_version.empty() ? "" : s.update ? "Update" : "gespeichert";
            if (job_.state == "running" && job_.id == s.id)
                item.value = "lädt …";
            item.tag = static_cast<int>(i);
            out.push_back(item);
        }
        return out;
    }

    // Rebuilt only when what the list shows changed: a refill restarts its animation.
    void rebuild(bool switched)
    {
        std::vector<ui::ListItem> next = items();
        std::string key = std::to_string(view_) + (editing_ ? "e" : "") + draft_.name;
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge;
        if (!switched && key == key_)
            return;
        key_ = std::move(key);
        const int keep = switched ? focus_[static_cast<std::size_t>(view_)] : list_.focus();
        // profiles and their entries have no picture: no empty column for one
        list_.style.leading_width = view_ == kProfiles ? 0.0f : 72.0f;
        list_.set_items(next);
        const int count = static_cast<int>(next.size());
        int focus = std::clamp(keep, 0, std::max(count - 1, 0));
        while (focus < count && next[static_cast<std::size_t>(focus)].header)
            ++focus; // a section title is never where the focus rests
        list_.set_focus(focus < count ? focus : 0);
        refresh_info();
    }

    void refresh_info()
    {
        std::vector<ui::DetailItem> rows;
        const ListItemRef item = focused();
        if (item.file != nullptr)
        {
            rows.push_back({"Ort", item.file->where});
            rows.push_back({"Größe", format_bytes(item.file->size)});
            rows.push_back({"Stand", format_date(item.file->mtime)});
        }
        else if (item.running != nullptr)
        {
            rows.push_back({"PID", std::to_string(item.running->pid)});
            rows.push_back({"Speicher", std::to_string(item.running->memory_mb) + " MB"});
        }
        else if (view_ == kProfiles && !editing_ && focused_profile() != nullptr)
        {
            const PayloadProfile &prof = *focused_profile();
            rows.push_back({"Einträge", std::to_string(prof.items.size())});
            rows.push_back({"Beim Start", startup_ == prof.id ? "ja" : "nein"});
            if (run_.id == prof.id && !run_.results.empty())
            {
                const ProfileRunResult &last = run_.results.back();
                rows.push_back({"Zuletzt", item_text(last.item) +
                                               (last.ok ? " · gestartet" : " – " + last.message)});
            }
        }
        else if (item.store != nullptr)
        {
            rows.push_back({"Version", item.store->version});
            rows.push_back({"Kategorie", item.store->category});
            rows.push_back({"Datei", item.store->filename});
            rows.push_back({"Geladen", item.store->installed_version.empty()
                                           ? "nein"
                                           : item.store->installed_version});
        }
        info_.set_items(rows);
        info_for_ = list_.focus();
    }

    ui::Theme theme_;
    ui::TabBar views_;
    ui::ListView list_;
    ui::DetailList info_;
    ui::ProgressBar bar_;
    int view_ = kFiles;
    std::array<int, kViews> focus_{};
    std::string key_;
    int info_for_ = -2;

    // Whether the row's payload runs: the running list always; a file or a store entry when a
    // running name is it.
    bool loaded(const ui::ListItem &item) const
    {
        if (item.tag < 0)
            return false;
        const std::size_t i = static_cast<std::size_t>(item.tag);
        if (view_ == kRunning)
            return i < running_.size();
        std::vector<RunningRef> running;
        for (const RunningPayload &r : running_)
        {
            if (!r.self)
                running.push_back({r.name, r.file});
        }
        // the payload folder of the console (the store downloads into it): what two versions are
        // judged against
        std::vector<std::string> folder;
        for (const PayloadFile &f : files_)
            folder.push_back(f.name);
        if (view_ == kFiles)
            return i < files_.size() && payload_file_loaded(files_[i].name, folder, running);
        return i < store_.size() && payload_file_loaded(store_[i].installed_file, folder, running);
    }

    // ---- profiles ----

    static ui::MenuItem menu_item(const char *label, int tag)
    {
        ui::MenuItem m;
        m.label = label;
        m.tag = tag;
        return m;
    }

    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1000;
        const ui::ListItem &item = list_.items()[static_cast<std::size_t>(i)];
        return item.header ? -1000 : item.tag;
    }

    const PayloadProfile *focused_profile() const
    {
        if (view_ != kProfiles || editing_)
            return nullptr;
        const int tag = focused_tag();
        return tag >= 0 && static_cast<std::size_t>(tag) < profiles_.size()
                   ? &profiles_[static_cast<std::size_t>(tag)]
                   : nullptr;
    }

    bool internal_has(const std::string &name) const
    {
        return std::any_of(files_.begin(), files_.end(), [&](const PayloadFile &f)
                           { return f.source == "internal" && f.name == name; });
    }

    void open_editor(const PayloadProfile &prof)
    {
        draft_ = prof;
        editing_ = true;
        focus_[kProfiles] = list_.focus();
        rebuild(true);
        list_.set_focus(0);
        list_.enter();
    }

    void close_editor()
    {
        editing_ = false;
        naming_ = false;
        rebuild(true);
        list_.enter();
    }

    void save_draft(ui::Feedback &feedback)
    {
        std::vector<PayloadProfile> all = profiles_;
        if (draft_.id.empty())
        {
            draft_.id = make_id(draft_.name, all);
            all.push_back(draft_);
        }
        else
        {
            bool found = false;
            for (PayloadProfile &p : all)
            {
                if (p.id == draft_.id)
                {
                    p = draft_;
                    found = true;
                }
            }
            if (!found)
                all.push_back(draft_);
        }
        feedback.play(audio::Cue::select);
        source().save_payload_profiles(all, startup_);
        profiles_ = all;
        close_editor();
    }

    void handle_editor(const InputFrame &input, ui::Feedback &feedback)
    {
        if (input.is_pressed(Action::back))
        {
            feedback.play(audio::Cue::back);
            close_editor();
            return;
        }
        const ui::Event event = list_.handle(input, feedback);
        const int tag = focused_tag();
        if (input.is_pressed(Action::north) && tag >= 0)
        {
            edit_index_ = tag;
            std::vector<ui::MenuItem> items;
            items.push_back(menu_item("Nach oben", kMenuUp));
            items.push_back(menu_item("Nach unten", kMenuDown));
            if (pause_ms(draft_.items[static_cast<std::size_t>(tag)]) >= 0)
            {
                items.push_back(menu_item("Pause 1 s", kMenuPause1));
                items.push_back(menu_item("Pause 2 s", kMenuPause2));
                items.push_back(menu_item("Pause 5 s", kMenuPause5));
                items.push_back(menu_item("Pause 10 s", kMenuPause10));
            }
            ui::MenuItem line;
            line.separator = true;
            items.push_back(line);
            ui::MenuItem del = menu_item("Entfernen", kMenuRemove);
            del.danger = true;
            items.push_back(del);
            menu_.set_items(items);
            menu_.open({kRightX + 360.0f, kBottom - 80.0f, 140.0f, 44.0f}, feedback);
            return;
        }
        if (event != ui::Event::activated)
            return;
        if (tag == kEditAddPayload)
        {
            std::vector<ui::MenuItem> items;
            for (std::size_t i = 0; i < files_.size(); ++i)
            {
                const PayloadFile &f = files_[i];
                if (f.source != "internal" || !f.valid)
                    continue;
                items.push_back(menu_item(f.name.c_str(), kMenuFileBase + static_cast<int>(i)));
            }
            if (items.empty() || draft_.items.size() >= 64)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            menu_.set_items(items);
            menu_.open({kRightX + 360.0f, kBottom - 80.0f, 140.0f, 44.0f}, feedback);
        }
        else if (tag == kEditAddPause)
        {
            if (draft_.items.size() >= 64)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            draft_.items.emplace_back("!2000");
            feedback.play(audio::Cue::select);
            rebuild(false);
        }
        else if (tag == kEditName)
        {
            naming_ = true;
            name_ = draft_.name;
            keys_.set_layout(0);
            keys_.set_length(static_cast<int>(name_.size()));
            keys_.set_active(true);
            keys_.enter();
        }
        else if (tag == kEditSave)
            save_draft(feedback);
        else if (tag == kEditDiscard)
        {
            feedback.play(audio::Cue::back);
            close_editor();
        }
    }

    void run_menu(int tag, ui::Feedback &feedback)
    {
        if (editing_)
        {
            if (tag >= kMenuFileBase)
            {
                const std::size_t i = static_cast<std::size_t>(tag - kMenuFileBase);
                if (i < files_.size() && draft_.items.size() < 64)
                    draft_.items.push_back(files_[i].name);
                rebuild(false);
                return;
            }
            const std::size_t i = static_cast<std::size_t>(edit_index_);
            if (i >= draft_.items.size())
                return;
            if (tag == kMenuUp && i > 0)
                std::swap(draft_.items[i], draft_.items[i - 1]);
            else if (tag == kMenuDown && i + 1 < draft_.items.size())
                std::swap(draft_.items[i], draft_.items[i + 1]);
            else if (tag == kMenuRemove)
                draft_.items.erase(draft_.items.begin() + static_cast<std::ptrdiff_t>(i));
            else if (tag == kMenuPause1 || tag == kMenuPause2 || tag == kMenuPause5 ||
                     tag == kMenuPause10)
            {
                const int ms = tag == kMenuPause1   ? 1000
                               : tag == kMenuPause2 ? 2000
                               : tag == kMenuPause5 ? 5000
                                                    : 10000;
                draft_.items[i] = "!" + std::to_string(ms);
            }
            else
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            rebuild(false);
            return;
        }
        const auto it =
            std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const PayloadProfile &p) { return p.id == menu_profile_; });
        if (it == profiles_.end())
            return;
        if (tag == kMenuRun)
        {
            if (run_.running && run_.id == it->id)
                source().stop_payload_profile();
            else if (!run_.running)
                source().run_payload_profile(it->id);
            else
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        }
        else if (tag == kMenuEdit)
            open_editor(*it);
        else if (tag == kMenuStartup)
        {
            // at once; the whole document goes, as the web page sends it
            const std::string startup = startup_ == it->id ? std::string() : it->id;
            source().save_payload_profiles(profiles_, startup);
            startup_ = startup;
            rebuild(false);
        }
        else if (tag == kMenuDelete)
        {
            std::vector<PayloadProfile> all = profiles_;
            const std::string gone = it->id;
            std::erase_if(all, [&](const PayloadProfile &p) { return p.id == gone; });
            const std::string startup = startup_ == gone ? std::string() : startup_;
            source().save_payload_profiles(all, startup);
            profiles_ = all;
            startup_ = startup;
            rebuild(false);
        }
    }

    std::vector<PayloadProfile> profiles_;
    std::string startup_;
    bool profiles_loaded_ = false;
    ProfileRun run_;
    bool editing_ = false;
    PayloadProfile draft_; // the profile in the editor (id "" for a new one)
    int edit_index_ = -1;  // the entry the △ menu is about
    std::string menu_profile_;
    ui::Menu menu_;
    ui::Keyboard keys_;
    bool naming_ = false;
    std::string name_;
    tween::Spring shown_;
    float clock_ = 0.0f;

    std::vector<PayloadFile> files_;
    bool files_loaded_ = false;
    std::vector<RunningPayload> running_;
    std::vector<StoreItem> store_;
    bool store_loaded_ = false;
    bool store_fetching_ = false;
    std::string store_error_;
    StoreJob job_;
};

} // namespace

std::unique_ptr<Page> make_payloads_page()
{
    return std::make_unique<PayloadsPage>();
}

} // namespace hui::ps5cc
