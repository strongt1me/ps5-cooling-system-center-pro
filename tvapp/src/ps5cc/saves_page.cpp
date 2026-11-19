// ps5cc - The Spielstände page (Spiele → Spielstände): the console's saves per user, backups of
// them, and putting one back.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's tab (src/savebackup.c does the work: an encrypted copy, read back and checked):
// the X button picks a game's saves for the next backup, Square opens what a game or a backup
// allows (delete with a backup first; check; put one game back), Triangle the rest (back up what is
// picked, the destination, pick all or none, cancel the job). Deleting asks first and names the
// game (06.11.2026); the rest fires at once, and a restore keeps the state before it as its own
// backup, as in the web app.
//
// To another console (19.11.2026): Square on a game offers "ZIP auslesen auf <drive>" (the ZIP goes
// to the drive's folder PS5-Sicherung/Spielstand-ZIP, a stick is carried to the other console), and
// the list ZIP-DATEIEN shows what the drives hold there: ✕ looks at the ZIP (nothing is written),
// then the menu offers "Für <user> einspielen". A file on a drive stays; the web app has the
// upload and the download.

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/playtime_rules.hpp"
#include "ps5cc/save_zip.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr int kTitleTag = 1000;  // + index into titles_
constexpr int kBackupTag = 5000; // + index into the backups
constexpr int kZipTag = 8000;    // + index into the ZIPs of the drives

