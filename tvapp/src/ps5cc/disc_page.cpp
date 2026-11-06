// ps5cc - The Disc page (Spiele → Disc): the drive, dumping disc games, the dump queue, the dumps
// and images.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's Disc tab, in one list with a section each: the drive (✕ makes an image of a
// PS2-DVD or PS3 disc), the games installed from disc (✕ starts the game or, while it runs, dumps
// it; □ puts it into the queue), the queue (□ takes a game out), the dumps this tab made and the
// disc images (□ deletes). △ holds the rest: the drive on and off, automatic images, the queue,
// stopping a dump, the dump settings, a new search, the drive's events. The tab is only there when
// the console has a drive. A game's own queue settings stay in the web app. Nothing is asked twice
// (user, 27.10.2026).

#include "core/tween.hpp"
#include "ps5cc/covers.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

const Rect kPanel{200.0f, 92.0f, 1520.0f, 864.0f};
constexpr float kSaveDelay = 0.8f;

enum Section
{
    kDrive = 0,
    kGames = 1,
    kQueue = 2,
    kDumps = 3,
    kImages = 4,
};
constexpr int kSectionStep = 1000; // a row's tag: section * kSectionStep + index

enum MenuTag
{
    kDriveSwitch = 1,
    kAutoDump,
    kQueueGo,
    kQueueSkip,
    kQueueClear,
    kDumpStop,
    kSettings,
    kRescan,
    kMeasure,
    kEvents,
};

enum Dialog
{
    kNone,
    kSettingsDialog,
    kEventsDialog,
};

enum Row
{
    kSetTarget = 1,
    kSetSubdir,
    kSetTitles,
    kSetDlc,
    kSetVerify,
    kSetSplit,
    kSetDecrypt,
    kSetFself,
    kSetBackport,
    kSetBp4,
    kSetBp5,
    kSetReplace,
    kSetDelay,
};

constexpr int kSplitValues[] = {3, 1, 2, 0};

const char *queue_state(const std::string &s)
{
    if (s == "pending")
        return "wartet";
    if (s == "waiting_disc")
        return "wartet auf die Disc";
    if (s == "launching")
        return "wird gestartet";
    if (s == "settling")
        return "lädt";
    if (s == "dumping")
        return "wird gedumpt";
    if (s == "done")
        return "fertig";
    if (s == "failed")
        return "gescheitert";
    if (s == "skipped")
        return "übersprungen";
    return "";
}

std::string json_text(const std::string &s)
{
    std::string out = "\"";
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out + "\"";
}

std::string eta_text(int seconds)
{
    if (seconds <= 0)
        return {};
    if (seconds < 60)
        return "noch " + std::to_string(seconds) + " s";
    return "noch " + std::to_string((seconds + 30) / 60) + " min";
}

