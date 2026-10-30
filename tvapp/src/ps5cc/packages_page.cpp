// ps5cc - The Pakete page (Spiele → Pakete): the packages the console app found, installing them, the queue.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's Pakete tab: ✕ installs the package in focus at once (the console app checks everything again
// and says no in words when something is in the way), □ puts it into the queue or takes it out, △ opens the rest
// (search again, start or stop the queue, cancel the install). Splitting a package and sending one from the PC stay
// in the web app.

#include "ps5cc/covers.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

// Where the card's lower part (queue, install, search) starts: under the details, above the toasts in the corner.
constexpr float kStatusY = kTop + 470.0f;

enum MenuTag
{
    kScan = 1,
    kQueueGo,
    kInstallCancel,
    kQueueRetry,
    kQueueClear,
};

const char *kind_label(const std::string &kind)
{
    if (kind == "update")
        return "Update";
    if (kind == "dlc")
        return "Zusatzinhalt";
    return "Spiel";
}

const char *plat_label(int plat)
{
    return plat == 5 ? "PS5" : plat == 4 ? "PS4" : "unbekannt";
}

const char *queue_badge(const std::string &state)
{
    if (state == "waiting")
        return "WARTET";
    if (state == "running")
        return "LÄUFT";
    if (state == "done")
        return "FERTIG";
    if (state == "failed" || state == "skipped" || state == "cancelled")
        return "FEHLER";
    return "";
}

const char *install_state(const PackageInstall &j)
{
    if (j.cancelling)
        return "wird abgebrochen …";
    if (j.state == "preparing" || j.state == "starting")
        return "wird vorbereitet …";
    if (j.state == "installing")
        return "wird installiert";
    if (j.state == "finishing")
        return "wird abgeschlossen …";
    if (j.state == "done")
        return "installiert";
    if (j.state == "failed")
        return "gescheitert";
    if (j.state == "cancelled")
        return "abgebrochen";
    return "";
}

std::string remain_text(int seconds)
{
    if (seconds < 0)
        return {};
    if (seconds < 60)
        return "noch " + std::to_string(std::max(seconds, 1)) + " s";
    return "noch " + std::to_string((seconds + 30) / 60) + " min";
}