enum MenuTag
{
    kDelete = 1,
    kVerify,
    kRestore = 100, // + index of the title in the backup
    kBackup = 20,
    kTarget,
    kAll,
    kNone,
    kCancel,
    kReload,
    kDrive = 60,        // + index into the drives
    kExportSlot = 2000, // ZIP files of a game: every slot it keeps
    kZipUser = 2100,    // + index into the users of the plan
    kZipUserAll = 2200, // + index: the same, for all slots of the title (the ZIP files of one run)
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

class SavesPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.header_rule = true; // title and line, as the forms (user, 29.10.2026)
        list_.style.row_height = 80.0f;
        list_.style.title_size = 24.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 20.0f;
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
        info_.set_bounds({kRightX + 30.0f, kTop + 110.0f, kRightW - 60.0f, 260.0f});

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 620.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});
        ask_.init(theme);
    }

    void absorb(const Snapshot &data) override
    {
        saves_ = data.saves;
        tz_ = data.config.tz_offset_min;
        titles_.clear();
        for (const SaveUser &u : saves_.users)
        {
            for (const SaveTitle &t : u.titles)
                titles_.push_back(t);
        }
        // nothing is picked on its own (user, 13.11.2026): the person picks what to back up; titles
        // that went are forgotten
        for (auto it = picked_.begin(); it != picked_.end();)
        {
            const bool there = std::any_of(titles_.begin(), titles_.end(), [&](const SaveTitle &t)
                                           { return t.uid + "/" + t.id == *it; });
            it = there ? std::next(it) : picked_.erase(it);
        }
        if (target_.empty() || std::none_of(saves_.drives.begin(), saves_.drives.end(),
                                            [&](const SaveDrive &d) { return d.mount == target_; }))
        {
            target_.clear();
            for (const SaveDrive &d : saves_.drives)
            {
                if (target_.empty() || (target_ == "/user" && d.mount != "/user"))
                    target_ = d.mount;
            }
        }
        // the answer to "look at this ZIP" has come: the menu opens in the next frame (it needs the
        // feedback)
        if (!wait_key_.empty() && saves_.plan.ready && saves_.plan.key == wait_key_)
        {
            wait_key_.clear();
            open_zip_menu_pending_ = true;
        }
        rebuild();
    }

    void enter() override
    {
        list_.enter();
    }

    bool modal() const override
    {
        return menu_.is_open() || ask_.is_open();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (open_zip_menu_pending_ && !menu_.is_open() && !ask_.is_open())
        {
            open_zip_menu_pending_ = false;
            open_zip_menu(feedback);
        }
        if (ask_.is_open())
        {
            // only "Löschen" in the question deletes the saves (06.11.2026)
            if (ask_.handle(input, feedback))
                delete_saves();
            return;
        }
        if (menu_.is_open())
        {
            if (menu_.handle(input, feedback) == ui::Event::activated)
                run(menu_.items()[static_cast<std::size_t>(menu_.focus())].tag, feedback);
            return;
        }
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
            refresh_info();
        const SaveTitle *t = focused_title();
        const SaveBackup *b = focused_backup();
        if (event == ui::Event::activated)
        {
            if (t == nullptr)
            {
                if (b != nullptr)
                    open_backup_menu(*b, feedback);
                else if (const SaveZip *z = focused_zip())
                    ask_plan(*z, feedback);
                else
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            feedback.play(audio::Cue::select);
            const std::string key = t->uid + "/" + t->id;
            if (!picked_.erase(key))
                picked_.insert(key);
            rebuild();
            return;
        }
        if (input.is_pressed(Action::west))
        {
            if (t != nullptr)
                open_title_menu(*t, feedback);
            else if (b != nullptr)
                open_backup_menu(*b, feedback);
            else if (const SaveZip *z = focused_zip())
                ask_plan(*z, feedback);
            else
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        if (input.is_pressed(Action::north))
            open_page_menu(feedback);
    }

    void update(float dt) override
    {
        list_.update(dt);
        info_.update(dt);
        menu_.update(dt);
        ask_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);
        const SaveTitle *t = focused_title();
        const SaveBackup *b = focused_backup();
        const SaveZip *z = focused_zip();
        const std::string title = t != nullptr   ? (t->name.empty() ? t->id : t->name)
                                  : b != nullptr ? backup_heading(*b)
                                  : z != nullptr ? zip_title(*z)
                                                 : std::string("Spielstände");
        ui::text(list, fonts.display, ui::fit_label(paint, title, 32.0f, card.w - 60.0f),
                 card.x + 30.0f, card.y + 66.0f, 32.0f, kInk);
        if (t != nullptr || b != nullptr || z != nullptr)
            info_.draw(canvas);
        else
            paragraph(canvas,
                      "Sichert die Spielstände der Konsole verschlüsselt und unverändert, liest "
                      "jede Kopie zurück und "
                      "prüft sie. Zurückspielen geht nur auf dieser Konsole; der Stand davor wird "
                      "vorher gesichert. Auf eine andere Konsole: ein Spiel → Aktionen → ZIP "
                      "auslesen, das Laufwerk dort anstecken, in der Liste ZIP-DATEIEN einspielen.",
                      card.x + 30.0f, card.y + 120.0f, card.w - 60.0f, kMuted, 7);

        // the job, or what the next backup would be
        const float y = card.y + 420.0f;
        list.line(card.x + 30.0f, y, card.x + card.w - 30.0f, y, 1.0f, Color::rgb(0x3880ff, 0.18f));
        const SaveJob &j = saves_.job;
        if (j.active)
        {
            ui::text(list, fonts.semibold, job_title(j) + " läuft", card.x + 30.0f, y + 46.0f,
                     24.0f, kInk);
            ui::text(list, fonts.regular, j.phase + " · " + std::to_string(j.percent) + " %",
                     card.x + 30.0f, y + 82.0f, 20.0f, kMuted);
            const Rect bar{card.x + 30.0f, y + 102.0f, card.w - 60.0f, 12.0f};
            list.rounded_rect(bar, 6.0f, Color::rgb(0x3880ff, 0.18f));
            list.rounded_rect({bar.x, bar.y,
                               bar.w * static_cast<float>(std::clamp(j.percent, 0, 100)) / 100.0f,
                               bar.h},
                              6.0f, kBlue);
            ui::text(list, fonts.regular,
                     ui::fit_label(paint,
                                   std::to_string(j.files_done) + " von " +
                                       std::to_string(j.files_total) + " Dateien · " + j.current,
                                   18.0f, card.w - 60.0f),
                     card.x + 30.0f, y + 146.0f, 18.0f, kMuted);
        }
        else if (j.finished_ago_s >= 0 && j.finished_ago_s < 900 && !j.kind.empty())
        {
            const bool ok = j.state == "done";
            ui::text(list, fonts.semibold,
                     job_title(j) + (ok                       ? " fertig"
                                     : j.state == "cancelled" ? " abgebrochen"
                                                              : " fehlgeschlagen"),
                     card.x + 30.0f, y + 46.0f, 24.0f, ok ? kGood : kWarning);
            std::string text = !j.error.empty() ? j.error : !j.note.empty() ? j.note : j.path;
            if (!j.undo.empty())
                text += " Der Stand davor liegt in " + j.undo + ".";
            paragraph(canvas, text, card.x + 30.0f, y + 84.0f, card.w - 60.0f, kMuted, 5);
        }
        else if (z != nullptr)
        {
            const std::string text = zip_text(*z);
            ui::text(list, fonts.semibold, "ZIP-Datei", card.x + 30.0f, y + 46.0f, 22.0f, kInk);
            paragraph(canvas, text, card.x + 30.0f, y + 84.0f, card.w - 60.0f,
                      zip_warns(*z) ? kWarning : kMuted, 5);
        }
        else
        {
            ui::text(list, fonts.semibold, "Nächste Sicherung", card.x + 30.0f, y + 46.0f, 22.0f,
                     kInk);
            paragraph(canvas, plan_text(), card.x + 30.0f, y + 84.0f, card.w - 60.0f, kMuted, 5);
        }
        if (saves_.unfinished > 0)
            paragraph(canvas,
                      std::to_string(saves_.unfinished) +
                          " unfertige Sicherung(en) liegen auf den Laufwerken; entfernen am PC "
                          "oder per FTP.",
                      card.x + 30.0f, kBottom - 80.0f, card.w - 60.0f, kWarning, 2);
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        menu_.draw(canvas);
        ask_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        if (const SaveTitle *t = focused_title())
        {
            out[n++] = {ui::Button::cross,
                        picked_.count(t->uid + "/" + t->id) ? "Abwählen" : "Auswählen"};
            out[n++] = {ui::Button::square, "Aktionen"};
        }
        else if (focused_backup() != nullptr)
            out[n++] = {ui::Button::cross, "Aktionen"};
        else if (focused_zip() != nullptr)
            out[n++] = {ui::Button::cross, "Ansehen und einspielen"};
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    // The games a backup holds, for its heading: the first three names, then "+N"
    // (user, 19.11.2026: the list said only when, not which game).
    static std::string backup_games(const SaveBackup &b)
    {
        std::string out;
        for (std::size_t i = 0; i < b.titles.size() && i < 3; ++i)
            out += (out.empty() ? "" : ", ") +
                   (b.titles[i].name.empty() ? b.titles[i].id : b.titles[i].name);
        if (b.titles.size() > 3)
            out += " +" + std::to_string(b.titles.size() - 3);
        return out;
    }

    std::string backup_heading(const SaveBackup &b) const
    {
        const std::string games = backup_games(b);
        return games.empty() ? "Sicherung vom " + play_date(b.created, tz_) : games;
    }

    static std::string job_title(const SaveJob &j)
    {
        return j.kind == "verify"    ? "Prüfung"
               : j.kind == "restore" ? "Zurückspielen"
               : j.kind == "delete"  ? "Löschen"
               : j.kind == "export"  ? "ZIP auslesen"
               : j.kind == "import"  ? "Einspielen"
                                     : "Sicherung";
    }

    const SaveDrive *target() const
    {
        for (const SaveDrive &d : saves_.drives)
        {
            if (d.mount == target_)
                return &d;
        }
        return nullptr;
    }

    std::uint64_t picked_bytes() const
    {
        std::uint64_t sum = 0;
        for (const SaveTitle &t : titles_)
        {
            if (picked_.count(t.uid + "/" + t.id))
                sum += t.bytes;
        }
        return sum;
    }

    // Why the backup cannot start now, in the web app's order; empty when it can.
    std::string blocked() const
    {
        if (saves_.game_running)
            return "Ein Spiel läuft (" + saves_.game_id + "). Erst beenden, dann sichern.";
        if (saves_.job.active)
            return "Es läuft schon ein Vorgang mit den Spielständen.";
        if (picked_.empty())
            return "Nichts ausgewählt: die X-Taste wählt Spiele aus.";
        const SaveDrive *d = target();
        if (d == nullptr)
            return "Kein Laufwerk für die Sicherung.";
        if (d->free_bytes < picked_bytes() + 64ull * 1024 * 1024)
            return "Auf " + d->label + " ist nicht genug Platz.";
        return {};
    }

    std::string plan_text() const
    {
        const std::string why = blocked();
        if (!why.empty())
            return why;
        const SaveDrive *d = target();
        return std::to_string(picked_.size()) + (picked_.size() == 1 ? " Spiel" : " Spiele") +
               " ausgewählt, zusammen " + format_bytes(picked_bytes()) + ", nach " + d->label +
               " (" + format_bytes(d->free_bytes) + " frei). Mehr → Ausgewählte sichern.";
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

    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1;
        const ui::ListItem &item = list_.items()[static_cast<std::size_t>(i)];
        return item.header ? -1 : item.tag;
    }

    const SaveTitle *focused_title() const
    {
        const int i = focused_tag() - kTitleTag;
        return i >= 0 && i < static_cast<int>(titles_.size())
                   ? &titles_[static_cast<std::size_t>(i)]
                   : nullptr;
    }

    const SaveBackup *focused_backup() const
    {
        const int i = focused_tag() - kBackupTag;
        return i >= 0 && i < static_cast<int>(saves_.backups.size())
                   ? &saves_.backups[static_cast<std::size_t>(i)]
                   : nullptr;
    }

    const SaveZip *focused_zip() const
    {
        const int i = focused_tag() - kZipTag;
        return i >= 0 && i < static_cast<int>(saves_.zips.size())
                   ? &saves_.zips[static_cast<std::size_t>(i)]
                   : nullptr;
    }

    // "<drive>|<file>": the question the plan answers (the source keeps the same key)
    static std::string zip_key(const SaveZip &z)
    {
        return z.mount + "|" + z.name;
    }

    // The game of a ZIP as this console knows it (a save of it is here), else its number.
    std::string zip_title(const SaveZip &z) const
    {
        const std::string id = zip_title_id(z.name);
        if (id.empty())
            return z.name;
        std::string name = id;
        for (const SaveTitle &t : titles_)
        {
            if (t.id == id)
            {
                if (!t.name.empty())
                    name = t.name;
                break;
            }
        }
        const std::string slot = zip_slot(z.name);
        return slot.empty() ? name : name + " · " + slot;
    }

    // The ZIP files of the same run as this one: the same drive, title and time (every slot of a
    // game read out together has one); this one is always in it. Sorted by name, at most sixteen.
    std::vector<const SaveZip *> zip_siblings(const SaveZip &z) const
    {
        std::vector<const SaveZip *> out;
        const std::string id = zip_title_id(z.name), stamp = zip_stamp(z.name);
        for (const SaveZip &x : saves_.zips)
        {
            if (out.size() < 16 && x.mount == z.mount &&
                (x.name == z.name || (!id.empty() && !stamp.empty() && zip_title_id(x.name) == id &&
                                      zip_stamp(x.name) == stamp)))
                out.push_back(&x);
        }
        std::sort(out.begin(), out.end(),
                  [](const SaveZip *a, const SaveZip *b) { return a->name < b->name; });
        return out;
    }

    bool zip_planned(const SaveZip &z) const
    {
        return saves_.plan.ready && saves_.plan.key == zip_key(z);
    }

    // Whether the text under the ZIP is a refusal.
    bool zip_warns(const SaveZip &z) const
    {
        return zip_planned(z) && (!saves_.plan.error.empty() || !saves_.plan.importable);
    }

    std::string zip_text(const SaveZip &z) const
    {
        if (!wait_key_.empty() && wait_key_ == zip_key(z))
            return "Die Datei wird geprüft …";
        if (!zip_planned(z))
            return "Die X-Taste sieht die Datei an: nichts wird geschrieben. Danach lässt sich der "
                   "Spielstand für einen Benutzer einspielen.";
        const SaveImportPlan &p = saves_.plan;
        if (!p.error.empty())
            return p.error;
        if (!p.importable)
            return p.why.empty() ? "Dieser Spielstand lässt sich hier nicht einspielen." : p.why;
        std::string text = std::to_string(p.files) + (p.files == 1 ? " Datei, " : " Dateien, ") +
                           format_bytes(p.bytes) + ". ";
        if (p.converted)
            text += "Das neuere Abbildformat wird als klassisches angelegt. ";
        const std::size_t same = zip_siblings(z).size();
        if (same > 1)
            text += "Dazu gehören " + std::to_string(same) +
                    " Spielstände dieses Titels: alle auf einmal einspielen ist möglich. ";
        const SaveDrive *d = target();
        text += d != nullptr
                    ? "Ein vorhandener Stand wird vorher auf " + d->label + " gesichert."
                    : "Für einen vorhandenen Stand wird ein Laufwerk zum Sichern gebraucht.";
        return text;
    }

    // ---------------------------------------------------------------- menus

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

    void open_title_menu(const SaveTitle &t, ui::Feedback &feedback)
    {
        menu_title_ = t;
        const SaveDrive *d = target();
        const bool busy = saves_.job.active || saves_.game_running;
        std::vector<ui::MenuItem> items;
        // one action for the whole game: the app reads out every slot it keeps (SaveData,
        // sce_sdmemory ...), a ZIP each, to the drive that is the target (as for the backups); the
        // person does not choose a slot (user, 19.11.2026)
        if (t.slots.empty())
            items.push_back(item("ZIP auslesen (kein Spielstand gefunden)", kExportSlot, true));
        else
            items.push_back(item(
                "ZIP auslesen auf " + (d != nullptr ? d->label : std::string("…")) +
                    (t.slots.size() > 1 ? " (" + std::to_string(t.slots.size()) + " Spielstände)"
                                        : std::string()),
                kExportSlot, busy || d == nullptr));
        items.push_back(item(d != nullptr ? "Löschen (vorher nach " + d->label + " sichern)"
                                          : "Löschen (ohne Sicherung)",
                             kDelete, busy, true));
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void open_backup_menu(const SaveBackup &b, ui::Feedback &feedback)
    {
        menu_backup_ = b;
        const bool busy = saves_.job.active || saves_.game_running;
        std::vector<ui::MenuItem> items;
        items.push_back(item("Sicherung prüfen", kVerify, saves_.job.active));
        if (b.this_console)
        {
            for (std::size_t i = 0; i < b.titles.size() && i < 40; ++i)
            {
                const SaveTitle &t = b.titles[i];
                items.push_back(item("„" + (t.name.empty() ? t.id : t.name) + "“ zurückspielen",
                                     kRestore + static_cast<int>(i), busy));
            }
        }
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    // ✕ on a ZIP: ask the app what it is (nothing is written); the menu follows with the answer.
    void ask_plan(const SaveZip &z, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        menu_zip_ = z;
        if (zip_planned(z))
        {
            open_zip_menu(feedback);
            return;
        }
        wait_key_ = zip_key(z);
        source().saves_post("import/plan", "{\"name\":" + json_text(z.name) +
                                               ",\"from\":" + json_text(z.mount) + "}");
        rebuild();
    }

    // The users the save can go to; the refusal (or an error) is told under the ZIP instead.
    void open_zip_menu(ui::Feedback &feedback)
    {
        const SaveImportPlan &p = saves_.plan;
        if (!zip_planned(menu_zip_) || !p.error.empty() || !p.importable || p.users.empty())
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        const bool busy = saves_.job.active || saves_.game_running;
        const SaveDrive *d = target();
        std::vector<ui::MenuItem> items;
        const std::size_t same = zip_siblings(menu_zip_).size();
        for (std::size_t i = 0; same > 1 && i < p.users.size() && i < 16; ++i)
        {
            const SaveImportUser &u = p.users[i];
            items.push_back(
                item("Für " + u.name + " alle " + std::to_string(same) + " Spielstände einspielen",
                     kZipUserAll + static_cast<int>(i), busy || (u.has_title && d == nullptr)));
        }
        for (std::size_t i = 0; i < p.users.size() && i < 16; ++i)
        {
            const SaveImportUser &u = p.users[i];
            items.push_back(item("Für " + u.name + " einspielen" +
                                     (u.has_slot    ? " (ersetzt)"
                                      : u.has_title ? " (ergänzt)"
                                                    : " (neu)"),
                                 kZipUser + static_cast<int>(i),
                                 busy || (u.has_title && d == nullptr)));
        }
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void open_page_menu(ui::Feedback &feedback)
    {
        const SaveDrive *d = target();
        std::vector<ui::MenuItem> items;
        items.push_back(item("Ausgewählte sichern (" + std::to_string(picked_.size()) + ")",
                             kBackup, !blocked().empty()));
        items.push_back(
            item("Ziel wählen: " + (d != nullptr ? d->label : std::string("keins")) + " …", kTarget,
                 saves_.drives.empty()));
        items.push_back(item("Alle auswählen", kAll, titles_.empty()));
        items.push_back(item("Keine auswählen", kNone, picked_.empty()));
        if (saves_.job.active && saves_.job.can_cancel)
            items.push_back(item(job_title(saves_.job) + " abbrechen", kCancel, false, true));
        items.push_back(item("Neu laden", kReload));
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    // The drives to back up to, each with its free space; the chosen one is marked. Cycling through
    // them on each press left it unclear whether the press already saved (user, 29.10.2026).
    void open_drive_menu(ui::Feedback &feedback)
    {
        std::vector<ui::MenuItem> items;
        int chosen = 0;
        for (std::size_t i = 0; i < saves_.drives.size() && i < 30; ++i)
        {
            const SaveDrive &d = saves_.drives[i];
            const bool on = d.mount == target_;
            if (on)
                chosen = static_cast<int>(i);
            items.push_back(item((on ? "Gewählt: " : "") + d.label + " · " +
                                     format_bytes(d.free_bytes) + " frei",
                                 kDrive + static_cast<int>(i)));
        }
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
        menu_.set_focus(chosen);
    }

    // Asks first (user, 06.11.2026): which game's saves go, and whether they are backed up before.
    void ask_delete(ui::Feedback &feedback)
    {
        const SaveDrive *d = target();
        const std::string name = menu_title_.name.empty() ? menu_title_.id : menu_title_.name;
        std::vector<std::string> notes;
        if (d != nullptr)
            notes.push_back("Sie werden vorher auf " + d->label + " gesichert.");
        else
            notes.push_back("Kein Laufwerk für eine Sicherung da:");
        if (d == nullptr)
            notes.push_back("Es wird ohne Sicherung gelöscht.");
        ask_.open("Spielstände löschen?", {DeleteAsk::line(name, "Spielstände")}, notes, feedback,
                  d != nullptr ? "Das wird gelöscht:" : "Das wird endgültig gelöscht:",
                  d != nullptr ? std::string()
                               : std::string("Das lässt sich nicht rückgängig machen."));
    }

    void delete_saves()
    {
        if (target() != nullptr)
            source().saves_post("delete", "{\"uid\":" + json_text(menu_title_.uid) +
                                              ",\"id\":" + json_text(menu_title_.id) +
                                              ",\"mount\":" + json_text(target_) + "}");
        else
            source().saves_post("delete", "{\"uid\":" + json_text(menu_title_.uid) + ",\"id\":" +
                                              json_text(menu_title_.id) + ",\"no_backup\":true}");
    }

    void run(int tag, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        if (tag == kExportSlot)
        {
            // no slot named: the app reads out all of them
            if (target() != nullptr)
                source().saves_post("export", "{\"user\":" + json_text(menu_title_.uid) +
                                                  ",\"title\":" + json_text(menu_title_.id) +
                                                  ",\"mount\":" + json_text(target_) + "}");
            return;
        }
        const bool all = tag >= kZipUserAll && tag < kZipUserAll + 100;
        if ((tag >= kZipUser && tag < kZipUser + 100) || all)
        {
            const std::size_t i = static_cast<std::size_t>(tag - (all ? kZipUserAll : kZipUser));
            const SaveImportPlan &p = saves_.plan;
            if (i < p.users.size() && zip_planned(menu_zip_))
            {
                const SaveImportUser &u = p.users[i];
                std::string body;
                if (all)
                {
                    // every ZIP of the run (one title, every slot): put in one after the other
                    for (const SaveZip *z : zip_siblings(menu_zip_))
                        body += (body.empty() ? "" : ",") + json_text(z->name);
                    body = "{\"names\":[" + body + "]";
                }
                else
                {
                    body = "{\"name\":" + json_text(menu_zip_.name);
                }
                body += ",\"from\":" + json_text(menu_zip_.mount) + ",\"user\":" + json_text(u.uid);
                if (u.has_title && target() != nullptr)
                    body += ",\"mount\":" + json_text(target_); // where the state before goes
                source().saves_post("import", body + "}");
            }
            return;
        }
        if (tag >= kDrive && tag < kRestore)
        {
            const std::size_t i = static_cast<std::size_t>(tag - kDrive);
            if (i < saves_.drives.size())
                target_ = saves_.drives[i].mount;
            open_page_menu(feedback);
            return;
        }
        if (tag >= kRestore)
        {
            const std::size_t i = static_cast<std::size_t>(tag - kRestore);
            if (i < menu_backup_.titles.size())
            {
                const SaveTitle &t = menu_backup_.titles[i];
                source().saves_post("restore", "{\"path\":" + json_text(menu_backup_.path) +
                                                   ",\"user\":" + json_text(t.uid) + ",\"title\":" +
                                                   json_text(t.id) + ",\"confirm\":true}");
            }
            return;
        }
        switch (tag)
        {
        case kDelete:
            ask_delete(feedback);
            break;
        case kVerify:
            source().saves_post("verify", "{\"path\":" + json_text(menu_backup_.path) + "}");
            break;
        case kBackup:
        {
            std::set<std::string> users;
            std::string titles;
            for (const std::string &key : picked_)
            {
                users.insert(key.substr(0, key.find('/')));
                titles += (titles.empty() ? "" : ",") + json_text(key);
            }
            std::string uj;
            for (const std::string &u : users)
                uj += (uj.empty() ? "" : ",") + json_text(u);
            source().saves_post("backup", "{\"target\":" + json_text(target_) + ",\"users\":[" +
                                              uj + "],\"titles\":[" + titles + "]}");
            break;
        }
        case kTarget:
            open_drive_menu(feedback);
            break;
        case kAll:
            for (const SaveTitle &t : titles_)
                picked_.insert(t.uid + "/" + t.id);
            break;
        case kNone:
            picked_.clear();
            break;
        case kCancel:
            source().saves_post("cancel", "{}");
            break;
        case kReload:
            source().reload();
            break;
        default:
            break;
        }
        rebuild();
    }

    // ---------------------------------------------------------------- list

    static ui::ListItem header(std::string title)
    {
        ui::ListItem h;
        h.title = std::move(title);
        h.header = true;
        h.tag = -1;
        return h;
    }

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        if (!saves_.loaded)
        {
            ui::ListItem n;
            n.title = "Wird gelesen …";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
            return out;
        }
        std::size_t index = 0;
        for (const SaveUser &u : saves_.users)
        {
            std::string head = u.name;
            for (char &c : head)
                c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
            out.push_back(header(head + " · " + std::to_string(u.titles.size()) + " TITEL · " +
                                 format_bytes(u.bytes)));
            for (const SaveTitle &t : u.titles)
            {
                ui::ListItem item;
                item.title = t.name.empty() ? t.id : t.name;
                item.subtitle = t.id + " · " + t.platform +
                                (t.installed ? "" : " · nicht installiert") + " · geändert " +
                                play_date(t.mtime, tz_);
                item.value = format_bytes(t.bytes);
                item.badge = picked_.count(t.uid + "/" + t.id) ? "AUSGEWÄHLT" : "";
                item.tag = kTitleTag + static_cast<int>(index);
                out.push_back(item);
                ++index;
            }
        }
        if (saves_.users.empty())
        {
            ui::ListItem n;
            n.title = saves_.error.empty() ? "Keine Spielstände gefunden" : saves_.error;
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
        }
        if (!saves_.backups.empty())
            out.push_back(header("SICHERUNGEN"));
        for (std::size_t i = 0; i < saves_.backups.size(); ++i)
        {
            const SaveBackup &b = saves_.backups[i];
            ui::ListItem item;
            item.title = backup_heading(b);
            std::string users;
            for (const std::string &n : b.user_names)
                users += (users.empty() ? "" : ", ") + n;
            item.subtitle = play_date(b.created, tz_) + ", " + play_clock(b.created, tz_) +
                            " Uhr · " + b.label + " · " + users;
            item.value = format_bytes(b.bytes);
            item.badge = b.kind == "undo"  ? "STAND VORHER"
                         : !b.this_console ? "ANDERE KONSOLE"
                                           : "";
            item.tag = kBackupTag + static_cast<int>(i);
            out.push_back(item);
        }
        if (!saves_.zips.empty())
            out.push_back(header("ZIP-DATEIEN"));
        for (std::size_t i = 0; i < saves_.zips.size(); ++i)
        {
            const SaveZip &z = saves_.zips[i];
            ui::ListItem item;
            item.title = zip_title(z);
            item.subtitle = z.name + " · " + play_date(z.created, tz_);
            item.value = format_bytes(z.bytes);
            item.badge = z.label;
            item.tag = kZipTag + static_cast<int>(i);
            out.push_back(item);
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
            const int count = static_cast<int>(next.size());
            int focus = std::clamp(list_.focus(), 0, std::max(count - 1, 0));
            while (focus < count && next[static_cast<std::size_t>(focus)].header)
                ++focus;
            list_.set_items(next);
            list_.set_focus(std::min(focus, std::max(count - 1, 0)));
        }
        refresh_info();
    }

    void refresh_info()
    {
        std::vector<ui::DetailItem> rows;
        if (const SaveTitle *t = focused_title())
        {
            rows.push_back({"Titel-ID", t->id});
            rows.push_back({"Plattform", t->platform});
            rows.push_back({"Größe", format_bytes(t->bytes)});
            rows.push_back(
                {"Geändert", play_date(t->mtime, tz_) + ", " + play_clock(t->mtime, tz_) + " Uhr"});
            rows.push_back({"Installiert", t->installed ? "ja" : "nein"});
        }
        else if (const SaveBackup *b = focused_backup())
        {
            rows.push_back({"Gesichert", play_date(b->created, tz_) + ", " +
                                             play_clock(b->created, tz_) + " Uhr"});
            rows.push_back({"Laufwerk", b->label});
            rows.push_back({"Titel", std::to_string(b->titles.size())});
            rows.push_back({"Größe", format_bytes(b->bytes)});
            rows.push_back({"Art", std::string(b->kind == "undo" ? "Stand vor dem Zurückspielen"
                                                                 : "Sicherung") +
                                       (b->this_console ? "" : " · andere Konsole")});
        }
        else if (const SaveZip *z = focused_zip())
        {
            rows.push_back({"Ort", z->label}); // the file name is the list's second line
            rows.push_back({"Größe", format_bytes(z->bytes)});
            rows.push_back({"Geändert", play_date(z->created, tz_) + ", " +
                                            play_clock(z->created, tz_) + " Uhr"});
            if (zip_planned(*z) && saves_.plan.error.empty())
            {
                const SaveImportPlan &p = saves_.plan;
                rows.push_back({"Spielstand", p.title_name.empty() ? p.title_id : p.title_name});
                if (p.importable)
                    rows.push_back({"Abbild", p.converted ? "neueres Format" : "klassisch"});
            }
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
    DeleteAsk ask_;
    std::string key_;
    std::string info_key_ = "\x01";
    Saves saves_;
    int tz_ = 0;
    std::vector<SaveTitle> titles_;
    std::set<std::string> picked_; // "uid/id"
    std::string target_;
    SaveTitle menu_title_;
    SaveBackup menu_backup_;
    SaveZip menu_zip_;
    std::string wait_key_; // the ZIP whose plan was asked for and has not come yet
    bool open_zip_menu_pending_ = false;
};

} // namespace

std::unique_ptr<Page> make_saves_page()
{
    return std::make_unique<SavesPage>();
}

} // namespace hui::ps5cc
