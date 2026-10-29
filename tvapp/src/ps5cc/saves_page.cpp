// ps5cc - The Spielstände page (Spiele → Spielstände): the console's saves per user, backups of them, and putting
// one back.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's tab (src/savebackup.c does the work: an encrypted copy, read back and checked): the X button picks
// a game's saves for the next backup, Square opens what a game or a backup allows (delete with a backup first; check;
// put one game back), Triangle the rest (back up what is picked, the destination, pick all or none, cancel the job).
// Everything fires at once; a restore keeps the state before it as its own backup, as in the web app.

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/playtime_rules.hpp"

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
        list_.style.row_height = 80.0f;
        list_.style.title_size = 24.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 20.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 110.0f, kRightW - 60.0f, 260.0f});

        menu_.style.theme = theme;
        menu_.style.width = 620.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});
    }

    void absorb(const Snapshot &data) override
    {
        saves_ = data.saves;
        tz_ = data.config.tz_offset_min;
        titles_.clear();
        for (const SaveUser &u : saves_.users)
        {
            for (const SaveTitle &t : u.titles)
            {
                titles_.push_back(t);
                // new titles are picked, as in the web app; ones that went are forgotten
                const std::string key = t.uid + "/" + t.id;
                if (!known_.count(key))
                {
                    known_.insert(key);
                    picked_.insert(key);
                }
            }
        }
        for (auto it = picked_.begin(); it != picked_.end();)
        {
            const bool there = std::any_of(titles_.begin(), titles_.end(),
                                           [&](const SaveTitle &t) { return t.uid + "/" + t.id == *it; });
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
        const std::string title = t != nullptr   ? (t->name.empty() ? t->id : t->name)
                                  : b != nullptr ? "Sicherung vom " + play_date(b->created, tz_)
                                                 : std::string("Spielstände");
        ui::text(list, fonts.display, ui::fit_label(paint, title, 32.0f, card.w - 60.0f), card.x + 30.0f, card.y + 66.0f,
                 32.0f, kInk);
        if (t != nullptr || b != nullptr)
            info_.draw(canvas);
        else
            paragraph(canvas,
                      "Sichert die Spielstände der Konsole verschlüsselt und unverändert, liest jede Kopie zurück und "
                      "prüft sie. Zurückspielen geht nur auf dieser Konsole; der Stand davor wird vorher gesichert.",
                      card.x + 30.0f, card.y + 120.0f, card.w - 60.0f, kMuted, 6);

        // the job, or what the next backup would be
        const float y = card.y + 420.0f;
        list.line(card.x + 30.0f, y, card.x + card.w - 30.0f, y, 1.0f, Color::rgb(0x3880ff, 0.18f));
        const SaveJob &j = saves_.job;
        if (j.active)
        {
            ui::text(list, fonts.semibold, job_title(j) + " läuft", card.x + 30.0f, y + 46.0f, 24.0f, kInk);
            ui::text(list, fonts.regular, j.phase + " · " + std::to_string(j.percent) + " %", card.x + 30.0f, y + 82.0f,
                     20.0f, kMuted);
            const Rect bar{card.x + 30.0f, y + 102.0f, card.w - 60.0f, 12.0f};
            list.rounded_rect(bar, 6.0f, Color::rgb(0x3880ff, 0.18f));
            list.rounded_rect({bar.x, bar.y, bar.w * static_cast<float>(std::clamp(j.percent, 0, 100)) / 100.0f, bar.h},
                              6.0f, kBlue);
            ui::text(list, fonts.regular,
                     ui::fit_label(paint,
                                   std::to_string(j.files_done) + " von " + std::to_string(j.files_total) +
                                       " Dateien · " + j.current,
                                   18.0f, card.w - 60.0f),
                     card.x + 30.0f, y + 146.0f, 18.0f, kMuted);
        }
        else if (j.finished_ago_s >= 0 && j.finished_ago_s < 900 && !j.kind.empty())
        {
            const bool ok = j.state == "done";
            ui::text(list, fonts.semibold, job_title(j) + (ok ? " fertig" : j.state == "cancelled" ? " abgebrochen" : " fehlgeschlagen"),
                     card.x + 30.0f, y + 46.0f, 24.0f, ok ? kGood : kWarning);
            std::string text = !j.error.empty() ? j.error : !j.note.empty() ? j.note : j.path;
            if (!j.undo.empty())
                text += " Der Stand davor liegt in " + j.undo + ".";
            paragraph(canvas, text, card.x + 30.0f, y + 84.0f, card.w - 60.0f, kMuted, 5);
        }
        else
        {
            ui::text(list, fonts.semibold, "Nächste Sicherung", card.x + 30.0f, y + 46.0f, 22.0f, kInk);
            paragraph(canvas, plan_text(), card.x + 30.0f, y + 84.0f, card.w - 60.0f, kMuted, 5);
        }
        if (saves_.unfinished > 0)
            paragraph(canvas,
                      std::to_string(saves_.unfinished) +
                          " unfertige Sicherung(en) liegen auf den Laufwerken; entfernen am PC oder per FTP.",
                      card.x + 30.0f, kBottom - 80.0f, card.w - 60.0f, kWarning, 2);
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        menu_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        if (const SaveTitle *t = focused_title())
        {
            out[n++] = {ui::Button::cross, picked_.count(t->uid + "/" + t->id) ? "Abwählen" : "Auswählen"};
            out[n++] = {ui::Button::square, "Aktionen"};
        }
        else if (focused_backup() != nullptr)
            out[n++] = {ui::Button::cross, "Aktionen"};
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    static std::string job_title(const SaveJob &j)
    {
        return j.kind == "verify" ? "Prüfung" : j.kind == "restore" ? "Zurückspielen" : j.kind == "delete" ? "Löschen" : "Sicherung";
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
        return std::to_string(picked_.size()) + (picked_.size() == 1 ? " Spiel" : " Spiele") + " ausgewählt, zusammen " +
               format_bytes(picked_bytes()) + ", nach " + d->label + " (" + format_bytes(d->free_bytes) +
               " frei). Mehr → Ausgewählte sichern.";
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
        return i >= 0 && i < static_cast<int>(titles_.size()) ? &titles_[static_cast<std::size_t>(i)] : nullptr;
    }

    const SaveBackup *focused_backup() const
    {
        const int i = focused_tag() - kBackupTag;
        return i >= 0 && i < static_cast<int>(saves_.backups.size()) ? &saves_.backups[static_cast<std::size_t>(i)]
                                                                       : nullptr;
    }

    // ---------------------------------------------------------------- menus

    static ui::MenuItem item(const std::string &label, int tag, bool disabled = false, bool danger = false)
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
        items.push_back(item(d != nullptr ? "Löschen (vorher nach " + d->label + " sichern)" : "Löschen (ohne Sicherung)",
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

    void open_page_menu(ui::Feedback &feedback)
    {
        const SaveDrive *d = target();
        std::vector<ui::MenuItem> items;
        items.push_back(item("Ausgewählte sichern (" + std::to_string(picked_.size()) + ")", kBackup, !blocked().empty()));
        items.push_back(item("Ziel: " + (d != nullptr ? d->label : std::string("keins")), kTarget, saves_.drives.size() < 2));
        items.push_back(item("Alle auswählen", kAll, titles_.empty()));
        items.push_back(item("Keine auswählen", kNone, picked_.empty()));
        if (saves_.job.active && saves_.job.can_cancel)
            items.push_back(item(job_title(saves_.job) + " abbrechen", kCancel, false, true));
        items.push_back(item("Neu laden", kReload));
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void run(int tag, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        if (tag >= kRestore)
        {
            const std::size_t i = static_cast<std::size_t>(tag - kRestore);
            if (i < menu_backup_.titles.size())
            {
                const SaveTitle &t = menu_backup_.titles[i];
                source().saves_post("restore", "{\"path\":" + json_text(menu_backup_.path) + ",\"user\":" +
                                                   json_text(t.uid) + ",\"title\":" + json_text(t.id) +
                                                   ",\"confirm\":true}");
            }
            return;
        }
        switch (tag)
        {
        case kDelete:
            if (target() != nullptr)
                source().saves_post("delete", "{\"uid\":" + json_text(menu_title_.uid) + ",\"id\":" +
                                                  json_text(menu_title_.id) + ",\"mount\":" + json_text(target_) + "}");
            else
                source().saves_post("delete", "{\"uid\":" + json_text(menu_title_.uid) + ",\"id\":" +
                                                  json_text(menu_title_.id) + ",\"no_backup\":true}");
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
            source().saves_post("backup",
                                "{\"target\":" + json_text(target_) + ",\"users\":[" + uj + "],\"titles\":[" + titles + "]}");
            break;
        }
        case kTarget:
        {
            // the next drive, round
            for (std::size_t i = 0; i < saves_.drives.size(); ++i)
            {
                if (saves_.drives[i].mount == target_)
                {
                    target_ = saves_.drives[(i + 1) % saves_.drives.size()].mount;
                    break;
                }
            }
            break;
        }
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
            out.push_back(header(head + " · " + std::to_string(u.titles.size()) + " TITEL · " + format_bytes(u.bytes)));
            for (const SaveTitle &t : u.titles)
            {
                ui::ListItem item;
                item.title = t.name.empty() ? t.id : t.name;
                item.subtitle = t.id + " · " + t.platform + (t.installed ? "" : " · nicht installiert") + " · geändert " +
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
            item.title = play_date(b.created, tz_) + ", " + play_clock(b.created, tz_) + " Uhr";
            std::string users;
            for (const std::string &n : b.user_names)
                users += (users.empty() ? "" : ", ") + n;
            item.subtitle = b.label + " · " + std::to_string(b.titles.size()) + " Titel · " + users;
            item.value = format_bytes(b.bytes);
            item.badge = b.kind == "undo" ? "STAND VORHER" : !b.this_console ? "ANDERE KONSOLE" : "";
            item.tag = kBackupTag + static_cast<int>(i);
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
            rows.push_back({"Geändert", play_date(t->mtime, tz_) + ", " + play_clock(t->mtime, tz_) + " Uhr"});
            rows.push_back({"Installiert", t->installed ? "ja" : "nein"});
        }
        else if (const SaveBackup *b = focused_backup())
        {
            rows.push_back({"Laufwerk", b->label});
            rows.push_back({"Titel", std::to_string(b->titles.size())});
            rows.push_back({"Größe", format_bytes(b->bytes)});
            rows.push_back({"Art", b->kind == "undo" ? "Stand vor dem Zurückspielen" : "Sicherung"});
            rows.push_back({"Konsole", b->this_console ? "diese" : "eine andere (nur ansehen)"});
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
    Saves saves_;
    int tz_ = 0;
    std::vector<SaveTitle> titles_;
    std::set<std::string> picked_; // "uid/id"
    std::set<std::string> known_;
    std::string target_;
    SaveTitle menu_title_;
    SaveBackup menu_backup_;
};

} // namespace

std::unique_ptr<Page> make_saves_page()
{
    return std::make_unique<SavesPage>();
}

} // namespace hui::ps5cc