class PackagesPage final : public Page
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
        list_.style.value_width = 130.0f; // the badges line up
        list_.style.leading_width = 76.0f;
        list_.leading = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int, float)
        {
            const Package *p = package_of(item);
            if (p == nullptr)
                return;
            const float side = std::min(56.0f, row.h - 20.0f);
            draw_icon(canvas, {row.x + 16.0f, row.y + (row.h - side) * 0.5f, side, side}, *p);
        };
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 40.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 170.0f, kRightW - 60.0f, 300.0f});

        bar_.style.theme = theme;
        bar_.style.height = 14.0f;
        bar_.style.sheen = true;
        bar_.style.text_size = 20.0f;
        bar_.set_bounds({kRightX + 30.0f, kStatusY + 62.0f, kRightW - 60.0f, 48.0f});

        menu_.style.theme = solid_theme(theme);
        menu_.style.width = 520.0f;
        menu_.style.scrim = 0.35f;
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});
    }

    void absorb(const Snapshot &data) override
    {
        packages_ = data.packages;
        scan_ = data.package_scan;
        job_ = data.package_install;
        queue_ = data.package_queue;
        rebuild();
        const bool known = job_.bytes_total > 0.0 && job_.percent >= 0;
        bar_.style.mode = job_.active && !known ? ui::ProgressMode::indeterminate : ui::ProgressMode::determinate;
        bar_.style.percent = known;
        bar_.style.status = job_.state == "failed"      ? ui::Status::danger
                            : job_.state == "cancelled" ? ui::Status::warning
                            : job_.active               ? ui::Status::accent
                                                        : ui::Status::success;
        std::string label = "„" + job_.name + "“ " + install_state(job_);
        const std::string remain = job_.active ? remain_text(job_.remain_s) : std::string();
        if (!remain.empty())
            label += " · " + remain;
        bar_.label = label;
        bar_.set_value(known ? std::clamp(static_cast<float>(job_.percent) / 100.0f, 0.0f, 1.0f)
                             : (job_.state == "done" ? 1.0f : 0.0f));
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
        const Package *p = focused();
        if (event == ui::Event::activated)
        {
            if (p == nullptr || job_.active)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            feedback.play(audio::Cue::select);
            source().package_action("install", p->id);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            if (p == nullptr)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            const PackageQueueItem *q = queued(p->id);
            if (q != nullptr && q->state == "running")
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            feedback.play(audio::Cue::select);
            source().package_action(q != nullptr && q->state == "waiting" ? "queue_remove" : "queue_add", p->id);
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
        // The worker fetches the picture in focus before the others.
        if (const Package *p = focused())
            covers().prefer(package_key(p->id));
    }

    void draw(ui::Canvas &canvas) const override
    {
        list_.draw(canvas);

        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);

        const Package *p = focused();
        float title_w = card.w - 60.0f;
        if (p != nullptr)
        {
            draw_icon(canvas, {card.x + card.w - 30.0f - 96.0f, card.y + 30.0f, 96.0f, 96.0f}, *p);
            title_w -= 116.0f;
        }
        const std::string title = p != nullptr ? p->name : "Pakete";
        float y = card.y + 66.0f;
        int lines = 0;
        for (const std::string &line : fonts.display.font->wrap(title, 32.0f, title_w))
        {
            if (++lines > 2)
                break;
            ui::text(list, fonts.display, line, card.x + 30.0f, y, 32.0f, kInk);
            y += 42.0f;
        }
        if (p == nullptr)
        {
            float ty = card.y + 120.0f;
            for (const std::string &line : fonts.regular.font->wrap(
                     "Pakete (.pkg) auf USB-Speichern, M.2 und in /data/pkg. Aufteilen und vom PC senden geht in der "
                     "Browser-App.",
                     21.0f, card.w - 60.0f))
            {
                ui::text(list, fonts.regular, line, card.x + 30.0f, ty, 21.0f, kMuted);
                ty += 29.0f;
            }
        }
        else
        {
            info_.draw(canvas);
        }

        // The bottom of the card: the install that runs (or the last one), the queue, a search.
        const float line_y = kStatusY;
        list.line(card.x + 30.0f, line_y, card.x + card.w - 30.0f, line_y, 1.0f, Color::rgb(0x3880ff, 0.18f));
        ui::text(list, fonts.semibold, queue_line(), card.x + 30.0f, line_y + 38.0f, 21.0f,
                 queue_.halted ? kWarning : kMuted);
        if (job_.state != "idle")
        {
            bar_.draw(canvas);
            const std::string why = !job_.error.empty() ? job_.error : job_.note;
            if (!why.empty() && !job_.active)
                ui::text(list, fonts.regular, fonts.regular.font->wrap(why, 19.0f, card.w - 60.0f).front(),
                         card.x + 30.0f, kStatusY + 150.0f, 19.0f, job_.state == "failed" ? kCritical : kMuted);
        }
        else if (scan_.scanning)
        {
            std::string text = "Die Suche läuft … " + std::to_string(scan_.parsed) + " gelesen";
            ui::text(list, fonts.regular, text, card.x + 30.0f, kStatusY + 100.0f, 21.0f, kBlue);
        }
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        menu_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        const Package *p = focused();
        if (p != nullptr && !job_.active)
            out[n++] = {ui::Button::cross, "Installieren"};
        if (p != nullptr)
        {
            const PackageQueueItem *q = queued(p->id);
            out[n++] = {ui::Button::square, q != nullptr && q->state == "waiting" ? "Aus der Warteschlange"
                                                                                   : "In die Warteschlange"};
        }
        out[n++] = {ui::Button::triangle, "Mehr"};
        return n;
    }

  private:
    const Package *package_of(const ui::ListItem &item) const
    {
        if (item.header || item.tag < 0 || item.tag >= static_cast<int>(packages_.size()))
            return nullptr;
        return &packages_[static_cast<std::size_t>(item.tag)];
    }

    const Package *focused() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return nullptr;
        return package_of(list_.items()[static_cast<std::size_t>(i)]);
    }

    // The newest entry of the queue for a package (an old finished one stays listed until it is cleared).
    const PackageQueueItem *queued(const std::string &id) const
    {
        const PackageQueueItem *found = nullptr;
        for (const PackageQueueItem &q : queue_.items)
        {
            if (q.id == id)
                found = &q;
        }
        return found;
    }

    // The package's own picture once it is loaded, a tile with its kind until then or without one.
    void draw_icon(ui::Canvas &canvas, const Rect &box, const Package &p) const
    {
        const CoverShown pic = p.has_icon ? covers().texture_of(package_key(p.id)) : CoverShown{};
        const float shown = pic.texture == 0 ? 0.0f : pic.alpha;
        if (shown < 1.0f)
        {
            const Color tint = p.kind == "update" ? Color::rgb(0x168f68)
                               : p.kind == "dlc"  ? Color::rgb(0x7f56d9)
                               : p.plat == 4      ? Color::rgb(0x2f5fb3)
                                                  : Color::rgb(0x3880ff);
            canvas.list.rounded_rect(box, box.w * 0.22f, tint.with_alpha(0.85f * (1.0f - shown)));
            const char *mark = p.kind == "update" ? "UP" : p.kind == "dlc" ? "DLC" : p.plat == 4 ? "P4" : "P5";
            const float size = box.w * 0.34f;
            ui::text(canvas.list, canvas.fonts.semibold, mark, box.x + box.w * 0.5f,
                     box.y + box.h * 0.5f + size * 0.36f, size, Color::rgb(0xffffff).with_alpha(1.0f - shown),
                     gfx::Align::center);
        }
        if (shown > 0.0f)
            canvas.list.image(pic.texture, box, gfx::kFullUv, Color{1.0f, 1.0f, 1.0f, shown}, box.w * 0.22f);
    }

    std::string queue_line() const
    {
        if (queue_.items.empty())
            return "Warteschlange: leer";
        std::string text = "Warteschlange: " + std::to_string(queue_.waiting) + " wartend";
        if (queue_.done > 0)
            text += " · " + std::to_string(queue_.done) + " fertig";
        if (queue_.failed > 0)
            text += " · " + std::to_string(queue_.failed) + " mit Fehler";
        if (queue_.running)
            text += queue_.pause_requested ? " · hält nach diesem an" : " · läuft";
        else if (queue_.halted && !queue_.halt_reason.empty())
            text += " · angehalten: " + queue_.halt_reason;
        return text;
    }

    void open_menu(ui::Feedback &feedback)
    {
        std::vector<ui::MenuItem> items;
        const auto add = [&](const char *label, int tag, bool danger = false)
        {
            ui::MenuItem m;
            m.label = label;
            m.tag = tag;
            m.danger = danger;
            items.push_back(m);
        };
        add(scan_.scanning ? "Suche läuft …" : "Neu suchen", kScan);
        items.back().disabled = scan_.scanning;
        if (queue_.running)
            add(queue_.pause_requested ? "Hält nach diesem Paket an" : "Warteschlange anhalten", kQueueGo);
        else
            add(queue_.halted ? "Warteschlange fortsetzen" : "Warteschlange starten", kQueueGo);
        items.back().disabled = queue_.pause_requested || (!queue_.running && queue_.waiting == 0);
        if (job_.active)
        {
            add(job_.cancelling ? "Wird abgebrochen …" : "Installation abbrechen", kInstallCancel, true);
            items.back().disabled = job_.cancelling || !job_.can_cancel;
        }
        if (queue_.failed > 0)
            add("Fehlgeschlagene wiederholen", kQueueRetry);
        if (queue_.done > 0)
            add("Erledigte entfernen", kQueueClear);
        menu_.set_items(items);
        menu_.open({kRightX, kTop + 10.0f, kRightW, 80.0f}, feedback);
    }

    // Each fires at once – nothing is asked twice (user, 27.10.2026).
    void run(int tag, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        switch (tag)
        {
        case kScan:
            source().package_action("scan", {});
            break;
        case kQueueGo:
            source().package_action(queue_.running ? "queue_pause" : "queue_start", {});
            break;
        case kInstallCancel:
            source().package_action("install_cancel", {});
            break;
        case kQueueRetry:
            source().package_action("queue_retry", {});
            break;
        case kQueueClear:
            source().package_action("queue_clear", {});
            break;
        default:
            break;
        }
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
        if (!scan_.loaded)
            return {note("Wird geladen …", "Die App liest die Liste der Pakete.")};
        if (packages_.empty())
        {
            if (scan_.scanning || !scan_.ever)
                return {note("Die Suche läuft …", "USB-Speicher, M.2 und /data/pkg werden nach .pkg-Dateien durchsucht.")};
            return {note("Keine Pakete gefunden",
                         "Leg .pkg-Dateien auf einen USB-Speicher oder nach /data/pkg; Mehr → Neu suchen.")};
        }
        // By drive, in the order the app found them.
        std::vector<std::string> drives;
        for (const Package &p : packages_)
        {
            if (std::find(drives.begin(), drives.end(), p.drive_label) == drives.end())
                drives.push_back(p.drive_label);
        }
        for (const std::string &drive : drives)
        {
            ui::ListItem h;
            h.title = drive;
            h.header = true;
            h.tag = -1;
            out.push_back(h);
            for (std::size_t i = 0; i < packages_.size(); ++i)
            {
                const Package &p = packages_[i];
                if (p.drive_label != drive)
                    continue;
                ui::ListItem item;
                item.title = p.name.empty() ? p.file : p.name;
                item.subtitle = std::string(kind_label(p.kind)) + " · " + p.version + " · " + plat_label(p.plat) +
                                " · " + p.title_id;
                item.value = format_bytes(p.total > 0 ? p.total : p.size);
                if (const PackageQueueItem *q = queued(p.id))
                    item.badge = queue_badge(q->state);
                if (item.badge.empty() && p.parts_total > 0 && p.parts_found < p.parts_total)
                    item.badge = "TEILE " + std::to_string(p.parts_found) + "/" + std::to_string(p.parts_total);
                item.tag = static_cast<int>(i);
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
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge;
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
        std::string key;
        if (const Package *p = focused())
        {
            rows.push_back({"Art", std::string(kind_label(p->kind)) + " · " + plat_label(p->plat)});
            rows.push_back({"Version", p->version});
            rows.push_back({"Titel-ID", p->title_id});
            rows.push_back({"Größe", format_bytes(p->total > 0 ? p->total : p->size)});
            if (p->parts_total > 0)
                rows.push_back({"Teile", std::to_string(p->parts_found) + " von " + std::to_string(p->parts_total) +
                                             (p->parts_found < p->parts_total ? " gefunden – es fehlen Teile" : "")});
            rows.push_back({"Ort", p->drive_label});
            if (const PackageQueueItem *q = queued(p->id))
            {
                std::string state = queue_badge(q->state);
                state = state == "WARTET"   ? "wartet"
                        : state == "LÄUFT"  ? "wird installiert"
                        : state == "FERTIG" ? "erledigt"
                                            : "Fehler";
                rows.push_back({"Warteschlange", q->message.empty() ? state : state + " – " + q->message});
            }
        }
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
    std::string key_;
    std::string info_key_ = "\x01";

    std::vector<Package> packages_;
    PackageScan scan_;
    PackageInstall job_;
    PackageQueue queue_;
};

} // namespace

std::unique_ptr<Page> make_packages_page()
{
    return std::make_unique<PackagesPage>();
}

} // namespace hui::ps5cc
