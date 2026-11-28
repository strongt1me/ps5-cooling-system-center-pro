// ps5cc - The KrakenMount page (Spiele → KrakenMount): the games of Kraken Mount.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's tab of the same name: Kraken Mount (quer3q) is a program of its own that the app
// carries, starts and stops (src/kraken.c); it registers (F)PKG games and game folders on the home
// screen without installing them. The list is its own list. Square opens what one game allows
// (build it again, delete it after a question that names it), triangle the rest (start, stop,
// search, build all, a newer release from GitHub, its log). Starting and stopping fire at once,
// nothing is asked twice; only deleting asks first (user, 06.11.2026).

#include "core/tween.hpp"
#include "ps5cc/covers.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

const Rect kPanel{200.0f, 92.0f, 1520.0f, 864.0f};

enum MenuTag
{
    // the game's (square)
    kGameRebuild = 1,
    kGameDelete,
    kGamePlaygo,
    // the page's (triangle)
    kPageSwitch = 20,
    kPageScan,
    kPageRebuildAll,
    kPageFetch,
    kPageLog,
};

struct StateText
{
    const char *text;
    const char *badge;
    Color color;
};

StateText state_of(const KrakenGame &g)
{
    if (g.running)
        return {"Läuft", "LÄUFT", kGood};
    if (g.status == "ready")
        return {"Bereit", "BEREIT", kGood};
    if (g.status == "pending")
        return {"Wartet", "WARTET", kWarning};
    if (g.status == "checking")
        return {"Wird geprüft", "PRÜFT", kWarning};
    if (g.status == "building")
        return {"Wird aufgebaut", "BAUT", kWarning};
    if (g.status == "error")
        return {"Fehlgeschlagen", "FEHLER", kCritical};
    if (g.status == "duplicate")
        return {"Liegt schon an anderer Stelle", "DOPPELT", kMuted};
    return {"", "", kMuted};
}

std::string kind_of(const KrakenGame &g)
{
    if (g.kind == "pkg")
        return "FPKG-Datei";
    if (!g.image.empty())
    {
        // Kraken Mount 0.4.2: "Abbild (exfat)"; a pattern "Abbild ({0})" in the dictionaries
        const std::size_t dot = g.image.find_last_of("./");
        std::string ext =
            dot != std::string::npos && g.image[dot] == '.' ? g.image.substr(dot + 1) : "?";
        for (char &c : ext)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return "Abbild (" + ext + ")";
    }
    if (g.packing == "zstd")
        return "Spielordner (zstd)";
    if (g.packing == "lz4")
        return "Spielordner (LZ4)";
    return "Spielordner";
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
        else if (c == '\n')
            out += "\\n";
        else if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out + "\"";
}

