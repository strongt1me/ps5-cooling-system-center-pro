// ps5cc - Invented data for the PC preview and the unit tests, and the source registry.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>

namespace hui::ps5cc
{

namespace
{

struct MockGame
{
    const char *id;
    const char *name;
    const char *platform;
    const char *version;
    const char *format;
    double gigabytes;
    double hours;
};

// The console's own list as it looked on 27.10.2026, with a few made-up ones
// to reach a realistic length.
constexpr MockGame kGames[] = {
    {"PPSA02433", "Crash Bandicoot 4: It's About Time", "PS5", "01.000.002", "exfat", 47.8, 2.9},
    {"PPSA86922", "PS5CC Vollbild-Test", "PS5", "01.000.010", "folder", 0.06, 0.03},
    {"PPSA01325", "ASTRO BOT", "PS5", "01.008.000", "ffpkg", 31.2, 14.5},
    {"PPSA01862", "Maneater", "PS5", "01.000.004", "ffpfs", 22.0, 6.1},
    {"CUSA44880", "Moorhuhn Remake", "PS4", "01.02", "fpkg", 1.4, 0.8},
    {"PPSA04311", "LEGO Batman: Legacy of the Dark Knight", "PS5", "01.000.000", "fpkg", 31.8, 0.5},
    {"PPSA05522", "Asterix & Obelix Heroes", "PS5", "01.000.001", "ffpkg", 6.4, 3.2},
    {"PPSA07788", "Double Dragon Revive", "PS5", "01.000.003", "exfat", 9.7, 1.1},
    {"PPSA09912", "Arcade Game Zone", "PS5", "01.000.000", "folder", 2.3, 0.4},
    {"CUSA05041", "Two Point Hospital", "PS4", "01.15", "ffpkg", 12.6, 22.0},
    {"PPSA13001", "Arkanoid – Eternal Battle", "PS5", "01.000.002", "folder", 4.1, 0.2},
    {"PPSA11420", "Styx: Blades of Greed", "PS5", "01.000.001", "exfat", 28.9, 5.7},
    {"PPSA03860", "Lantern Pass", "PS5", "01.002.000", "ffpfsc", 18.5, 9.4},
    {"PPSA06620", "Harbor Lights", "PS5", "01.000.000", "folder", 7.9, 1.6},
    {"PPSA07001", "Sky Drifters", "PS5", "01.010.000", "ffpkg", 41.0, 33.8},
    {"CUSA12100", "Pixel Garden", "PS4", "01.00", "fpkg", 0.9, 4.4},
    {"PPSA08455", "Iron Tide", "PS5", "01.003.001", "exfat", 55.2, 12.0},
    {"PPSA09001", "Northwind Rally", "PS5", "01.000.004", "ffpfsc", 24.3, 7.2},
    {"PPSA09330", "Quiet Orbit", "PS5", "01.000.000", "folder", 3.3, 0.0},
    {"CUSA20220", "Brick Breakers HD", "PS4", "01.04", "fpkg", 0.7, 1.9},
    {"PPSA10777", "Echo Valley", "PS5", "01.001.000", "ffpkg", 15.0, 2.2},
    {"PPSA12345", "Starlight Express Tour", "PS5", "01.000.000", "exfat", 11.1, 0.7},
};

class MockSource final : public Source
{
  public:
    MockSource()
    {
        data_.link = Link::online;
        for (const MockGame &m : kGames)
        {
            Game g;
            g.id = m.id;
            g.name = m.name;
            g.platform = m.platform;
            g.version = m.version;
            g.format = m.format;
            g.path = std::string("/mnt/usb0/homebrew/") + m.id;
            g.size_bytes = static_cast<std::uint64_t>(m.gigabytes * 1e9);
            g.played_s = static_cast<std::uint64_t>(m.hours * 3600.0);
            g.last_played = m.hours > 0.0 ? "2026-10-27T10:29:26Z" : "";
            data_.games.push_back(g);
        }
        data_.games_loaded = true;
        data_.status.game_id = "PPSA02433";
        data_.status.game_name = "Crash Bandicoot 4: It's About Time";
        data_.status.game_state = "im Spiel";
        data_.status.game_foreground = false;
        data_.status.target_c = 75;
        Job &copy = data_.jobs[static_cast<std::size_t>(JobKind::copy)];
        copy.active = true;
        copy.state = "running";
        copy.name = "ASTRO BOT";
        copy.detail = "→ Interne SSD";
        copy.total = 31.2e9;
        copy.done = copy.total * 0.42;
        data_.history = {
            {1793098000, "convert", "Maneater", "ffpfs → /mnt/usb0/exfat", "done", 22000000000ull},
            {1793091000, "copy", "Double Dragon Revive", "→ USB-Laufwerk", "done", 9700000000ull},
            {1793080000, "image", "Lantern Pass", "Abbild geprüft", "done", 18500000000ull},
            {1793070000, "convert", "Iron Tide", "exFAT → ffpkg", "cancelled", 0},
            {1793060000, "copy", "Sky Drifters", "→ Interne SSD", "failed", 0},
        };
        refresh_status();
    }

