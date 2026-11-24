// ps5cc - The Remote Play area: the console prepared for chiaki-ng or ps5upload.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's page of the same name (24.11.2026): the service on or off, the users (account,
// permission), a pairing PIN, the devices paired. The app does not stream; it only prepares the
// console, and the person plays later on a PC or phone. Every change is one post to the console
// app whose answer is the new state (RemotePlayData); the page asks for the state only while it is
// open. The PIN stands large on the right card, because it is typed on another device while
// looking at the television. No device can be removed here (the console keeps only the user and
// the kind of each); nothing is deleted, so nothing asks first.

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/remoteplay.hpp"

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

// The page has no sub-tabs: it starts level with the sidebar's top, as Dateimanager does.
constexpr float kPageTop = kTabY;

// List tags: what a row does.
constexpr int kInfo = 1;
constexpr int kService = 1000;   // the service on or off
constexpr int kPin = 1001;       // make a PIN
constexpr int kPinCancel = 1002; // let the PIN go
constexpr int kAllow = 1100;     // + slot: Remote Play allowed for that user
constexpr int kActivate = 1200;  // + slot: activate the offline account of that user
constexpr int kSlotRange = 100;

constexpr float kPinSeconds = 300.0f; // a PIN lives five minutes

// the line under the title of the web page's banner
const char *const kIntro = "Die Konsole am PC oder Handy spielen: Diese Seite bereitet sie vor, "
                           "gespielt wird mit chiaki-ng oder ps5upload.";
const char *const kDevicesNote =
    "Die Konsole merkt sich je Gerät nur den Benutzer und die Art, keinen Namen. Die App zeigt die "
    "Geräte nur an und entfernt keines.";
const char *const kBackupNote =
    "Vor jeder Änderung sichert die App die betroffenen Einstellungen der Konsole nach";
const char *const kBackupDir = "/data/PS5-Cooling-Center/remoteplay-backup";

ui::ListItem row(const std::string &title, const std::string &value, int tag = kInfo)
{
    ui::ListItem i;
    i.title = title;
    i.value = value;
    i.tag = tag;
    return i;
}

ui::ListItem header(const std::string &title)
{
    ui::ListItem h;
    h.title = title;
    h.header = true;
    h.tag = -1;
    return h;
}

class RemotePlayPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.row_height = 58.0f;
        list_.style.title_size = 22.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 21.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.header_rule = true;
        list_.style.header_height = 50.0f;
        list_.set_bounds({kLeftX, kPageTop, kLeftW, kBottom - kPageTop});
    }

    void absorb(const Snapshot &data) override
    {
        bool changed = false;
        if (data.remoteplay.seq != seq_)
        {
            seq_ = data.remoteplay.seq;
            RpState next;
            if (parse_remoteplay(data.remoteplay.state, &next))
            {
                rp_ = std::move(next);
                rp_left_ = static_cast<float>(rp_.pairing.seconds_left);
            }
            changed = true;
        }
        if (data.remoteplay.pending != pending_)
        {
            pending_ = data.remoteplay.pending;
            if (pending_ == 0)
                posted_tag_ = -1;
            changed = true;
        }
        if (changed)
            rebuild();
    }

    void enter() override
    {
        list_.enter();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (list_.handle(input, feedback) == ui::Event::activated)
            activate(feedback);
    }

    void update(float dt) override
    {
        list_.update(dt);
        // the PIN's countdown runs here between the polls (every second), so it never stands still
        if (rp_.pairing.state == "waiting")
            rp_left_ = std::max(rp_left_ - dt, 0.0f);
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        const Rect card{kRightX, kPageTop, kRightW, kBottom - kPageTop};
        paint.panel(card);
        draw_card(canvas, card);
    }

    int hints(ui::Hint *out) const override
    {
        const int tag = focused_tag();
        // a row that refuses (the service while it cannot be read, a PIN that cannot be made, a
        // post on its way) offers nothing
        const int i = list_.focus();
        if (i >= 0 && i < static_cast<int>(list_.items().size()) &&
            list_.items()[static_cast<std::size_t>(i)].disabled)
            return 0;
        if (tag == kService)
        {
            out[0] = {ui::Button::cross, switched_on(tag) ? "Ausschalten" : "Einschalten"};
            return 1;
        }
        if (tag == kPin || tag == kPinCancel || (tag >= kActivate && tag < kActivate + kSlotRange) ||
            (tag >= kAllow && tag < kAllow + kSlotRange))
        {
            out[0] = {ui::Button::cross, "Ausführen"};
            return 1;
        }
        return 0;
    }

  private:
    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1;
        return list_.items()[static_cast<std::size_t>(i)].tag;
    }

    const RpUser *user_of(int slot) const
    {
        for (const RpUser &u : rp_.users)
        {
            if (u.slot == slot)
                return &u;
        }
        return nullptr;
    }

    // Whether the switch under this tag stands on (for the hint)
    bool switched_on(int tag) const
    {
        return tag == kService && rp_.service == 1;
    }

    void post(const std::string &route, const std::string &json, int tag)
    {
        posted_tag_ = tag; // its value shows "…" while the source reports the post on its way
        source().remoteplay_post(route, json);
    }

    void activate(ui::Feedback &feedback)
    {
        const int tag = focused_tag();
        const auto refuse = [&] { feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f); };
        // the buttons of the web page rest while a post is on its way
        if (!rp_.loaded || tag < kService || pending_ > 0)
        {
            refuse();
            return;
        }
        if (tag == kService)
        {
            if (!rp_.available || rp_.service < 0)
            {
                refuse();
                return;
            }
            feedback.play(audio::Cue::select);
            post("service", rp_.service == 1 ? "{\"enabled\":false}" : "{\"enabled\":true}", tag);
            return;
        }
        if (tag >= kAllow && tag < kAllow + kSlotRange)
        {
            const RpUser *u = user_of(tag - kAllow);
            // only "allow": the console does not take the switching off of a user's permission (measured on the Slim, firmware 12.00: the
            // write is answered with success and the old value is read back), so there is no such action
            if (u == nullptr || u->allowed != 0)
            {
                refuse();
                return;
            }
            feedback.play(audio::Cue::select);
            post("user", "{\"slot\":" + std::to_string(u->slot) + ",\"enabled\":true}", tag);
            return;
        }
        if (tag >= kActivate && tag < kActivate + kSlotRange)
        {
            const RpUser *u = user_of(tag - kActivate);
            if (u == nullptr || u->name.empty())
            {
                refuse();
                return;
            }
            // the console app's own offline activation (the Profil page's), by the user's name
            feedback.play(audio::Cue::select);
            source().account_action("activate", u->name);
            return;
        }
        if (tag == kPin)
        {
            if (!pin_possible(rp_))
            {
                refuse();
                return;
            }
            feedback.play(audio::Cue::select);
            post("pin", "{}", tag);
            return;
        }
        if (tag == kPinCancel)
        {
            feedback.play(audio::Cue::select);
            post("pin/cancel", "{}", tag);
            return;
        }
        refuse();
    }

    // ------------------------------------------------------------ the list

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        if (!rp_.loaded)
        {
            out.push_back(row("Wird gelesen …", ""));
            return out;
        }
        const auto pending_row = [&](int tag) { return pending_ > 0 && posted_tag_ == tag; };
        const auto on_off = [](bool on) { return std::string(on ? "An" : "Aus"); };

        out.push_back(header("Dienst"));
        ui::ListItem service = row("Remote Play einschalten",
                                   pending_row(kService) ? "…"
                                   : rp_.service < 0     ? "–"
                                                         : on_off(rp_.service == 1),
                                   kService);
        service.subtitle = "Schaltet den Dienst der Konsole an. Ein Neustart ist nicht nötig.";
        service.disabled = !rp_.available || rp_.service < 0;
        out.push_back(service);

        out.push_back(header("Benutzer"));
        if (rp_.users.empty())
            out.push_back(row("Keine Benutzer gefunden (läuft der Jailbreak?).", ""));
        for (const RpUser &u : rp_.users)
        {
            ui::ListItem user = row(u.name, "");
            user.subtitle = u.activated ? "Konto aktiviert" : "Konto nicht aktiviert";
            user.subtitle += " · Platz " + std::to_string(u.slot);
            if (rp_.pairing_slot == u.slot)
                user.subtitle += " · für die PIN genutzt";
            if (u.activated)
                user.swatch = kGood.with_alpha(1.0f); // as the Profil page marks an active account
            out.push_back(user);
            if (u.activated && !u.account_id.empty())
                out.push_back(row("Konto-ID", u.account_id));
            if (!u.activated)
            {
                ui::ListItem act = row("Offline aktivieren", "", kActivate + u.slot);
                act.chevron = true;
                out.push_back(act);
            }
            if (rp_.per_user && u.allowed == 1)
                out.push_back(row("Remote Play erlaubt", "Ja"));
            else if (rp_.per_user && u.allowed == 0)
            {
                const int tag = kAllow + u.slot;
                ui::ListItem allow = row("Remote Play erlauben", pending_row(tag) ? "…" : "", tag);
                allow.chevron = true;
                out.push_back(allow);
            }
        }

        out.push_back(header("Kopplungs-PIN"));
        const bool waiting = rp_.pairing.state == "waiting";
        ui::ListItem pin = row(waiting ? "Neue PIN erzeugen" : "PIN erzeugen", "", kPin);
        if (pending_row(kPin))
            pin.subtitle = "wird erzeugt …";
        else if (rp_.pairing.state == "idle")
            pin.subtitle = pin_status_text(rp_.pairing);
        pin.disabled = !pin_possible(rp_) || pending_ > 0;
        pin.chevron = !pin.disabled;
        out.push_back(pin);
        if (waiting)
        {
            ui::ListItem cancel = row("PIN abbrechen", "", kPinCancel);
            cancel.chevron = pending_ == 0;
            cancel.disabled = pending_ > 0;
            out.push_back(cancel);
        }

        out.push_back(header("Gekoppelte Geräte"));
        if (rp_.devices.empty())
            out.push_back(row("Noch kein Gerät gekoppelt.", ""));
        for (const RpDevice &d : rp_.devices)
        {
            const std::string who =
                d.user.empty() ? std::string("Benutzer unbekannt") : "Benutzer " + d.user;
            out.push_back(row("Gerät " + std::to_string(d.slot),
                              who + " · Art " + std::to_string(d.client_type)));
        }
        return out;
    }

    // Rebuilt only when what the list shows changed: a refill restarts its animation. The PIN and
    // its countdown are not in the list (they change every second): the card draws them.
    void rebuild()
    {
        std::vector<ui::ListItem> next = items();
        std::string key;
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge +
                   (i.disabled ? "-" : "") + (i.swatch.a > 0.0f ? "o" : "");
        if (key == key_)
            return;
        key_ = std::move(key);
        const int focus = refocus(list_, next);
        list_.set_items(next);
        list_.set_focus(focus);
    }

    // ------------------------------------------------------------ the card

    // A wrapped paragraph (the translation sees the whole sentence, then it is wrapped); returns
    // the baseline after its last line.
    static float note(ui::Canvas &canvas, const std::string &text, float x, float y, float width,
                      float size, Color ink)
    {
        for (const std::string &line : canvas.fonts.regular.font->wrap(text, size, width))
        {
            ui::text(canvas.list, canvas.fonts.regular, line, x, y, size, ink);
            y += size * 1.38f;
        }
        return y;
    }

    static float note_height(const ui::Canvas &canvas, const std::string &text, float width,
                             float size)
    {
        return static_cast<float>(canvas.fonts.regular.font->wrap(text, size, width).size()) *
               size * 1.38f;
    }

    // One live value of the banner: the label at the left, the value at the right, on a hairline.
    void draw_stat(ui::Canvas &canvas, const std::string &label, const std::string &value, float x,
                   float y, float width) const
    {
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        const float vw = ui::text(canvas.list, canvas.fonts.semibold, value, x + width, y, 28.0f,
                                  kInk, gfx::Align::right);
        ui::text(canvas.list, canvas.fonts.regular,
                 ui::fit_label(paint, label, 21.0f, std::max(width - vw - 20.0f, 60.0f)), x, y,
                 21.0f, kMuted);
        canvas.list.rounded_rect({x, y + 14.0f, width, 1.5f}, 0.0f, kMuted.with_alpha(0.22f));
    }

    void heading(ui::Canvas &canvas, const std::string &title, const Rect &card) const
    {
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        ui::text(canvas.list, canvas.fonts.display,
                 ui::fit_label(paint, title, 32.0f, card.w - 60.0f), card.x + 30.0f, card.y + 66.0f,
                 32.0f, kInk);
    }

    void draw_card(ui::Canvas &canvas, const Rect &card) const
    {
        const float x = card.x + 30.0f;
        const float w = card.w - 60.0f;
        const bool waiting = rp_.pairing.state == "waiting";

        // the two notes that stay: at the foot of the card
        const float small = 19.0f;
        const float backup_h = note_height(canvas, kBackupNote, w, small) + small * 1.38f;
        const float devices_h = note_height(canvas, kDevicesNote, w, small);
        float foot = card.y + card.h - 28.0f - backup_h;
        {
            const float y = note(canvas, kBackupNote, x, foot, w, small, kMuted);
            ui::text(canvas.list, canvas.fonts.semibold,
                     rp_.backup_dir.empty() ? kBackupDir : rp_.backup_dir, x, y, small, kInk);
        }
        foot -= devices_h + 14.0f;
        note(canvas, kDevicesNote, x, foot, w, small, kMuted);

        if (waiting)
        {
            heading(canvas, "Kopplungs-PIN", card);
            draw_pin(canvas, card, x, w, card.y + 104.0f);
            return;
        }
        heading(canvas, "Remote Play", card);
        float y = note(canvas, kIntro, x, card.y + 116.0f, w, 21.0f, kInk);
        if (!rp_.loaded)
            return;
        // the web page's banner: the service, the accounts that are active, the devices paired
        {
            int accounts = 0;
            for (const RpUser &u : rp_.users)
                accounts += u.activated ? 1 : 0;
            y += 14.0f;
            draw_stat(canvas, "Dienst",
                      rp_.service < 0    ? "–"
                      : rp_.service == 1 ? "An"
                                         : "Aus",
                      x, y + 30.0f, w);
            draw_stat(canvas, "Aktive Konten", std::to_string(accounts), x, y + 76.0f, w);
            draw_stat(canvas, "Gekoppelte Geräte", std::to_string(rp_.devices.size()), x,
                      y + 122.0f, w);
            y += 160.0f;
        }
        // what keeps Remote Play from being used as it is
        const std::string warning = state_warning(rp_);
        if (!warning.empty())
            y = note(canvas, warning, x, y + 16.0f, w, 21.0f, kWarning);
        // how the last PIN ended
        const RpPairing &p = rp_.pairing;
        if (p.state == "paired")
            y = note(canvas, pin_status_text(p), x, y + 16.0f, w, 21.0f, kGood);
        else if (p.state == "failed" || p.state == "timeout")
            y = note(canvas, pin_status_text(p), x, y + 16.0f, w, 21.0f, kCritical);
    }

    // The PIN as large as the card allows, the time left, and what to do with it. Returns the
    // baseline after the last line.
    float draw_pin(ui::Canvas &canvas, const Rect &card, float x, float w, float top) const
    {
        gfx::DrawList &list = canvas.list;
        const std::string group = pin_grouped(rp_.pairing.pin);
        const Rect box{x, top, w, 170.0f};
        list.rounded_rect(box, 18.0f, Color::rgb(0x3880ff, 0.14f));
        const float width100 = canvas.fonts.display.measure(group, 100.0f);
        const float size =
            std::clamp(100.0f * (w - 40.0f) / std::max(width100, 1.0f), 40.0f, 120.0f);
        ui::text(list, canvas.fonts.display, group, box.cx(), box.cy() + size * 0.36f, size, kInk,
                 gfx::Align::center);

        const int left =
            std::clamp(static_cast<int>(std::ceil(rp_left_)), 0, static_cast<int>(kPinSeconds));
        float y = box.y + box.h + 50.0f;
        ui::text(list, canvas.fonts.semibold, countdown_text(left), card.cx(), y, 30.0f, kInk,
                 gfx::Align::center);
        const Rect track{x, y + 22.0f, w, 8.0f};
        list.rounded_rect(track, 4.0f, Color::rgb(0x3880ff, 0.2f));
        const float share = std::clamp(static_cast<float>(left) / kPinSeconds, 0.0f, 1.0f);
        if (share > 0.0f)
            list.rounded_rect({track.x, track.y, std::max(track.w * share, 8.0f), track.h}, 4.0f,
                              kBlue);
        y = note(canvas, pin_status_text(rp_.pairing), x, y + 78.0f, w, 22.0f, kInk);
        return y;
    }

    ui::Theme theme_;
    ui::ListView list_;
    RpState rp_;
    std::uint64_t seq_ = 0;
    int pending_ = 0;      // posts on their way (the source counts them; a press counts at once)
    int posted_tag_ = -1;  // the row whose post is on its way: its value shows "…"
    float rp_left_ = 0.0f; // the PIN's seconds left, counted down between the polls
    std::string key_;
};

} // namespace

std::unique_ptr<Page> make_remoteplay_page()
{
    return std::make_unique<RemotePlayPage>();
}

} // namespace hui::ps5cc
