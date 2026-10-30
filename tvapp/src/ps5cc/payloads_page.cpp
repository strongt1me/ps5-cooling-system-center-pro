// ps5cc - The Payloads page: own payloads to start, the running ones, the payload store.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/covers.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ps5cc/payload_match.hpp"
#include "ps5cc/payload_tile.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Rect;

enum View
{
    kFiles = 0,
    kRunning = 1,
    kStore = 2,
    kViews = 3,
};

constexpr float kListTop = kTop + 10.0f;

class PayloadsPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        style_subtabs(views_, theme, 3);
        views_.set_tabs({{"Meine Payloads"}, {"Bereits geladen"}, {"Payload-Store"}});
        views_.set_active(kFiles, true);

        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.header_rule = true; // title and line, as the forms (user, 29.10.2026)
        list_.style.row_height = 76.0f;
        list_.style.title_size = 24.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 20.0f;
        list_.style.header_size = 18.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.badge_column = true;
        list_.style.value_width = 110.0f; // the badges line up
        list_.style.leading_width = 72.0f;
        list_.leading = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int index, float focus)
        {
            (void)index;
            (void)focus;
            PayloadTile tile;
            if (!tile_of(item, tile))
                return;
            const float side = std::min(52.0f, row.h - 20.0f);
            const Rect box{row.x + 16.0f, row.y + (row.h - side) * 0.5f, side, side};
            draw_icon(canvas, box, item, tile, item.disabled ? 0.45f : 1.0f);
            // every payload carries a dot on its picture's corner: softly glowing green when it is loaded, a dark one
            // that does not glow when it is not (user, 29.10.2026; Meine Payloads, Bereits geladen, Payload-Store)
            // a store entry not yet downloaded has none: there is nothing on the console that could run
            const bool none = view_ == kStore && item.tag >= 0 && static_cast<std::size_t>(item.tag) < store_.size() &&
                              store_[static_cast<std::size_t>(item.tag)].installed_file.empty();
            if (item.tag >= 0 && !none)
            {
                const float cx = box.x + box.w - 3.0f, cy = box.y + box.h - 3.0f;
                canvas.list.circle(cx, cy, 8.5f, kNavy);
                if (loaded(item))
                {
                    const float pulse = 0.5f + 0.5f * std::sin(canvas.time * 2.6f + static_cast<float>(item.tag) * 0.7f);
                    canvas.list.circle(cx, cy, 13.0f + 3.0f * pulse, kGood.with_alpha(0.18f + 0.22f * pulse));
                    canvas.list.circle(cx, cy, 8.5f, kNavy);
                    canvas.list.circle(cx, cy, 6.5f, gfx::Color::rgb(0x2fd44a));
                }
                else
                    canvas.list.circle(cx, cy, 6.0f, gfx::Color::rgb(0x4a5677));
            }
        };
        list_.set_bounds({kLeftX, kListTop, kLeftW, kBottom - kListTop});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 21.0f;
        info_.style.row_height = 42.0f;
        info_.style.max_lines = 1;

        bar_.style.theme = theme;
        bar_.style.height = 14.0f;
        bar_.style.sheen = true;
        bar_.style.text_size = 20.0f;
        bar_.set_bounds({kRightX + 30.0f, kBottom - 150.0f, kRightW - 60.0f, 48.0f});

        // Under at most two lines of title and four of text.
        info_.set_bounds({kRightX + 30.0f, kTop + 300.0f, kRightW - 60.0f, kBottom - kTop - 480.0f});
    }

    void absorb(const Snapshot &data) override
    {
        files_ = data.payload_files;
        files_loaded_ = data.payload_files_loaded;
        running_ = data.running;
        store_ = data.store;
        store_loaded_ = data.store_loaded;
        store_fetching_ = data.store_fetching;
        store_error_ = data.store_error;
        job_ = data.store_job;
        rebuild(false);
        const bool known = job_.total > 0.0;
        bar_.style.mode = job_.state == "running" && !known ? ui::ProgressMode::indeterminate
                                                            : ui::ProgressMode::determinate;
        bar_.style.percent = known;
        bar_.style.status = job_.state == "failed" ? ui::Status::danger
                            : job_.state == "running" ? ui::Status::accent
                                                      : ui::Status::success;
        bar_.label = job_.state == "running" ? "„" + job_.name + "“ wird geladen"
                     : job_.state == "failed" ? "„" + job_.name + "“: " + (job_.error.empty() ? "gescheitert" : job_.error)
                     : job_.state == "done"   ? "„" + job_.name + "“ geladen"
                                              : "";
        bar_.set_value(known ? static_cast<float>(std::clamp(job_.done / job_.total, 0.0, 1.0)) : 0.0f);
    }

    void enter() override
    {
        list_.enter();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            focus_[static_cast<std::size_t>(view_)] = list_.focus();
            if (views_.step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) == ui::Event::changed)
            {
                view_ = views_.active();
                rebuild(true);
                list_.enter();
            }
            return;
        }
        const ui::Event event = list_.handle(input, feedback);
        const ListItemRef item = focused();
        if (event == ui::Event::activated)
        {
            activate(item, feedback);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            if (view_ == kStore)
            {
                source().store_action("refresh", {});
                return;
            }
            if (view_ == kRunning && item.running != nullptr)
            {
                if (item.running->self)
                {
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                    return;
                }
                // □ ends it at once – nothing is asked twice (user, 27.10.2026).
                feedback.play(audio::Cue::select);
                source().kill_payload(item.running->pid);
            }
        }
    }

    void update(float dt) override
    {
        views_.update(dt);
        list_.update(dt);
        if (list_.focus() != info_for_)
            refresh_info();
        info_.update(dt);
        bar_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        views_.draw(canvas);
        draw_subtab_glyphs(canvas, views_);
        list_.draw(canvas);

        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);
        const ListItemRef item = focused();
        std::string title;
        std::string text;
        if (item.file != nullptr)
        {
            title = item.file->name;
            text = item.file->valid ? "Wird beim Starten an den ELF-Loader (Port 9021) gesendet." : item.file->why;
        }
        else if (item.running != nullptr)
        {
            title = item.running->name;
            text = item.running->self ? "Diese App selbst; sie lässt sich hier nicht beenden."
                                      : "Läuft gerade auf der Konsole.";
        }
        else if (item.store != nullptr)
        {
            title = item.store->name;
            text = item.store->description;
        }
        else
        {
            title = view_ == kStore ? "Payload-Store" : view_ == kRunning ? "Bereits geladene Payloads" : "Meine Payloads";
            text = view_ == kFiles ? "Payloads aus /data/PS5-Cooling-Center/payloads und von USB-Sticks."
                                   : view_ == kStore ? "Liste: itsPLK ps5-payloads-mirror."
                                                     : "";
        }
        float title_w = card.w - 60.0f;
        PayloadTile tile;
        const bool has_tile = list_.focus() >= 0 && list_.focus() < static_cast<int>(list_.items().size()) &&
                              tile_of(list_.items()[static_cast<std::size_t>(list_.focus())], tile);
        if (has_tile)
        {
            draw_icon(canvas, {card.x + card.w - 30.0f - 96.0f, card.y + 30.0f, 96.0f, 96.0f},
                      list_.items()[static_cast<std::size_t>(list_.focus())], tile, 1.0f);
            title_w -= 116.0f;
        }
        float y = card.y + 64.0f;
        int lines = 0;
        for (const std::string &line : fonts.display.font->wrap(title, 32.0f, title_w))
        {
            if (++lines > 2)
                break;
            ui::text(list, fonts.display, line, card.x + 30.0f, y, 32.0f, kInk);
            y += 42.0f;
        }
        y += 4.0f;
        lines = 0;
        // Beside the tile while the text is still level with it.
        if (has_tile && y < card.y + 30.0f + 96.0f + 10.0f)
            y = card.y + 30.0f + 96.0f + 34.0f;
        for (const std::string &line : fonts.regular.font->wrap(text, 21.0f, card.w - 60.0f))
        {
            if (++lines > 4)
                break;
            ui::text(list, fonts.regular, line, card.x + 30.0f, y, 21.0f, kMuted);
            y += 29.0f;
        }
        info_.draw(canvas);
        if (view_ == kStore && job_.state != "idle")
            bar_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        out[n++] = {ui::Button::l2, "Ansicht", ui::Button::r2};
        const ListItemRef item = focused();
        if (view_ == kFiles && item.file != nullptr && item.file->valid)
            out[n++] = {ui::Button::cross, "Starten"};
        if (view_ == kRunning && item.running != nullptr && !item.running->self)
            out[n++] = {ui::Button::square, "Beenden"};
        if (view_ == kStore)
        {
            if (item.store != nullptr)
                out[n++] = {ui::Button::cross, store_verb(*item.store)};
            out[n++] = {ui::Button::square, "Liste neu laden"};
        }
        return n;
    }

  private:
    // What the focused row stands for: at most one of the three.
    struct ListItemRef
    {
        const PayloadFile *file = nullptr;
        const RunningPayload *running = nullptr;
        const StoreItem *store = nullptr;
    };

    // The GitHub account whose picture the console app has for this row: a store item's own, or for a file or a running
    // payload the one of the store item with the same name stem. "" when there is none.
    std::string owner_of(const ui::ListItem &item) const
    {
        if (item.header || item.tag < 0)
            return {};
        const std::size_t at = static_cast<std::size_t>(item.tag);
        if (view_ == kStore)
            return at < store_.size() && store_[at].avatar ? store_[at].owner : std::string();
        std::string stem;
        if (view_ == kFiles && at < files_.size())
            stem = payload_stem(files_[at].name);
        else if (view_ == kRunning && at < running_.size())
            stem = payload_stem(running_[at].name);
        if (stem.empty())
            return {};
        for (const StoreItem &s : store_)
            if (s.avatar && !s.owner.empty() && (payload_stem(s.filename) == stem || payload_stem(s.name) == stem))
                return s.owner;
        return {};
    }

    // The developer's picture once it is loaded (faded in over the tile), the coloured tile until then or without one.
    void draw_icon(ui::Canvas &canvas, const Rect &box, const ui::ListItem &item, const PayloadTile &tile,
                   float alpha) const
    {
        const std::string owner = owner_of(item);
        const CoverShown pic = owner.empty() ? CoverShown{} : covers().texture_of(avatar_key(owner));
        const float shown = pic.texture == 0 ? 0.0f : pic.alpha;
        if (shown < 1.0f)
            draw_payload_tile(canvas, box, tile, alpha);
        if (shown > 0.0f)
            canvas.list.image(pic.texture, box, gfx::kFullUv, gfx::Color{1.0f, 1.0f, 1.0f, shown * alpha},
                              box.w * 0.22f);
    }

    // The row's payload as a tile; false for section titles and notes.
    bool tile_of(const ui::ListItem &item, PayloadTile &out) const
    {
        if (item.header || item.tag < 0)
            return false;
        const std::size_t at = static_cast<std::size_t>(item.tag);
        if (view_ == kFiles && at < files_.size())
            out = payload_tile(files_[at].name);
        else if (view_ == kRunning && at < running_.size())
            out = payload_tile(running_[at].name);
        else if (view_ == kStore && at < store_.size())
            out = payload_tile(store_[at].filename.empty() ? store_[at].name : store_[at].filename, store_[at].category);
        else
            return false;
        return true;
    }

    static const char *store_verb(const StoreItem &s)
    {
        if (s.installed_version.empty())
            return "Laden";
        return s.update ? "Aktualisieren" : "Starten";
    }

    ListItemRef focused() const
    {
        ListItemRef ref;
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return ref;
        const ui::ListItem &item = list_.items()[static_cast<std::size_t>(i)];
        if (item.header || item.tag < 0)
            return ref;
        const std::size_t at = static_cast<std::size_t>(item.tag);
        if (view_ == kFiles && at < files_.size())
            ref.file = &files_[at];
        else if (view_ == kRunning && at < running_.size())
            ref.running = &running_[at];
        else if (view_ == kStore && at < store_.size())
            ref.store = &store_[at];
        return ref;
    }

    void activate(const ListItemRef &item, ui::Feedback &feedback)
    {
        if (item.file != nullptr && item.file->valid)
        {
            source().start_payload(*item.file);
            return;
        }
        if (item.store != nullptr)
        {
            if (!item.store->installed_version.empty() && !item.store->update)
                source().store_action("start", item.store->id);
            else if (job_.state == "running")
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            else
                source().store_action("install", item.store->id);
            return;
        }
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    }

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
        if (view_ == kFiles)
        {
            if (!files_loaded_)
                return {note("Wird geladen …", "Die App liest die Ordner.")};
            std::string where;
            for (std::size_t i = 0; i < files_.size(); ++i)
            {
                const PayloadFile &f = files_[i];
                if (f.where != where)
                {
                    where = f.where;
                    out.push_back(header(where));
                }
                ui::ListItem item;
                item.title = f.name;
                item.subtitle = f.valid ? format_bytes(f.size) + " · " + format_date(f.mtime) : "ungültig – " + f.why;
                item.disabled = !f.valid;
                item.tag = static_cast<int>(i);
                out.push_back(item);
            }
            if (out.empty())
                out.push_back(note("Keine Payloads gefunden",
                                   "Leg .elf-Dateien in /data/PS5-Cooling-Center/payloads oder auf einen USB-Stick."));
            return out;
        }
        if (view_ == kRunning)
        {
            // a title above the list, as the other two views have their folders and categories (user, 29.10.2026)
            if (!running_.empty())
                out.push_back(header("BEREITS GELADENE PAYLOADS (" + std::to_string(running_.size()) + ")"));
            for (std::size_t i = 0; i < running_.size(); ++i)
            {
                const RunningPayload &p = running_[i];
                ui::ListItem item;
                item.title = p.name;
                item.subtitle = "PID " + std::to_string(p.pid) + " · " + std::to_string(p.memory_mb) + " MB";
                if (p.self)
                    item.badge = "DIESE APP";
                item.tag = static_cast<int>(i);
                out.push_back(item);
            }
            if (out.empty())
                out.push_back(note("Wird geladen …", "Die App fragt die laufenden Prozesse ab."));
            return out;
        }
        if (!store_loaded_)
            return {note(store_fetching_ ? "Liste wird geladen …" : "Liste nicht geladen",
                         store_error_.empty() ? "Die Quadrat-Taste lädt die Liste der Payloads." : store_error_)};
        std::vector<std::size_t> order(store_.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return store_[a].category < store_[b].category; });
        std::string category;
        for (std::size_t i : order)
        {
            const StoreItem &s = store_[i];
            if (s.category != category)
            {
                category = s.category;
                out.push_back(header(category));
            }
            ui::ListItem item;
            item.title = s.name + "  " + s.version;
            item.subtitle = s.description;
            // "gespeichert", not "geladen": loaded means running now, which the dot shows (29.10.2026)
            item.value = s.installed_version.empty() ? "" : s.update ? "Update" : "gespeichert";
            if (job_.state == "running" && job_.id == s.id)
                item.value = "lädt …";
            item.tag = static_cast<int>(i);
            out.push_back(item);
        }
        return out;
    }

    // Rebuilt only when what the list shows changed: a refill restarts its animation.
    void rebuild(bool switched)
    {
        std::vector<ui::ListItem> next = items();
        std::string key = std::to_string(view_);
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge;
        if (!switched && key == key_)
            return;
        key_ = std::move(key);
        const int keep = switched ? focus_[static_cast<std::size_t>(view_)] : list_.focus();
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
        const ListItemRef item = focused();
        if (item.file != nullptr)
        {
            rows.push_back({"Ort", item.file->where});
            rows.push_back({"Größe", format_bytes(item.file->size)});
            rows.push_back({"Stand", format_date(item.file->mtime)});
        }
        else if (item.running != nullptr)
        {
            rows.push_back({"PID", std::to_string(item.running->pid)});
            rows.push_back({"Speicher", std::to_string(item.running->memory_mb) + " MB"});
        }
        else if (item.store != nullptr)
        {
            rows.push_back({"Version", item.store->version});
            rows.push_back({"Kategorie", item.store->category});
            rows.push_back({"Datei", item.store->filename});
            rows.push_back({"Geladen", item.store->installed_version.empty() ? "nein" : item.store->installed_version});
        }
        info_.set_items(rows);
        info_for_ = list_.focus();
    }

    ui::Theme theme_;
    ui::TabBar views_;
    ui::ListView list_;
    ui::DetailList info_;
    ui::ProgressBar bar_;
    int view_ = kFiles;
    std::array<int, kViews> focus_{};
    std::string key_;
    int info_for_ = -2;

    // Whether the row's payload runs: the running list always; a file or a store entry when a running name is it.
    bool loaded(const ui::ListItem &item) const
    {
        if (item.tag < 0)
            return false;
        const std::size_t i = static_cast<std::size_t>(item.tag);
        if (view_ == kRunning)
            return i < running_.size();
        std::vector<RunningRef> running;
        for (const RunningPayload &r : running_)
        {
            if (!r.self)
                running.push_back({r.name, r.file});
        }
        // the payload folder of the console (the store downloads into it): what two versions are judged against
        std::vector<std::string> folder;
        for (const PayloadFile &f : files_)
            folder.push_back(f.name);
        if (view_ == kFiles)
            return i < files_.size() && payload_file_loaded(files_[i].name, folder, running);
        return i < store_.size() && payload_file_loaded(store_[i].installed_file, folder, running);
    }

    std::vector<PayloadFile> files_;
    bool files_loaded_ = false;
    std::vector<RunningPayload> running_;
    std::vector<StoreItem> store_;
    bool store_loaded_ = false;
    bool store_fetching_ = false;
    std::string store_error_;
    StoreJob job_;
};

} // namespace

std::unique_ptr<Page> make_payloads_page()
{
    return std::make_unique<PayloadsPage>();
}

} // namespace hui::ps5cc