    bool fetch(Snapshot &out, std::uint64_t &seen) override
    {
        if (seen == data_.serial)
            return false;
        out = data_;
        seen = data_.serial;
        return true;
    }

    bool poll_notice(Notice &out) override
    {
        if (notices_.empty())
            return false;
        out = notices_.front();
        notices_.pop_front();
        return true;
    }

    void tick(float dt) override
    {
        clock_ += dt;
        second_ += dt;
        if (pending_target_ > 0 && clock_ >= target_at_)
        {
            data_.status.target_c = pending_target_;
            char text[64];
            std::snprintf(text, sizeof(text), "Zieltemperatur %d °C übernommen", pending_target_);
            notice(Notice::Kind::success, text);
            pending_target_ = 0;
            changed();
        }
        if (!closing_.empty() && clock_ >= close_at_)
        {
            notice(Notice::Kind::success, "„" + data_.status.game_name + "“ wurde beendet");
            data_.status.game_id.clear();
            data_.status.game_name.clear();
            data_.status.game_state = "kein Spiel";
            closing_.clear();
            changed();
        }
        if (second_ >= 1.0f)
        {
            second_ = 0.0f;
            Job &copy = data_.jobs[static_cast<std::size_t>(JobKind::copy)];
            if (copy.active && copy.done < copy.total)
                copy.done = std::fmin(copy.total, copy.done + copy.total * 0.002);
            refresh_status();
            changed();
        }
    }

    void set_target(int celsius) override
    {
        pending_target_ = celsius;
        target_at_ = clock_ + 0.3f;
    }

    void launch(const std::string &id) override
    {
        const Game *game = find(id);
        const std::string name = game != nullptr ? game->name : id;
        if (!data_.status.game_id.empty() && data_.status.game_id != id)
        {
            Notice n;
            n.kind = Notice::Kind::other_running;
            n.title = "Ein anderes Spiel läuft";
            n.running_id = data_.status.game_id;
            n.running_name = data_.status.game_name;
            n.target_id = id;
            n.target_name = name;
            notices_.push_back(n);
            return;
        }
        notice(Notice::Kind::success, "„" + name + "“ wird gestartet");
    }

    void close_game(const std::string &id) override
    {
        if (data_.status.game_id != id)
            return;
        closing_ = id;
        close_at_ = clock_ + 1.5f;
        notice(Notice::Kind::info, "„" + data_.status.game_name + "“ wird beendet …");
    }

    void cancel_job(JobKind kind) override
    {
        Job &job = data_.jobs[static_cast<std::size_t>(kind)];
        if (!job.active)
            return;
        job.active = false;
        job.finished = true;
        job.state = "cancelled";
        notice(Notice::Kind::info, "Auftrag abgebrochen", job.name);
        changed();
    }

    void open_web_ui() override
    {
        notice(Notice::Kind::info, "Weboberfläche", "Öffnet sich auf der Konsole im Browser.");
    }

