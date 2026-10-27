// ps5cc - Invented data for the PC preview and the unit tests, and the source registry.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/covers.hpp"
#include "ps5cc/data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

namespace hui::ps5cc
{

namespace
{

// A diagonal two-colour gradient with a soft light, different for every seed. No artwork of any real game.
std::vector<std::uint8_t> invented_cover(std::size_t seed)
{
    static constexpr std::uint8_t kPalette[][3] = {{0x2b, 0x3a, 0x8c}, {0xc0, 0x4b, 0xd6}, {0x0c, 0x8f, 0x7a},
                                                   {0xf0, 0x99, 0x3b}, {0x3a, 0x5b, 0xd9}, {0xd8, 0x5a, 0x30},
                                                   {0x53, 0x4a, 0xb7}, {0x1d, 0x9e, 0x75}};
    const std::uint8_t *a = kPalette[seed % 8];
    const std::uint8_t *b = kPalette[(seed * 3 + 5) % 8];
    const float lx = 0.25f + 0.5f * static_cast<float>((seed * 37) % 100) / 100.0f;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(kCoverSize) * kCoverSize * 4u);
    for (int y = 0; y < kCoverSize; ++y)
    {
        for (int x = 0; x < kCoverSize; ++x)
        {
            const float u = static_cast<float>(x) / kCoverSize;
            const float v = static_cast<float>(y) / kCoverSize;
            const float t = std::clamp((u + v) * 0.5f, 0.0f, 1.0f);
            const float dx = u - lx;
            const float dy = v - 0.3f;
            const float light = std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy) * 2.2f) * 0.45f;
            std::uint8_t *p = out.data() + (static_cast<std::size_t>(y) * kCoverSize + x) * 4u;
            for (int c = 0; c < 3; ++c)
            {
                const float base = static_cast<float>(a[c]) * (1.0f - t) + static_cast<float>(b[c]) * t;
                p[c] = static_cast<std::uint8_t>(std::min(255.0f, base + light * 255.0f));
            }
            p[3] = 255;
        }
    }
    return out;
}

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
        // Invented covers for every other game, so the preview shows covers next to initials.
        for (std::size_t i = 0; i < data_.games.size(); i += 2)
            covers().deliver(data_.games[i].id, invented_cover(i));
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
        data_.config.valid = true;
        data_.config.mode = "automatic";
        data_.config.profile = "comfort";
        data_.config.safety_temp_c = 82;
        data_.config.night_enabled = true;
        data_.config.night_start_min = 23 * 60;
        data_.config.night_end_min = 7 * 60;
        data_.config.night_target_c = 80;
        data_.config.tz_offset_min = 60;
        fill_payloads();
        fill_store();
        fill_logs();
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
            step_selftest();
            step_store();
            add_klog_lines(2);
            changed();
        }
    }

    void watch(Watch page) override
    {
        (void)page;
    }

    void save_config(const std::string &patch) override
    {
        (void)patch;
        notice(Notice::Kind::success, "Gespeichert");
    }

    void selftest(bool start) override
    {
        SelfTest &t = data_.selftest;
        if (!start)
        {
            if (t.state == "running")
            {
                t.state = "finished";
                t.verdict = "aborted";
                t.why = "Abgebrochen.";
                changed();
            }
            return;
        }
        t = SelfTest{};
        t.state = "running";
        t.limit_s = 30;
        t.threshold_c = 45;
        t.hottest_c = 52;
        t.baseline_pct = 20;
        t.samples = {20};
        changed();
    }

    void power(const std::string &action) override
    {
        notice(Notice::Kind::info, "Befehl angenommen", "Vorschau am PC: „" + action + "“ wird nicht ausgeführt.");
    }

    void start_payload(const PayloadFile &file) override
    {
        notice(Notice::Kind::success, "„" + file.name + "“ gestartet", "An den ELF-Loader gesendet.");
    }

    void kill_payload(int pid) override
    {
        for (std::size_t i = 0; i < data_.running.size(); ++i)
        {
            if (data_.running[i].pid != pid)
                continue;
            notice(Notice::Kind::info, "„" + data_.running[i].name + "“ beendet");
            data_.running.erase(data_.running.begin() + static_cast<std::ptrdiff_t>(i));
            changed();
            return;
        }
    }

    void store_action(const std::string &action, const std::string &id) override
    {
        if (action == "refresh")
        {
            notice(Notice::Kind::info, "Liste wird neu geladen");
            return;
        }
        const StoreItem *item = nullptr;
        for (const StoreItem &s : data_.store)
        {
            if (s.id == id)
                item = &s;
        }
        if (item == nullptr)
            return;
        if (action == "start")
        {
            notice(Notice::Kind::success, "„" + item->name + "“ gestartet");
            return;
        }
        if (action == "install")
        {
            data_.store_job.state = "running";
            data_.store_job.id = item->id;
            data_.store_job.name = item->name;
            data_.store_job.total = 4.2e6;
            data_.store_job.done = 0.0;
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

    void fill_payloads()
    {
        struct File
        {
            const char *source, *mount, *where, *name;
            double mb;
            bool valid;
        };
        static constexpr File kFiles[] = {
            {"internal", "", "Intern", "ShadowMountPlus_1.7beta7.elf", 2.5, true},
            {"internal", "", "Intern", "PKG-Manager_v1.5.1.elf", 2.0, true},
            {"internal", "", "Intern", "pldmgr_v0.6.3.elf", 2.8, true},
            {"internal", "", "Intern", "nanodns_v0.4.elf", 0.13, true},
            {"internal", "", "Intern", "kaputt.elf", 0.0, false},
            {"usb", "/mnt/usb0", "USB-Speicher 1", "ps4mount_v0.7.0.elf", 1.1, true},
        };
        for (const File &f : kFiles)
        {
            PayloadFile p;
            p.source = f.source;
            p.mount = f.mount;
            p.where = f.where;
            p.name = f.name;
            p.size = static_cast<std::uint64_t>(f.mb * 1e6);
            p.mtime = 1792448650;
            p.valid = f.valid;
            if (!f.valid)
                p.why = "Keine ELF-Datei (die ersten Bytes passen nicht).";
            data_.payload_files.push_back(p);
        }
        data_.payload_files_loaded = true;
        data_.running = {{236, "PS5 Cooling & System Center - Pro (diese App)", 25, true},
                         {194, "shadowmountplus.elf", 101, false},
                         {114, "elfldr.elf", 15, false},
                         {112, "ftpsrv.elf", 3, false},
                         {111, "klogsrv.elf", 2, false},
                         {106, "kstuff.elf", 4, false}};
    }

    void fill_store()
    {
        struct Item
        {
            const char *id, *name, *version, *category, *description, *installed;
            bool update;
        };
        static constexpr Item kItems[] = {
            {"shadowmountplus", "ShadowMountPlus", "1.7beta7", "Utilities & Tools",
             "Ein automatischer Mounter für Spiele-Abbilder im Hintergrund.", "1.7beta7", false},
            {"pldmgr", "pldmgr", "v0.6.4", "Utilities & Tools", "Payloads verwalten und automatisch laden.", "v0.6.3",
             true},
            {"ps5upload", "ps5upload", "v8.4.0", "Networking & Servers",
             "Schnelle Übertragung vom Rechner auf die PS5 (braucht die PC-App).", "", false},
            {"ftpsrv", "ftpsrv", "v0.15", "Networking & Servers", "FTP-Server auf Port 2121.", "", false},
            {"kstuff", "kstuff", "v1.6.7", "System & Jailbreak", "Kernel-Hilfen für den Jailbreak.", "", false},
            {"elfldr", "elfldr", "v0.21", "Loaders", "Der ELF-Loader auf Port 9021.", "", false},
        };
        for (const Item &i : kItems)
        {
            StoreItem s;
            s.id = i.id;
            s.name = i.name;
            s.filename = std::string(i.name) + "_" + i.version + ".elf";
            s.version = i.version;
            s.category = i.category;
            s.description = i.description;
            s.installed_version = i.installed;
            s.update = i.update;
            data_.store.push_back(s);
        }
        data_.store_loaded = true;
    }

    void fill_logs()
    {
        static constexpr const char *kLog[][3] = {
            {"INFO", "target_temp_changed", "Zieltemperatur auf 75 °C gesetzt."},
            {"INFO", "library_after_exit", "Start von Crash Bandicoot 4 (PPSA02433) vorgemerkt."},
            {"INFO", "playtime_session", "Spielzeit: Titel PPSA02433 – Sitzung beginnt."},
            {"WARN", "fan_threshold_overridden", "Die Konsole hat die Lüftereinstellung auf 91 °C gesetzt. Zurückgestellt auf 80 °C."},
            {"INFO", "fan_threshold_applied", "An die Konsole übergeben: 78 °C – Durchschnitt 71,4 °C, gewünschte Drehzahl 46 %."},
            {"ERROR", "paystore_fetch_failed", "Die Liste der Payloads ließ sich nicht laden (Zeitüberschreitung)."},
            {"INFO", "paystore_loaded", "34 Payloads in der Liste."},
        };
        std::int64_t t = 1793114617243;
        for (const auto &line : kLog)
        {
            data_.log.push_back({t, line[0], line[1], line[2]});
            t += 41000;
        }
        add_klog_lines(40);
    }

    void add_klog_lines(int count)
    {
        static constexpr const char *kLines[] = {
            "icc_fan_get_fan_manual_duty: duty: 0000",
            "[dce-ft:1131 Info]<236>0x0: opened",
            "[dce-ft:1303 Info]<236>0x0: closed",
            "[SceShellUI] I/PSM.UI : SceneQ : Loaded[0.00msec] : SplashScreen.PPSA86921 : SplashScreen",
            "[SceLncService] launchApp(PPSA02433)",
            "[AvControl] Waiting for the pstate event : done",
            "[Syscore Umd] Set memory pstate (hasRunningGame=1, hasRunningMiniApp=0, isSplashDisplayed=0)",
        };
        std::vector<KlogLine> fresh;
        for (int i = 0; i < count; ++i)
        {
            ++klog_n_;
            fresh.push_back({klog_n_, 1793116465587 + klog_n_ * 480, kLines[klog_n_ % 7]});
        }
        klog_append(data_.klog, fresh, kKlogKeep);
    }

    void step_selftest()
    {
        SelfTest &t = data_.selftest;
        if (t.state != "running")
            return;
        ++t.elapsed_s;
        const int pct = std::min(20 + t.elapsed_s * 4, 62);
        t.samples.push_back(pct);
        t.peak_pct = std::max(t.peak_pct, pct);
        t.rise_pct = t.peak_pct - t.baseline_pct;
        if (t.elapsed_s >= 12)
        {
            t.state = "finished";
            t.verdict = "responds";
            t.why = "Der Lüfter reagiert.";
        }
    }

    void step_store()
    {
        StoreJob &job = data_.store_job;
        if (job.state != "running")
            return;
        job.done = std::min(job.total, job.done + job.total * 0.18);
        if (job.done >= job.total)
        {
            job.state = "done";
            for (StoreItem &s : data_.store)
            {
                if (s.id == job.id)
                {
                    s.installed_version = s.version;
                    s.update = false;
                }
            }
            notice(Notice::Kind::success, "„" + job.name + "“ geladen");
        }
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
    std::int64_t klog_n_ = 51400;
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

std::string format_clock(int minutes)
{
    const int m = ((minutes % 1440) + 1440) % 1440;
    char text[8];
    std::snprintf(text, sizeof(text), "%02d:%02d", m / 60, m % 60);
    return text;
}

std::string format_time_of_day(std::int64_t ms, int tz_offset_min)
{
    const std::int64_t s = ms / 1000 + static_cast<std::int64_t>(tz_offset_min) * 60;
    const std::int64_t day = ((s % 86400) + 86400) % 86400;
    char text[12];
    std::snprintf(text, sizeof(text), "%02d:%02d:%02d", static_cast<int>(day / 3600),
                  static_cast<int>(day / 60 % 60), static_cast<int>(day % 60));
    return text;
}

std::string format_date(std::int64_t seconds)
{
    // Civil date from days since 1970 (Howard Hinnant's algorithm): no time zone database on the console.
    std::int64_t z = (seconds >= 0 ? seconds : seconds - 86399) / 86400 + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
    char text[16];
    std::snprintf(text, sizeof(text), "%02d.%02d.%04d", d, m, y);
    return text;
}

bool klog_append(std::vector<KlogLine> &ring, const std::vector<KlogLine> &fresh, std::size_t keep)
{
    const std::int64_t last = ring.empty() ? 0 : ring.back().n;
    bool added = false;
    for (const KlogLine &line : fresh)
    {
        if (line.n <= last)
            continue;
        ring.push_back(line);
        added = true;
    }
    if (ring.size() > keep)
        ring.erase(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(ring.size() - keep));
    return added;
}

} // namespace hui::ps5cc
