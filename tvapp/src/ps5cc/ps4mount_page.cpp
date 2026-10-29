// ps5cc - The PS4 von USB page (Spiele → PS4 von USB): PS4Mount's games, their updates and add-ons, its settings.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's tab of the same name: ✕ removes a game's tile or brings it back, □ opens what this game allows
// (updates, add-ons, no longer playing it from USB), △ the rest (switching PS4Mount on and off, a search, the
// settings, its log, taking all out). Below the games, the pkgs the search left on the drives, each with the reason. The folder list of the settings is shown, not edited: typing paths belongs to
// the web app. Everything fires at once, nothing is asked twice (user, 27.10.2026).

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
constexpr float kSaveDelay = 0.8f;

enum MenuTag
{
    // the game's (□)
    kGameToggle = 1,
    kGameUpdates,
    kGameDlc,
    kGameRestore,
    // the page's (△)
    kPageSwitch = 20,
    kPageScan,
    kPageSettings,
    kPageLog,
    kPageRestoreAll,
    kPageForget,
};

enum Dialog
{
    kNone,
    kSettings,
    kUpdates,
    kDlcs,
    kLog,
};

enum Row
{
    kSetDlc = 1,
    kSetCopy,
    kSetBatch,
    kSetSize,
    kSetFast,
    kSetRestore,
    kSetPaths,
    kDlcAll = 50,
    kDlcSave,
    kUpd = 100, // kUpd + index
    kDlc = 300, // kDlc + index
};

struct StateText
{
    const char *text;
    Color color;
};

StateText state_of(const std::string &state)
{
    if (state == "ready")
        return {"Bereit", kGood};
    if (state == "running")
        return {"Läuft", kGood};
    if (state == "pending")
        return {"Wird hinzugefügt", kWarning};
    if (state == "removed")
        return {"Kachel entfernt", kMuted};
    if (state == "internal")
        return {"Auf der Konsole installiert, läuft nicht von USB", kWarning};
    if (state == "duplicate")
        return {"Liegt auch auf einem anderen Laufwerk", kMuted};
    if (state == "addons")
        return {"Zusatzinhalte werden kopiert", kWarning};
    return {"", kMuted};
}

const char *badge_of(const std::string &state)
{
    if (state == "ready" || state == "running")
        return "BEREIT";
    if (state == "pending")
        return "KOMMT";
    if (state == "removed")
        return "ENTFERNT";
    if (state == "internal")
        return "INTERN";
    if (state == "addons")
        return "DLC";
    return "";
}

// Why the search left a pkg where it is (ps4m_core.c skip_note), as the web app says it.
const char *why_text(const std::string &why)
{
    if (why == "internal")
        return "Schon auf der Konsole installiert. Um es von USB zu spielen, erst das installierte Spiel löschen "
               "(Bibliothek → Aktionen → Löschen).";
    if (why == "restored")
        return "Mit „Nicht mehr von USB spielen“ herausgenommen. Mehr → Herausgenommene wieder zulassen holt es zurück.";
    if (why == "not_ps4")
        return "Kein PS4-Paket, zum Beispiel ein PS5-PKG. Solche Pakete installiert der Reiter „Pakete“.";
    if (why == "not_fake")
        return "Kein Fake-PKG. Von USB spielen lassen sich nur Fake-PKGs.";
    if (why == "dlc_foreign")
        return "Zusatzinhalt, der zu keinem PS4-Spiel gehört.";
    if (why == "dlc_off")
        return "Zusatzinhalt. Zusatzinhalte sind in den Einstellungen ausgeschaltet.";
    if (why == "duplicate")
        return "Eine zweite Kopie einer Datei, die schon benutzt wird.";
    if (why == "second_game")
        return "Für dieses Spiel wird schon ein anderes Spielpaket benutzt.";
    if (why == "damaged")
        return "Das Spielpaket ist beschädigt (Kennungen in der param.sfo fehlerhaft). Es wird nicht angemeldet, weil so "
               "ein Paket die Konsole abstürzen lassen und ihren Speicher beschädigen kann.";
    return "Nicht eingebunden.";
}

const char *why_short(const std::string &why)
{
    if (why == "internal")
        return "schon installiert";
    if (why == "restored")
        return "herausgenommen";
    if (why == "not_ps4")
        return "kein PS4-Paket";
    if (why == "not_fake")
        return "kein Fake-PKG";
    if (why == "dlc_off")
        return "Zusatzinhalte aus";
    if (why == "duplicate")
        return "zweite Kopie";
    if (why == "damaged")
        return "beschädigt";
    return "nicht eingebunden";
}

