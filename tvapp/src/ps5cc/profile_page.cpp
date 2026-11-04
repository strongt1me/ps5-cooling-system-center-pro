// ps5cc - The Profil area: the user's name and profile picture, and the people thanked (Danke).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's Profil and Credits pages. ✕ on a built-in picture makes it the profile picture
// at once (the files are built here, avatar_build.cpp, and sent as the web page sends them), □ gets
// the one before back, △ changes the name on the on-screen keyboard. Pictures of one's own come
// from a USB drive or /data/PS5-Cooling-Center/Profilbilder (31.10.2026). The Konto view does what
// the web page's two account cards do: an offline account (the name asked on the keyboard), the
// fake sign-in and everything back, each at ✕ at once (04.11.2026). The export and import of
// profiles and the links of the Danke page stay in the web app.

#include "ps5cc/covers.hpp"
#include "ps5cc/credits.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

enum View
{
    kProfile = 0,
    kAccount = 1,
    kThanks = 2,
};

enum AccountRow
{
    kRowActivate = 0,
    kRowFakeSignin = 1,
    kRowRestore = 2,
};

constexpr float kInfoW = 560.0f;
constexpr float kGridX = kLeftX + kInfoW + 30.0f;
constexpr float kAccountH = 3.0f * 104.0f + 2.0f * 8.0f + 28.0f;
const Rect kDialog{360.0f, 150.0f, 1200.0f, 760.0f};

// The first letter (a whole UTF-8 character), as the web page's initial.
std::string initial(const std::string &name)
{
    if (name.empty())
        return "?";
    std::size_t n = 1;
    while (n < name.size() && (static_cast<unsigned char>(name[n]) & 0xC0u) == 0x80u)
        ++n;
    return name.substr(0, n);
}