class KrakenPage final : public Page
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
        list_.style.value_size = 20.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.badge_column = true;
        list_.style.value_width = 130.0f; // the badges line up
        list_.style.leading_width = 80.0f;
        list_.leading =
            [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int, float)
        {
            const float side = std::min(60.0f, row.h - 20.0f);
            const Rect box{row.x + 16.0f, row.y + (row.h - side) * 0.5f, side, side};
            if (const KrakenGame *g = game_of(item))
                draw_icon(canvas, box, g->title_id, 1.0f);
        };
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 170.0f, kRightW - 60.0f,
                          424.0f}); // nine rows of 46.5 px: with a conflict and a hint all fit

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 600.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});
        ask_.init(theme);
    }

    void absorb(const Snapshot &data) override
    {
        state_ = data.kraken;
        rebuild();
    }

    void enter() override
    {
        list_.enter();
    }

    bool modal() const override
    {
        return menu_.is_open() || ask_.is_open() || log_open_;
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (ask_.is_open())
        {
            // only "Löschen" in the question deletes
            if (ask_.handle(input, feedback))
                source().kraken_post("{\"action\":\"delete\",\"id\":" + json_text(menu_id_) +
                                     ",\"path\":" + json_text(menu_path_) + "}");
            return;
        }
        if (menu_.is_open())
        {
            if (menu_.handle(input, feedback) == ui::Event::activated)
                run(menu_.items()[static_cast<std::size_t>(menu_.focus())].tag, feedback);
            return;
        }
        if (log_open_)
        {
            if (input.is_pressed(Action::back))
            {
                feedback.play(audio::Cue::back);
                log_open_ = false;
            }
            return;
        }
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
        {
            if (const KrakenGame *now = focused())
                focus_id_ = now->title_id;
            refresh_info();
        }
        const KrakenGame *g = focused();
        if (event == ui::Event::activated || input.is_pressed(Action::west))
        {
            if (g == nullptr)
            {
                if (event == ui::Event::activated)
                    open_page_menu(feedback);
                else
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            open_game_menu(*g, feedback);
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
        shown_.target = log_open_ ? 1.0f : 0.0f;
        shown_.update(dt, 14.0f);
        if (const KrakenGame *g = focused())
            covers().prefer(kraken_key(g->title_id));
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);

        const KrakenGame *g = focused();
        float title_w = card.w - 60.0f;
        if (g != nullptr)
        {
            draw_icon(canvas, {card.x + card.w - 30.0f - 96.0f, card.y + 30.0f, 96.0f, 96.0f},
                      g->title_id, 1.0f);
            title_w -= 116.0f;
        }
        const std::string title =
            g != nullptr ? (g->name.empty() ? g->title_id : g->name) : std::string("KrakenMount");
        float y = card.y + 66.0f;
        int lines = 0;
        for (const std::string &line : fonts.display.font->wrap(title, 32.0f, title_w))
        {
            if (++lines > 2)
                break;
            ui::text(list, fonts.display, line, card.x + 30.0f, y, 32.0f, kInk);
            y += 42.0f;
        }
        if (g != nullptr)
        {
            const StateText st = state_of(*g);
            ui::text(list, fonts.semibold, st.text, card.x + 30.0f, card.y + 140.0f, 22.0f,
                     st.color);
            info_.draw(canvas);
            float below = card.y + (g->conflict_with.empty() ? 590.0f : 550.0f);
            if (!g->conflict_with.empty())
                below = paragraph(canvas,
                                  "Achtung! Lege Spiele für Kraken Mount nur in den Ordner "
                                  "Kraken-Games (auf dem internen Speicher /data/Kraken-Games, auf "
                                  "einem USB- oder M.2-Laufwerk der Ordner Kraken-Games im "
                                  "Hauptordner), dort sucht ShadowMountPlus nicht!",
                                  card.x + 30.0f, below + 10.0f, card.w - 60.0f, kWarning, 5);
            // With the warning there is no room left for the error text; the state above names it
            // and the log has it.
            if (!g->error.empty() && g->conflict_with.empty())
                paragraph(canvas, g->error, card.x + 30.0f, below + 10.0f, card.w - 60.0f, kWarning,
                          3);
            // homebrew and etaHEN/games while ShadowMountPlus does not run (28.11.2026): where the
            // warning or the error would stand
            else if (g->conflict_with.empty())
                smp_note(canvas, card.x + 30.0f, below + 10.0f, card.w - 60.0f, 3);
        }
        else
        {
            paragraph(canvas,
                      "Spielt Spiele ohne Installation, mit dem Programm Kraken Mount von "
                      "quer3q: Debug-FPKG (.pkg), Spielordner und Abbilder (.exfat, .ffpkg, "
                      ".ffpfs, .ffpfsc). Lege sie in den Ordner Kraken-Games; Kraken Mount "
                      "meldet das Spiel von selbst an.",
                      card.x + 30.0f, card.y + 130.0f, card.w - 60.0f, kMuted, 5);
            paragraph(canvas, roots_text(), card.x + 30.0f, card.y + 300.0f, card.w - 60.0f, kInk,
                      5);
            const float after = paragraph(
                canvas,
                "Nach einem Neustart der Konsole muss Kraken Mount laufen, damit die Spiele "
                "sichtbar bleiben. Die Ordner homebrew und etaHEN/games gibt ihm die App nur, "
                "solange ShadowMountPlus nicht läuft.",
                card.x + 30.0f, card.y + 470.0f, card.w - 60.0f, kWarning, 4);
            smp_note(canvas, card.x + 30.0f, after + 10.0f, card.w - 60.0f, 3);
        }
        const float line_y = kBottom - 96.0f;
        list.line(card.x + 30.0f, line_y, card.x + card.w - 30.0f, line_y, 1.0f,
                  Color::rgb(0x3880ff, 0.18f));
        ui::text(list, fonts.semibold, ui::fit_label(paint, status_line(), 21.0f, card.w - 60.0f),
                 card.x + 30.0f, line_y + 40.0f, 21.0f, state_.running ? kMuted : kWarning);
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
        ui::text(list, fonts.display, "Protokoll von Kraken Mount", panel.x + 40.0f,
                 panel.y + 66.0f, 36.0f, kInk);
        draw_log(canvas, {panel.x + 40.0f, panel.y + 110.0f, panel.w - 80.0f, panel.h - 200.0f});
        ui::HintLayout layout;
        layout.size = 34.0f;
        layout.text_size = 21.0f;
        layout.cy = panel.y + panel.h - 42.0f;
        const ui::Hint hints[] = {{ui::Button::circle, "Schließen"}};
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 1, panel.x + 40.0f, false,
                       layout);
        list.pop_opacity();
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
    const KrakenGame *game_of(const ui::ListItem &item) const
    {
        if (item.header || item.tag < 0 || item.tag >= static_cast<int>(state_.games.size()))
            return nullptr;
        return &state_.games[static_cast<std::size_t>(item.tag)];
    }

    const KrakenGame *focused() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return nullptr;
        return game_of(list_.items()[static_cast<std::size_t>(i)]);
    }

    static bool busy(const KrakenGame &g)
    {
        return g.running || g.status == "building" || g.status == "checking";
    }

    void draw_icon(ui::Canvas &canvas, const Rect &box, const std::string &title_id,
                   float alpha) const
    {
        const CoverShown pic =
            title_id.empty() ? CoverShown{} : covers().texture_of(kraken_key(title_id));
        const float shown = pic.texture == 0 ? 0.0f : pic.alpha;
        if (shown < 1.0f)
        {
            canvas.list.rounded_rect(
                box, box.w * 0.22f,
                Color::rgb(0x1f8a8a).with_alpha(0.85f * alpha * (1.0f - shown)));
            const float size = box.w * 0.34f;
            ui::text(canvas.list, canvas.fonts.semibold, "FP", box.x + box.w * 0.5f,
                     box.y + box.h * 0.5f + size * 0.36f, size,
                     Color::rgb(0xffffff).with_alpha(alpha * (1.0f - shown)), gfx::Align::center);
        }
        if (shown > 0.0f)
            canvas.list.image(pic.texture, box, gfx::kFullUv,
                              Color{1.0f, 1.0f, 1.0f, shown * alpha}, box.w * 0.22f);
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

    void draw_log(ui::Canvas &canvas, const Rect &box) const
    {
        // The newest lines at the bottom, as many as fit; long lines are cut, the web app shows
        // them whole.
        const float step = 27.0f;
        const int fit = static_cast<int>(box.h / step);
        const int count = static_cast<int>(state_.log.size());
        const int first = std::max(0, count - fit);
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        float y = box.y + 22.0f;
        for (int i = first; i < count; ++i)
        {
            ui::text(canvas.list, canvas.fonts.regular,
                     ui::fit_label(paint, state_.log[static_cast<std::size_t>(i)], 18.0f, box.w),
                     box.x, y, 18.0f, kInk.with_alpha(0.85f));
            y += step;
        }
        if (count == 0)
            ui::text(canvas.list, canvas.fonts.regular,
                     state_.running ? "Das Protokoll ist leer."
                                    : "Kraken Mount läuft nicht: kein Protokoll.",
                     box.x, y, 21.0f, kMuted);
    }

    // The folders Kraken Mount searches: its setting, else its defaults (the same texts as the web
    // page).
    std::string roots_text() const
    {
        if (state_.running && !state_.settings_known)
            return {}; // it did not tell its settings this time: nothing rather than a wrong text
        if (state_.scanpaths.empty())
        {
            // Not running: its setting is not known, the app writes the folder Kraken-Games at the
            // first start. Running with an empty list: its own default.
            if (!state_.running)
                return "Kraken Mount ist ausgeschaltet. Die App trägt beim ersten Start den Ordner "
                       "Kraken-Games ein: /data/Kraken-Games und der Ordner Kraken-Games auf jedem "
                       "USB- und M.2-Laufwerk.";
            return "Suchordner (Grundeinstellung von Kraken Mount): /data/homebrew, "
                   "/data/etaHEN/games, homebrew und etaHEN/games sowie der Hauptordner jedes USB- "
                   "und M.2-Laufwerks. Diese Ordner durchsucht auch ShadowMountPlus: Lege "
                   "Spiele für Kraken Mount besser in den Ordner Kraken-Games (/data/Kraken-Games "
                   "oder Kraken-Games auf dem Laufwerk) und trage ihn auf der Seite von Kraken "
                   "Mount als Suchordner ein.";
        }
        // the folders the app writes into its config (kraken.c seed_config)
        std::vector<std::string> own = {"/data/Kraken-Games"};
        for (int i = 0; i < 8; ++i)
            own.push_back("/mnt/usb" + std::to_string(i) + "/Kraken-Games");
        own.push_back("/mnt/ext0/Kraken-Games");
        own.push_back("/mnt/ext1/Kraken-Games");
        // the same list under the folder's old name (until 27.11.2026): renamed at the next start of
        // Kraken Mount by the app (kraken.c migrate_folders)
        std::vector<std::string> old = own;
        for (std::string &p : old)
            p.replace(p.size() - std::string("Kraken-Games").size(), std::string::npos, "FPKG-Games");
        if (state_.scanpaths == old)
            return "Suchordner unter dem alten Namen FPKG-Games. Kraken Mount einmal aus- und "
                   "wieder einschalten: Dann benennt die App die Ordner FPKG-Games in Kraken-Games "
                   "um und stellt die Suchordner um.";
        // with homebrew and etaHEN/games while ShadowMountPlus does not run (kraken.c, 28.11.2026)
        if (state_.smp_list == "app_smp")
            return "Suchordner: /data/Kraken-Games und der Ordner Kraken-Games auf jedem USB- und "
                   "M.2-Laufwerk, dazu, solange ShadowMountPlus nicht läuft, homebrew und "
                   "etaHEN/games (intern und auf jedem Laufwerk) sowie der Hauptordner jedes "
                   "Laufwerks.";
        if (state_.scanpaths == own)
            return "Suchordner: /data/Kraken-Games und der Ordner Kraken-Games auf jedem USB- und "
                   "M.2-Laufwerk. ShadowMountPlus sucht dort nicht. Liegt dasselbe Spiel "
                   "(dieselbe Titel-ID) auch bei ShadowMountPlus, kommen sich beide doch in die "
                   "Quere; die App meldet es dann.";
        std::string out = "Suchordner: ";
        for (std::size_t i = 0; i < state_.scanpaths.size(); ++i)
            out += (i == 0 ? "" : ", ") + state_.scanpaths[i];
        return out;
    }

    // ShadowMountPlus's folders (kraken.c, 28.11.2026): whether Kraken Mount searches homebrew and
    // etaHEN/games because ShadowMountPlus does not run - the texts of the web page, the green one
    // without its second sentence (three lines at most fit here)
    std::string smp_text() const
    {
        if (!state_.running || state_.stopping)
            return {};
        // ShadowMountPlus runs and the list still has its folders: they are being given back (checked
        // first: never "läuft nicht" while it runs, review 28.11.2026)
        if (giving_back())
            return "ShadowMountPlus läuft: Die App gibt ihm homebrew und etaHEN/games gerade zurück.";
        if (state_.smp_list == "app_smp")
            return "ShadowMountPlus läuft nicht: Kraken Mount spielt auch die Spiele aus homebrew "
                   "und etaHEN/games.";
        if (state_.smp == "gone" && state_.smp_list == "app" && state_.smp_in_s > 0)
            return "ShadowMountPlus läuft nicht: In " + std::to_string(state_.smp_in_s) +
                   " s übernimmt Kraken Mount auch homebrew und etaHEN/games.";
        if (state_.smp == "gone" && state_.smp_list == "hand")
            return "ShadowMountPlus läuft nicht. Die Suchordner von Kraken Mount wurden von Hand "
                   "geändert; die App ergänzt sie deshalb nicht.";
        if (state_.smp == "running" && state_.smp_list == "app")
            return "ShadowMountPlus läuft und hat homebrew und etaHEN/games. Läuft es nicht, "
                   "übernimmt Kraken Mount sie nach einer Minute.";
        return {};
    }

    bool giving_back() const
    {
        return state_.smp == "running" && state_.smp_list == "app_smp";
    }

    // The line about ShadowMountPlus's folders with a round lamp in front of it (user, 28.11.2026:
    // "damit man gleich darauf aufmerksam wird ... ein rundes leuchtendes Lämpchen, somit weiß der
    // Nutzer gleich, was gerade gilt"): green and breathing while Kraken Mount plays homebrew and
    // etaHEN/games, amber and blinking while it takes them over in a moment, blue while
    // ShadowMountPlus has them, grey for a list changed by hand. Drawn like the dot of a loaded
    // payload (payloads_page); the web page has the same lamp. Returns the y below the text.
    float smp_note(ui::Canvas &canvas, float x, float y, float width, int max_lines) const
    {
        const std::string text = smp_text();
        if (text.empty())
            return y;
        Color core = Color::rgb(0x6b7896);
        float glow = -1.0f; // < 0: no light around it
        if (giving_back())
        {
            core = kWarning;
            glow = 0.5f + 0.5f * std::sin(canvas.time * 6.0f);
        }
        else if (state_.smp_list == "app_smp")
        {
            core = Color::rgb(0x2fd44a);
            glow = 0.5f + 0.5f * std::sin(canvas.time * 2.6f);
        }
        else if (state_.smp == "gone" && state_.smp_list == "app")
        {
            core = kWarning;
            glow = 0.5f + 0.5f * std::sin(canvas.time * 6.0f);
        }
        else if (state_.smp == "running")
        {
            core = Color::rgb(0x3880ff);
            glow = 0.7f;
        }
        // the first line's middle: the text is drawn on its baseline
        const float cx = x + 11.0f, cy = y - 7.0f;
        if (glow >= 0.0f)
        {
            canvas.list.circle(cx, cy, 19.0f + 4.0f * glow, core.with_alpha(0.10f + 0.12f * glow));
            canvas.list.circle(cx, cy, 13.0f + 2.0f * glow, core.with_alpha(0.28f + 0.22f * glow));
            canvas.list.circle(cx, cy, 9.5f, core.with_alpha(0.6f));
        }
        canvas.list.circle(cx, cy, 7.5f, core);
        if (glow >= 0.0f) // the bright middle of a lit lamp
            canvas.list.circle(cx - 1.5f, cy - 1.5f, 2.5f, Color::rgb(0xffffff).with_alpha(0.55f));
        return paragraph(canvas, text, x + 40.0f, y, width - 40.0f, kMuted, max_lines);
    }

    std::string status_line() const
    {
        // parts divided by " · ": the translator looks each one up (a version number inside a part
        // is a value of a pattern)
        if (!state_.loaded)
            return "Wird gefragt …";
        if (state_.fetching)
            return "Eine neue Fassung wird von GitHub geladen …";
        std::string s = "Kraken Mount";
        if (!state_.running)
            return s + (state_.starting ? " · startet gleich …" : " · ausgeschaltet") +
                   " · In der App: Kraken Mount " + state_.embedded_version +
                   (state_.fetch_result.empty() ? "" : " · " + state_.fetch_result);
        if (!state_.version.empty())
            s += " " + state_.version;
        if (state_.stopping)
            return s + " · wird beendet …"; // switched off a moment ago: it answers for a few
                                            // seconds more
        if (state_.waiting == "shadowmount")
            return s + " · wartet auf ShadowMountPlus";
        if (state_.waiting == "kstuff")
            return s + " · wartet auf kstuff";
        if (state_.busy)
            return s + " · arbeitet gerade …";
        if (state_.paused)
            return s + " · ein Spiel läuft";
        // The count comes first and the line does not end with "Spiele": the translator would take
        // the whole line for the pattern
        // "{0} Spiele" (the version number and all), and a Russian plural is chosen by the digits
        // of {0} (audit 15.11.2026).
        const std::size_t n = state_.games.size();
        s = std::to_string(n) + (n == 1 ? " Spiel" : " Spiele") + " · " + s;
        // what the web page says in its note: the cache of the games is built on the internal
        // storage, and that one is nearly full
        if (state_.low_space)
            s += " · wenig Platz auf der internen SSD";
        // a newer release was loaded: the next start takes it (the web page says the same)
        if (!state_.fetched_version.empty())
            s += " · geladen: " + state_.fetched_version + " (wird beim Start genommen)";
        if (!state_.fetch_result.empty())
            s += " · " + state_.fetch_result;
        // it runs on from before an update (the app never stops it itself): the web page says the
        // same in its note
        if (state_.restart_needed)
            s += " · Eine ältere Fassung läuft noch: Zum Wechsel Kraken Mount aus- und wieder "
                 "einschalten.";
        return s;
    }

    // ---------------------------------------------------------------- menus

    void open_game_menu(const KrakenGame &g, ui::Feedback &feedback)
    {
        menu_id_ = g.title_id;
        menu_path_ = g.path;
        menu_source_ = g.image.empty() ? g.path : g.image; // an image game: the file that goes
        menu_name_ = g.name.empty() ? g.title_id : g.name;
        menu_size_ = g.size;
        std::vector<ui::MenuItem> items;
        ui::MenuItem rebuild_item;
        rebuild_item.label = "Neu aufbauen";
        rebuild_item.tag = kGameRebuild;
        // Kraken Mount 0.3.2 (#115): a rebuild asked for and not started yet, or a game still waiting: no second one
        rebuild_item.disabled =
            busy(g) || g.rebuild_queued || g.status == "pending" || !state_.running;
        items.push_back(rebuild_item);
        // the PlayGo stub of this game (Kraken Mount 0.3.1): shown when the game imports PlayGo or the stub is
        // off for it already, as Kraken Mount's own page does
        menu_playgo_off_ = g.playgo_off;
        if (g.playgo_known && (g.playgo || g.playgo_off))
        {
            ui::MenuItem playgo_item;
            playgo_item.label = g.playgo_off ? "PlayGo-Stub einschalten" : "PlayGo-Stub ausschalten";
            playgo_item.tag = kGamePlaygo;
            playgo_item.disabled = busy(g) || !state_.running;
            items.push_back(playgo_item);
        }
        ui::MenuItem del;
        del.label = "Spiel löschen";
        del.tag = kGameDelete;
        del.danger = true;
        del.disabled = busy(g) || !state_.running;
        items.push_back(del);
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 160.0f}, feedback);
    }

    void open_page_menu(ui::Feedback &feedback)
    {
        std::vector<ui::MenuItem> items;
        const auto add = [&](const std::string &label, int tag, bool disabled = false)
        {
            ui::MenuItem m;
            m.label = label;
            m.tag = tag;
            m.disabled = disabled;
            items.push_back(m);
        };
        add(state_.running ? "Kraken Mount beenden" : "Kraken Mount starten", kPageSwitch,
            !state_.loaded || state_.starting || state_.stopping);
        add("Jetzt suchen", kPageScan, !state_.running || state_.stopping);
        add("Alle neu aufbauen", kPageRebuildAll, !state_.running || state_.stopping);
        add("Neue Fassung von GitHub laden", kPageFetch, state_.fetching);
        add("Protokoll", kPageLog);
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void run(int tag, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        switch (tag)
        {
        case kGameRebuild:
            source().kraken_post("{\"action\":\"rebuild\",\"id\":" + json_text(menu_id_) + "}");
            break;
        case kGamePlaygo:
            source().kraken_post("{\"action\":\"playgo\",\"id\":" + json_text(menu_id_) + ",\"off\":" +
                                 (menu_playgo_off_ ? "false" : "true") + "}");
            break;
        case kGameDelete:
        {
            std::vector<std::string> names;
            names.push_back(DeleteAsk::line(menu_name_, menu_size_ != 0 ? format_bytes(menu_size_)
                                                                        : std::string()));
            std::vector<std::string> notes;
            notes.push_back("Quelle: " + menu_source_); // a pattern "Quelle: {0}" in the dictionaries
            notes.push_back("Die Kachel und der Zwischenspeicher gehen mit. Spielstände bleiben.");
            ask_.open("Spiel löschen?", names, notes, feedback);
            break;
        }
        case kPageSwitch:
            source().kraken_post(state_.running ? "{\"action\":\"stop\"}"
                                                : "{\"action\":\"start\"}");
            break;
        case kPageScan:
            source().kraken_post("{\"action\":\"rescan\"}");
            break;
        case kPageRebuildAll:
            source().kraken_post("{\"action\":\"rebuild_all\"}");
            break;
        case kPageFetch:
            source().kraken_post("{\"action\":\"fetch\"}");
            break;
        case kPageLog:
            log_open_ = true;
            break;
        default:
            break;
        }
    }

    // ---------------------------------------------------------------- list

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        for (std::size_t i = 0; i < state_.games.size(); ++i)
        {
            const KrakenGame &g = state_.games[i];
            const StateText st = state_of(g);
            ui::ListItem item;
            item.title = g.name.empty() ? g.title_id : g.name;
            item.subtitle = g.title_id + (g.version.empty() ? "" : " · Version " + g.version) +
                            " · " + kind_of(g) + " · " + g.drive;
            item.value = g.size != 0 ? format_bytes(g.size) : std::string();
            item.badge = st.badge;
            item.tag = static_cast<int>(i);
            out.push_back(item);
        }
        if (out.empty())
        {
            ui::ListItem n;
            n.title = !state_.loaded    ? "Wird gefragt …"
                      : state_.starting ? "Kraken Mount startet …"
                      : !state_.running ? "Kraken Mount ist ausgeschaltet"
                                        : "Keine Spiele gefunden";
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
            // The focus stays on the same GAME (by its title id): the rows carry the index into
            // state_.games as their tag, which shifts when a game appears above, and `refocus` of
            // the other pages compares the list that is already replaced here.
            int focus =
                std::clamp(list_.focus(), 0, std::max(static_cast<int>(next.size()) - 1, 0));
            if (!focus_id_.empty())
            {
                for (std::size_t i = 0; i < next.size(); ++i)
                {
                    const KrakenGame *g = game_of(next[i]);
                    if (g != nullptr && g->title_id == focus_id_)
                    {
                        focus = static_cast<int>(i);
                        break;
                    }
                }
            }
            list_.set_items(next);
            list_.set_focus(focus);
            if (const KrakenGame *g = focused())
                focus_id_ = g->title_id;
        }
        refresh_info();
    }

    void refresh_info()
    {
        std::vector<ui::DetailItem> rows;
        if (const KrakenGame *g = focused())
        {
            rows.push_back({"Titel-ID", g->title_id});
            rows.push_back({"Version", g->version.empty() ? "-" : g->version});
            rows.push_back({"Art", kind_of(*g)});
            rows.push_back({"Laufwerk", g->drive});
            rows.push_back({"Größe", g->size != 0 ? format_bytes(g->size) : "-"});
            rows.push_back(
                {"Zwischenspeicher", g->cache_size != 0 ? format_bytes(g->cache_size) : "-"});
            rows.push_back({"Quelle", g->image.empty() ? g->path : g->image});
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
    tween::Spring shown_;
    KrakenState state_;
    std::string key_;
    std::string
        focus_id_; // the game the focus is on (title id): it stays on it when the list is refilled
    std::string info_key_ = "\x01";
    std::string menu_id_;
    std::string menu_path_;   // sent back to Kraken Mount as it listed it (it checks it)
    std::string menu_source_; // shown: the image file of an image game, else the path
    std::string menu_name_;
    std::uint64_t menu_size_ = 0;
    bool menu_playgo_off_ = false; // the PlayGo stub of the game in the menu is off now
    bool log_open_ = false;
};

} // namespace

std::unique_ptr<Page> make_kraken_page()
{
    return std::make_unique<KrakenPage>();
}

} // namespace hui::ps5cc
