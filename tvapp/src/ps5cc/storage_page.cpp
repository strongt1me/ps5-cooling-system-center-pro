// ps5cc - The Speicher page (System): the console's drives, and a USB drive ejected safely.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the drives on the web app's System page: each with its free space; ✕ on a USB drive ejects it through
// ShadowMountPlus (1.7beta10). The app itself refuses while one of its jobs runs or PS4Mount plays games from the
// drive (src/usbeject.c), and says why; nothing is asked twice (user, 27.10.2026).

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

class StoragePage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.row_height = 96.0f;
        list_.style.title_size = 26.0f;
        list_.style.subtitle_size = 19.0f;
        list_.style.value_size = 21.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 130.0f, kRightW - 60.0f, 220.0f});
    }

    void absorb(const Snapshot &data) override
    {
        volumes_ = data.volumes;
        loaded_ = data.volumes_loaded;
        eject_ = data.usb_eject;
        rebuild();
    }

    void enter() override
    {
        list_.enter();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
            refresh_info();
        if (event != ui::Event::activated)
            return;
        const Volume *v = focused();
        if (v == nullptr || !can_eject(*v))
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        feedback.play(audio::Cue::select);
        source().usb_eject(v->path);
    }

    void update(float dt) override
    {
        list_.update(dt);
        info_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);
        const Volume *v = focused();
        ui::text(list, fonts.display, ui::fit_label(paint, v != nullptr ? v->label : "Speicher", 32.0f, card.w - 60.0f),
                 card.x + 30.0f, card.y + 66.0f, 32.0f, kInk);
        float y = card.y + 360.0f;
        if (v != nullptr)
        {
            // how full: a bar under the name, as the web app's
            const float frac =
                v->total_bytes > 0 ? static_cast<float>(static_cast<double>(v->used_bytes) / v->total_bytes) : 0.0f;
            const Rect bar{card.x + 30.0f, card.y + 92.0f, card.w - 60.0f, 10.0f};
            list.rounded_rect(bar, 5.0f, Color::rgb(0x3880ff, 0.18f));
            list.rounded_rect({bar.x, bar.y, bar.w * std::clamp(frac, 0.0f, 1.0f), bar.h}, 5.0f,
                              frac >= 0.92f ? kCritical : frac >= 0.8f ? kWarning : kBlue);
            info_.draw(canvas);
            const char *about =
                v->ejectable
                    ? (eject_.available
                           ? "Die X-Taste wirft das Laufwerk sicher aus. Solange ein Auftrag der App läuft (Kopieren, "
                             "Konvertieren, Sichern, Dateimanager …) oder PS4Mount Spiele davon spielt, lehnt die App "
                             "ab und sagt, was noch läuft."
                           : "Auswerfen braucht ShadowMountPlus ab 1.7beta10; es läuft nicht oder ist älter.")
                    : "Dieses Laufwerk lässt sich nicht auswerfen.";
            y = paragraph(canvas, about, card.x + 30.0f, y, card.w - 60.0f, v->ejectable ? kInk : kMuted, 6);
        }
        // the last eject, until the next one: what happened
        if (!eject_.state.empty() && !eject_.message.empty())
        {
            const Color ink = eject_.state == "done" ? kGood : eject_.state == "running" ? kWarning : kCritical;
            paragraph(canvas, eject_.message, card.x + 30.0f, std::max(y + 30.0f, card.y + 600.0f), card.w - 60.0f, ink, 4);
        }
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        if (const Volume *v = focused(); v != nullptr && can_eject(*v))
            out[n++] = {ui::Button::cross, "Auswerfen"};
        return n;
    }

  private:
    bool can_eject(const Volume &v) const
    {
        return v.ejectable && eject_.available && !eject_.running;
    }

    const Volume *focused() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return nullptr;
        const int tag = list_.items()[static_cast<std::size_t>(i)].tag;
        if (tag < 0 || tag >= static_cast<int>(volumes_.size()))
            return nullptr;
        return &volumes_[static_cast<std::size_t>(tag)];
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

    std::vector<ui::ListItem> items() const
    {
        std::vector<ui::ListItem> out;
        if (!loaded_)
        {
            ui::ListItem n;
            n.title = "Wird gelesen …";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
            return out;
        }
        for (std::size_t i = 0; i < volumes_.size(); ++i)
        {
            const Volume &v = volumes_[i];
            ui::ListItem item;
            item.title = v.label;
            item.subtitle = format_bytes(v.free_bytes) + " frei von " + format_bytes(v.total_bytes) + " · " + v.path;
            const int pct = v.total_bytes > 0 ? static_cast<int>(100.0 * static_cast<double>(v.used_bytes) /
                                                                     static_cast<double>(v.total_bytes) +
                                                                 0.5)
                                              : 0;
            item.value = std::to_string(pct) + " %";
            if (eject_.running && eject_.mount == v.path)
                item.badge = "WIRD AUSGEWORFEN";
            else if (v.ejectable)
                item.badge = "USB";
            item.tag = static_cast<int>(i);
            out.push_back(item);
        }
        if (volumes_.empty())
        {
            ui::ListItem n;
            n.title = "Keine Laufwerke lesbar";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
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
        if (const Volume *v = focused())
        {
            rows.push_back({"Ordner", v->path});
            rows.push_back({"Belegt", format_bytes(v->used_bytes)});
            rows.push_back({"Frei", format_bytes(v->free_bytes)});
            rows.push_back({"Gesamt", format_bytes(v->total_bytes)});
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
    std::string key_;
    std::string info_key_ = "\x01";
    std::vector<Volume> volumes_;
    bool loaded_ = false;
    UsbEject eject_;
};

} // namespace

std::unique_ptr<Page> make_storage_page()
{
    return std::make_unique<StoragePage>();
}

} // namespace hui::ps5cc
