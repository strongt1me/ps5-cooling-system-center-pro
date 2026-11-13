// ps5cc - The FPKGMount page (Spiele → FPKGMount): the games of Kraken Mount.
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
        info_.set_bounds({kRightX + 30.0f, kTop + 170.0f, kRightW - 60.0f, 330.0f});

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
            refresh_info();
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
            g != nullptr ? (g->name.empty() ? g->title_id : g->name) : std::string("FPKGMount");
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
            if (!g->error.empty())
                paragraph(canvas, g->error, card.x + 30.0f, card.y + 520.0f, card.w - 60.0f,
                          kWarning, 3);
        }
        else
        {
            paragraph(canvas,
                      "Spielt (F)PKG-Spiele ohne Installation, mit dem Programm Kraken Mount von "
                      "quer3q. Lege ein unverschlüsseltes Debug-FPKG (.pkg) in den Ordner "
                      "FPKG-Games; Kraken Mount meldet das Spiel von selbst an.",
                      card.x + 30.0f, card.y + 130.0f, card.w - 60.0f, kMuted, 5);
            paragraph(canvas, roots_text(), card.x + 30.0f, card.y + 300.0f, card.w - 60.0f, kInk,
                      5);
            paragraph(
                canvas,
                "Kraken Mount ist noch eine Beta. Nach einem Neustart der Konsole muss "
                "Kraken Mount laufen, damit die Spiele sichtbar bleiben. Sucht es in Ordnern, "
                "die auch ShadowMountPlus durchsucht, würden deren Ordner-Spiele doppelt "
                "angemeldet.",
                card.x + 30.0f, card.y + 470.0f, card.w - 60.0f, kWarning, 6);
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
        if (state_.scanpaths.empty())
            return "Suchordner (Grundeinstellung): /data/homebrew, /data/etaHEN/games, homebrew "
                   "und "
                   "etaHEN/games sowie der Hauptordner jedes USB- und M.2-Laufwerks.";
        // the folders the app writes into its config (kraken.c seed_config)
        std::vector<std::string> own = {"/data/FPKG-Games"};
        for (int i = 0; i < 8; ++i)
            own.push_back("/mnt/usb" + std::to_string(i) + "/FPKG-Games");
        own.push_back("/mnt/ext0/FPKG-Games");
        own.push_back("/mnt/ext1/FPKG-Games");
        if (state_.scanpaths == own)
            return "Suchordner: /data/FPKG-Games und der Ordner FPKG-Games auf jedem USB- und "
                   "M.2-Laufwerk. ShadowMountPlus sucht dort nicht, deshalb kommen sich beide "
                   "nicht "
                   "in die Quere.";
        std::string out = "Suchordner: ";
        for (std::size_t i = 0; i < state_.scanpaths.size(); ++i)
            out += (i == 0 ? "" : ", ") + state_.scanpaths[i];
        return out;
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
                   " · In der App: Kraken Mount " + state_.embedded_version;
        if (!state_.version.empty())
            s += " " + state_.version;
        if (state_.busy)
            return s + " · arbeitet gerade …";
        if (state_.paused)
            return s + " · ein Spiel läuft";
        const std::size_t n = state_.games.size();
        return s + " · " + std::to_string(n) + (n == 1 ? " Spiel" : " Spiele");
    }

    // ---------------------------------------------------------------- menus

    void open_game_menu(const KrakenGame &g, ui::Feedback &feedback)
    {
        menu_id_ = g.title_id;
        menu_path_ = g.path;
        menu_name_ = g.name.empty() ? g.title_id : g.name;
        menu_size_ = g.size;
        std::vector<ui::MenuItem> items;
        ui::MenuItem rebuild_item;
        rebuild_item.label = "Neu aufbauen";
        rebuild_item.tag = kGameRebuild;
        rebuild_item.disabled = busy(g) || !state_.running;
        items.push_back(rebuild_item);
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
            !state_.loaded || state_.starting);
        add("Jetzt suchen", kPageScan, !state_.running);
        add("Alle neu aufbauen", kPageRebuildAll, !state_.running);
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
        case kGameDelete:
        {
            std::vector<std::string> names;
            names.push_back(DeleteAsk::line(menu_name_, menu_size_ != 0 ? format_bytes(menu_size_)
                                                                        : std::string()));
            std::vector<std::string> notes;
            notes.push_back("Quelle: " + menu_path_); // a pattern "Quelle: {0}" in the dictionaries
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
            list_.set_items(next);
            list_.set_focus(refocus(list_, next));
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
            rows.push_back({"Quelle", g->path});
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
    std::string info_key_ = "\x01";
    std::string menu_id_;
    std::string menu_path_;
    std::string menu_name_;
    std::uint64_t menu_size_ = 0;
    bool log_open_ = false;
};

} // namespace

std::unique_ptr<Page> make_kraken_page()
{
    return std::make_unique<KrakenPage>();
}

} // namespace hui::ps5cc