class ProfilePage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        style_subtabs(views_, theme, 3);
        views_.set_tabs({{"Profil"}, {"Konto"}, {"Danke"}});
        views_.set_active(kProfile, true);

        grid_.style.theme = theme;
        grid_.style.columns = 5;
        grid_.style.card.art_aspect = 1.0f;
        grid_.style.card.title_size = 18.0f;
        grid_.style.card.subtitle_size = 0.0f;
        grid_.style.card.on_panel = true;
        grid_.style.wrap = GridWrapFlow();
        grid_.art = [](ui::Canvas &canvas, const Rect &art, float radius, const ui::CardItem &item,
                       float focus)
        {
            (void)focus;
            // tag 1: a picture of one's own (subtitle = its path), else a built-in one
            const CoverShown pic = covers().texture_of(
                item.tag == 1 ? own_picture_key(item.subtitle) : avatar_choice_key(item.subtitle));
            canvas.list.rounded_rect(art, radius, kNavy);
            if (pic.texture != 0)
                canvas.list.image(pic.texture, art, gfx::kFullUv,
                                  Color{1.0f, 1.0f, 1.0f, pic.alpha}, radius);
        };
        grid_.set_bounds({kGridX, kTop + 10.0f, 1824.0f - kGridX, kBottom - kTop - 10.0f});

        account_.style.theme = theme;
        account_.style.panel = true;
        account_.style.row_height = 104.0f;
        account_.style.gap = 8.0f;
        account_.style.title_size = 28.0f;
        account_.style.subtitle_size = 20.0f;
        account_.style.value_size = 24.0f;
        account_.style.entrance_step = 0.0f;
        account_.style.highlight.kind = ui::HighlightKind::bar;
        account_.set_bounds({kLeftX, kTop + 10.0f, 1824.0f - kLeftX, kAccountH});

        thanks_.style.theme = theme;
        thanks_.style.panel = true;
        thanks_.style.header_rule = true;
        thanks_.style.row_height = 112.0f;
        thanks_.style.header_size = 18.0f;
        thanks_.style.highlight.kind = ui::HighlightKind::bar;
        thanks_.content = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item,
                                 int index, float focus)
        {
            (void)index;
            (void)focus;
            draw_credit(canvas, row, item.tag);
        };
        thanks_.set_bounds({kLeftX, kTop + 10.0f, 1824.0f - kLeftX, kBottom - kTop - 10.0f});
        std::vector<ui::ListItem> rows;
        std::string group;
        for (std::size_t i = 0; i < std::size(kCredits); ++i)
        {
            if (group != kCredits[i].group)
            {
                group = kCredits[i].group;
                ui::ListItem h;
                h.title = group;
                h.header = true;
                h.tag = -1;
                rows.push_back(h);
            }
            ui::ListItem r;
            r.title = kCredits[i].name;
            r.tag = static_cast<int>(i);
            rows.push_back(r);
        }
        thanks_.set_items(rows);
        thanks_.set_focus(1);

        keys_.style.theme = theme;
        keys_.style.bindings = ui::KeyboardBindings::standard();
        keys_.style.space_label = "Leer";
        keys_.style.done_label = "Fertig";
        keys_.style.max_length = 16;
        keys_.set_layouts(german_layouts());
        keys_.set_bounds({kDialog.x + 60.0f, kDialog.y + 250.0f, kDialog.w - 120.0f, 400.0f});
        keys_.on_text = [this](std::string_view utf8)
        {
            if (static_cast<int>(name_chars() + 1) <= std::max(profile_.name_max, 1))
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

    void absorb(const Snapshot &data) override
    {
        profile_ = data.profile;
        account_info_ = data.account;
        fill_account();
        revision_ = data.avatar_revision;
        busy_ = data.avatar_busy;
        std::string key;
        for (const OwnPicture &o : data.own_pictures)
            key += o.path + '\n';
        for (const AvatarChoice &a : data.avatars)
            key += a.file + '\n';
        if (key == avatars_key_)
            return;
        avatars_key_ = key;
        std::vector<ui::CardItem> items;
        // one's own pictures first, marked, then the built-in ones
        for (const OwnPicture &o : data.own_pictures)
        {
            ui::CardItem c;
            const std::size_t dot = o.name.find_last_of('.');
            c.title = dot == std::string::npos ? o.name : o.name.substr(0, dot);
            c.subtitle = o.path;
            c.badge = "EIGENES";
            c.tag = 1;
            items.push_back(c);
        }
        for (const AvatarChoice &a : data.avatars)
        {
            ui::CardItem c;
            c.title = a.label;
            c.subtitle =
                a.file; // not drawn (subtitle_size 0): the art slot finds the picture by it
            items.push_back(c);
        }
        const int keep = grid_.focus();
        grid_.set_items(items);
        grid_.set_focus(std::clamp(keep, 0, std::max(static_cast<int>(items.size()) - 1, 0)));
    }

    void enter() override
    {
        grid_.enter();
        account_.enter();
        thanks_.enter();
    }

    bool modal() const override
    {
        return naming_;
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
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
                naming_ = false;
                feedback.play(audio::Cue::select);
                if (naming_account_)
                    source().account_action("activate", trimmed);
                else
                    source().set_username(trimmed);
            }
            return;
        }
        if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            if (views_.step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) ==
                ui::Event::changed)
            {
                view_ = views_.active();
                if (view_ == kProfile)
                    grid_.enter();
                else if (view_ == kAccount)
                    account_.enter();
                else
                    thanks_.enter();
            }
            return;
        }
        if (view_ == kThanks)
        {
            thanks_.handle(input, feedback);
            return;
        }
        if (view_ == kAccount)
        {
            handle_account(input, feedback);
            return;
        }
        if (input.is_pressed(Action::north))
        {
            if (!profile_.valid || !profile_.can_rename)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            naming_ = true;
            naming_account_ = false;
            name_ = profile_.username;
            keys_.style.max_length = std::max(profile_.name_max, 1);
            keys_.set_layout(0);
            keys_.set_length(static_cast<int>(name_.size()));
            keys_.set_active(true);
            keys_.enter();
            return;
        }
        if (input.is_pressed(Action::west))
        {
            // at once, nothing asked twice
            if (!profile_.valid || busy_)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            feedback.play(audio::Cue::select);
            source().restore_avatar();
            return;
        }
        if (grid_.handle(input, feedback) == ui::Event::activated)
        {
            const int i = grid_.focus();
            if (!profile_.valid || busy_ || i < 0 || i >= grid_.count())
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            const ui::CardItem &item = grid_.items()[static_cast<std::size_t>(i)];
            if (item.tag == 1)
                source().apply_own_avatar(item.subtitle);
            else
                source().apply_avatar(item.subtitle);
        }
    }

    void update(float dt) override
    {
        views_.update(dt);
        grid_.update(dt);
        account_.update(dt);
        thanks_.update(dt);
        clock_ += dt;
        if (naming_)
            keys_.update(dt);
        shown_.target = naming_ ? 1.0f : 0.0f;
        shown_.update(dt, 14.0f);
    }

    void draw(ui::Canvas &canvas) const override
    {
        views_.draw(canvas);
        draw_subtab_glyphs(canvas, views_);
        if (view_ == kThanks)
        {
            thanks_.draw(canvas);
            return;
        }
        if (view_ == kAccount)
        {
            draw_account(canvas);
            return;
        }
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        ui::Painter paint(list, fonts, theme_);
        const Rect card{kLeftX, kTop + 10.0f, kInfoW, kBottom - kTop - 10.0f};
        paint.panel(card);

        // the picture in use, or the initial while there is none
        const Rect pic{card.x + (card.w - 220.0f) * 0.5f, card.y + 40.0f, 220.0f, 220.0f};
        const CoverShown cur = covers().texture_of(profile_picture_key(revision_));
        list.circle(pic.x + pic.w * 0.5f, pic.y + pic.h * 0.5f, pic.w * 0.5f,
                    kBlue.with_alpha(0.25f));
        if (cur.texture != 0)
            list.image(cur.texture, pic, gfx::kFullUv, Color{1.0f, 1.0f, 1.0f, cur.alpha},
                       pic.w * 0.5f);
        else
            ui::text(list, fonts.display, initial(profile_.username), pic.x + pic.w * 0.5f,
                     pic.y + pic.h * 0.5f + 34.0f, 96.0f, kInk, gfx::Align::center);

        float y = pic.y + pic.h + 64.0f;
        const std::string name = !profile_.loaded ? "Wird geladen …"
                                 : profile_.valid ? profile_.username
                                                  : "Kein Benutzer angemeldet";
        ui::text(list, fonts.display, ui::fit_label(paint, name, 36.0f, card.w - 60.0f),
                 card.x + card.w * 0.5f, y, 36.0f, kInk, gfx::Align::center);
        y += 44.0f;
        const std::string console =
            profile_.console_name + (profile_.model.empty() ? "" : " · " + profile_.model);
        ui::text(list, fonts.regular, ui::fit_label(paint, console, 21.0f, card.w - 60.0f),
                 card.x + card.w * 0.5f, y, 21.0f, kMuted, gfx::Align::center);
        y += 32.0f;
        if (!profile_.uid_hex.empty())
            ui::text(list, fonts.regular, "Benutzer-ID " + profile_.uid_hex, card.x + card.w * 0.5f,
                     y, 19.0f, kMuted, gfx::Align::center);
        y += 58.0f;
        const char *note =
            busy_ ? "Das Profilbild wird umgerechnet und übertragen …"
                  : "Name und Bild erscheinen im Startmenü nach einem Benutzerwechsel. Die "
                    "Quadrat-Taste holt das vorherige Bild zurück. Eigene Bilder (JPG, PNG): in "
                    "den Ordner Profilbilder eines USB-Sticks oder nach /data/PS5-Cooling-Center/"
                    "Profilbilder legen, sie erscheinen vorn als EIGENES.";
        int lines = 0;
        for (const std::string &line : fonts.regular.font->wrap(note, 20.0f, card.w - 60.0f))
        {
            if (++lines > 6)
                break;
            ui::text(list, fonts.regular, line, card.x + 30.0f, y, 20.0f, busy_ ? kBlue : kMuted);
            y += 28.0f;
        }
        grid_.draw(canvas);
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
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
        ui::text(list, fonts.display, naming_account_ ? "Name des Offline-Kontos" : "Anzeigename",
                 panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
        ui::text(list, fonts.regular,
                 "höchstens " + std::to_string(std::max(profile_.name_max, 1)) + " Zeichen",
                 panel.x + panel.w - 40.0f, panel.y + 66.0f, 21.0f, kMuted, gfx::Align::right);
        const Rect field{panel.x + 60.0f, panel.y + 120.0f, panel.w - 120.0f, 84.0f};
        list.rounded_rect(field, 14.0f, Color::rgb(0x3880ff, 0.12f));
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
        out[n++] = {ui::Button::l2, "Ansicht", ui::Button::r2};
        if (view_ == kThanks)
        {
            out[n++] = {ui::Button::dpad, "Blättern"};
            return n;
        }
        if (view_ == kAccount)
        {
            out[n++] = {ui::Button::cross, "Ausführen"};
            return n;
        }
        out[n++] = {ui::Button::cross, "Als Profilbild"};
        out[n++] = {ui::Button::triangle, "Name ändern"};
        return n;
    }

  private:
    // The three rows of the Konto view, rebuilt only when what they say changes (a rebuild starts
    // the list's entrance again).
    void fill_account()
    {
        const std::string key = std::to_string(account_info_.loaded) +
                                std::to_string(account_info_.activated) +
                                std::to_string(account_info_.fakesignin) + account_info_.username;
        if (key == account_key_)
            return;
        account_key_ = key;
        std::vector<ui::ListItem> rows(3);
        rows[kRowActivate].title = "Offline-Konto aktivieren";
        rows[kRowActivate].subtitle =
            "Setzt Name und Konto-Kennung in der Registrierung der Konsole.";
        rows[kRowActivate].value = !account_info_.loaded     ? "…"
                                   : account_info_.activated ? "aktiv: " + account_info_.username
                                                             : "nicht aktiv";
        rows[kRowActivate].tag = kRowActivate;
        rows[kRowFakeSignin].title = "Fake-Anmeldung schreiben";
        rows[kRowFakeSignin].subtitle = "Schickt np-fake-signin an den Payload-Lader: Die Konsole "
                                        "sieht das Konto als angemeldet.";
        rows[kRowFakeSignin].value = !account_info_.loaded      ? "…"
                                     : account_info_.fakesignin ? "vorhanden"
                                                                : "keine";
        rows[kRowFakeSignin].tag = kRowFakeSignin;
        rows[kRowRestore].title = "Alles zurücksetzen";
        rows[kRowRestore].subtitle =
            "Löscht auth.dat und config.dat und leert die Einträge beider Funktionen.";
        rows[kRowRestore].tag = kRowRestore;
        const int keep = account_.focus();
        account_.set_items(rows);
        account_.set_focus(std::clamp(keep, 0, 2));
    }

    void handle_account(const InputFrame &input, ui::Feedback &feedback)
    {
        if (account_.handle(input, feedback) != ui::Event::activated)
            return;
        if (!profile_.valid)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        switch (account_.focus())
        {
        case kRowActivate:
            naming_ = true;
            naming_account_ = true;
            name_ = account_info_.activated && !account_info_.username.empty()
                        ? account_info_.username
                        : profile_.username;
            keys_.style.max_length = std::max(profile_.name_max, 1);
            keys_.set_layout(0);
            keys_.set_length(static_cast<int>(name_.size()));
            keys_.set_active(true);
            keys_.enter();
            break;
        case kRowFakeSignin:
            feedback.play(audio::Cue::select);
            source().account_action("fakesignin", {});
            break;
        default:
            feedback.play(audio::Cue::select);
            source().account_action("restore", {});
            break;
        }
    }

    void draw_account(ui::Canvas &canvas) const
    {
        account_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const float top = kTop + 10.0f + kAccountH + 24.0f;
        const char *note =
            profile_.valid
                ? "Zuerst das Offline-Konto aktivieren, danach die Fake-Anmeldung schreiben. Die "
                  "Fake-Anmeldung braucht den Payload-Lader (Port 9021). Die Konsole zeigt eine "
                  "Meldung, sobald sie fertig ist."
                : "Kein Benutzer angemeldet: Ohne Benutzer lässt sich kein Konto anlegen.";
        const std::vector<std::string> lines =
            fonts.regular.font->wrap(note, 20.0f, 1824.0f - kLeftX - 60.0f);
        ui::Painter paint(list, fonts, theme_);
        paint.panel(
            {kLeftX, top, 1824.0f - kLeftX, 28.0f * static_cast<float>(lines.size()) + 36.0f});
        float y = top + 42.0f;
        for (const std::string &line : lines)
        {
            ui::text(list, fonts.regular, line, kLeftX + 30.0f, y, 20.0f, kMuted);
            y += 28.0f;
        }
    }

    static ui::GridWrap GridWrapFlow()
    {
        return ui::GridWrap::flow;
    }

    std::size_t name_chars() const
    {
        std::size_t n = 0;
        for (const char c : name_)
            n += (static_cast<unsigned char>(c) & 0xC0u) != 0x80u;
        return n;
    }

    void draw_credit(ui::Canvas &canvas, const Rect &row, int index) const
    {
        if (index < 0 || static_cast<std::size_t>(index) >= std::size(kCredits))
            return;
        const Credit &c = kCredits[static_cast<std::size_t>(index)];
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        ui::Painter paint(list, fonts, theme_);
        const float side = 72.0f;
        const Rect pic{row.x + 16.0f, row.y + (row.h - side) * 0.5f, side, side};
        list.circle(pic.x + side * 0.5f, pic.y + side * 0.5f, side * 0.5f, kBlue.with_alpha(0.25f));
        const CoverShown tex =
            c.picture[0] != 0 ? covers().texture_of(credit_key(c.picture)) : CoverShown{};
        if (tex.texture != 0)
            list.image(tex.texture, pic, gfx::kFullUv, Color{1.0f, 1.0f, 1.0f, tex.alpha},
                       side * 0.5f);
        else
            ui::text(list, fonts.semibold, initial(c.name), pic.x + side * 0.5f,
                     pic.y + side * 0.5f + 12.0f, 32.0f, kInk, gfx::Align::center);
        const float x = pic.x + side + 22.0f;
        const float w = row.x + row.w - 24.0f - x;
        ui::text(list, fonts.semibold, c.name, x, row.y + 36.0f, 24.0f, kInk);
        if (c.login[0] != 0)
            ui::text(list, fonts.regular, c.login,
                     x + fonts.semibold.font->measure(c.name, 24.0f) + 14.0f, row.y + 36.0f, 19.0f,
                     kBlue);
        float y = row.y + 66.0f;
        int lines = 0;
        for (const std::string &line : fonts.regular.font->wrap(c.text, 19.0f, w))
        {
            if (++lines > 2)
                break;
            const bool cut = lines == 2 && fonts.regular.font->wrap(c.text, 19.0f, w).size() > 2;
            ui::text(list, fonts.regular, cut ? ui::fit_label(paint, line + " …", 19.0f, w) : line,
                     x, y, 19.0f, kMuted);
            y += 26.0f;
        }
    }

    ui::Theme theme_;
    ui::TabBar views_;
    ui::GridView grid_;
    ui::ListView account_;
    ui::ListView thanks_;
    ui::Keyboard keys_;
    int view_ = kProfile;
    ProfileInfo profile_;
    AccountInfo account_info_;
    std::string account_key_ = "\x01";
    bool naming_account_ = false;
    int revision_ = 0;
    bool busy_ = false;
    std::string avatars_key_ = "\x01";
    bool naming_ = false;
    std::string name_;
    tween::Spring shown_;
    float clock_ = 0.0f;
};

} // namespace

std::unique_ptr<Page> make_profile_page()
{
    return std::make_unique<ProfilePage>();
}

} // namespace hui::ps5cc
