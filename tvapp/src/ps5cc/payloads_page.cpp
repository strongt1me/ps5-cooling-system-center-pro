// ps5cc - The Payloads page: own payloads to start, the running ones, the payload store.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
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

constexpr float kListTop = kTop + 82.0f;

class PayloadsPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        views_.style.theme = theme;
        views_.style.kind = ui::TabKind::segmented;
        views_.style.width = ui::TabWidth::fill;
        views_.style.text_size = 22.0f;
        views_.style.height = 52.0f;
        views_.set_tabs({{"Meine Payloads"}, {"Laufend"}, {"Payload-Store"}});
        views_.set_bounds({kMargin, kTop + 8.0f, kLeftW, 56.0f});
        views_.set_active(kFiles, true);

        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.row_height = 76.0f;
        list_.style.title_size = 24.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 20.0f;
        list_.style.header_size = 18.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.set_bounds({kMargin, kListTop, kLeftW, kBottom - kListTop});

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

        dialog_.style.theme = theme;
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

    bool modal() const override
    {
        return dialog_.is_open() || dialog_.visible();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (dialog_.is_open())
        {
            if (dialog_.handle(input, feedback) == ui::Event::activated && dialog_.choice() == 1 && kill_pid_ > 0)
                source().kill_payload(kill_pid_);
            return;
        }
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
                kill_pid_ = item.running->pid;
                ui::DialogContent content;
                content.icon = ui::StatusKind::warning;
                content.title = "„" + item.running->name + "“ beenden?";
                content.body = "Der Prozess wird sofort beendet. Was er gerade tut, bricht ab.";
                content.buttons = {{"Laufen lassen"}, {"Beenden", ui::ButtonKind::primary, true}};
                dialog_.open(content, feedback);
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
        dialog_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        views_.draw(canvas);
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
            title = view_ == kStore ? "Payload-Store" : view_ == kRunning ? "Laufende Payloads" : "Meine Payloads";
            text = view_ == kFiles ? "Payloads aus /data/PS5-Cooling-Center/payloads und von USB-Sticks."
                                   : view_ == kStore ? "Liste: itsPLK ps5-payloads-mirror."
                                                     : "";
        }
        float y = card.y + 64.0f;
        int lines = 0;
        for (const std::string &line : fonts.display.font->wrap(title, 32.0f, card.w - 60.0f))
        {
            if (++lines > 2)
                break;
            ui::text(list, fonts.display, line, card.x + 30.0f, y, 32.0f, kInk);
            y += 42.0f;
        }
        y += 4.0f;
        lines = 0;
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

    void draw_overlay(ui::Canvas &canvas) const override
    {
        dialog_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        int n = 0;
        out[n++] = {ui::Button::l2, "Bereich", ui::Button::r2};
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
            item.value = s.installed_version.empty() ? "" : s.update ? "Update" : "geladen";
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
    ui::Dialog dialog_;
    int view_ = kFiles;
    std::array<int, kViews> focus_{};
    std::string key_;
    int kill_pid_ = 0;
    int info_for_ = -2;

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