std::string file_of(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

constexpr int kSkipTag = 1000;                // list tags from here on are skipped pkgs
constexpr int kSkipToggle = kSkipTag - 1; // the row that folds them in and out

const char *copy_text(const std::string &copy)
{
    if (copy == "missing")
        return "wartet";
    if (copy == "installing")
        return "wird kopiert";
    if (copy == "installed")
        return "kopiert";
    if (copy == "store")
        return "schon vorhanden";
    if (copy == "problem")
        return "fehlgeschlagen";
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
        else if (c == '\n')
            out += "\\n";
        else if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out + "\"";
}

class Ps4MountPage final : public Page
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
        list_.leading = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int, float)
        {
            const float side = std::min(60.0f, row.h - 20.0f);
            const Rect box{row.x + 16.0f, row.y + (row.h - side) * 0.5f, side, side};
            if (const Ps4Game *g = game_of(item))
                draw_icon(canvas, box, g->title_id, g->state == "removed" ? 0.5f : 1.0f);
            else if (const Ps4Skipped *s = skipped_of(item))
                draw_icon(canvas, box, s->title_id, 0.5f);
        };
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 170.0f, kRightW - 60.0f, 330.0f});

        menu_.style.theme = theme;
        menu_.style.width = 560.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});

        form_.style.theme = theme;
        form_.style.panel = false;
        form_.style.on_page = false;
        form_.style.row_height = 62.0f;
        form_.style.header_height = 46.0f;
        form_.style.label_size = 24.0f;
        form_.style.value_size = 21.0f;
        form_.style.description_size = 19.0f;
        form_.style.control_width = 360.0f;
        form_.style.label_ratio = 0.55f;
        form_.style.on_text = "An";
        form_.style.off_text = "Aus";
        form_.set_bounds({kPanel.x + 24.0f, kPanel.y + 130.0f, 1080.0f, 650.0f});
    }

    void absorb(const Snapshot &data) override
    {
        mount_ = data.ps4mount;
        rebuild();
        if (dialog_ == kSettings && mount_.config.valid && pending_.empty() && !settings_shown_)
            build_settings();
        if ((dialog_ == kUpdates || dialog_ == kDlcs) && waiting_detail_ && data.ps4_detail.id == detail_for_ &&
            data.ps4_detail.seq != detail_seq_)
        {
            waiting_detail_ = false;
            detail_ = data.ps4_detail;
            build_detail();
        }
        if (dialog_ == kLog && data.ps4_log_seq != log_seq_)
        {
            log_seq_ = data.ps4_log_seq;
            log_ = data.ps4_log;
        }
        // Remembered while no dialog waits, so the next one can tell a new answer from this one.
        if (!waiting_detail_)
            detail_seq_ = data.ps4_detail.seq;
        if (dialog_ != kLog)
            log_seq_ = data.ps4_log_seq;
    }

    void enter() override
    {
        list_.enter();
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
        if (dialog_ != kNone)
        {
            handle_dialog(input, feedback);
            return;
        }
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
            refresh_info();
        const Ps4Game *g = focused();
        if (event == ui::Event::activated && on_skip_toggle())
        {
            feedback.play(audio::Cue::select);
            skip_open_ = !skip_open_;
            rebuild();
            return;
        }
        if (event == ui::Event::activated)
        {
            if (g == nullptr || !can_toggle(*g))
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            feedback.play(audio::Cue::select);
            toggle(*g);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            if (g == nullptr)
            {
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
        clock_ += dt;
        list_.update(dt);
        info_.update(dt);
        menu_.update(dt);
        form_.update(dt);
        shown_.target = dialog_ != kNone ? 1.0f : 0.0f;
        shown_.update(dt, 14.0f);
        if (save_in_ > 0.0f)
        {
            save_in_ -= dt;
            if (save_in_ <= 0.0f)
                save_settings();
        }
        if (const Ps4Game *g = focused())
            covers().prefer(ps4_key(g->title_id));
        else if (const Ps4Skipped *s = focused_skipped(); s != nullptr && !s->title_id.empty())
            covers().prefer(ps4_key(s->title_id));
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);

        const Ps4Game *g = focused();
        const Ps4Skipped *skip = focused_skipped();
        float title_w = card.w - 60.0f;
        if (g != nullptr || skip != nullptr)
        {
            draw_icon(canvas, {card.x + card.w - 30.0f - 96.0f, card.y + 30.0f, 96.0f, 96.0f},
                      g != nullptr ? g->title_id : skip->title_id, g != nullptr ? 1.0f : 0.6f);
            title_w -= 116.0f;
        }
        const std::string title = g != nullptr      ? g->name
                                  : skip != nullptr ? (skip->name.empty() ? file_of(skip->path) : skip->name)
                                                    : std::string("PS4 von USB");
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
            const StateText st = state_of(g->state);
            ui::text(list, fonts.semibold, st.text, card.x + 30.0f, card.y + 140.0f, 22.0f, st.color);
            info_.draw(canvas);
            if (!g->update_issue.empty())
                paragraph(canvas, g->update_issue, card.x + 30.0f, card.y + 520.0f, card.w - 60.0f, kWarning, 3);
        }
        else if (skip != nullptr)
        {
            ui::text(list, fonts.semibold, "Nicht eingebunden", card.x + 30.0f, card.y + 140.0f, 22.0f, kWarning);
            info_.draw(canvas);
            paragraph(canvas, why_text(skip->why), card.x + 30.0f, card.y + 360.0f, card.w - 60.0f, kInk, 6);
        }
        else
        {
            paragraph(canvas,
                      "Spielt PS4-Spiele (Fake-PKG) direkt vom USB-Laufwerk, ohne sie zu installieren. Die PKG-Dateien "
                      "bleiben auf dem Laufwerk.",
                      card.x + 30.0f, card.y + 130.0f, card.w - 60.0f, kMuted, 5);
        }
        const float line_y = kBottom - 96.0f;
        list.line(card.x + 30.0f, line_y, card.x + card.w - 30.0f, line_y, 1.0f, Color::rgb(0x3880ff, 0.18f));
        const bool waits = mount_.enabled && !mount_.running && !mount_.standalone.empty();
        ui::text(list, fonts.semibold, status_line(), card.x + 30.0f, line_y + 40.0f, 21.0f,
                 !mount_.enabled || waits ? kWarning : kMuted);
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
        const float rise = 24.0f * (1.0f - amount);
        const Rect panel{kPanel.x, kPanel.y + rise, kPanel.w, kPanel.h};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(panel);
        ui::text(list, fonts.display, ui::fit_label(paint, dialog_title(), 36.0f, panel.w - 80.0f), panel.x + 40.0f,
                 panel.y + 66.0f, 36.0f, kInk);
        ui::text(list, fonts.regular, dialog_about(), panel.x + 40.0f, panel.y + 104.0f, 21.0f, kMuted);

        ui::HintLayout layout;
        layout.size = 34.0f;
        layout.text_size = 21.0f;
        layout.cy = panel.y + panel.h - 42.0f;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        if (dialog_ == kLog)
        {
            draw_log(canvas, {panel.x + 40.0f, panel.y + 140.0f, panel.w - 80.0f, panel.h - 230.0f});
            const ui::Hint hints[] = {{ui::Button::triangle, "Neu laden"}, {ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 2, panel.x + 40.0f, false, layout);
        }
        else if (waiting_detail_ || (dialog_ == kSettings && !settings_shown_))
        {
            const float pulse = 0.6f + 0.4f * std::sin(clock_ * 4.0f);
            ui::text(list, fonts.semibold, "Wird geladen …", panel.x + panel.w * 0.5f, panel.y + 420.0f, 30.0f,
                     kInk.with_alpha(pulse), gfx::Align::center);
            const ui::Hint hints[] = {{ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 1, panel.x + 40.0f, false, layout);
        }
        else if (!detail_.error.empty() && dialog_ != kSettings)
        {
            ui::text(list, fonts.semibold, "Nicht möglich", panel.x + 40.0f, panel.y + 200.0f, 28.0f, kCritical);
            paragraph(canvas, detail_.error, panel.x + 40.0f, panel.y + 246.0f, panel.w - 80.0f, kInk, 5);
            const ui::Hint hints[] = {{ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 1, panel.x + 40.0f, false, layout);
        }
        else
        {
            list.rounded_rect({panel.x + 1120.0f, panel.y + 140.0f, 1.5f, 630.0f}, 0.0f, Color::rgb(0x3880ff, 0.18f));
            form_.draw(canvas);
            paragraph(canvas, dialog_side(), panel.x + 1150.0f, panel.y + 160.0f, panel.w - 1190.0f, kMuted, 18);
            const ui::Hint hints[] = {{ui::Button::cross, dialog_ == kSettings ? "Ändern" : "Wählen"},
                                      {ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, glyphs, hints, 2, panel.x + 40.0f, false, layout);
        }
        list.pop_opacity();
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        if (const Ps4Game *g = focused())
        {
            if (can_toggle(*g))
                out[n++] = {ui::Button::cross, g->state == "removed" ? "Kachel wieder anlegen" : "Kachel entfernen"};
            out[n++] = {ui::Button::square, "Aktionen"};
        }
        else if (on_skip_toggle())
            out[n++] = {ui::Button::cross, skip_open_ ? "Zuklappen" : "Aufklappen"};
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    const Ps4Game *game_of(const ui::ListItem &item) const
    {
        if (item.header || item.tag < 0 || item.tag >= static_cast<int>(mount_.games.size()))
            return nullptr;
        return &mount_.games[static_cast<std::size_t>(item.tag)];
    }

    const Ps4Skipped *skipped_of(const ui::ListItem &item) const
    {
        const int i = item.tag - kSkipTag;
        if (item.header || i < 0 || i >= static_cast<int>(mount_.skipped.size()))
            return nullptr;
        return &mount_.skipped[static_cast<std::size_t>(i)];
    }

    bool on_skip_toggle() const
    {
        const int i = list_.focus();
        return i >= 0 && i < static_cast<int>(list_.items().size()) &&
               list_.items()[static_cast<std::size_t>(i)].tag == kSkipToggle;
    }

    const Ps4Skipped *focused_skipped() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return nullptr;
        return skipped_of(list_.items()[static_cast<std::size_t>(i)]);
    }

    const Ps4Game *focused() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return nullptr;
        return game_of(list_.items()[static_cast<std::size_t>(i)]);
    }

    static bool can_toggle(const Ps4Game &g)
    {
        return g.state == "removed" || g.state == "ready" || g.state == "pending";
    }

    void toggle(const Ps4Game &g)
    {
        source().ps4mount_post({}, std::string("{\"action\":\"") + (g.state == "removed" ? "add" : "remove") +
                                       "\",\"id\":" + json_text(g.title_id) + "}");
    }

    void draw_icon(ui::Canvas &canvas, const Rect &box, const std::string &title_id, float alpha) const
    {
        const CoverShown pic = title_id.empty() ? CoverShown{} : covers().texture_of(ps4_key(title_id));
        const float shown = pic.texture == 0 ? 0.0f : pic.alpha;
        if (shown < 1.0f)
        {
            canvas.list.rounded_rect(box, box.w * 0.22f, Color::rgb(0x2f5fb3).with_alpha(0.85f * alpha * (1.0f - shown)));
            const float size = box.w * 0.34f;
            ui::text(canvas.list, canvas.fonts.semibold, "P4", box.x + box.w * 0.5f, box.y + box.h * 0.5f + size * 0.36f,
                     size, Color::rgb(0xffffff).with_alpha(alpha * (1.0f - shown)), gfx::Align::center);
        }
        if (shown > 0.0f)
            canvas.list.image(pic.texture, box, gfx::kFullUv, Color{1.0f, 1.0f, 1.0f, shown * alpha}, box.w * 0.22f);
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

    void draw_log(ui::Canvas &canvas, const Rect &box) const
    {
        // The newest lines at the bottom, as many as fit; long lines are cut, the web app shows them whole.
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= log_.size())
        {
            const std::size_t end = log_.find('\n', start);
            lines.push_back(log_.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        while (!lines.empty() && lines.back().empty())
            lines.pop_back();
        const float step = 27.0f;
        const int fit = static_cast<int>(box.h / step);
        const int first = std::max(0, static_cast<int>(lines.size()) - fit);
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        float y = box.y + 22.0f;
        for (int i = first; i < static_cast<int>(lines.size()); ++i)
        {
            ui::text(canvas.list, canvas.fonts.regular, ui::fit_label(paint, lines[static_cast<std::size_t>(i)], 18.0f, box.w),
                     box.x, y, 18.0f, kInk.with_alpha(0.85f));
            y += step;
        }
    }

    std::string status_line() const
    {
        if (!mount_.loaded)
            return "PS4Mount wird gefragt …";
        std::string s = mount_.version.empty() ? "PS4Mount" : mount_.version;
        if (mount_.busy)
            return s + " · arbeitet gerade …";
        if (!mount_.enabled)
            return s + " · ausgeschaltet";
        if (!mount_.running)
            return s + (mount_.standalone.empty() ? " · startet gleich" : " · wartet: das eigenständige PS4Mount läuft");
        const std::size_t n = mount_.games.size();
        s += " · " + std::to_string(n) + (n == 1 ? " Spiel" : " Spiele") + " eingebunden";
        if (const std::size_t k = mount_.skipped.size(); k > 0)
            s += " · " + std::to_string(k) + (k == 1 ? " PKG" : " PKGs") + " nicht";
        return s;
    }

    // ---------------------------------------------------------------- menus

    void open_game_menu(const Ps4Game &g, ui::Feedback &feedback)
    {
        std::vector<ui::MenuItem> items;
        const auto add = [&](const std::string &label, int tag, bool disabled = false, bool danger = false)
        {
            ui::MenuItem m;
            m.label = label;
            m.tag = tag;
            m.disabled = disabled;
            m.danger = danger;
            items.push_back(m);
        };
        if (can_toggle(g))
            add(g.state == "removed" ? "Kachel wieder anlegen" : "Kachel entfernen (PKG bleibt)", kGameToggle);
        if (g.updates > 1)
            add("Updates wählen (" + std::to_string(g.updates) + ")", kGameUpdates);
        if (g.dlc_total > 0)
            add("Zusatzinhalte (" + std::to_string(g.dlc) + " von " + std::to_string(g.dlc_total) + " an)", kGameDlc);
        if (g.state != "internal")
            add("Nicht mehr von USB spielen", kGameRestore, !mount_.running, true);
        if (items.empty())
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        menu_game_ = g.title_id;
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void open_page_menu(ui::Feedback &feedback)
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
        add(mount_.enabled ? "PS4Mount ausschalten" : "PS4Mount einschalten", kPageSwitch, !mount_.loaded);
        add("Jetzt suchen", kPageScan, !mount_.running);
        add("Einstellungen", kPageSettings, !mount_.loaded);
        add("Protokoll von PS4Mount", kPageLog);
        ui::MenuItem line;
        line.separator = true;
        items.push_back(line);
        add("Herausgenommene wieder zulassen", kPageForget, !mount_.running);
        add("Alle nicht mehr von USB spielen", kPageRestoreAll, !mount_.running || mount_.games.empty(), true);
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    void run(int tag, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        const Ps4Game *g = nullptr;
        for (const Ps4Game &x : mount_.games)
        {
            if (x.title_id == menu_game_)
                g = &x;
        }
        switch (tag)
        {
        case kGameToggle:
            if (g != nullptr)
                toggle(*g);
            break;
        case kGameUpdates:
        case kGameDlc:
            if (g != nullptr)
                open_detail(tag == kGameUpdates ? kUpdates : kDlcs, *g, feedback);
            break;
        case kGameRestore:
            source().ps4mount_post({}, "{\"action\":\"restore\",\"id\":" + json_text(menu_game_) + "}");
            break;
        case kPageSwitch:
            source().ps4mount_post({}, mount_.enabled ? "{\"action\":\"disable\"}" : "{\"action\":\"enable\"}");
            break;
        case kPageScan:
            source().ps4mount_post({}, "{\"action\":\"scan\"}");
            break;
        case kPageSettings:
            open_dialog(kSettings, feedback);
            settings_shown_ = false;
            if (mount_.config.valid)
                build_settings();
            break;
        case kPageLog:
            open_dialog(kLog, feedback);
            log_ = "wird geladen …";
            source().ps4mount_fetch("log", {});
            break;
        case kPageForget:
            source().ps4mount_post({}, "{\"action\":\"restored_forget\"}");
            break;
        case kPageRestoreAll:
            source().ps4mount_post({}, "{\"action\":\"restore_all\"}");
            break;
        default:
            break;
        }
    }

    // ---------------------------------------------------------------- dialogs

    void open_dialog(Dialog d, ui::Feedback &feedback)
    {
        (void)feedback;
        dialog_ = d;
        form_.clear();
    }

    void close_dialog(ui::Feedback &feedback)
    {
        if (dialog_ == kSettings && save_in_ > 0.0f)
            save_settings();
        dialog_ = kNone;
        waiting_detail_ = false;
        feedback.play(audio::Cue::back);
    }

    void open_detail(Dialog d, const Ps4Game &g, ui::Feedback &feedback)
    {
        open_dialog(d, feedback);
        detail_for_ = g.title_id;
        detail_name_ = g.name;
        detail_ = Ps4GameDetail{};
        waiting_detail_ = true;
        source().ps4mount_fetch("game", g.title_id);
    }

    std::string dialog_title() const
    {
        switch (dialog_)
        {
        case kSettings:
            return "Einstellungen von PS4Mount";
        case kUpdates:
            return "Updates von „" + detail_name_ + "“";
        case kDlcs:
            return "Zusatzinhalte von „" + detail_name_ + "“";
        case kLog:
            return "Protokoll von PS4Mount";
        case kNone:
            break;
        }
        return {};
    }

    std::string dialog_about() const
    {
        if (dialog_ == kSettings)
            return "gelten für die Suche nach PS4-Spielen · gespeichert wird sofort";
        if (dialog_ == kLog)
            return "die letzten Zeilen von /data/ps4mount/log.txt";
        return detail_for_;
    }

    std::string dialog_side() const
    {
        if (dialog_ == kSettings)
            return "Neue Spiele in Gruppen hinzufügen: die Konsole meldet so viele auf einmal an.\n\nSchneller anmelden "
                   "zeigt der Anmeldung nur die neuen Spiele (bis zu zwei Minuten lang), danach wieder alle; nur, wenn "
                   "kein Spiel läuft.\n\nDie Ordner mit PKG-Dateien änderst du in der Browser-App.";
        if (dialog_ == kUpdates)
            return "Ein Update ist jeweils benutzt. Ein anderes gewählt, wird das Spiel damit neu zur Bibliothek "
                   "hinzugefügt.";
        return "Höchstens " + std::to_string(detail_.dlc_limit) +
               " Zusatzinhalte können gleichzeitig an sein. Gespeichert, wird das Spiel damit neu zur Bibliothek "
               "hinzugefügt.";
    }

    void build_settings()
    {
        const Ps4MountConfig &c = mount_.config;
        settings_shown_ = true;
        form_.clear();
        form_.add_header("ZUSATZINHALTE");
        form_.add_toggle(kSetDlc, "Zusatzinhalte (DLC) einbinden", c.dlc);
        form_.add_toggle(kSetCopy, "Auf den internen Speicher kopieren", c.dlc_copy).description =
            "Statt vom USB-Laufwerk einzuhängen: braucht Platz und Zeit, zeigt sie dafür wie gewohnt an.";
        form_.add_header("HINZUFÜGEN");
        form_.add_toggle(kSetBatch, "Neue Spiele in Gruppen hinzufügen", c.batch);
        form_.add_slider(kSetSize, "Spiele pro Gruppe", static_cast<float>(c.batch_size), 1.0f, 50.0f, 1.0f);
        form_.add_toggle(kSetFast, "Neue Spiele schneller anmelden", c.fast_reg).description =
            "Verbirgt dabei kurz die anderen Spiele.";
        form_.add_header("NICHT MEHR VON USB SPIELEN");
        form_.add_slider(kSetRestore, "Spiele pro Gruppe beim Herausnehmen", static_cast<float>(c.restore_batch), 1.0f,
                         static_cast<float>(std::max(1, c.max_restore_batch)), 1.0f);
        std::string paths;
        for (const std::string &p : c.paths)
            paths += (paths.empty() ? "" : ", ") + p;
        form_.add_value(kSetPaths, "Ordner mit PKG-Dateien", paths.empty() ? "/" : paths);
        form_.set_disabled(kSetSize, !c.batch);
        form_.set_active(true);
        form_.focus_row(kSetDlc);
        form_.enter();
    }

    void build_detail()
    {
        form_.clear();
        if (dialog_ == kUpdates)
        {
            form_.add_header("UPDATES");
            for (std::size_t i = 0; i < detail_.updates.size(); ++i)
            {
                const Ps4Update &u = detail_.updates[i];
                ui::FormRow &row = form_.add_action(kUpd + static_cast<int>(i),
                                                    (u.active ? "● " : "") + (u.name.empty() ? u.file : u.name));
                row.description = "Version " + u.version + (u.active ? " · wird benutzt" : " · X-Taste benutzt dieses");
            }
            if (detail_.updates.empty())
                form_.add_value(kUpd, "Keine Updates gefunden", "");
        }
        else
        {
            form_.add_header("ZUSATZINHALTE");
            for (std::size_t i = 0; i < detail_.dlcs.size(); ++i)
            {
                const Ps4Dlc &d = detail_.dlcs[i];
                ui::FormRow &row = form_.add_toggle(kDlc + static_cast<int>(i), d.name.empty() ? d.content_id : d.name, d.on);
                std::string about = d.data ? "mit Daten" : "nur Freischaltung";
                if (!d.copy.empty())
                    about += std::string(" · ") + copy_text(d.copy);
                if (!d.reason.empty())
                    about += " (" + d.reason + ")";
                row.description = about;
            }
            if (detail_.dlcs.empty())
                form_.add_value(kDlc, "Keine Zusatzinhalte gefunden", "");
            else
            {
                form_.add_action(kDlcAll, "Alle an (bis zum Höchstwert)");
                form_.add_action(kDlcSave, "Speichern");
            }
        }
        form_.set_active(true);
        form_.enter();
    }

    void handle_dialog(const InputFrame &input, ui::Feedback &feedback)
    {
        if (input.is_pressed(Action::back))
        {
            close_dialog(feedback);
            return;
        }
        if (dialog_ == kLog)
        {
            if (input.is_pressed(Action::north))
            {
                feedback.play(audio::Cue::select);
                source().ps4mount_fetch("log", {});
            }
            return;
        }
        if (waiting_detail_ || (dialog_ == kSettings && !settings_shown_))
            return;
        const ui::Event event = form_.handle(input, feedback);
        const int id = form_.changed_id();
        if (dialog_ == kSettings)
        {
            if (event == ui::Event::changed)
            {
                if (id == kSetBatch)
                    form_.set_disabled(kSetSize, !form_.toggle_value(kSetBatch));
                pending_ = "x";
                save_in_ = kSaveDelay;
            }
            return;
        }
        if (dialog_ == kUpdates && event == ui::Event::activated && id >= kUpd &&
            id < kUpd + static_cast<int>(detail_.updates.size()))
        {
            const Ps4Update &u = detail_.updates[static_cast<std::size_t>(id - kUpd)];
            if (u.active)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            source().ps4mount_post("update", "{\"id\":" + json_text(detail_for_) + ",\"file\":" + json_text(u.file) + "}");
            close_dialog(feedback);
            return;
        }
        if (dialog_ == kDlcs && event == ui::Event::activated)
        {
            if (id == kDlcAll)
            {
                for (std::size_t i = 0; i < detail_.dlcs.size(); ++i)
                    form_.set_toggle(kDlc + static_cast<int>(i), static_cast<int>(i) < detail_.dlc_limit);
                return;
            }
            if (id == kDlcSave)
            {
                std::string on;
                for (std::size_t i = 0; i < detail_.dlcs.size(); ++i)
                {
                    if (form_.toggle_value(kDlc + static_cast<int>(i)))
                        on += (on.empty() ? "" : ",") + detail_.dlcs[i].content_id;
                }
                source().ps4mount_post("dlc", "{\"id\":" + json_text(detail_for_) + ",\"on\":" + json_text(on) + "}");
                close_dialog(feedback);
            }
        }
    }

    // The settings as a whole: PS4Mount sets what is missing to its defaults (as the web page, which sends them all).
    void save_settings()
    {
        save_in_ = 0.0f;
        if (pending_.empty() || !mount_.config.valid)
            return;
        pending_.clear();
        std::string paths;
        for (const std::string &p : mount_.config.paths)
            paths += (paths.empty() ? "" : "\n") + p;
        const auto flag = [&](int row) { return form_.toggle_value(row) ? "true" : "false"; };
        const auto number = [&](int row) { return std::to_string(static_cast<int>(form_.slider_value(row) + 0.5f)); };
        source().ps4mount_post({}, std::string("{\"action\":\"config\",\"dlc\":") + flag(kSetDlc) +
                                       ",\"dlc_copy\":" + flag(kSetCopy) + ",\"batch\":" + flag(kSetBatch) +
                                       ",\"fast_reg\":" + flag(kSetFast) + ",\"batch_size\":" + number(kSetSize) +
                                       ",\"paths\":" + json_text(paths.empty() ? "/" : paths) +
                                       ",\"restore_batch\":" + number(kSetRestore) + "}");
    }

    // ---------------------------------------------------------------- list

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
        if (!mount_.loaded)
            return {note("Wird geladen …", "Die App fragt PS4Mount.")};
        if (!mount_.enabled)
            return {note("PS4Mount ist ausgeschaltet",
                         "Mehr → PS4Mount einschalten: Dann sucht die App PS4-Spiele (Fake-PKG) auf den USB-Laufwerken.")};
        if (!mount_.running && !mount_.standalone.empty())
            return {note("Das eigenständige PS4Mount läuft",
                         "Die App übernimmt von selbst, sobald es beendet ist (Payloads → Laufend).")};
        if (mount_.games.empty() && mount_.skipped.empty())
            return {note("Keine PS4-Spiele gefunden",
                         "Spiel, Update und Zusatzinhalte (.pkg) in die oberste Ebene eines exFAT-Laufwerks legen, dann "
                         "Mehr → Jetzt suchen.")};
        std::vector<ui::ListItem> out;
        if (mount_.games.empty())
            out.push_back(note("Kein PS4-Spiel eingebunden", "Gefunden wurde nur, was unten steht; den Grund zeigt die "
                                                             "rechte Seite."));
        for (std::size_t i = 0; i < mount_.games.size(); ++i)
        {
            const Ps4Game &g = mount_.games[i];
            ui::ListItem item;
            item.title = g.name.empty() ? g.title_id : g.name;
            std::string sub = g.title_id + " · Version " + g.version;
            if (g.has_patch)
                sub += " · mit Update";
            if (g.dlc > 0)
                sub += " · " + std::to_string(g.dlc) + (g.dlc == 1 ? " Zusatzinhalt" : " Zusatzinhalte");
            item.subtitle = sub;
            item.value = format_bytes(g.size);
            item.badge = badge_of(g.state);
            item.tag = static_cast<int>(i);
            out.push_back(item);
        }
        // Folded in until the X button opens it (as the web app, user 29.10.2026): the list can be long and is only for reading.
        if (!mount_.skipped.empty())
        {
            ui::ListItem fold;
            fold.title = "Auf USB gefunden, nicht eingebunden (" + std::to_string(mount_.skipped.size()) + ")";
            fold.tag = kSkipToggle;
            out.push_back(fold);
        }
        for (std::size_t i = 0; skip_open_ && i < mount_.skipped.size(); ++i)
        {
            const Ps4Skipped &s = mount_.skipped[i];
            ui::ListItem item;
            item.title = s.name.empty() ? file_of(s.path) : s.name;
            std::string sub = s.mount.size() > 5 ? s.mount.substr(5) : s.mount;
            if (!s.title_id.empty())
                sub += " · " + s.title_id;
            item.subtitle = sub + " · " + why_short(s.why);
            item.tag = kSkipTag + static_cast<int>(i);
            out.push_back(item);
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
        {
            refresh_info();
            return;
        }
        key_ = std::move(key);
        const int count = static_cast<int>(next.size());
        int focus = std::clamp(list_.focus(), 0, std::max(count - 1, 0));
        while (focus < count && next[static_cast<std::size_t>(focus)].header)
            ++focus;
        list_.set_items(next);
        list_.set_focus(std::min(focus, std::max(count - 1, 0)));
        refresh_info();
    }

    void refresh_info()
    {
        std::vector<ui::DetailItem> rows;
        if (const Ps4Game *g = focused())
        {
            rows.push_back({"Titel-ID", g->title_id});
            rows.push_back({"Version", g->version});
            rows.push_back({"Größe", format_bytes(g->size)});
            rows.push_back({"Update", g->has_patch ? (g->updates > 1 ? std::to_string(g->updates) + " gefunden, eins benutzt"
                                                                     : std::string("ja"))
                                                   : std::string("keins")});
            rows.push_back({"Zusatzinhalte", g->dlc_total > 0 ? std::to_string(g->dlc) + " von " +
                                                                    std::to_string(g->dlc_total) + " an"
                                                              : std::string("keine")});
            rows.push_back({"Laufwerk", g->mount});
        }
        else if (const Ps4Skipped *s = focused_skipped())
        {
            rows.push_back({"Datei", file_of(s->path)});
            rows.push_back({"Laufwerk", s->mount});
            if (!s->title_id.empty())
                rows.push_back({"Titel-ID", s->title_id});
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
    ui::Form form_;
    tween::Spring shown_;
    float clock_ = 0.0f;
    std::string key_;
    std::string info_key_ = "\x01";
    std::string menu_game_;

    Dialog dialog_ = kNone;
    bool settings_shown_ = false;
    std::string pending_; // not empty: a setting changed and waits for the save
    float save_in_ = 0.0f;
    std::string detail_for_;
    std::string detail_name_;
    Ps4GameDetail detail_;
    std::uint64_t detail_seq_ = 0;
    bool waiting_detail_ = false;
    std::string log_;
    std::uint64_t log_seq_ = 0;

    Ps4Mount mount_;
    bool skip_open_ = false;
};

} // namespace

std::unique_ptr<Page> make_ps4mount_page()
{
    return std::make_unique<Ps4MountPage>();
}

} // namespace hui::ps5cc
