// ps5cc - The Speicher page (System): the console's drives, and a USB drive ejected safely.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the drives on the web app's System page: each with its free space; ✕ on a USB drive ejects it through
// ShadowMountPlus (1.7beta10). The app itself refuses while one of its jobs runs or PS4Mount plays games from the
// drive (src/usbeject.c), and says why; nothing is asked twice (user, 27.10.2026).
// Under the drives: the library's cover and metadata folder (GET /api/v1/library/cache), moved here from the
// library window (user, 29.10.2026), and the pictures behind the areas, folded in until the X button opens them (moved
// from Kühlung → Einstellungen, user 29.10.2026). Each part has its own title.

#include "ps5cc/backdrops.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr int kCacheToggle = 1000; // list tags past the drives
constexpr int kCacheClear = 1001;
constexpr int kBgToggle = 1100; // the row that folds the pictures in and out
constexpr int kBgArea = 1101;   // + area
constexpr const char *kAreaLabels[kBackdropAreas] = {"Kühlung", "Spiele", "Dateimanager", "Payloads",
                                                     "System",  "Profil", "Protokoll"};
constexpr const char *kModeLabels[3] = {"Standard", "Eigenes Bild", "Aus"};

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
        list_.style.badge_column = true;
        list_.style.header_rule = true;
        list_.style.value_width = 80.0f; // the badges line up
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
        if (data.backdrops_loaded)
        {
            for (const BackdropSetting &b : data.backdrops)
            {
                const int area = backdrop_area(b.id);
                if (area < 0)
                    continue;
                const auto a = static_cast<std::size_t>(area);
                bg_custom_[a] = b.custom;
                // a choice made here holds until the app reports it, so the row does not jump back meanwhile
                const int mode = b.mode == BackdropMode::custom ? 1 : b.mode == BackdropMode::off ? 2 : 0;
                if (bg_wanted_[a] < 0 || bg_wanted_[a] == mode)
                {
                    bg_wanted_[a] = -1;
                    bg_mode_[a] = mode;
                }
            }
            bg_loaded_ = true;
        }
        if (data.library_tools.seq != cache_seq_)
        {
            cache_seq_ = data.library_tools.seq;
            read_cache(data.library_tools.cache);
        }
        rebuild();
    }

    void enter() override
    {
        list_.enter();
        source().library_tools_fetch();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        const int before = list_.focus();
        const ui::Event event = list_.handle(input, feedback);
        if (list_.focus() != before)
            refresh_info();
        if (event != ui::Event::activated)
            return;
        const int tag = focused_tag();
        if (tag == kBgToggle)
        {
            feedback.play(audio::Cue::select);
            bg_open_ = !bg_open_;
            rebuild();
            return;
        }
        if (tag >= kBgArea && tag < kBgArea + kBackdropAreas)
        {
            const auto a = static_cast<std::size_t>(tag - kBgArea);
            // Standard -> Eigenes Bild -> Aus, round; without an uploaded picture "Eigenes Bild" is left out
            int next = (bg_mode_[a] + 1) % 3;
            if (next == 1 && !bg_custom_[a])
                next = 2;
            feedback.play(audio::Cue::select);
            bg_mode_[a] = next;
            bg_wanted_[a] = next;
            source().set_backdrop(backdrop_name(tag - kBgArea), next == 1   ? BackdropMode::custom
                                                                : next == 2 ? BackdropMode::off
                                                                            : BackdropMode::standard);
            rebuild();
            return;
        }
        if (tag == kCacheToggle || tag == kCacheClear)
        {
            if (tag == kCacheClear && cache_titles_ <= 0)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            feedback.play(audio::Cue::select);
            if (tag == kCacheToggle)
            {
                cache_on_ = !cache_on_;
                source().save_config(std::string("{\"library_cache\":") + (cache_on_ ? "1" : "0") + "}");
                source().library_tools_fetch();
                rebuild();
            }
            else
                source().library_tools_post("library/cache/clear", "{}");
            return;
        }
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
        const int tag = focused_tag();
        if (tag == kBgToggle || (tag >= kBgArea && tag < kBgArea + kBackdropAreas))
        {
            ui::text(list, fonts.display, "Hintergrundbilder", card.x + 30.0f, card.y + 66.0f, 32.0f, kInk);
            const float y0 = paragraph(canvas,
                                       "Das Bild hinter jedem Bereich der App. Die X-Taste wechselt zwischen "
                                       "Standard, Eigenes Bild und Aus (ein schlichter Verlauf).",
                                       card.x + 30.0f, card.y + 130.0f, card.w - 60.0f, kInk, 6);
            paragraph(canvas,
                      "Eigene Bilder lädst du in der Browser-App hoch: System → Hintergründe. Ohne hochgeladenes "
                      "Bild wird „Eigenes Bild“ übersprungen.",
                      card.x + 30.0f, y0 + 24.0f, card.w - 60.0f, kMuted, 6);
            return;
        }
        if (tag == kCacheToggle || tag == kCacheClear)
        {
            ui::text(list, fonts.display, "Covers & Metadaten", card.x + 30.0f, card.y + 66.0f, 32.0f, kInk);
            const float y0 = paragraph(canvas,
                                       "Die App speichert Cover, Namen und Größen der Spiele auf der Konsole. So ist "
                                       "die Bibliothek nach einem Neustart sofort mit Bildern da, ohne alles neu zu "
                                       "lesen.",
                                       card.x + 30.0f, card.y + 130.0f, card.w - 60.0f, kInk, 6);
            paragraph(canvas,
                      "Ordner leeren löscht nur diese gespeicherten Bilder und Angaben; die App liest sie beim "
                      "nächsten Öffnen der Bibliothek neu. Spiele und Spielstände bleiben unberührt.",
                      card.x + 30.0f, y0 + 24.0f, card.w - 60.0f, kMuted, 6);
            return;
        }
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
        const int tag = focused_tag();
        if (tag == kBgToggle)
            out[n++] = {ui::Button::cross, bg_open_ ? "Zuklappen" : "Aufklappen"};
        else if (tag >= kBgArea && tag < kBgArea + kBackdropAreas)
            out[n++] = {ui::Button::cross, "Wechseln"};
        else if (tag == kCacheToggle)
            out[n++] = {ui::Button::cross, cache_on_ ? "Ausschalten" : "Einschalten"};
        else if (tag == kCacheClear && cache_titles_ > 0)
            out[n++] = {ui::Button::cross, "Leeren"};
        else if (const Volume *v = focused(); v != nullptr && can_eject(*v))
            out[n++] = {ui::Button::cross, "Auswerfen"};
        return n;
    }

  private:
    bool can_eject(const Volume &v) const
    {
        return v.ejectable && eject_.available && !eject_.running;
    }

    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1;
        return list_.items()[static_cast<std::size_t>(i)].tag;
    }

    const Volume *focused() const
    {
        const int tag = focused_tag();
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
        out.push_back(header("LAUFWERKE"));
        if (!loaded_)
        {
            ui::ListItem n;
            n.title = "Wird gelesen …";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
        }
        for (std::size_t i = 0; loaded_ && i < volumes_.size(); ++i)
            out.push_back(volume_item(i));
        if (loaded_ && volumes_.empty())
        {
            ui::ListItem n;
            n.title = "Keine Laufwerke lesbar";
            n.disabled = true;
            n.tag = -1;
            out.push_back(n);
        }
        out.push_back(header("COVERS & METADATEN"));
        append_cache(out);
        out.push_back(header("HINTERGRUNDBILDER"));
        ui::ListItem fold;
        fold.title = "Hintergrundbilder der Bereiche";
        int own = 0;
        for (const bool c : bg_custom_)
            own += c ? 1 : 0;
        fold.subtitle = std::to_string(kBackdropAreas) + " Bereiche · " +
                        (own == 0   ? std::string("kein eigenes Bild")
                         : own == 1 ? std::string("1 eigenes Bild")
                                    : std::to_string(own) + " eigene Bilder");
        fold.tag = kBgToggle;
        out.push_back(fold);
        for (int i = 0; bg_open_ && i < kBackdropAreas; ++i)
        {
            const auto a = static_cast<std::size_t>(i);
            ui::ListItem item;
            item.title = kAreaLabels[i];
            item.subtitle = bg_custom_[a] ? "Ein eigenes Bild ist hochgeladen." : "Kein eigenes Bild hochgeladen.";
            item.value = bg_loaded_ ? kModeLabels[std::clamp(bg_mode_[a], 0, 2)] : "…";
            item.tag = kBgArea + i;
            out.push_back(item);
        }
        return out;
    }

    static ui::ListItem header(const char *title)
    {
        ui::ListItem h;
        h.title = title;
        h.header = true;
        h.tag = -1;
        return h;
    }

    ui::ListItem volume_item(std::size_t i) const
    {
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
            return item;
        }
    }

    void append_cache(std::vector<ui::ListItem> &out) const
    {
        ui::ListItem toggle;
        toggle.title = "Covers & Metadaten speichern";
        toggle.subtitle = "Damit die Bibliothek nach einem Neustart sofort mit Bildern da ist.";
        toggle.value = cache_on_ ? "An" : "Aus";
        toggle.tag = kCacheToggle;
        out.push_back(toggle);
        ui::ListItem clear;
        clear.title = "Covers-Ordner leeren";
        clear.subtitle = cache_known_ ? std::to_string(cache_titles_) + " Spiele gespeichert · " + format_bytes(cache_bytes_)
                                      : std::string("Wird gelesen …");
        clear.disabled = cache_titles_ <= 0;
        clear.tag = kCacheClear;
        out.push_back(clear);
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

    void read_cache(const std::string &text)
    {
        cJSON *root = text.empty() ? nullptr : cJSON_Parse(text.c_str());
        cache_known_ = root != nullptr;
        if (root != nullptr)
        {
            const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "enabled");
            const cJSON *titles = cJSON_GetObjectItemCaseSensitive(root, "titles");
            const cJSON *bytes = cJSON_GetObjectItemCaseSensitive(root, "bytes");
            cache_on_ = !cJSON_IsBool(on) || cJSON_IsTrue(on);
            cache_titles_ = cJSON_IsNumber(titles) ? titles->valueint : 0;
            cache_bytes_ = cJSON_IsNumber(bytes) && bytes->valuedouble > 0.0 ? static_cast<std::uint64_t>(bytes->valuedouble) : 0;
            cJSON_Delete(root);
        }
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
    std::uint64_t cache_seq_ = 0;
    bool cache_known_ = false;
    bool cache_on_ = true;
    int cache_titles_ = 0;
    std::uint64_t cache_bytes_ = 0;
    bool bg_open_ = false;
    bool bg_loaded_ = false;
    std::array<bool, kBackdropAreas> bg_custom_{};
    std::array<int, kBackdropAreas> bg_mode_{};
    std::array<int, kBackdropAreas> bg_wanted_{-1, -1, -1, -1, -1, -1, -1};
};

} // namespace

std::unique_ptr<Page> make_storage_page()
{
    return std::make_unique<StoragePage>();
}

} // namespace hui::ps5cc