    void reload() override
    {
        notice(Notice::Kind::info, "Daten werden neu geladen");
        changed();
    }

    void retry() override
    {
        data_.link = Link::starting;
        data_.link_detail = "Warte auf die App …";
        changed();
    }

    void watch_jobs(bool watching) override
    {
        (void)watching;
    }

    void quit_app() override
    {
        notice(Notice::Kind::info, "Beenden", "In der Vorschau am PC gibt es nichts zu beenden.");
    }

    void debug_cycle_link() override
    {
        switch (data_.link)
        {
        case Link::online:
            data_.link = Link::starting;
            data_.link_detail = "Die App wird gestartet (ELF an Port 9021 gesendet) …";
            break;
        case Link::starting:
            data_.link = Link::failed;
            data_.link_detail = "127.0.0.1:8086 antwortet nach 30 s nicht.";
            break;
        case Link::failed:
            data_.link = Link::online;
            data_.link_detail.clear();
            break;
        }
        changed();
    }

  private:
    const Game *find(const std::string &id) const
    {
        for (const Game &g : data_.games)
        {
            if (g.id == id)
                return &g;
        }
        return nullptr;
    }

    void refresh_status()
    {
        Status &s = data_.status;
        const double t = static_cast<double>(clock_);
        const bool playing = !s.game_id.empty();
        s.valid = true;
        s.cpu_c = static_cast<int>(std::lround((playing ? 71.0 : 52.0) + 1.6 * std::sin(t * 0.21)));
        s.soc_c = static_cast<int>(std::lround((playing ? 64.0 : 47.0) + 1.2 * std::sin(t * 0.17 + 1.0)));
        s.gpu_c = static_cast<int>(std::lround((playing ? 66.0 : 46.0) + 1.4 * std::sin(t * 0.13 + 2.0)));
        s.hottest_c = std::max(s.cpu_c, std::max(s.soc_c, s.gpu_c));
        s.fan_pct = static_cast<float>(std::round(((playing ? 46.3 : 20.4) + 2.2 * std::sin(t * 0.09)) * 10.0) / 10.0);
        s.automatic = true;
    }

    void notice(Notice::Kind kind, std::string title, std::string body = {})
    {
        Notice n;
        n.kind = kind;
        n.title = std::move(title);
        n.body = std::move(body);
        notices_.push_back(n);
    }

    void changed()
    {
        ++data_.serial;
    }

    Snapshot data_;
    std::deque<Notice> notices_;
    float clock_ = 0.0f;
    float second_ = 0.0f;
    int pending_target_ = 0;
    float target_at_ = 0.0f;
    std::string closing_;
    float close_at_ = 0.0f;
};

Source *g_source = nullptr;

} // namespace

Source &source()
{
    if (g_source == nullptr)
    {
        static MockSource mock;
        g_source = &mock;
    }
    return *g_source;
}

void set_source(Source *source)
{
    g_source = source;
}

std::string format_decimal(float value, int decimals)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(value));
    for (char &c : text)
    {
        if (c == '.')
            c = ',';
    }
    return text;
}

std::string format_bytes(std::uint64_t bytes)
{
    const double b = static_cast<double>(bytes);
    if (b >= 1e12)
        return format_decimal(static_cast<float>(b / 1e12), 2) + " TB";
    if (b >= 1e9)
        return format_decimal(static_cast<float>(b / 1e9), 1) + " GB";
    if (b >= 1e6)
        return format_decimal(static_cast<float>(b / 1e6), 0) + " MB";
    if (b >= 1e3)
        return format_decimal(static_cast<float>(b / 1e3), 0) + " KB";
    return std::to_string(bytes) + " B";
}

std::string format_playtime(std::uint64_t seconds)
{
    if (seconds == 0)
        return "–";
    if (seconds < 3600)
        return std::to_string((seconds + 59) / 60) + " min";
    return format_decimal(static_cast<float>(seconds) / 3600.0f, 1) + " h";
}

} // namespace hui::ps5cc