class DiscPage final : public Page
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
        list_.style.header_size = 18.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.badge_column = true;
        list_.style.value_width = 200.0f; // the badges line up
        list_.style.leading_width = 76.0f;
        list_.leading =
            [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int, float)
        {
            if (item.header || item.tag < 0)
                return;
            const float side = std::min(56.0f, row.h - 20.0f);
            draw_mark(canvas, {row.x + 16.0f, row.y + (row.h - side) * 0.5f, side, side}, item.tag);
        };
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 170.0f, kRightW - 60.0f, 290.0f});

        bar_.style.theme = theme;
        bar_.style.height = 14.0f;
        bar_.style.sheen = true;
        bar_.style.text_size = 20.0f;
        bar_.set_bounds({kRightX + 30.0f, kTop + 532.0f, kRightW - 60.0f, 48.0f});

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 560.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});
        ask_.init(theme);

        form_.style.theme = theme;
        form_.style.panel = false;
        form_.style.on_page = false;
        form_.style.row_height = 58.0f;
        form_.style.header_height = 44.0f;
        form_.style.label_size = 23.0f;
        form_.style.value_size = 21.0f;
        form_.style.description_size = 19.0f;
        form_.style.control_width = 420.0f;
        form_.style.label_ratio = 0.5f;
        form_.style.on_text = "An";
        form_.style.off_text = "Aus";
        form_.set_bounds({kPanel.x + 24.0f, kPanel.y + 130.0f, 1080.0f, 650.0f});
    }

    void absorb(const Snapshot &data) override
    {
        disc_ = data.disc;
        dump_ = data.dump;
        dumps_ = data.dumps;
        games_.clear();
        for (const Game &g : data.games)
        {
            if (!g.disc.empty() && (g.platform == "PS5" || g.platform == "PS4"))
                games_.push_back(g);
        }
        rebuild();
        update_bar();
        if (dialog_ == kSettingsDialog && !settings_shown_ && dump_.settings.valid)
            build_settings();
    }

    void enter() override
    {
        list_.enter();
    }

    bool modal() const override
    {
        return menu_.is_open() || dialog_ != kNone || ask_.is_open();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (ask_.is_open())
        {
            // only "Löschen" in the question deletes (06.11.2026)
            if (ask_.handle(input, feedback) && !delete_route_.empty())
                source().disc_post(delete_route_, delete_json_);
            return;
        }
        if (menu_.is_open())
        {
            if (menu_.handle(input, feedback) == ui::Event::activated)
                run(menu_.items()[static_cast<std::size_t>(menu_.focus())].tag, feedback);
            return;
        }
        if (dialog_ != kNone)
        {
            handle_dialog(input, feedback);
            return;
        }
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
            refresh_info();
        if (event == ui::Event::activated)
        {
            if (!confirm(focused_tag()))
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            else
                feedback.play(audio::Cue::select);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            if (!square(focused_tag(), feedback))
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            else
                feedback.play(audio::Cue::select);
            return;
        }
        if (input.is_pressed(Action::north))
            open_menu(feedback);
    }

    void update(float dt) override
    {
        list_.update(dt);
        info_.update(dt);
        bar_.update(dt);
        menu_.update(dt);
        ask_.update(dt);
        form_.update(dt);
        shown_.target = dialog_ != kNone ? 1.0f : 0.0f;
        shown_.update(dt, 14.0f);
        if (save_in_ > 0.0f)
        {
            save_in_ -= dt;
            if (save_in_ <= 0.0f)
                save_settings();
        }
        if (const Game *g = focused_game())
            covers().prefer(g->id);
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);
        const int tag = focused_tag();
        float title_w = card.w - 60.0f;
        if (tag >= 0)
        {
            draw_mark(canvas, {card.x + card.w - 30.0f - 96.0f, card.y + 30.0f, 96.0f, 96.0f}, tag);
            title_w -= 116.0f;
        }
        float y = card.y + 66.0f;
        int lines = 0;
        for (const std::string &line : fonts.display.font->wrap(card_title(tag), 32.0f, title_w))
        {
            if (++lines > 2)
                break;
            ui::text(list, fonts.display, line, card.x + 30.0f, y, 32.0f, kInk);
            y += 42.0f;
        }
        info_.draw(canvas);
        const std::string note = card_note(tag);
        if (!note.empty())
            paragraph(canvas, note, card.x + 30.0f, kTop + 470.0f, card.w - 60.0f, kMuted, 2);

        // The bottom of the card: the dump (or the disc image) that runs or ran last, and where
        // dumps go.
        const float line_y = kTop + 500.0f;
        list.line(card.x + 30.0f, line_y, card.x + card.w - 30.0f, line_y, 1.0f,
                  Color::rgb(0x3880ff, 0.18f));
        if (bar_shown_)
            bar_.draw(canvas);
        else
            ui::text(list, fonts.regular, "Kein Dump läuft.", card.x + 30.0f, kTop + 560.0f, 21.0f,
                     kMuted);
        if (!bar_note_.empty())
            paragraph(
                canvas, bar_note_, card.x + 30.0f, kTop + 620.0f, card.w - 60.0f,
                dump_.job.state == "failed" || disc_.job_state == "failed" ? kCritical : kMuted, 3);
        ui::text(list, fonts.semibold, ui::fit_label(paint, target_line(), 20.0f, card.w - 60.0f),
                 card.x + 30.0f, kBottom - 40.0f, 20.0f, kMuted);
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        menu_.draw(canvas);
        ask_.draw(canvas);
        const float amount = shown_.value;
        if (amount <= 0.01f)
            return;
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, kNavy.with_alpha(0.78f * amount));
        list.push_opacity(amount);
        const float rise = 24.0f * (1.0f - amount);
        const Rect panel{kPanel.x, kPanel.y + rise, kPanel.w, kPanel.h};
        ui::Painter paint(list, fonts, theme_);
        solid_floor(list, panel, theme_.radius_card);
        solid_floor(list, panel, theme_.radius_card);
        paint.panel(panel);
        const bool events = dialog_ == kEventsDialog;
        ui::text(list, fonts.display, events ? "Ereignisse am Laufwerk" : "Dump-Einstellungen",
                 panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
        ui::text(list, fonts.regular,
                 events ? "die letzten, das neueste zuerst"
                        : "für PS5- und PS4-Spiele · gelten für den nächsten Dump",
                 panel.x + 40.0f, panel.y + 104.0f, 21.0f, kMuted);
        ui::HintLayout layout;
        layout.size = 34.0f;
        layout.text_size = 21.0f;
        layout.cy = panel.y + panel.h - 42.0f;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        if (events)
        {
            float y = panel.y + 170.0f;
            for (std::size_t i = 0; i < disc_.events.size() && i < 20; ++i)
            {
                ui::text(list, fonts.regular,
                         ui::fit_label(paint, disc_.events[i], 20.0f, panel.w - 80.0f),
                         panel.x + 40.0f, y, 20.0f, kInk.with_alpha(0.85f));
                y += 30.0f;
            }
            if (disc_.events.empty())
                ui::text(list, fonts.regular, "Noch nichts.", panel.x + 40.0f, y, 20.0f, kMuted);
            const ui::Hint hints[] = {{ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 1, panel.x + 40.0f, false, layout);
        }
        else
        {
            list.rounded_rect({panel.x + 1120.0f, panel.y + 140.0f, 1.5f, 630.0f}, 0.0f,
                              Color::rgb(0x3880ff, 0.18f));
            form_.draw(canvas);
            paragraph(canvas,
                      "Spielbar machen: Entschlüsseln und FSELF müssen beide an sein, sonst zeigt "
                      "die Konsole den Dump "
                      "nicht, ShadowMountPlus bindet ihn nicht ein und er lässt sich nicht "
                      "spielen.\n\nDer Backport ist "
                      "nur für ältere Firmware nötig und kann den Dump beschädigen.\n\nDen Ordner "
                      "auf dem Ziel änderst "
                      "du in der Browser-App.",
                      panel.x + 1150.0f, panel.y + 160.0f, panel.w - 1190.0f, kMuted, 18);
            const ui::Hint hints[] = {{ui::Button::cross, "Ändern"},
                                      {ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 2, panel.x + 40.0f, false, layout);
        }
        list.pop_opacity();
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        const int tag = focused_tag();
        const std::string cross = cross_label(tag);
        const std::string sq = square_label(tag);
        if (!cross.empty())
        {
            cross_hint_ = cross;
            out[n++] = {ui::Button::cross, cross_hint_.c_str()};
        }
        if (!sq.empty())
        {
            square_hint_ = sq;
            out[n++] = {ui::Button::square, square_hint_.c_str()};
        }
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    static int section_of(int tag)
    {
        return tag < 0 ? -1 : tag / kSectionStep;
    }
    static std::size_t index_of(int tag)
    {
        return static_cast<std::size_t>(tag % kSectionStep);
    }

    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1;
        const ui::ListItem &item = list_.items()[static_cast<std::size_t>(i)];
        return item.header ? -1 : item.tag;
    }

    const Game *focused_game() const
    {
        const int tag = focused_tag();
        return section_of(tag) == kGames && index_of(tag) < games_.size() ? &games_[index_of(tag)]
                                                                          : nullptr;
    }

    bool queued(const std::string &id) const
    {
        return std::any_of(dump_.queue.items.begin(), dump_.queue.items.end(),
                           [&](const DumpQueueItem &q) { return q.id == id; });
    }

    bool dump_busy() const
    {
        return dump_.job.active || dump_.queue.active;
    }

    // ---------------------------------------------------------------- the buttons of a row

    std::string cross_label(int tag) const
    {
        switch (section_of(tag))
        {
        case kDrive:
            if (!disc_.enabled)
                return "Laufwerk einschalten";
            if (disc_.job_state == "running" || disc_.job_state == "verifying")
                return "Abbild abbrechen";
            return disc_.dumpable ? "Abbild erstellen" : "";
        case kGames:
        {
            const Game &g = games_[index_of(tag)];
            if (!g.dump_mounted)
                return g.can_launch ? "Starten" : "";
            if (dump_busy())
                return "";
            return g.dump == "done" ? "Ersetzen und dumpen" : "Dumpen";
        }
        default:
            return "";
        }
    }

    std::string square_label(int tag) const
    {
        switch (section_of(tag))
        {
        case kGames:
            return queued(games_[index_of(tag)].id) || dump_.queue.active ? ""
                                                                          : "In die Warteschlange";
        case kQueue:
            return dump_.queue.active ? "" : "Entfernen";
        case kDumps:
            return dumps_.dumps[index_of(tag)].state == "running" ? "" : "Löschen";
        case kImages:
            return "Löschen";
        default:
            return "";
        }
    }

    bool confirm(int tag)
    {
        if (cross_label(tag).empty())
            return false;
        switch (section_of(tag))
        {
        case kDrive:
            if (!disc_.enabled)
                source().disc_post("disc", "{\"action\":\"enable\"}");
            else if (disc_.job_state == "running" || disc_.job_state == "verifying")
                source().disc_post("disc", "{\"action\":\"cancel\"}");
            else
                source().disc_post("disc", "{\"action\":\"dump\"}");
            return true;
        case kGames:
        {
            const Game &g = games_[index_of(tag)];
            if (!g.dump_mounted)
                source().launch(g.id);
            else
                source().disc_post("dump", "{\"action\":\"start\",\"id\":" + json_text(g.id) +
                                               (g.dump == "done" ? ",\"overwrite\":true}" : "}"));
            return true;
        }
        default:
            return false;
        }
    }

    bool square(int tag, ui::Feedback &feedback)
    {
        if (square_label(tag).empty())
            return false;
        const std::size_t i = index_of(tag);
        switch (section_of(tag))
        {
        case kGames:
            source().disc_post("dump", "{\"action\":\"queue_add\",\"id\":" +
                                           json_text(games_[i].id) + ",\"disc\":true}");
            return true;
        case kQueue:
            source().disc_post("dump", "{\"action\":\"queue_remove\",\"id\":" +
                                           json_text(dump_.queue.items[i].id) + "}");
            return true;
        case kDumps:
        {
            // asks first (06.11.2026): the dump's folder and where it lies
            const DumpEntry &d = dumps_.dumps[i];
            delete_route_ = "dumps/delete";
            delete_json_ = "{\"mount\":" + json_text(d.mount) + ",\"dir\":" + json_text(d.dir) +
                           ",\"folder\":" + json_text(d.folder) + "}";
            const std::string what = !d.title.empty() && d.title != d.folder ? d.title
                                     : d.bytes > 0 ? format_bytes(d.bytes)
                                                   : std::string();
            const std::vector<std::string> names{DeleteAsk::line(d.folder, what)};
            ask_.open("Dump löschen?", names,
                      {"Ordner: " + d.mount + (d.dir.empty() ? "" : "/" + d.dir)}, feedback);
            return true;
        }
        case kImages:
        {
            const DiscImage &img = disc_.images[i];
            delete_route_ = "disc";
            delete_json_ = "{\"action\":\"delete\",\"name\":" + json_text(img.name) + "}";
            ask_.open("Abbild löschen?",
                      {DeleteAsk::line(img.name, format_bytes(img.size))}, {}, feedback);
            return true;
        }
        default:
            return false;
        }
    }

    // ---------------------------------------------------------------- the card

    void draw_mark(ui::Canvas &canvas, const Rect &box, int tag) const
    {
        if (const Game *g = section_of(tag) == kGames && index_of(tag) < games_.size()
                                ? &games_[index_of(tag)]
                                : nullptr)
        {
            const CoverShown pic = covers().texture_of(g->id);
            if (pic.texture != 0)
            {
                canvas.list.image(pic.texture, box, gfx::kFullUv,
                                  Color{1.0f, 1.0f, 1.0f, pic.alpha}, box.w * 0.22f);
                return;
            }
        }
        const int s = section_of(tag);
        const Color tint = s == kDrive   ? Color::rgb(0x3880ff)
                           : s == kGames ? Color::rgb(0x2f5fb3)
                           : s == kQueue ? Color::rgb(0x7f56d9)
                           : s == kDumps ? Color::rgb(0x168f68)
                                         : Color::rgb(0xd9822b);
        const char *mark = s == kDrive   ? "BD"
                           : s == kGames ? "DISC"
                           : s == kQueue ? "WS"
                           : s == kDumps ? "DMP"
                                         : "ISO";
        canvas.list.rounded_rect(box, box.w * 0.22f, tint.with_alpha(0.85f));
        const float size = box.w * (std::string(mark).size() > 3 ? 0.24f : 0.3f);
        ui::text(canvas.list, canvas.fonts.semibold, mark, box.x + box.w * 0.5f,
                 box.y + box.h * 0.5f + size * 0.36f, size, Color::rgb(0xffffff),
                 gfx::Align::center);
    }

    std::string card_title(int tag) const
    {
        const std::size_t i = index_of(tag);
        switch (section_of(tag))
        {
        case kDrive:
            return disc_.present ? (disc_.name.empty() ? disc_.type : disc_.name) : "Laufwerk";
        case kGames:
            return games_[i].name;
        case kQueue:
            return dump_.queue.items[i].name;
        case kDumps:
            return dumps_.dumps[i].title.empty() ? dumps_.dumps[i].folder : dumps_.dumps[i].title;
        case kImages:
            return disc_.images[i].name;
        default:
            return "Disc";
        }
    }

    std::string card_note(int tag) const
    {
        switch (section_of(tag))
        {
        case kDrive:
            if (!disc_.enabled)
                return "Ausgeschaltet: Die App fragt das Laufwerk nicht.";
            if (disc_.present && !disc_.dumpable && disc_.has_title)
                return "PS4- und PS5-Discs werden erkannt, aber nicht kopiert.";
            return disc_.title_state == "generic"
                       ? "Die Titelkennung ließ sich nicht lesen (GENERIC)."
                       : "";
        case kGames:
        {
            const Game &g = games_[index_of(tag)];
            if (g.dump_mounted)
                return "Das Spiel läuft: Der Dump kann beginnen, das Spiel läuft dabei weiter.";
            if (g.disc == "out")
                return "Die Disc liegt nicht im Laufwerk. Zum Starten und Dumpen muss sie drin "
                       "liegen.";
            return "Zum Dumpen muss das Spiel laufen: Die X-Taste startet es (die Vollbild-App "
                   "schließt sich dafür).";
        }
        case kQueue:
            return dump_.queue.items[index_of(tag)].message;
        default:
            return "";
        }
    }

    std::string target_line() const
    {
        for (const DumpTarget &t : dump_.targets)
        {
            if (t.mount == dump_.settings.target)
                return "Ziel der Dumps: " + t.label + " · " + format_bytes(t.free_bytes) + " frei";
        }
        return "Ziel der Dumps: noch keins gewählt (Mehr → Dump-Einstellungen)";
    }

    void update_bar()
    {
        const DumpJob &j = dump_.job;
        bar_note_.clear();
        if (j.state != "idle" && !j.state.empty())
        {
            bar_shown_ = true;
            const double work = j.done_bytes + (j.verify ? j.checked_bytes : 0.0);
            const double all = j.total_bytes * (j.verify ? 2.0 : 1.0);
            const float value = j.state == "done" ? 1.0f
                                : all > 0.0       ? static_cast<float>(std::min(1.0, work / all))
                                                  : 0.0f;
            std::string label = j.name + " · ";
            if (j.active)
                label += (j.stage.empty() ? "läuft" : j.stage) +
                         (eta_text(j.eta_s).empty() ? "" : " · " + eta_text(j.eta_s));
            else
                label += j.state == "done"        ? "Dump fertig"
                         : j.state == "cancelled" ? "abgebrochen"
                                                  : "gescheitert";
            bar_.label = label;
            bar_.style.percent = j.active;
            bar_.style.status = j.state == "failed" ? ui::Status::danger
                                : j.active          ? ui::Status::accent
                                                    : ui::Status::success;
            bar_.set_value(value);
            bar_note_ =
                !j.error.empty() ? j.error
                : j.active
                    ? format_bytes(static_cast<std::uint64_t>(j.done_bytes)) + " von " +
                          format_bytes(static_cast<std::uint64_t>(j.total_bytes)) +
                          (j.rate > 0.0
                               ? " · " + format_bytes(static_cast<std::uint64_t>(j.rate)) + "/s"
                               : std::string())
                    : j.note;
            return;
        }
        if (disc_.job_state != "idle" && !disc_.job_state.empty())
        {
            bar_shown_ = true;
            const bool running = disc_.job_state == "running" || disc_.job_state == "verifying";
            bar_.label = disc_.job_name + " · " +
                         (disc_.job_state == "verifying" ? std::string("wird geprüft")
                          : running ? "Abbild" + (eta_text(disc_.job_eta_s).empty()
                                                      ? std::string()
                                                      : " · " + eta_text(disc_.job_eta_s))
                          : disc_.job_state == "done"      ? std::string("fertig")
                          : disc_.job_state == "cancelled" ? std::string("abgebrochen")
                                                           : std::string("gescheitert"));
            bar_.style.percent = running;
            bar_.style.status = disc_.job_state == "failed" ? ui::Status::danger
                                : running                   ? ui::Status::accent
                                                            : ui::Status::success;
            bar_.set_value(disc_.job_state == "done" ? 1.0f
                           : disc_.job_total > 0.0
                               ? static_cast<float>(std::min(1.0, disc_.job_done / disc_.job_total))
                               : 0.0f);
            bar_note_ = !disc_.job_error.empty() ? disc_.job_error : disc_.job_note;
            return;
        }
        bar_shown_ = false;
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

    // ---------------------------------------------------------------- △

    void open_menu(ui::Feedback &feedback)
    {
        std::vector<ui::MenuItem> items;
        const auto add = [&](const char *label, int tag, bool disabled = false, bool danger = false)
        {
            ui::MenuItem m;
            m.label = label;
            m.tag = tag;
            m.disabled = disabled;
            m.danger = danger;
            items.push_back(m);
        };
        add(disc_.enabled ? "Laufwerk ausschalten" : "Laufwerk einschalten", kDriveSwitch,
            disc_.job_state == "running" || disc_.job_state == "verifying");
        add(disc_.auto_dump ? "Neue Disc nicht mehr automatisch sichern"
                            : "Neue Disc automatisch sichern",
            kAutoDump, !disc_.enabled);
        ui::MenuItem line;
        line.separator = true;
        items.push_back(line);
        add(dump_.queue.active ? "Warteschlange stoppen" : "Warteschlange starten", kQueueGo,
            !dump_.queue.active && dump_.queue.items.empty());
        if (dump_.queue.active)
            add("Diesen Titel überspringen", kQueueSkip);
        else
            add("Warteschlange leeren", kQueueClear, dump_.queue.items.empty(), true);
        if (dump_.job.active)
            add("Dump stoppen", kDumpStop, false, true);
        items.push_back(line);
        add("Dump-Einstellungen", kSettings, !dump_.settings.valid);
        add("Dumps neu suchen", kRescan, dumps_.scanning);
        add("Dumps suchen und Größen nachmessen", kMeasure, dumps_.scanning);
        add("Ereignisse am Laufwerk", kEvents);
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void run(int tag, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        switch (tag)
        {
        case kDriveSwitch:
            source().disc_post("disc", disc_.enabled ? "{\"action\":\"disable\"}"
                                                     : "{\"action\":\"enable\"}");
            break;
        case kAutoDump:
            source().disc_post("disc", std::string("{\"action\":\"settings\",\"auto_dump\":") +
                                           (disc_.auto_dump ? "false}" : "true}"));
            break;
        case kQueueGo:
            source().disc_post("dump", dump_.queue.active
                                           ? "{\"action\":\"queue_stop\"}"
                                           : "{\"action\":\"queue_start\",\"confirm_close\":true}");
            break;
        case kQueueSkip:
            source().disc_post("dump", "{\"action\":\"queue_skip\"}");
            break;
        case kQueueClear:
            source().disc_post("dump", "{\"action\":\"queue_clear\"}");
            break;
        case kDumpStop:
            source().disc_post("dump", "{\"action\":\"cancel\"}");
            break;
        case kSettings:
            dialog_ = kSettingsDialog;
            settings_shown_ = false;
            if (dump_.settings.valid)
                build_settings();
            break;
        case kRescan:
            source().disc_post("dumps/scan", "{}");
            break;
        case kMeasure:
            source().disc_post("dumps/scan", "{\"measure\":true}");
            break;
        case kEvents:
            dialog_ = kEventsDialog;
            break;
        default:
            break;
        }
    }

    // ---------------------------------------------------------------- settings

    void build_settings()
    {
        const DumpSettings &s = dump_.settings;
        settings_shown_ = true;
        form_.clear();
        form_.add_header("SPEICHERORT");
        std::vector<std::string> targets;
        int target = 0;
        for (std::size_t i = 0; i < dump_.targets.size(); ++i)
        {
            const DumpTarget &t = dump_.targets[i];
            targets.push_back(t.label + " · " + format_bytes(t.free_bytes) + " frei");
            if (t.mount == s.target)
                target = static_cast<int>(i);
        }
        if (targets.empty())
            targets.push_back("kein Laufwerk");
        form_.add_choice(kSetTarget, "Ziel", targets, target);
        form_.add_value(kSetSubdir, "Ordner", s.subdir.empty() ? "–" : s.subdir);
        form_.add_toggle(kSetTitles, "Spielname im Ordnernamen", s.folder_titles);
        form_.add_header("WAS WIRD GEDUMPT");
        form_.add_toggle(kSetDlc, "DLC mitdumpen", s.dump_dlc);
        form_.add_toggle(kSetVerify, "Nach dem Schreiben prüfen", s.verify).description =
            "Sicherer, dauert aber doppelt so lange.";
        int split = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (kSplitValues[i] == s.ps4_split)
                split = i;
        }
        form_.add_choice(kSetSplit, "PS4: Spiel und Update",
                         {"getrennt", "nur Spiel", "nur Update", "in einem Ordner"}, split);
        form_.add_header("SPIELBAR MACHEN");
        form_.add_toggle(kSetDecrypt, "Programmdateien entschlüsseln", s.decrypt).description =
            s.decrypt_supported ? "Nötig zum Spielen." : "Diese Firmware kann nicht entschlüsseln.";
        form_.add_toggle(kSetFself, "Als FSELF signieren", s.fself && s.decrypt).description =
            "Nötig zum Spielen · braucht „Entschlüsseln“.";
        form_.add_header("FÜR FORTGESCHRITTENE");
        form_.add_toggle(kSetBackport, "SDK herunterpatchen (Backport)", s.backport && s.decrypt)
            .description = "Nur für ältere Firmware · kann den Dump beschädigen.";
        form_.add_choice(
            kSetBp4, "PS4-Ziel",
            {"SDK 5.05", "SDK 6.00", "SDK 7.00", "SDK 9.00 (Standard)", "SDK 10.00", "SDK 11.00"},
            std::clamp(s.ps4_backport_level - 1, 0, 5));
        form_.add_choice(kSetBp5, "PS5-Ziel",
                         {"SDK 1.00 (Standard)", "SDK 2.00", "SDK 3.00", "SDK 4.00", "SDK 5.00",
                          "SDK 6.00", "SDK 7.00", "SDK 8.00", "SDK 9.00", "SDK 10.00"},
                         std::clamp(s.ps5_backport_level - 1, 0, 9));
        form_.add_header("WARTESCHLANGE");
        form_.add_choice(kSetReplace, "Vorhandene Dumps", {"Überspringen", "Ersetzen"},
                         dump_.queue.replace ? 1 : 0);
        form_
            .add_slider(kSetDelay, "Ladezeit nach dem Start",
                        static_cast<float>(std::clamp(dump_.queue.delay, 5, 600)), 5.0f, 600.0f,
                        5.0f)
            .unit = " s";
        dependent_rows();
        form_.set_active(true);
        form_.focus_row(kSetTarget);
        form_.enter();
    }

    void dependent_rows()
    {
        const bool dec = form_.toggle_value(kSetDecrypt);
        form_.set_disabled(kSetFself, !dec);
        form_.set_disabled(kSetBackport, !dec);
        const bool bp = dec && form_.toggle_value(kSetBackport);
        form_.set_disabled(kSetBp4, !bp);
        form_.set_disabled(kSetBp5, !bp);
        form_.set_disabled(kSetReplace, dump_.queue.active);
        form_.set_disabled(kSetDelay, dump_.queue.active);
    }

    void handle_dialog(const InputFrame &input, ui::Feedback &feedback)
    {
        if (input.is_pressed(Action::back))
        {
            if (save_in_ > 0.0f)
                save_settings();
            dialog_ = kNone;
            feedback.play(audio::Cue::back);
            return;
        }
        if (dialog_ != kSettingsDialog || !settings_shown_)
            return;
        if (form_.handle(input, feedback) == ui::Event::changed)
        {
            const int id = form_.changed_id();
            dependent_rows();
            if (id == kSetReplace || id == kSetDelay)
                queue_dirty_ = true;
            else
                dump_dirty_ = true;
            save_in_ = kSaveDelay;
        }
    }

    // The dump settings as a whole, as the web page sends them; the queue's two in their own
    // request.
    void save_settings()
    {
        save_in_ = 0.0f;
        const auto flag = [&](int row) { return form_.toggle_value(row) ? "true" : "false"; };
        if (dump_dirty_)
        {
            dump_dirty_ = false;
            const int t = form_.choice_index(kSetTarget);
            const std::string target = t >= 0 && t < static_cast<int>(dump_.targets.size())
                                           ? dump_.targets[static_cast<std::size_t>(t)].mount
                                           : dump_.settings.target;
            const bool dec = form_.toggle_value(kSetDecrypt);
            source().disc_post(
                "dump",
                "{\"action\":\"settings\",\"target\":" + json_text(target) +
                    ",\"subdir\":" + json_text(dump_.settings.subdir) +
                    ",\"folder_titles\":" + flag(kSetTitles) + ",\"dump_dlc\":" + flag(kSetDlc) +
                    ",\"verify\":" + flag(kSetVerify) + ",\"ps4_split\":" +
                    std::to_string(kSplitValues[std::clamp(form_.choice_index(kSetSplit), 0, 3)]) +
                    ",\"decrypt\":" + (dec ? "true" : "false") + ",\"fself\":" +
                    (dec && form_.toggle_value(kSetFself) ? "true" : "false") + ",\"backport\":" +
                    (dec && form_.toggle_value(kSetBackport) ? "true" : "false") +
                    ",\"ps4_backport_level\":" + std::to_string(form_.choice_index(kSetBp4) + 1) +
                    ",\"ps5_backport_level\":" + std::to_string(form_.choice_index(kSetBp5) + 1) +
                    "}");
        }
        if (queue_dirty_)
        {
            queue_dirty_ = false;
            source().disc_post(
                "dump",
                std::string("{\"action\":\"queue_settings\",\"replace\":") +
                    (form_.choice_index(kSetReplace) == 1 ? "true" : "false") + ",\"delay\":" +
                    std::to_string(static_cast<int>(form_.slider_value(kSetDelay) + 0.5f)) + "}");
        }
    }

    // ---------------------------------------------------------------- the list

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
        out.push_back(header("LAUFWERK"));
        {
            ui::ListItem d;
            d.tag = kDrive * kSectionStep;
            if (!disc_.loaded)
                d.title = "Wird gelesen …";
            else if (!disc_.enabled)
            {
                d.title = "Laufwerk ausgeschaltet";
                d.subtitle = "Die X-Taste schaltet es ein; dann fragt die App alle anderthalb "
                             "Sekunden nach der Disc.";
            }
            else if (disc_.drive != "ok")
                d.title = disc_.drive == "none" ? "Kein Laufwerk gefunden" : "Fragt das Laufwerk …";
            else if (!disc_.present)
                d.title = "Keine Disc im Laufwerk";
            else
            {
                d.title = disc_.name.empty() ? disc_.type : disc_.name;
                d.subtitle = disc_.type + (disc_.serial.empty() ? "" : " · " + disc_.serial) +
                             (disc_.image_bytes > 0 ? " · " + format_bytes(disc_.image_bytes) : "");
                d.badge = disc_.dumpable ? "BEREIT" : "ERKANNT";
            }
            if (disc_.auto_dump && disc_.enabled)
                d.value = "sichert automatisch";
            out.push_back(d);
        }
        out.push_back(header("DISC-SPIELE DUMPEN"));
        for (std::size_t i = 0; i < games_.size(); ++i)
        {
            const Game &g = games_[i];
            ui::ListItem item;
            item.title = g.name;
            item.subtitle =
                g.id + " · " + g.platform + (g.disc == "out" ? " · Disc nicht im Laufwerk" : "");
            item.value = g.size_bytes > 0 ? format_bytes(g.size_bytes) : "";
            item.badge = g.dump_mounted     ? "LÄUFT"
                         : g.dump == "done" ? "GEDUMPT"
                         : queued(g.id)     ? "WARTET"
                                            : "";
            item.tag = kGames * kSectionStep + static_cast<int>(i);
            out.push_back(item);
        }
        if (games_.empty())
            out.push_back(note(
                "Keine Disc-Spiele installiert",
                "Eine Disc muss erst installiert sein; die Konsole kopiert sie beim Einlegen."));
        const DumpQueue &q = dump_.queue;
        out.push_back(header(q.active ? "WARTESCHLANGE · LÄUFT" : "WARTESCHLANGE"));
        for (std::size_t i = 0; i < q.items.size(); ++i)
        {
            const DumpQueueItem &it = q.items[i];
            ui::ListItem item;
            item.title = std::to_string(i + 1) + ". " + it.name;
            std::string sub = it.id + " · " + queue_state(it.state);
            if (q.active && static_cast<int>(i) == q.current && q.wait > 0)
                sub += " · noch " + std::to_string(q.wait) + " s";
            if (it.disc_out)
                sub += " · Disc raus";
            item.subtitle = sub;
            item.badge = it.custom ? "EIGENE" : it.dumped ? "GEDUMPT" : "";
            item.tag = kQueue * kSectionStep + static_cast<int>(i);
            out.push_back(item);
        }
        if (q.items.empty())
            out.push_back(note(q.interrupted ? "Unterbrochen" : "Leer",
                               q.interrupted
                                   ? "Die App wurde neu gestartet, die Liste ist weg."
                                   : "Die Quadrat-Taste an einem Disc-Spiel legt es hinein."));
        out.push_back(
            header(dumps_.scanning ? "DUMPS · DIE SUCHE LÄUFT …" : "DUMPS AUS DEM DISC-BEREICH"));
        for (std::size_t i = 0; i < dumps_.dumps.size(); ++i)
        {
            const DumpEntry &d = dumps_.dumps[i];
            ui::ListItem item;
            item.title = d.title.empty() ? d.folder : d.title;
            std::string sub = d.folder + " · " + d.mount;
            if (d.decrypt)
                sub += " · entschlüsselt";
            if (d.fself)
                sub += " · FSELF";
            if (d.backport)
                sub += " · Backport";
            item.subtitle = sub;
            item.value = d.bytes > 0 ? format_bytes(d.bytes) : "";
            item.badge = d.state == "done"      ? "FERTIG"
                         : d.state == "running" ? "LÄUFT"
                         : d.state.empty()      ? ""
                                                : "FEHLER";
            item.tag = kDumps * kSectionStep + static_cast<int>(i);
            out.push_back(item);
        }
        if (dumps_.dumps.empty())
            out.push_back(note(dumps_.ever ? "Noch keine Dumps" : "Noch nicht gesucht",
                               "Hier stehen nur Dumps, die in diesem Reiter entstanden sind."));
        if (!disc_.images.empty())
        {
            out.push_back(header("ABBILDER"));
            for (std::size_t i = 0; i < disc_.images.size(); ++i)
            {
                const DiscImage &d = disc_.images[i];
                ui::ListItem item;
                item.title = d.name;
                item.subtitle = (d.serial.empty() ? "" : d.serial + " · ") +
                                (d.crc32.empty() ? "" : "CRC-32 " + d.crc32);
                item.value = format_bytes(d.size);
                item.badge = d.verified ? "REDUMP" : "";
                item.tag = kImages * kSectionStep + static_cast<int>(i);
                out.push_back(item);
            }
        }
        return out;
    }

    // Rebuilt only when what the list shows changed: a refill restarts its animation.
    void rebuild()
    {
        std::vector<ui::ListItem> next = items();
        std::string key;
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge +
                   std::to_string(i.tag);
        if (key == key_)
        {
            refresh_info();
            return;
        }
        key_ = std::move(key);
        const int keep = list_.focus();
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
        const int tag = focused_tag();
        const std::size_t i = index_of(tag);
        switch (section_of(tag))
        {
        case kDrive:
            rows.push_back({"Laufwerk", !disc_.enabled        ? "ausgeschaltet"
                                        : disc_.drive == "ok" ? "bereit"
                                                              : disc_.drive});
            if (disc_.present)
            {
                rows.push_back({"Disc", disc_.type});
                rows.push_back({"Titelkennung", disc_.serial.empty() ? "–" : disc_.serial});
                rows.push_back(
                    {"Größe", disc_.image_bytes > 0 ? format_bytes(disc_.image_bytes) : "–"});
            }
            rows.push_back({"Redump-Liste", disc_.redump ? "wird benutzt" : "keine"});
            rows.push_back({"Abbilder in", disc_.dir});
            break;
        case kGames:
        {
            const Game &g = games_[i];
            rows.push_back({"Titel-ID", g.id});
            rows.push_back({"Plattform", g.platform});
            rows.push_back({"Disc", g.disc == "in" ? "im Laufwerk" : "nicht im Laufwerk"});
            rows.push_back({"Zustand", g.dump_mounted ? "läuft" : "läuft nicht"});
            rows.push_back({"Dump", g.dump == "done"         ? "fertig vorhanden"
                                    : g.dump == "unfinished" ? "unfertig"
                                                             : "keiner"});
            break;
        }
        case kQueue:
        {
            const DumpQueueItem &it = dump_.queue.items[i];
            rows.push_back({"Titel-ID", it.id});
            rows.push_back({"Zustand", queue_state(it.state)});
            rows.push_back({"Einstellungen", it.custom ? "eigene" : "wie die Dump-Einstellungen"});
            break;
        }
        case kDumps:
        {
            const DumpEntry &d = dumps_.dumps[i];
            rows.push_back({"Ordner", d.folder});
            rows.push_back({"Ort", d.mount + (d.dir.empty() ? "" : "/" + d.dir)});
            rows.push_back({"Art", d.kind == "dlc"     ? "DLC"
                                   : d.kind == "patch" ? "Patch"
                                                       : "Spiel"});
            rows.push_back({"Größe", d.bytes > 0 ? format_bytes(d.bytes) : "–"});
            rows.push_back({"Spielbar", d.decrypt && d.fself ? "ja (entschlüsselt, FSELF)"
                                                             : "nein (nur Sicherung)"});
            break;
        }
        case kImages:
        {
            const DiscImage &d = disc_.images[i];
            rows.push_back({"Titelkennung", d.serial.empty() ? "–" : d.serial});
            rows.push_back({"Größe", format_bytes(d.size)});
            rows.push_back({"CRC-32", d.crc32.empty() ? "–" : d.crc32});
            rows.push_back({"Redump", d.verified ? "geprüft" : "nicht geprüft"});
            break;
        }
        default:
            break;
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
    ui::ProgressBar bar_;
    ui::Menu menu_;
    DeleteAsk ask_;                // asked before a dump or a disc image is deleted
    std::string delete_route_;     // what the question is about: the route ...
    std::string delete_json_;      // ... and its body
    ui::Form form_;
    tween::Spring shown_;
    std::string key_;
    std::string info_key_ = "\x01";
    bool bar_shown_ = false;
    std::string bar_note_;
    mutable std::string cross_hint_; // the hint row keeps a pointer to the text until it is drawn
    mutable std::string square_hint_;

    Dialog dialog_ = kNone;
    bool settings_shown_ = false;
    bool dump_dirty_ = false;
    bool queue_dirty_ = false;
    float save_in_ = 0.0f;

    DiscState disc_;
    DumpState dump_;
    DumpList dumps_;
    std::vector<Game> games_;
};

} // namespace

std::unique_ptr<Page> make_disc_page()
{
    return std::make_unique<DiscPage>();
}

} // namespace hui::ps5cc
