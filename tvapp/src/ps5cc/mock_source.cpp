// ps5cc - Invented data for the PC preview and the unit tests, and the source registry.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "ps5cc/covers.hpp"
#include "ps5cc/data.hpp"
#include "ps5cc/i18n.hpp"
#include "ps5cc/plans.hpp"
#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <map>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <deque>
#include <vector>

namespace hui::ps5cc
{

namespace
{

// A diagonal two-colour gradient with a soft light, different for every seed. No artwork of any
// real game.
std::vector<std::uint8_t> invented_cover(std::size_t seed)
{
    static constexpr std::uint8_t kPalette[][3] = {
        {0x2b, 0x3a, 0x8c}, {0xc0, 0x4b, 0xd6}, {0x0c, 0x8f, 0x7a}, {0xf0, 0x99, 0x3b},
        {0x3a, 0x5b, 0xd9}, {0xd8, 0x5a, 0x30}, {0x53, 0x4a, 0xb7}, {0x1d, 0x9e, 0x75}};
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
                const float base =
                    static_cast<float>(a[c]) * (1.0f - t) + static_cast<float>(b[c]) * t;
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
    {"FAKE86922", "PS5CC Vollbild-Test", "PS5", "01.000.010", "folder", 0.06, 0.03},
    {"PPSA01325", "ASTRO BOT", "PS5", "01.008.000", "ffpkg", 31.2, 14.5},
    {"PPSA01862", "Maneater", "PS5", "01.000.004", "ffpfs", 22.0, 6.1},
    {"CUSA44880", "Moorhuhn Remake", "PS4", "01.02", "fpkg", 1.4, 0.8},
    {"PPSA04311", "LEGO Batman: Legacy of the Dark Knight", "PS5", "01.000.000", "fpkg", 31.8, 0.5},
    {"PPSA05522", "Asterix & Obelix Heroes", "PS5", "01.000.001", "ffpkg", 6.4, 3.2},
    {"PPSA07788", "Double Dragon Revive", "PS5", "01.000.003", "exfat", 9.7, 1.1},
    {"PPSA09912", "Arcade Game Zone", "PS5", "01.000.000", "folder", 2.3, 0.4},
    {"CUSA05041", "Two Point Hospital", "PS4", "01.15", "folder", 12.6, 22.0},
    {"PPSA13001", "Arkanoid – Eternal Battle", "PS5", "01.000.002", "folder", 4.1, 0.2},
    {"PPSA11420", "Styx: Blades of Greed", "PS5", "01.000.001", "exfat", 28.9, 5.7},
    {"PPSA03860", "Lantern Pass", "PS5", "01.002.000", "ffpfsc", 18.5, 9.4},
    {"PPSA06620", "Harbor Lights", "PS5", "01.000.000", "folder", 7.9, 1.6},
    {"PPSA07001", "Sky Drifters", "PS5", "01.010.000", "ffpkg", 41.0, 33.8},
    {"CUSA12100", "Pixel Garden", "PS4", "01.00", "fpkg", 0.9, 4.4},
    {"PPSA08455", "Iron Tide", "PS5", "01.003.001", "exfat", 55.2, 12.0},
    {"PPSA09001", "Northwind Rally", "PS5", "01.000.004", "ffpfsc", 24.3, 7.2},
    {"PPSA09330", "Quiet Orbit", "PS5", "01.000.000", "folder", 3.3, 0.0},
    {"CUSA20220", "Brick Breakers HD", "PS4", "01.04", "ps4mount", 0.7, 1.9},
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
            const std::string format = m.format;
            const bool folder = format == "folder";
            const bool installed = format == "fpkg" || format == "ps4mount";
            g.path = installed
                         ? std::string("/user/app/") + m.id
                         : std::string("/mnt/usb0/homebrew/") + m.id + (folder ? "" : "." + format);
            // The library's rules (library.c): copy a folder or an image file, convert a folder or
            // an uncompressed image, move and unpack through ShadowMountPlus.
            g.can_copy = !installed;
            g.can_convert = folder || format == "exfat" || format == "ffpkg" || format == "ffpfs";
            g.can_move = !installed;
            g.can_unpack = !installed && !folder;
            g.can_remount = !installed;
            g.smp_mounted = !installed;
            g.ampr_emu = std::string(m.id) == "PPSA01325";
            g.size_bytes = static_cast<std::uint64_t>(m.gigabytes * 1e9);
            g.played_s = static_cast<std::uint64_t>(m.hours * 3600.0);
            g.last_played = m.hours > 0.0 ? "2026-10-27T10:29:26Z" : "";
            // Two games installed from disc: one with its disc in the drive and running, one
            // without its disc.
            if (g.id == "PPSA03860")
            {
                g.disc = "in";
                g.dump_mounted = true;
            }
            if (g.id == "CUSA12100")
                g.disc = "out";
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
        // A move by ShadowMountPlus that crawls through the whole preview; the app's own kinds stay
        // free for the tools.
        Job &move = data_.jobs[static_cast<std::size_t>(JobKind::storage)];
        move.active = true;
        move.state = "transferring";
        move.name = "Maneater";
        move.detail = "Verschieben → /mnt/ext0/homebrew";
        move.total = 22.0e9;
        move.done = move.total * 0.42;
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
        data_.config.target_temp_c = 68;
        data_.config.warning_cpu_c = 85;
        data_.config.warning_countdown_s = 120;
        data_.config.lightbar_enabled = true;
        data_.config.lightbar_warn_c = 70;
        data_.config.lightbar_hot_c = 80;
        data_.game_rules = {{"PPSA01284", "ASTRO BOT", 72, "balanced"},
                            {"PPSA02433", "Crash Bandicoot 4: It's About Time", 66, "cool"}};
        data_.active_rule = "PPSA02433";
        data_.rules_loaded = true;
        fill_backdrops();
        fill_packages();
        fill_ps4mount();
        fill_disc();
        fill_volumes();
        fill_phase4();
        fill_files();
        fill_payloads();
        fill_store();
        fill_logs();
        refresh_status();
        data_.console_language = 4; // German, as the owner's console
        load_language();
    }

    // For pictures of another language: PS5CC_LANG=en and
    // PS5CC_LANG_FILES=web/lang/en.json:assets/lang/en.json (the browser's dictionary, then the
    // fullscreen app's own), as the console app and /app0 would give them.
    void load_language()
    {
        const char *lang = std::getenv("PS5CC_LANG");
        const char *files = std::getenv("PS5CC_LANG_FILES");
        if (lang == nullptr || files == nullptr || std::string(lang) == "de")
            return;
        auto entries = std::make_shared<std::vector<i18n::Entry>>();
        std::string list = files;
        for (std::size_t from = 0; from <= list.size();)
        {
            const std::size_t colon = list.find(':', from);
            const std::string path =
                list.substr(from, colon == std::string::npos ? std::string::npos : colon - from);
            std::string text;
            if (!path.empty() && save::read_file(path, &text, 8u << 20))
                i18n::parse_dictionary(text, entries.get());
            if (colon == std::string::npos)
                break;
            from = colon + 1;
        }
        data_.config.tv_language = lang;
        data_.language = lang;
        data_.dictionary = std::move(entries);
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
        tick_packages(dt);
        if (pending_target_ > 0 && clock_ >= target_at_)
        {
            data_.status.target_c = pending_target_;
            char text[64];
            std::snprintf(text, sizeof(text), "Zieltemperatur %d °C übernommen", pending_target_);
            notice(Notice::Kind::success, text);
            pending_target_ = 0;
            changed();
        }
        if (data_.usb_eject.running && clock_ >= eject_at_)
        {
            UsbEject &e = data_.usb_eject;
            e.running = false;
            e.state = "done";
            e.message = "usb0 ist ausgeworfen und kann abgezogen werden.";
            data_.volumes.erase(std::remove_if(data_.volumes.begin(), data_.volumes.end(),
                                               [&](const Volume &v) { return v.path == e.mount; }),
                                data_.volumes.end());
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
        if (plan_at_ > 0.0f && clock_ >= plan_at_)
        {
            plan_at_ = 0.0f;
            data_.plan = make_plan(plan_tool_, plan_id_);
            data_.plan.ticket = plan_ticket_;
            changed();
        }
        if (second_ >= 1.0f)
        {
            second_ = 0.0f;
            for (std::size_t i = 0; i < data_.jobs.size(); ++i)
            {
                Job &job = data_.jobs[i];
                if (!job.active || job.total <= 0.0)
                    continue;
                // The move of the opening screen crawls; the jobs started from the tools finish
                // within the preview.
                const bool crawl =
                    i == static_cast<std::size_t>(JobKind::storage) && job.name == "Maneater";
                job.done = std::fmin(job.total, job.done + job.total * (crawl ? 0.002 : 0.04));
                if (job.done >= job.total)
                {
                    job.active = false;
                    job.finished = true;
                    job.state = "done";
                    notice(Notice::Kind::success, "„" + job.name + "“ fertig");
                }
            }
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
        // the language choice is kept, as the app does (the preview has no other dictionary to
        // switch to)
        if (cJSON *root = cJSON_Parse(patch.c_str()))
        {
            const cJSON *lang = cJSON_GetObjectItemCaseSensitive(root, "tv_language");
            if (cJSON_IsString(lang) && lang->valuestring != nullptr)
            {
                data_.config.tv_language = lang->valuestring;
                changed();
            }
            cJSON_Delete(root);
        }
        notice(Notice::Kind::success, "Gespeichert");
    }

    void save_game_rule(const GameRule &rule) override
    {
        for (GameRule &r : data_.game_rules)
        {
            if (r.title_id == rule.title_id)
            {
                r = rule;
                notice(Notice::Kind::success, "Spielprofil gespeichert", rule.title_name);
                changed();
                return;
            }
        }
        data_.game_rules.push_back(rule);
        notice(Notice::Kind::success, "Spielprofil gespeichert", rule.title_name);
        changed();
    }

    void remove_game_rule(const std::string &title_id) override
    {
        std::erase_if(data_.game_rules, [&](const GameRule &r) { return r.title_id == title_id; });
        if (data_.active_rule == title_id)
            data_.active_rule.clear();
        notice(Notice::Kind::success, "Spielprofil entfernt");
        changed();
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
        notice(Notice::Kind::info, "Befehl angenommen",
               "Vorschau am PC: „" + action + "“ wird nicht ausgeführt.");
    }

    void start_payload(const PayloadFile &file) override
    {
        notice(Notice::Kind::success, "„" + file.name + "“ gestartet",
               "An den ELF-Loader gesendet.");
    }

    void copy_payload_file(const PayloadFile &file) override
    {
        for (const PayloadFile &f : data_.payload_files)
        {
            if (f.source == "internal" && f.name == file.name)
            {
                notice(Notice::Kind::error, "Kopieren nicht möglich",
                       "Im internen Ordner gibt es diese Datei schon.");
                return;
            }
        }
        PayloadFile copy = file;
        copy.source = "internal";
        copy.mount.clear();
        copy.where = "Intern";
        data_.payload_files.insert(data_.payload_files.begin(), copy);
        notice(Notice::Kind::success, "„" + file.name + "“ kopiert");
        changed();
    }

    void delete_payload_file(const PayloadFile &file) override
    {
        std::erase_if(data_.payload_files, [&](const PayloadFile &f)
                      { return f.source == "internal" && f.name == file.name; });
        notice(Notice::Kind::success, "„" + file.name + "“ gelöscht");
        changed();
    }

    void save_payload_profiles(const std::vector<PayloadProfile> &profiles,
                               const std::string &startup) override
    {
        data_.payload_profiles = profiles;
        data_.profile_startup = startup;
        notice(Notice::Kind::success, "Profile gespeichert");
        changed();
    }

    void run_payload_profile(const std::string &id) override
    {
        for (const PayloadProfile &p : data_.payload_profiles)
        {
            if (p.id != id)
                continue;
            ProfileRun &r = data_.profile_run;
            r = ProfileRun{};
            r.running = true;
            r.id = p.id;
            r.name = p.name;
            r.total = static_cast<int>(p.items.size());
            r.step = 1;
            if (!p.items.empty())
                r.results.push_back({p.items.front(), true, "gesendet"});
            notice(Notice::Kind::success, "Profil gestartet");
            changed();
            return;
        }
    }

    void stop_payload_profile() override
    {
        data_.profile_run.running = false;
        notice(Notice::Kind::info, "Profil wird angehalten");
        changed();
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

    std::uint64_t request_plan(Tool tool, const std::string &id) override
    {
        plan_ticket_ += 1;
        plan_tool_ = tool;
        plan_id_ = id;
        // As long as a small game takes to measure on the console, so the "wird vermessen" state
        // shows too.
        plan_at_ = clock_ + 0.6f;
        return plan_ticket_;
    }

    void start_tool(Tool tool, const std::string &id, const std::string &body) override
    {
        (void)body;
        const Game *game = find(id);
        const std::string name = game != nullptr ? game->name : id;
        const JobKind kind = tool == Tool::copy                           ? JobKind::copy
                             : tool == Tool::convert                      ? JobKind::convert
                             : tool == Tool::move || tool == Tool::unpack ? JobKind::storage
                             : tool == Tool::erase                        ? JobKind::deletion
                                                                          : JobKind::image;
        Job &job = data_.jobs[static_cast<std::size_t>(kind)];
        if (job.active)
        {
            // What the console app answers with 409 while one of its kind runs.
            notice(Notice::Kind::error, "Nicht gestartet",
                   "Es läuft schon ein Auftrag dieser Art. Bitte danach noch einmal.");
            return;
        }
        job = Job{};
        job.active = true;
        job.state = "running";
        job.name = name;
        job.total = game != nullptr ? static_cast<double>(game->size_bytes) : 1e9;
        job.cancellable = kind != JobKind::deletion;
        switch (tool)
        {
        case Tool::copy:
            job.detail = "→ USB-Laufwerk 2";
            job.total *= 2.0; // written and read back
            break;
        case Tool::convert:
            job.detail = "→ /mnt/ext0/homebrew/" + id + ".ffpfsc";
            break;
        case Tool::move:
            job.detail = "Verschieben → /mnt/ext0/homebrew";
            break;
        case Tool::unpack:
            job.detail = "Entpacken → /mnt/ext0/homebrew/" + id;
            break;
        case Tool::image_check:
            job.detail = "Prüfen";
            break;
        case Tool::image_repair:
            job.detail = "Reparieren";
            break;
        case Tool::image_unpack:
            job.detail = "Entpacken";
            break;
        case Tool::erase:
            job.detail = "Wird gelöscht";
            break;
        case Tool::count:
            break;
        }
        job.done = job.total * 0.07;
        Notice n;
        n.kind = Notice::Kind::job_started;
        n.title = std::string(tool_label(tool)) + " gestartet";
        n.body = "„" + name + "“";
        n.job = kind;
        notices_.push_back(n);
        changed();
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

    void remount_game(const std::string &id, const std::string &action) override
    {
        for (Game &g : data_.games)
        {
            if (g.id != id)
                continue;
            g.smp_mounted = action != "unmount";
            notice(Notice::Kind::success, g.name,
                   action == "unmount"   ? "Ausgehängt."
                   : action == "remount" ? "Neu eingehängt."
                                         : "Eingehängt.");
            changed();
            return;
        }
    }

    void close_game(const std::string &id) override
    {
        if (data_.status.game_id != id)
            return;
        closing_ = id;
        close_at_ = clock_ + 1.5f; // silent like the console source: the screen says it itself
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

    void split_package(const std::string &id, const std::string &target, int part_mb) override
    {
        (void)target;
        PackageSplit &j = data_.package_split;
        for (const Package &k : data_.packages)
        {
            if (k.id != id)
                continue;
            j = PackageSplit{};
            j.state = "splitting";
            j.active = true;
            j.can_cancel = true;
            j.name = k.name;
            j.phase = "Teile schreiben";
            const std::uint64_t part = static_cast<std::uint64_t>(part_mb) << 20;
            j.parts = static_cast<int>((k.size + part - 1) / part);
            j.part = 1;
            j.percent = 12;
            notice(Notice::Kind::info, "Paket wird aufgeteilt");
            changed();
            return;
        }
    }

    void package_action(const std::string &action, const std::string &id) override
    {
        if (action == "split_cancel")
        {
            data_.package_split.active = false;
            data_.package_split.state = "cancelled";
            notice(Notice::Kind::info, "Aufteilen wird abgebrochen");
            changed();
            return;
        }
        PackageInstall &job = data_.package_install;
        PackageQueue &q = data_.package_queue;
        const Package *pkg = nullptr;
        for (const Package &k : data_.packages)
        {
            if (k.id == id)
                pkg = &k;
        }
        if (action == "scan")
        {
            data_.package_scan.scanning = true;
            data_.package_scan.parsed = 0;
            scan_left_ = 2.0f;
        }
        else if (action == "install" && pkg != nullptr)
        {
            if (job.active)
            {
                notice(Notice::Kind::error, "Installation nicht gestartet",
                       "Es läuft schon eine Installation. Warte, bis sie fertig ist.");
                return;
            }
            if (pkg->parts_total > 0 && pkg->parts_found < pkg->parts_total)
            {
                notice(Notice::Kind::error, "Installation nicht gestartet",
                       "Es fehlen Teile dieses Pakets: " + std::to_string(pkg->parts_found) +
                           " von " + std::to_string(pkg->parts_total) + " gefunden.");
                return;
            }
            job = PackageInstall{};
            job.state = "installing";
            job.active = true;
            job.can_cancel = true;
            job.name = pkg->name;
            job.version = pkg->version;
            job.bytes_total = static_cast<double>(pkg->total > 0 ? pkg->total : pkg->size);
            job.percent = 0;
            notice(Notice::Kind::success, "Installation gestartet");
        }
        else if (action == "install_cancel" && job.active)
        {
            job.active = false;
            job.state = "cancelled";
            notice(Notice::Kind::info, "Installation abgebrochen");
        }
        else if (action == "queue_add" && pkg != nullptr)
        {
            for (const PackageQueueItem &i : q.items)
            {
                if (i.id == id && (i.state == "waiting" || i.state == "running"))
                {
                    notice(Notice::Kind::info, "Schon in der Warteschlange");
                    return;
                }
            }
            PackageQueueItem i;
            i.id = pkg->id;
            i.name = pkg->name;
            i.version = pkg->version;
            i.kind = pkg->kind;
            i.state = "waiting";
            i.size = pkg->size;
            q.items.push_back(i);
            notice(Notice::Kind::success, "In die Warteschlange gelegt");
        }
        else if (action == "queue_remove")
        {
            std::erase_if(q.items, [&](const PackageQueueItem &i)
                          { return i.id == id && i.state != "running"; });
        }
        else if (action == "queue_start")
        {
            q.running = true;
            q.halted = false;
        }
        else if (action == "queue_pause")
        {
            q.pause_requested = true;
        }
        else if (action == "queue_retry")
        {
            for (PackageQueueItem &i : q.items)
            {
                if (i.state == "failed" || i.state == "skipped" || i.state == "cancelled")
                    i.state = "waiting";
            }
        }
        else if (action == "queue_clear")
        {
            std::erase_if(q.items, [](const PackageQueueItem &i) { return i.state == "done"; });
        }
        count_queue();
        changed();
    }

    // Enough of PS4Mount for the preview: switching it, adding and removing, the settings, updates
    // and add-ons.
    void ps4mount_post(const std::string &route, const std::string &json) override
    {
        Ps4Mount &m = data_.ps4mount;
        const auto has = [&](const char *text) { return json.find(text) != std::string::npos; };
        const auto id_in = [&]() -> Ps4Game *
        {
            for (Ps4Game &g : m.games)
            {
                if (has(("\"" + g.title_id + "\"").c_str()))
                    return &g;
            }
            return nullptr;
        };
        std::string msg;
        if (route == "update")
            msg = "Das Update wird gewechselt, das Spiel wird neu hinzugefügt.";
        else if (route == "dlc")
            msg = "Die Zusatzinhalte sind gespeichert, das Spiel wird neu hinzugefügt.";
        else if (has("\"enable\""))
        {
            m.enabled = m.running = true;
            msg = "PS4Mount ist eingeschaltet.";
        }
        else if (has("\"disable\""))
        {
            m.enabled = m.running = false;
            msg = "PS4Mount ist ausgeschaltet.";
        }
        else if (has("\"scan\""))
            msg = "Die Suche läuft.";
        else if (has("\"config\""))
        {
            m.config.dlc = has("\"dlc\":true");
            m.config.dlc_copy = has("\"dlc_copy\":true");
            m.config.batch = has("\"batch\":true");
            m.config.fast_reg = has("\"fast_reg\":true");
            msg = "Einstellungen gespeichert.";
        }
        else if (has("\"add\""))
        {
            if (Ps4Game *g = id_in())
                g->state = "ready";
            msg = "Das Spiel wird hinzugefügt.";
        }
        else if (has("\"remove\""))
        {
            if (Ps4Game *g = id_in())
                g->state = "removed";
            msg = "Die Kachel ist entfernt, die PKG bleibt auf dem USB-Laufwerk.";
        }
        else if (has("\"restored_forget\""))
            msg = "Die herausgenommenen Spiele sind wieder zugelassen.";
        else if (has("\"restore"))
            msg = "Die Spiele werden herausgenommen, die PKGs kommen an ihren alten Platz.";
        if (!msg.empty())
            notice(Notice::Kind::success, "PS4Mount", msg);
        changed();
    }

    void ps4mount_fetch(const std::string &what, const std::string &id) override
    {
        if (what == "log")
        {
            data_.ps4_log =
                "2026-10-29 10:12:01.120 [scan] /mnt/usb0: 3 Spiele gefunden\n"
                "2026-10-29 10:12:01.480 [reg] CUSA18842: Update 01.020 benutzt\n"
                "2026-10-29 10:12:02.010 [reg] CUSA18842: 3 Zusatzinhalte eingehängt\n"
                "2026-10-29 10:12:02.700 [panel] Iron Tide ist bereit\n"
                "2026-10-29 10:14:40.300 [remove] CUSA09911: aus der Bibliothek entfernt";
            ++data_.ps4_log_seq;
            changed();
            return;
        }
        Ps4GameDetail d;
        d.id = id;
        d.loaded = true;
        d.dlc_limit = 29;
        Ps4Update a;
        a.file = "replaced/patch-01.010.pkg";
        a.version = "01.010";
        a.name = "Update 01.010";
        Ps4Update b;
        b.file = "patch-01.020.pkg";
        b.version = "01.020";
        b.name = "Update 01.020";
        b.active = true;
        d.updates = {a, b};
        const char *const names[] = {"Seefahrer-Paket", "Kapitänsuniform", "Soundtrack"};
        for (int i = 0; i < 3; ++i)
        {
            Ps4Dlc x;
            x.content_id = "EP0000-CUSA18842_00-DLC0000000000" + std::to_string(i + 1);
            x.name = names[i];
            x.on = i < 2;
            x.data = i == 0;
            d.dlcs.push_back(x);
        }
        d.seq = data_.ps4_detail.seq + 1;
        data_.ps4_detail = d;
        changed();
    }

    void extras_open(const std::string &id) override
    {
        ExtrasState &e = data_.extras;
        if (id.empty())
        {
            e = ExtrasState{};
            changed();
            return;
        }
        e.id = id;
        ++e.seq;
        e.ampr_job = R"({"ok":true,"state":"idle","active":false})";
        e.image_job = R"({"ok":true,"state":"idle","active":false})";
        e.ampr =
            R"({"ok":true,"has_index":true,"files":18342,"mtime":1793100000,"can_build":true,"why":"","format":"exfat"})";
        e.image = R"({"ok":true,"can_check":true,"format":"exfat"})";
        e.emu_ampr =
            R"({"ok":true,"place":"game","format":"exfat","can_change":true,"why":"","packed":false,)"
            R"("installed":{"present":true,"known":true,"label":"0.4.2.1 test-pack","has_orig":true},"emus":{}})";
        e.emu_playgo =
            R"({"ok":true,"place":"game","can_change":true,"why":"","installed":{"present":false},"emus":{}})";
        e.lz4 =
            R"({"ok":true,"can":true,"why":"","ampr_import":1,"has_index":true,"free_bytes":240000000000,"packed":false,)"
            R"("profiles":[{"id":"builtin-lantern","name":"Lantern Pass","where":"builtin","tested":true}],"own_profile":"",)"
            R"("suggest":{"id":"builtin-lantern","label":"Getestetes Profil für dieses Spiel","tested":true}})";
        e.trace =
            R"({"ok":true,"can":true,"why":"","enabled":false,"sessions":0,"journal_new":false,"game_running":false,)"
            R"("profile":"PPSA01325-mitschnitt.toml","busy":false,"msg":""})";
        e.plugins =
            R"({"ok":true,"can":true,"why":"","state":"update","files":[)"
            R"({"name":"cheats.prx","project":"Cheat-Plugin","state":"older","version":"1.2"},)"
            R"({"name":"fps.prx","project":"FPS-Anzeige","state":"current","version":"0.3"}]})";
        changed();
    }

    void extras_post(const std::string &route, const std::string &json) override
    {
        (void)json;
        if (route == "library/ampr")
            data_.extras.ampr_job =
                R"({"ok":true,"state":"running","active":true,"kind":"index","phase":"Dateien werden gelesen","files":6120})";
        notice(Notice::Kind::success, "Extras", "Auftrag angenommen.");
        changed();
    }

    void extras_versions(const std::string &lib) override
    {
        ExtrasVersions &v = data_.extras_versions;
        ++v.seq;
        v.lib = lib;
        v.json =
            lib == "playgo"
                ? R"({"ok":true,"versions":[{"name":"libScePlayGo.sprx-0.3","label":"0.3","valid":true,"bundled":true,"sha256":"b1"}]})"
                : R"({"ok":true,"versions":[{"name":"libSceAmpr.sprx-0.4.2.1-test-pack","label":"0.4.2.1 test-pack","pack":true,)"
                  R"("valid":true,"bundled":true,"sha256":"a1"},{"name":"libSceAmpr.sprx-0.4.2.1-debug","label":"0.4.2.1 mit Protokoll",)"
                  R"("debug":true,"valid":true,"bundled":true,"sha256":"a2"},{"name":"libSceAmpr.sprx-0.4.1","label":"0.4.1",)"
                  R"("valid":true,"bundled":false,"sha256":"a3"}]})";
        changed();
    }

    void extras_lz4_plan(const std::string &id, int level, const std::string &profile) override
    {
        (void)id;
        (void)profile;
        ExtrasLz4Plan &l = data_.extras_lz4_plan;
        ++l.seq;
        l.json =
            std::string(
                R"({"ok":true,"valid":true,"pack_files":812,"pack_bytes":29000000000,"loose_files":44,)") +
            R"("loose_bytes":1200000000,"chunks":118000,"est_s":)" +
            std::to_string(level == 12  ? 4200
                           : level == 3 ? 900
                                        : 1860) +
            "}";
        changed();
    }

    void system_post(const std::string &route, const std::string &json) override
    {
        (void)json;
        if (route == "cooling-health")
            notice(Notice::Kind::success, "System", "Der Vergleich beginnt neu.");
        else
            notice(Notice::Kind::success, "System", "Erledigt.");
        changed();
    }

    void risky_measure() override
    {
        SystemData &d = data_.system_data;
        d.risky =
            R"({"ok":true,"load":{"cpu_valid":true,"cpu_pct":34,"game_pct":28,"system_pct":6,"cpu_mhz_valid":true,"cpu_mhz":3850,)"
            R"("cpu_mode_valid":true,"cpu_mode":"Spiel"},"power":{"valid":true,"live_w":196.0,"total_w":204.0,)"
            R"("rails":[{"name":"GFX","w":121.5,"v":0.86,"a":141.3},{"name":"CPU","w":31.0,"v":0.95,"a":32.6}]},)"
            R"("top_threads":[{"name":"GameRender","own":"PPSA01325","pct":22},{"name":"ps5tm-http","own":"ps5tm","pct":1}]})";
        ++d.seq;
        changed();
    }

    void library_tools_fetch() override
    {
        LibraryTools &l = data_.library_tools;
        ++l.seq;
        l.duplicates =
            R"({"ok":true,"images":5,"groups":[{"title_id":"PPSA01325","name":"Lantern Pass","sources":[)"
            R"({"path":"/mnt/usb0/homebrew/Lantern Pass.ffpfsc","size":31200000000,"folder":false,"set_aside":false,"smp_uses":true,"mounted":true},)"
            R"({"path":"/mnt/ext1/homebrew/Lantern Pass.exfat","size":41000000000,"folder":false,"set_aside":false,"smp_uses":false,"mounted":false}]}],)"
            R"("aside":[{"path":"/mnt/usb0/homebrew/Iron Tide.exfat.off","title_id":"CUSA18842","size":22000000000}]})";
        l.cache =
            R"({"ok":true,"enabled":true,"dir":"/data/PS5-Cooling-Center/covers_and_more","titles":36,"covers":36,"bytes":9400000})";
        changed();
    }

    void library_tools_post(const std::string &route, const std::string &json) override
    {
        (void)route;
        (void)json;
        notice(Notice::Kind::success, "Bibliothek", "Erledigt.");
        changed();
    }

    void klog_record(bool on) override
    {
        KlogRecord &r = data_.klog_record;
        r.loaded = true;
        r.recording = on;
        r.file = on ? "klog-2026-10-29_1205.txt" : r.file;
        r.bytes = on ? 182000 : r.bytes;
        r.max_bytes = 64ull * 1024 * 1024;
        r.lines = on ? 1840 : r.lines;
        changed();
    }

    void klog_save(const std::string &text) override
    {
        KlogFile f;
        f.name = "auszug-2026-10-31_2010.log";
        f.kind = "save";
        f.size = text.size();
        f.mtime = 1793476200;
        data_.klog_files.insert(data_.klog_files.begin(), f);
        notice(Notice::Kind::success, "Auszug gespeichert", f.name);
        changed();
    }

    void set_username(const std::string &name) override
    {
        data_.profile.username = name;
        notice(Notice::Kind::success, "Name geändert",
               "Im Startmenü erscheint er nach einem Benutzerwechsel.");
        changed();
    }

    void apply_avatar(const std::string &file) override
    {
        (void)file;
        ++data_.avatar_revision;
        notice(Notice::Kind::success, "Profilbild übernommen",
               "Im Startmenü erscheint es nach einem Benutzerwechsel.");
        changed();
    }

    void apply_own_avatar(const std::string &path) override
    {
        (void)path;
        ++data_.avatar_revision;
        notice(Notice::Kind::success, "Profilbild übernommen",
               "Im Startmenü erscheint es nach einem Benutzerwechsel.");
        changed();
    }

    void restore_avatar() override
    {
        ++data_.avatar_revision;
        notice(Notice::Kind::success, "Vorheriges Profilbild zurückgeholt");
        changed();
    }

    void klog_files_clear() override
    {
        std::erase_if(data_.klog_files, [](const KlogFile &f) { return !f.active; });
        notice(Notice::Kind::success, "Ordner geleert");
        changed();
    }

    void files_open(const std::string &path) override
    {
        FsList &l = data_.fs_list;
        l = FsList{};
        l.loaded = true;
        l.path = path;
        ++fs_seq_;
        l.seq = fs_seq_;
        auto it = fs_tree_.find(path);
        if (!path.empty() && it == fs_tree_.end())
            l.error = "Der Ordner lässt sich nicht lesen.";
        else if (it != fs_tree_.end())
        {
            l.entries = it->second;
            l.total = static_cast<int>(l.entries.size());
            l.writable = path.rfind("/mnt/usb0", 0) == 0 || path.rfind("/data", 0) == 0;
            l.free_bytes = static_cast<std::int64_t>(247e9);
            const std::size_t slash = path.find_last_of('/');
            l.parent = path == "/" ? "" : slash == 0 ? "/" : path.substr(0, slash);
        }
        changed();
    }

    void files_post(const std::string &route, const std::string &json) override
    {
        if (route == "copy" || route == "move")
        {
            FsJob &j = data_.fs_job;
            j = FsJob{};
            j.active = true;
            j.state = "copying";
            j.kind = route;
            j.dest = "/mnt/usb0/homebrew";
            j.current = "eboot.bin";
            j.percent = 37;
            j.bytes_total = static_cast<std::uint64_t>(3.2e9);
            j.bytes_done = static_cast<std::uint64_t>(1.18e9);
        }
        else if (route == "mkdir")
            notice(Notice::Kind::success, "Dateien", "Der Ordner ist angelegt.");
        (void)json;
        changed();
    }

    void files_delete(const std::vector<std::string> &paths) override
    {
        notice(Notice::Kind::info, "Dateien",
               std::to_string(paths.size()) + " Eintrag/Einträge werden gelöscht.");
        changed();
    }

    void files_view(const std::string &path) override
    {
        FsView &v = data_.fs_view;
        v = FsView{};
        v.seq = ++fs_seq_;
        v.path = path;
        v.text = "[general]\nautomount = 1\nscan_interval = 30\nbackport = "
                 "auto\n\n[paths]\n/mnt/usb0/homebrew\n"
                 "/mnt/ext1/homebrew\n/data/homebrew\n";
        v.size = v.text.size();
        changed();
    }

    void files_size(const std::string &path) override
    {
        FsSize s;
        s.bytes = static_cast<std::uint64_t>(41.7e9);
        s.files = 1284;
        data_.fs_sizes[path] = s;
        changed();
    }

    void playtime_reset() override
    {
        data_.playtime.sessions.clear();
        data_.playtime.total = 0;
        notice(Notice::Kind::success, "Spielzeit", "Der Verlauf ist gelöscht.");
        changed();
    }

    void saves_post(const std::string &route, const std::string &json) override
    {
        SaveJob &j = data_.saves.job;
        j = SaveJob{};
        j.active = j.can_cancel = route != "cancel";
        j.kind = route;
        j.state = route == "cancel" ? "cancelled" : "copying";
        j.phase = route == "verify" ? "wird geprüft" : "Dateien werden kopiert";
        j.percent = 42;
        j.files_done = 17;
        j.files_total = 40;
        j.current = "CUSA18842/savedata0/sce_sys/param.sfo";
        j.elapsed_s = 12;
        (void)json;
        changed();
    }

    void backup_delete(const std::string &path) override
    {
        auto &b = data_.game_backups.backups;
        b.erase(
            std::remove_if(b.begin(), b.end(), [&](const GameBackup &x) { return x.path == path; }),
            b.end());
        notice(Notice::Kind::info, "Sicherung", "Die Sicherung wird gelöscht.");
        changed();
    }

    void usb_eject(const std::string &mount) override
    {
        UsbEject &e = data_.usb_eject;
        e.running = true;
        e.state = "running";
        e.mount = mount;
        e.message = "usb0 wird ausgeworfen …";
        eject_at_ = clock_ + 1.5f;
        changed();
    }

    void disc_post(const std::string &route, const std::string &json) override
    {
        const auto has = [&](const char *text) { return json.find(text) != std::string::npos; };
        DiscState &d = data_.disc;
        DumpState &m = data_.dump;
        std::string msg;
        if (route == "disc")
        {
            if (has("\"enable\""))
                d.enabled = true;
            else if (has("\"disable\""))
                d.enabled = false;
            else if (has("\"settings\""))
                d.auto_dump = has("\"auto_dump\":true");
            else if (has("\"dump\""))
            {
                d.job_state = "running";
                d.job_name = d.name;
                d.job_total = static_cast<double>(d.image_bytes);
                d.job_done = d.job_total * 0.3;
                d.job_eta_s = 240;
                msg = "Das Abbild wird angelegt.";
            }
            else if (has("\"cancel\""))
                d.job_state = "cancelled";
        }
        else if (route == "dump")
        {
            if (has("\"start\"") || has("\"dlc_only\""))
            {
                m.job = DumpJob{};
                m.job.state = "running";
                m.job.active = true;
                m.job.verify = m.settings.verify;
                m.job.name = "Lantern Pass";
                m.job.title_id = "PPSA03860";
                m.job.stage = "Dateien werden kopiert";
                m.job.kind = has("\"dlc_only\"") ? "dlc" : "game";
                m.job.drive = "usb0";
                m.job.dest = m.settings.subdir;
                m.job.folder = "PPSA03860_Lantern_Pass";
                m.job.total_bytes = 18.5e9;
                m.job.done_bytes = 6.1e9;
                m.job.rate = 92e6;
                m.job.eta_s = 135;
                msg = "Der Dump hat begonnen.";
            }
            else if (has("\"cancel\""))
            {
                m.job.active = false;
                m.job.state = "cancelled";
            }
            else if (has("\"queue_add\""))
            {
                DumpQueueItem i;
                i.id = has("CUSA12100") ? "CUSA12100" : "PPSA03860";
                i.name = i.id == "CUSA12100" ? "Pixel Garden" : "Lantern Pass";
                i.state = "pending";
                i.disc_out = i.id == "CUSA12100";
                m.queue.items.push_back(i);
                msg = "In die Warteschlange gelegt.";
            }
            else if (has("\"queue_remove\""))
            {
                for (std::size_t i = 0; i < m.queue.items.size(); ++i)
                {
                    if (has(("\"" + m.queue.items[i].id + "\"").c_str()))
                    {
                        m.queue.items.erase(m.queue.items.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
                }
            }
            else if (has("\"queue_clear\""))
                m.queue.items.clear();
            else if (has("\"queue_start\""))
            {
                m.queue.active = !m.queue.items.empty();
                m.queue.current = 0;
            }
            else if (has("\"queue_stop\""))
                m.queue.active = false;
            else if (has("\"settings\""))
            {
                m.settings.verify = has("\"verify\":true");
                m.settings.decrypt = has("\"decrypt\":true");
                m.settings.fself = has("\"fself\":true");
                m.settings.backport = has("\"backport\":true");
            }
            else if (has("\"queue_settings\""))
                m.queue.replace = has("\"replace\":true");
        }
        else if (route == "dumps/delete" && !data_.dumps.dumps.empty())
        {
            data_.dumps.dumps.erase(data_.dumps.dumps.begin());
            msg = "Der Dump ist gelöscht.";
        }
        if (!msg.empty())
            notice(Notice::Kind::success, route == "disc" ? "Laufwerk" : "Dump", msg);
        changed();
    }

    void set_backdrop(const std::string &area, BackdropMode mode) override
    {
        for (BackdropSetting &b : data_.backdrops)
        {
            if (b.id != area)
                continue;
            if (mode == BackdropMode::custom && !b.custom)
            {
                notice(Notice::Kind::error, "Kein eigenes Bild",
                       "Lade es zuerst in der Browser-App hoch: System → Hintergründe der "
                       "Vollbild-App.");
                changed();
                return;
            }
            b.mode = mode;
        }
        backdrops().apply(data_.backdrops);
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
    struct MockDrive
    {
        const char *mount, *label, *fs;
        double free_gb;
    };
    // One of each case the dialogs know: too little room, the source's own drive, a good one,
    // FAT32.
    static constexpr MockDrive kDrives[] = {{"/data", "Interne SSD", "ufs", 24.0},
                                            {"/mnt/usb0", "USB-Laufwerk 1", "exfatfs", 1210.0},
                                            {"/mnt/ext0", "M.2-Laufwerk", "exfatfs", 640.0},
                                            {"/mnt/usb1", "USB-Stick", "msdosfs", 59.0}};

    // Every plan goes through JSON and ps5cc/plans.cpp, as the console app's answers do.
    PlanReply make_plan(Tool tool, const std::string &id) const
    {
        PlanReply reply;
        reply.tool = tool;
        reply.id = id;
        const Game *g = find(id);
        if (g == nullptr)
        {
            reply.error = "Das Spiel ist nicht in der Liste.";
            return reply;
        }
        const double size = static_cast<double>(g->size_bytes);
        const double largest = std::min(size, 6.2e9);
        const bool folder = g->format == "folder";
        const bool running = g->id == data_.status.game_id;
        const std::string leaf = folder ? g->id : g->id + "." + g->format;
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", 1);
        cJSON_AddStringToObject(
            root, tool == Tool::erase || tool >= Tool::image_unpack ? "id" : "title_id",
            g->id.c_str());
        cJSON_AddStringToObject(root, "name", g->name.c_str());
        cJSON_AddBoolToObject(root, "running", running);
        const auto job_active = [this](JobKind kind)
        { return data_.jobs[static_cast<std::size_t>(kind)].active; };
        const auto drive_base = [&](cJSON *d, const MockDrive &m)
        {
            cJSON_AddStringToObject(d, "mount", m.mount);
            cJSON_AddStringToObject(d, "label", m.label);
            cJSON_AddStringToObject(d, "base", m.mount);
            cJSON_AddStringToObject(d, "fs", m.fs);
            cJSON_AddNumberToObject(d, "free_bytes", m.free_gb * 1e9);
            cJSON_AddBoolToObject(d, "source_here",
                                  g->path.rfind(std::string(m.mount) + "/", 0) == 0);
        };
        const auto fat32 = [](const MockDrive &m, double file)
        { return std::string(m.fs) == "msdosfs" && file > 4.29e9; };
        switch (tool)
        {
        case Tool::copy:
        {
            cJSON_AddStringToObject(root, "source", g->path.c_str());
            cJSON_AddStringToObject(root, "kind", folder ? "folder" : "file");
            cJSON_AddNumberToObject(root, "size_bytes", size);
            cJSON_AddNumberToObject(root, "files", folder ? 2312 : 1);
            cJSON_AddNumberToObject(root, "largest_bytes", largest);
            cJSON_AddNumberToObject(root, "skipped", 0);
            cJSON_AddBoolToObject(root, "busy", job_active(JobKind::copy));
            // The preview starts every kind beside the others; the console app keeps copy and
            // convert apart.
            cJSON_AddBoolToObject(root, "other_busy", 0);
            cJSON *targets = cJSON_AddArrayToObject(root, "targets");
            for (const MockDrive &m : kDrives)
            {
                cJSON *d = cJSON_CreateObject();
                drive_base(d, m);
                cJSON_AddBoolToObject(d, "fat32_too_big", fat32(m, largest));
                cJSON_AddBoolToObject(d, "enough_space", m.free_gb * 1e9 >= size);
                for (int mode = 0; mode < kModes; ++mode)
                {
                    cJSON *o = cJSON_AddObjectToObject(d, kModeKeys[mode]);
                    const std::string path = std::string(m.mount) +
                                             (mode == 0 ? "/homebrew/" : "/PS5-Sicherung/Spiele/") +
                                             leaf;
                    const bool leftover = mode == 1 && std::string(m.mount) == "/mnt/ext0";
                    cJSON_AddStringToObject(o, "path", path.c_str());
                    cJSON_AddBoolToObject(o, "exists", path == g->path);
                    cJSON_AddBoolToObject(o, "unfinished", leftover);
                    cJSON_AddNumberToObject(o, "unfinished_bytes", leftover ? 2.1e9 : 0.0);
                    cJSON_AddBoolToObject(o, "enough_space", m.free_gb * 1e9 >= size);
                    cJSON_AddBoolToObject(o, "path_too_long", 0);
                }
                cJSON_AddItemToArray(targets, d);
            }
            break;
        }
        case Tool::convert:
        {
            const bool ps4 = g->platform == "PS4" && folder;
            const double raw = size * 1.02;
            const double ffpkg = folder ? size * 1.04 : 0.0;
            cJSON_AddStringToObject(root, "source", g->path.c_str());
            cJSON_AddStringToObject(root, "kind", folder ? "folder" : "file");
            cJSON_AddBoolToObject(root, "ps4", ps4);
            cJSON_AddStringToObject(root, "stem", g->id.c_str());
            cJSON_AddNumberToObject(root, "raw_bytes", raw);
            cJSON *risk = cJSON_AddObjectToObject(root, "stream_risk");
            const bool big = size > 30e9;
            cJSON_AddBoolToObject(risk, "risky", big);
            cJSON_AddNumberToObject(risk, "data_bytes", size);
            cJSON_AddBoolToObject(risk, "big_data", big);
            cJSON_AddNumberToObject(risk, "container_bytes", big ? size * 0.4 : 0.0);
            cJSON *names = cJSON_AddArrayToObject(risk, "containers");
            if (big)
                cJSON_AddItemToArray(names, cJSON_CreateString("streaming.pak"));
            cJSON_AddStringToObject(risk, "stream_dir", "");
            cJSON_AddNumberToObject(risk, "stream_dir_bytes", 0);
            cJSON_AddNumberToObject(root, "ffpkg_bytes", ffpkg);
            cJSON_AddNumberToObject(root, "files", folder ? 2312 : 1);
            cJSON *ops = cJSON_AddArrayToObject(root, "ops");
            cJSON_AddItemToArray(ops, cJSON_CreateString("ffpfsc"));
            if (folder)
            {
                cJSON_AddItemToArray(ops, cJSON_CreateString("ffpkg"));
                cJSON_AddItemToArray(ops, cJSON_CreateString("exfat"));
            }
            cJSON_AddBoolToObject(root, "busy", job_active(JobKind::convert));
            cJSON_AddBoolToObject(root, "other_busy", 0);
            cJSON *targets = cJSON_AddArrayToObject(root, "targets");
            for (const MockDrive &m : kDrives)
            {
                // A PS4 title from the console's own storage brings it down: the app lists only USB
                // and M.2.
                if (ps4 && std::string(m.mount) == "/data")
                    continue;
                cJSON *d = cJSON_CreateObject();
                drive_base(d, m);
                cJSON_AddBoolToObject(d, "fat32_too_big", fat32(m, raw));
                cJSON_AddBoolToObject(d, "fat32_too_big_ffpkg",
                                      fat32(m, ffpkg > 0.0 ? ffpkg : raw));
                cJSON_AddBoolToObject(d, "enough_space", m.free_gb * 1e9 >= raw);
                for (int mode = 0; mode < kModes; ++mode)
                {
                    cJSON *o = cJSON_AddObjectToObject(d, kModeKeys[mode]);
                    const std::string dir =
                        std::string(m.mount) + (mode == 0 ? "/homebrew" : "/PS5-Sicherung/Spiele");
                    cJSON_AddStringToObject(o, "dir", dir.c_str());
                    for (const char *op : kOpKeys)
                    {
                        cJSON *t = cJSON_AddObjectToObject(o, op);
                        const std::string path = dir + "/" + g->id + "." + op;
                        cJSON_AddStringToObject(t, "path", path.c_str());
                        cJSON_AddBoolToObject(t, "exists", path == g->path);
                        cJSON_AddBoolToObject(t, "unfinished", 0);
                        cJSON_AddNumberToObject(t, "unfinished_bytes", 0);
                        cJSON_AddBoolToObject(t, "enough_space", m.free_gb * 1e9 >= raw);
                    }
                }
                cJSON_AddItemToArray(targets, d);
            }
            break;
        }
        case Tool::move:
        case Tool::unpack:
        {
            const bool unpack = tool == Tool::unpack;
            const std::size_t slash = g->path.find_last_of('/');
            const std::string here = g->path.substr(0, slash);
            cJSON_AddStringToObject(root, "op", unpack ? "unpack" : "move");
            cJSON_AddStringToObject(root, "source", g->path.c_str());
            cJSON_AddStringToObject(root, "source_dir", here.c_str());
            cJSON_AddStringToObject(root, "source_type", folder ? "folder" : "image");
            cJSON_AddStringToObject(root, "image_type",
                                    g->format == "ffpfsc" ? "pfsc" : g->format.c_str());
            cJSON_AddNumberToObject(root, "size_bytes", size);
            cJSON *dests = cJSON_AddArrayToObject(root, "destinations");
            for (const MockDrive &m : kDrives)
            {
                cJSON *d = cJSON_CreateObject();
                const std::string path = std::string(m.mount) + "/homebrew";
                const std::string target =
                    path + "/" + (unpack ? g->id : g->path.substr(slash + 1));
                cJSON_AddStringToObject(d, "path", path.c_str());
                cJSON_AddStringToObject(d, "drive", m.label);
                cJSON_AddStringToObject(d, "fs", m.fs);
                cJSON_AddNumberToObject(d, "free_bytes", m.free_gb * 1e9);
                cJSON_AddBoolToObject(d, "exists", 1);
                cJSON_AddBoolToObject(d, "here", path == here);
                cJSON_AddBoolToObject(d, "enough_space",
                                      path == here && !unpack ? 1 : m.free_gb * 1e9 >= size);
                cJSON_AddBoolToObject(d, "fat32_too_big", fat32(m, largest));
                cJSON_AddStringToObject(d, "target", target.c_str());
                cJSON_AddBoolToObject(d, "target_exists", target == g->path);
                cJSON_AddItemToArray(dests, d);
            }
            break;
        }
        case Tool::image_unpack:
        {
            cJSON_AddStringToObject(root, "image", g->path.c_str());
            cJSON_AddNumberToObject(root, "size_bytes", size);
            cJSON_AddNumberToObject(root, "files", 2312);
            cJSON_AddNumberToObject(root, "largest_bytes", largest);
            cJSON_AddBoolToObject(root, "damaged", 0);
            cJSON_AddBoolToObject(root, "busy", job_active(JobKind::image));
            cJSON *targets = cJSON_AddArrayToObject(root, "targets");
            for (const MockDrive &m : kDrives)
            {
                cJSON *d = cJSON_CreateObject();
                drive_base(d, m);
                const std::string path = std::string(m.mount) + "/homebrew/" + g->id;
                cJSON_AddStringToObject(d, "path", path.c_str());
                cJSON_AddBoolToObject(d, "exists", 0);
                cJSON_AddBoolToObject(d, "enough_space", m.free_gb * 1e9 >= size + 268435456.0);
                cJSON_AddBoolToObject(d, "fat32_too_big", fat32(m, largest));
                cJSON_AddBoolToObject(d, "path_too_long", 0);
                cJSON_AddItemToArray(targets, d);
            }
            break;
        }
        case Tool::image_check:
        case Tool::image_repair:
        {
            cJSON_AddStringToObject(root, "format", g->format.c_str());
            cJSON_AddNumberToObject(root, "image_bytes", size);
            cJSON_AddBoolToObject(root, "can_check", 1);
            cJSON_AddBoolToObject(root, "can_unpack", 1);
            cJSON *repair = cJSON_AddObjectToObject(root, "repair");
            if (g->format != "ffpfsc")
            {
                cJSON_AddStringToObject(repair, "state", "na");
            }
            else if (g->id == "PPSA03860")
            {
                cJSON_AddStringToObject(repair, "state", "fixable");
                cJSON_AddNumberToObject(repair, "declared", size);
                cJSON_AddNumberToObject(repair, "actual", size + 65536.0);
            }
            else
            {
                cJSON_AddStringToObject(repair, "state", "fine");
            }
            cJSON *last = cJSON_AddObjectToObject(root, "last_check");
            cJSON_AddNumberToObject(last, "when", 1792800000);
            cJSON_AddBoolToObject(last, "clean", 1);
            cJSON_AddNumberToObject(last, "files", 2312);
            cJSON_AddNumberToObject(last, "bad_files", 0);
            cJSON_AddArrayToObject(last, "bad");
            break;
        }
        case Tool::erase:
        {
            const bool installed = g->format == "fpkg" || g->format == "ps4mount";
            cJSON_AddStringToObject(root, "kind", installed ? "installed" : "smp");
            cJSON_AddStringToObject(root, "path", g->path.c_str());
            cJSON_AddStringToObject(root, "format", g->format.c_str());
            cJSON_AddStringToObject(root, "version", g->version.c_str());
            cJSON_AddNumberToObject(root, "size_bytes", size);
            cJSON *items = cJSON_AddArrayToObject(root, "items");
            const std::string what = (folder ? "Der Spielordner " : "Die Abbilddatei ") + g->path;
            cJSON_AddItemToArray(
                items,
                cJSON_CreateString(installed ? "Das installierte Spiel (über die Deinstallation "
                                               "der Konsole)"
                                             : what.c_str()));
            cJSON_AddItemToArray(items, cJSON_CreateString("Die Kachel auf dem Startbildschirm"));
            if (!installed)
                cJSON_AddItemToArray(items, cJSON_CreateString("Der Eintrag bei ShadowMountPlus"));
            cJSON_AddBoolToObject(root, "saves_kept", 1);
            cJSON_AddBoolToObject(root, "can_delete", !running);
            if (running)
                cJSON_AddStringToObject(root, "why", "Das Spiel läuft gerade. Bitte erst beenden.");
            cJSON_AddStringToObject(root, "confirm", "4f9c2a17");
            break;
        }
        case Tool::count:
            break;
        }
        char *text = cJSON_PrintUnformatted(root);
        reply.json = text != nullptr ? text : "{}";
        cJSON_free(text);
        cJSON_Delete(root);
        reply.ok = true;
        return reply;
    }

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
        s.soc_c =
            static_cast<int>(std::lround((playing ? 64.0 : 47.0) + 1.2 * std::sin(t * 0.17 + 1.0)));
        s.gpu_c =
            static_cast<int>(std::lround((playing ? 66.0 : 46.0) + 1.4 * std::sin(t * 0.13 + 2.0)));
        s.hottest_c = std::max(s.cpu_c, std::max(s.soc_c, s.gpu_c));
        s.fan_pct = static_cast<float>(
            std::round(((playing ? 46.3 : 20.4) + 2.2 * std::sin(t * 0.09)) * 10.0) / 10.0);
        s.automatic = true;
    }

    // Two drives with a handful of packages: a PS5 game with its update and add-on, a PS4 game, a
    // split package with a part missing; one already waits in the queue.
    void fill_packages()
    {
        struct Seed
        {
            const char *id, *name, *drive, *label, *title, *version, *kind;
            int plat;
            double gb;
            int parts, found;
        };
        static const Seed kSeeds[] = {
            {"a1f04c2e9b7d3016", "Lantern Pass", "/mnt/usb0", "usb0", "PPSA04211", "01.000", "base",
             5, 38.2, 0, 0},
            {"b2e15d3fac8e4127", "Lantern Pass", "/mnt/usb0", "usb0", "PPSA04211", "01.020",
             "update", 5, 4.6, 0, 0},
            {"c3d26e40bd9f5238", "Lantern Pass – Nordwind-Paket", "/mnt/usb0", "usb0", "PPSA04211",
             "01.000", "dlc", 5, 1.1, 0, 0},
            {"d4c37f51ce0a6349", "Iron Tide", "/mnt/usb0", "usb0", "CUSA18842", "01.000", "base", 4,
             22.7, 0, 0},
            {"e5b48062df1b745a", "Sky Drifters", "/user", "Konsolenspeicher", "PPSA07730", "01.000",
             "base", 5, 61.0, 3, 2},
            {"f6a59173e02c856b", "Paper Harbor", "/user", "Konsolenspeicher", "PPSA02954", "01.003",
             "base", 5, 12.4, 0, 0},
        };
        data_.packages.clear();
        for (const Seed &s : kSeeds)
        {
            Package k;
            k.id = s.id;
            k.name = s.name;
            k.drive = s.drive;
            k.drive_label = s.label;
            k.title_id = s.title;
            k.content_id = std::string("EP0000-") + s.title + "_00-0000000000000000";
            k.version = s.version;
            k.kind = s.kind;
            k.plat = s.plat;
            k.total = static_cast<std::uint64_t>(s.gb * 1e9);
            k.parts_total = s.parts;
            k.parts_found = s.found;
            k.size = s.parts > 0 ? k.total / static_cast<std::uint64_t>(s.parts) *
                                       static_cast<std::uint64_t>(s.found)
                                 : k.total;
            k.file =
                std::string(s.title) + "-" + s.version + (s.parts > 0 ? ".pkg (geteilt)" : ".pkg");
            data_.packages.push_back(k);
        }
        data_.package_drives = {{"/mnt/usb0", "usb0", 182ull << 30},
                                {"/mnt/ext1", "M.2-Speicher", 640ull << 30},
                                {"/data", "Konsolenspeicher", 9ull << 30}};
        data_.package_scan.loaded = true;
        data_.package_scan.ever = true;
        data_.package_scan.parsed = static_cast<int>(data_.packages.size());
        PackageQueueItem waiting;
        waiting.id = "f6a59173e02c856b";
        waiting.name = "Paper Harbor";
        waiting.version = "01.003";
        waiting.kind = "base";
        waiting.state = "waiting";
        data_.package_queue.items = {waiting};
        count_queue();
    }

    void count_queue()
    {
        PackageQueue &q = data_.package_queue;
        q.waiting = q.done = q.failed = 0;
        for (const PackageQueueItem &i : q.items)
        {
            q.waiting += i.state == "waiting" ? 1 : 0;
            q.done += i.state == "done" ? 1 : 0;
            q.failed +=
                i.state == "failed" || i.state == "skipped" || i.state == "cancelled" ? 1 : 0;
        }
    }

    // A search takes two seconds; an install about eight, then the queue takes the next one.
    void tick_packages(float dt)
    {
        if (scan_left_ > 0.0f)
        {
            scan_left_ -= dt;
            data_.package_scan.parsed =
                static_cast<int>(data_.packages.size() * (1.0f - scan_left_ / 2.0f));
            if (scan_left_ <= 0.0f)
            {
                data_.package_scan.scanning = false;
                data_.package_scan.parsed = static_cast<int>(data_.packages.size());
            }
            changed();
        }
        PackageInstall &job = data_.package_install;
        if (job.active)
        {
            job.bytes_done = std::min(job.bytes_total, job.bytes_done + job.bytes_total * dt / 8.0);
            job.percent = job.bytes_total > 0.0
                              ? static_cast<int>(job.bytes_done * 100.0 / job.bytes_total)
                              : -1;
            job.remain_s = static_cast<int>((job.bytes_total - job.bytes_done) /
                                            std::max(job.bytes_total / 8.0, 1.0));
            if (job.bytes_done >= job.bytes_total)
            {
                job.active = false;
                job.state = "done";
                job.percent = 100;
                notice(Notice::Kind::success, "„" + job.name + "“ installiert");
                for (PackageQueueItem &i : data_.package_queue.items)
                {
                    if (i.state == "running")
                        i.state = "done";
                }
                count_queue();
            }
            changed();
        }
        PackageQueue &q = data_.package_queue;
        if (q.running && !job.active)
        {
            const auto next =
                std::find_if(q.items.begin(), q.items.end(),
                             [](const PackageQueueItem &i) { return i.state == "waiting"; });
            if (next == q.items.end() || q.pause_requested)
            {
                q.running = false;
                q.pause_requested = false;
            }
            else
            {
                next->state = "running";
                job = PackageInstall{};
                job.state = "installing";
                job.active = true;
                job.can_cancel = true;
                job.name = next->name;
                job.version = next->version;
                job.bytes_total = 12.4e9;
                job.percent = 0;
            }
            count_queue();
            changed();
        }
    }

    // PS4Mount on, three games on the USB drive: one ready with an update and add-ons, one taken
    // out of the library, one installed on the console as well.
    void fill_ps4mount()
    {
        Ps4Mount &m = data_.ps4mount;
        m.loaded = m.enabled = m.running = true;
        m.version = "PS4Mount 0.7.0";
        m.config.valid = true;
        m.config.paths = {"/", "/pkg"};
        struct Seed
        {
            const char *id, *name, *version, *state;
            double gb;
            bool patch;
            int dlc, dlc_total, updates;
        };
        static const Seed kSeeds[] = {
            {"CUSA18842", "Iron Tide", "01.020", "ready", 22.7, true, 2, 3, 2},
            {"CUSA09911", "Harbor Lights", "01.000", "removed", 8.4, false, 0, 0, 0},
            {"CUSA05530", "Moorhen Racer", "01.050", "internal", 14.1, true, 0, 0, 1},
        };
        for (const Seed &s : kSeeds)
        {
            Ps4Game g;
            g.title_id = s.id;
            g.name = s.name;
            g.version = s.version;
            g.state = s.state;
            g.mount = "/mnt/usb0";
            g.size = static_cast<std::uint64_t>(s.gb * 1e9);
            g.has_patch = s.patch;
            g.dlc = s.dlc;
            g.dlc_total = s.dlc_total;
            g.updates = s.updates;
            m.games.push_back(g);
        }
        // What the search left on the drive: a game installed on the console, a PS5 pkg, one taken
        // out earlier.
        struct Skip
        {
            const char *file, *id, *name, *why;
        };
        static const Skip kSkips[] = {
            {"CUSA20417 - Lantern Bay - game.pkg", "CUSA20417", "Lantern Bay", "internal"},
            {"UP4402-PPSA07710_00-GLASSHARBOR00000.pkg", "", "", "not_ps4"},
            {"CUSA11208 - Copper Meadow - game.pkg", "", "", "restored"},
        };
        for (const Skip &s : kSkips)
        {
            Ps4Skipped x;
            x.path = std::string("/mnt/usb0/") + s.file;
            x.mount = "/mnt/usb0";
            x.title_id = s.id;
            x.name = s.name;
            x.why = s.why;
            m.skipped.push_back(x);
        }
    }

    // Dateien: three places and a few folders under the USB drive.
    void fill_files()
    {
        const auto place = [](const char *label, const char *path, double free_gb, bool writable)
        {
            FsPlace p;
            p.label = label;
            p.path = path;
            p.free_bytes = free_gb < 0 ? -1 : static_cast<std::int64_t>(free_gb * 1e9);
            p.writable = writable;
            return p;
        };
        data_.fs_places = {place("usb0", "/mnt/usb0", 247.1, true),
                           place("Daten der Konsole", "/data", 612.4, true),
                           place("Ganzes System", "/", -1, true)};
        const auto entry =
            [](const char *name, const char *type, double mb, int days, bool writable)
        {
            FsEntry e;
            e.name = name;
            e.type = type;
            e.size = static_cast<std::uint64_t>(mb * 1e6);
            e.mtime = 1793275200 - static_cast<std::int64_t>(days) * 86400;
            e.writable = writable;
            return e;
        };
        fs_tree_["/mnt/usb0"] = {entry("homebrew", "dir", 0, 2, true),
                                 entry("ps4", "dir", 0, 1, true),
                                 entry("PS5-Sicherung", "dir", 0, 5, true),
                                 entry("payloads", "dir", 0, 9, true),
                                 entry("shadowmount.ini", "file", 0.002, 3, true),
                                 entry("Lantern Pass.ffpfsc", "file", 31200, 6, true),
                                 entry("notes.txt", "file", 0.004, 12, true)};
        fs_tree_["/mnt/usb0/homebrew"] = {entry("PPSA01325", "dir", 0, 2, true),
                                          entry("CUSA05041", "dir", 0, 4, true)};
        fs_tree_["/data"] = {entry("homebrew", "dir", 0, 1, true),
                             entry("PS5-Cooling-Center", "dir", 0, 0, false)};
        fs_tree_["/"] = {entry("data", "dir", 0, 0, false), entry("mnt", "dir", 0, 0, false),
                         entry("system", "dir", 0, 30, false), entry("user", "dir", 0, 0, false)};
    }

    // Spielzeit, Spielstände and Sicherungen: a few weeks of play, two users with saves, two
    // backups of games.
    void fill_phase4()
    {
        Playtime &p = data_.playtime;
        p.loaded = true;
        p.now = 1793275200; // 29.10.2026 12:00 UTC
        struct Seed
        {
            const char *id, *name;
            int days_ago, hour, minutes, cpu;
        };
        static const Seed kSeeds[] = {
            {"PPSA01325", "Lantern Pass", 0, 9, 95, 71},
            {"CUSA18842", "Iron Tide", 1, 20, 140, 68},
            {"PPSA01325", "Lantern Pass", 2, 18, 60, 70},
            {"PPSA04311", "Harbor Lights", 3, 21, 45, 66},
            {"CUSA18842", "Iron Tide", 4, 19, 210, 69},
            {"PPSA01325", "Lantern Pass", 6, 17, 30, 70},
            {"CUSA12100", "Pixel Garden", 8, 15, 25, 58},
            {"CUSA18842", "Iron Tide", 9, 22, 120, 72},
            {"PPSA04311", "Harbor Lights", 11, 20, 80, 67},
            {"PPSA01325", "Lantern Pass", 12, 19, 150, 71},
            {"CUSA12100", "Pixel Garden", 20, 16, 40, 57},
        };
        for (const Seed &s : kSeeds)
        {
            PlaySession x;
            x.id = s.id;
            x.name = s.name;
            x.start =
                p.now - static_cast<std::int64_t>(s.days_ago) * 86400 - 12 * 3600 + s.hour * 3600;
            x.end = x.start + s.minutes * 60 + 300;
            x.play = s.minutes * 60;
            x.cpu_max = s.cpu;
            x.soc_max = s.cpu - 3;
            x.fan_avg = 34;
            p.sessions.insert(p.sessions.begin(), x);
        }
        p.total = static_cast<int>(p.sessions.size());
        p.kept = 2000;
        p.live = true;
        p.live_state = "front";
        p.current.id = "CUSA18842";
        p.current.name = "Iron Tide";
        p.current.start = p.now - 2400;
        p.current.play = 2280;
        p.current.cpu_max = 69;
        p.current.soc_max = 66;

        Saves &s = data_.saves;
        s.loaded = true;
        struct Title
        {
            const char *uid, *id, *name, *platform;
            double mb;
            bool installed;
        };
        static const Title kTitles[] = {
            {"10000001", "PPSA01325", "Lantern Pass", "PS5", 48.2, true},
            {"10000001", "PPSA04311", "Harbor Lights", "PS5", 12.5, true},
            {"10000001", "CUSA18842", "Iron Tide", "PS4", 3.1, true},
            {"10000002", "CUSA12100", "Pixel Garden", "PS4", 0.8, false},
        };
        for (const char *uid : {"10000001", "10000002"})
        {
            SaveUser u;
            u.uid = uid;
            u.name = std::string(uid) == "10000001" ? "Mira" : "Jonas";
            for (const Title &x : kTitles)
            {
                if (std::string(x.uid) != uid)
                    continue;
                SaveTitle st;
                st.uid = uid;
                st.id = x.id;
                st.name = x.name;
                st.platform = x.platform;
                st.installed = x.installed;
                st.bytes = static_cast<std::uint64_t>(x.mb * 1e6);
                st.mtime = p.now - 86400;
                u.bytes += st.bytes;
                u.titles.push_back(st);
            }
            s.users.push_back(u);
        }
        SaveDrive usb;
        usb.mount = "/mnt/usb0";
        usb.label = "usb0";
        usb.free_bytes = static_cast<std::uint64_t>(247e9);
        SaveDrive ssd;
        ssd.mount = "/user";
        ssd.label = "Interne SSD";
        ssd.free_bytes = static_cast<std::uint64_t>(612e9);
        s.drives = {usb, ssd};
        SaveBackup b;
        b.created = p.now - 3 * 86400;
        b.path = "/mnt/usb0/PS5-Sicherung/Spielstaende/2026-10-26_1830";
        b.label = "usb0";
        b.kind = "backup";
        b.bytes = static_cast<std::uint64_t>(63.8e6);
        b.user_names = {"Mira"};
        for (const Title &x : kTitles)
        {
            if (std::string(x.uid) != "10000001")
                continue;
            SaveTitle st;
            st.uid = x.uid;
            st.id = x.id;
            st.name = x.name;
            st.platform = x.platform;
            st.bytes = static_cast<std::uint64_t>(x.mb * 1e6);
            b.titles.push_back(st);
        }
        s.backups.push_back(b);
        SaveBackup undo = b;
        undo.created = p.now - 9 * 86400;
        undo.path = "/mnt/usb0/PS5-Sicherung/Spielstaende/2026-10-20_2110-vorher";
        undo.kind = "undo";
        undo.titles.resize(1);
        undo.bytes = undo.titles[0].bytes;
        s.backups.push_back(undo);

        GameBackups &g = data_.game_backups;
        g.loaded = true;
        GameBackup a;
        a.path = "/mnt/usb0/PS5-Sicherung/Spiele/PPSA01325_Lantern_Pass";
        a.name = "Lantern Pass";
        a.drive = "usb0";
        a.type = "Ordner";
        a.size_bytes = static_cast<std::uint64_t>(41.7e9);
        a.modified = p.now - 5 * 86400;
        a.sums = true;
        GameBackup c = a;
        c.path = "/mnt/usb0/PS5-Sicherung/Spiele/CUSA18842_Iron_Tide";
        c.name = "Iron Tide";
        c.size_bytes = static_cast<std::uint64_t>(22.7e9);
        c.modified = p.now - 12 * 86400;
        c.sums = false;
        c.unfinished = true;
        g.backups = {a, c};
    }

    // The internal SSD, a USB drive that can be ejected, the M.2 drive that cannot.
    void fill_volumes()
    {
        struct Seed
        {
            const char *label, *path;
            double total_gb, free_gb;
            bool eject;
        };
        static const Seed kSeeds[] = {
            {"Interne SSD", "/user", 1840.0, 612.4, false},
            {"usb0", "/mnt/usb0", 2000.4, 247.1, true},
            {"M.2-Erweiterung", "/mnt/ext1", 1000.2, 388.6, false},
        };
        for (const Seed &s : kSeeds)
        {
            Volume v;
            v.label = s.label;
            v.path = s.path;
            v.total_bytes = static_cast<std::uint64_t>(s.total_gb * 1e9);
            v.free_bytes = static_cast<std::uint64_t>(s.free_gb * 1e9);
            v.used_bytes = v.total_bytes - v.free_bytes;
            v.ejectable = s.eject;
            data_.volumes.push_back(v);
        }
        data_.volumes_loaded = true;
        data_.usb_eject.loaded = data_.usb_eject.available = true;
        fill_system();
    }

    // System: what the console app answers, invented but shaped like it (PS5 Pro, WLAN, a 4K
    // television).
    void fill_system()
    {
        SystemData &d = data_.system_data;
        d.system =
            R"({"ok":true,"model":"CFI-7021 B01Y","serial_masked":"••••8023","firmware_raw":"0x12000043","firmware_version":"12.00",)"
            R"("firmware_group":"12.00-12.70","app_version":"1.75.3","firmware_reported":"12.000.043","uptime_sec":7425,)"
            R"("network":{"probed":true,"up":true,"kind":"wlan","ip":"192.168.1.94","ssid":"Wohnzimmer-5G","signal_pct":86,"band_ghz":5,"link_mbit":866},)"
            R"("display":{"name":"Wohnzimmer-TV","width":3840,"height":2160,"refresh_hz":60,"hdr":true},)"
            R"("controller":{"in_use":true,"press_age_s":4,"battery_pct":80,"charging":false,"from_log":true,"restored":false,"age_s":30},)"
            R"("console_name":"PS5-054","locale":{"language":4,"time_zone":61,"timezone_offset_min":3600,"utc_offset_raw":3600,"summer_time":false,"local_offset_min":60,"region_code":"EU"},)"
            R"("memory":{"rss_mb":297.8,"kernel_mb":314,"wire_mb":411.8,"swap_mb":0,"pt_cpu_used":4996,"pt_cpu_total":9216,"pt_gpu_used":141,"pt_gpu_total":9216,)"
            R"("processes":[{"name":"PPSA01325 eboot.bin","used_mb":10480,"total_mb":13312},{"name":"NPXS40087 SceShellUI.elf","used_mb":612,"total_mb":1024},)"
            R"({"name":"ps5tm.elf","used_mb":298,"total_mb":512},{"name":"shadowmountplus.elf","used_mb":42,"total_mb":64}]},)"
            R"("power_state":{"mode":"BIG_APP","idle_s":12,"age_s":3}})";
        d.status =
            R"({"ok":true,"temperatures":{"hottest_c":71,"gpu_valid":true,"gpu_c":66},)"
            R"("load":{"cores":[38,41,22,57,12,9,30,25],"cpus":[40,36,44,38,20,24,61,53,14,10,8,10,33,27,26,24],"system_cpu_mask":49152,)"
            R"("cpu_mode":"Spiel",)"
            R"("live_clocks":{"gfx_mhz":2350,"gfx_limit_mhz":2350,"mem_mhz":2250,"fabric_mhz":1600},"soc_power_valid":true,"soc_power_w":182.4},)"
            R"("power":{"valid":true,"live_w":196.0,"gpu_w":121.5,"mem_w":18.2,"rails":[{"name":"GFX","w":121.5,"v":0.86,"a":141.3}]},)"
            R"("fps":{"state":"aus"},"adapters":{"tile":"installed"},"messages":{"tile":"Die Kachel ist auf dem Startbildschirm."}})";
        d.health =
            R"({"ok":true,"weeks":6,"verdict_ready":true,"weeks_usable":5,"weeks_needed":4,"since_ms":1790000000000,)"
            R"("older_weeks":4,"delta_c":1.4,"baseline_c":69.8,"current_c":71.2})";
        d.config = R"({"probe_mask":13,"probe_revert_in_s":0,"probe_revert_mask":0})";
        d.channels =
            R"({"ok":true,"channel_count":8,"channels":[[61,62,63],[58,59,59],[66,66,67],[52,52,53],[70,71,71],[49,49,50],[63,64,64],[55,55,56]]})";
        d.diag =
            R"({"ok":true,"host_test":false,"scepad":{"init":true,"open":true,"read_state":true,"get_handle":true,"set_lightbar":true,)"
            R"("reset_lightbar":true,"set_process_privilege":true},"hid":{"resolved":true,"dev_hid_present":true,)"
            R"("dev_bluetooth_hid_present":true,"hidcontrol_get_battery_state":true,"hidcontrol_init":true,"bluetoothhid_init":false}})";
        d.drives = "";
        // a pattern of the QR code's shape: three finder squares and invented modules
        const int n = 29;
        std::string path;
        unsigned seed = 7;
        for (int y = 0; y < n; ++y)
        {
            for (int x = 0; x < n; ++x)
            {
                const auto finder = [&](int fx, int fy)
                {
                    const int dx = x - fx, dy = y - fy;
                    if (dx < 0 || dy < 0 || dx > 6 || dy > 6)
                        return -1;
                    return (dx == 0 || dy == 0 || dx == 6 || dy == 6 ||
                            (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4))
                               ? 1
                               : 0;
                };
                int on = finder(0, 0);
                if (on < 0)
                    on = finder(n - 7, 0);
                if (on < 0)
                    on = finder(0, n - 7);
                if (on < 0)
                {
                    seed = seed * 1103515245u + 12345u;
                    on = ((seed >> 16) & 3) == 0 || ((seed >> 18) & 1) == 0 ? 1 : 0;
                }
                if (on == 1)
                    path += "M" + std::to_string(x + 4) + " " + std::to_string(y + 4) + "h1v1h-1z";
            }
        }
        d.qr =
            R"({"ok":true,"url":"http://192.168.1.94:8086/","svg":"<svg viewBox=\"0 0 37 37\"><path d=\")" +
            path + R"(\"/></svg>"})";
        ++d.seq;
    }

    // A console with a drive: a PS2-DVD in it, one image made earlier, one dump of the Disc tab on
    // the USB drive.
    void fill_disc()
    {
        DiscState &d = data_.disc;
        d.loaded = d.has_drive = d.enabled = true;
        d.drive = "ok";
        d.present = d.has_title = d.dumpable = true;
        d.type = "PS2-DVD";
        d.serial = "SLES-50330";
        d.name = "Harbor Racer (Europe)";
        d.title_state = "ok";
        d.image_bytes = static_cast<std::uint64_t>(3.9e9);
        d.dir = "/data/PS5-Cooling-Center/disc";
        d.redump = true;
        d.events = {"Disc eingelegt: PS2-DVD, SLES-50330", "Laufwerk eingeschaltet"};
        DiscImage img;
        img.name = "Quiet Orbit (Europe).iso";
        img.serial = "SLES-51002";
        img.crc32 = "5A1C93E4";
        img.size = static_cast<std::uint64_t>(4.2e9);
        img.verified = true;
        d.images = {img};

        DumpState &m = data_.dump;
        m.loaded = true;
        m.settings.valid = true;
        m.settings.target = "/mnt/usb0";
        m.settings.subdir = "homebrew";
        DumpTarget ssd;
        ssd.mount = "/user";
        ssd.label = "Interne SSD";
        ssd.fs = "nullfs";
        ssd.free_bytes = static_cast<std::uint64_t>(1.83e12);
        ssd.console = true;
        DumpTarget usb;
        usb.mount = "/mnt/usb0";
        usb.label = "usb0";
        usb.fs = "exfatfs";
        usb.free_bytes = static_cast<std::uint64_t>(247e9);
        m.targets = {ssd, usb};
        DumpQueueItem q;
        q.id = "CUSA12100";
        q.name = "Pixel Garden";
        q.state = "pending";
        q.disc_out = true;
        m.queue.items = {q};

        DumpList &l = data_.dumps;
        l.loaded = l.ever = true;
        DumpEntry e;
        e.mount = "/mnt/usb0";
        e.dir = "homebrew";
        e.folder = "CUSA12100-app0_Pixel_Garden";
        e.title_id = "CUSA12100";
        e.title = "Pixel Garden";
        e.state = "done";
        e.kind = "game";
        e.bytes = static_cast<std::uint64_t>(0.9e9);
        e.decrypt = e.fself = true;
        l.dumps = {e};
    }

    // Every area on the tile's own picture; Spiele has one of the user's as well (set aside).
    void fill_backdrops()
    {
        static const char *const kLabels[kBackdropAreas] = {
            "Kühlung", "Spiele", "Dateimanager", "Payloads", "System", "Profil", "Protokoll"};
        data_.backdrops.clear();
        for (int i = 0; i < kBackdropAreas; ++i)
        {
            BackdropSetting b;
            b.id = backdrop_name(i);
            b.label = kLabels[i];
            b.custom = i == 1;
            b.stamp = b.custom ? "1793098000-412000" : "";
            data_.backdrops.push_back(b);
        }
        data_.backdrops_loaded = true;
        backdrops().apply(data_.backdrops);
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
            {"internal", "", "Intern", "ShadowMountPlus_1.7beta10.elf", 3.6, true},
            {"internal", "", "Intern", "PKG-Manager_v1.5.1.elf", 2.0, true},
            {"internal", "", "Intern", "pldmgr_v0.6.3.elf", 2.8, true},
            {"internal", "", "Intern", "nanodns_v0.4.elf", 0.13, true},
            {"internal", "", "Intern", "kaputt.elf", 0.0, false},
            {"usb", "/mnt/usb0", "usb0", "ps4mount_v0.7.0.elf", 1.1, true},
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
        data_.payload_profiles = {
            {"start",
             "Nach dem Start",
             {"ShadowMountPlus_1.7beta10.elf", "!2000", "nanodns_v0.4.elf"}},
            {"pkg", "Pakete installieren", {"PKG-Manager_v1.5.1.elf"}},
        };
        data_.profile_startup = "start";
        data_.profiles_loaded = true;
        data_.klog_files = {{"aufnahme-2026-10-30_2214.log", "rec", 4200000, 1793398440, false},
                            {"auszug-2026-10-29_1931.log", "save", 86000, 1793302260, false}};
        data_.klog_files_loaded = true;
        data_.own_pictures = {
            {"/mnt/usb0/Profilbilder/urlaub.jpg", "urlaub.jpg", "usb0", 2400000},
            {"/data/PS5-Cooling-Center/Profilbilder/katze.png", "katze.png", "Konsole", 880000}};
        data_.own_pictures_loaded = true;
        data_.profile.loaded = true;
        data_.profile.valid = true;
        data_.profile.username = "Spieler1";
        data_.profile.uid_hex = "1EA2F4D9";
        data_.profile.console_name = "PS5-054";
        data_.profile.model = "PS5 Pro (CFI-7021)";
        data_.profile.name_max = 16;
        data_.profile.can_rename = true;
        data_.avatars = {
            {"01-neon-portraet.jpg", "Neon-Porträt"}, {"02-neon-fuchs.jpg", "Neon-Fuchs"},
            {"03-neon-roboter.jpg", "Neon-Roboter"},  {"04-neon-pilz.jpg", "Neon-Pilz"},
            {"05-blau-astronaut.jpg", "Astronaut"},   {"06-blau-katze.jpg", "Katze"},
            {"07-gruen-drache.jpg", "Drache"},        {"08-gold-loewe.jpg", "Löwe"}};
        data_.running = {{236, "PS5 Cooling & System Center - Pro (diese App)", 25, true, ""},
                         {194, "shadowmountplus.elf", 101, false, "ShadowMountPlus_1.7beta10.elf"},
                         {121, "nanodns.elf", 2, false, ""},
                         {114, "elfldr.elf", 15, false, ""},
                         {112, "ftpsrv.elf", 3, false, ""},
                         {111, "klogsrv.elf", 2, false, ""},
                         {106, "kstuff.elf", 4, false, ""}};
    }

    void fill_store()
    {
        struct Item
        {
            const char *id, *name, *version, *category, *description, *installed;
            bool update;
            const char *owner; // invented accounts: a picture for some, none for others
            bool avatar;
        };
        static constexpr Item kItems[] = {
            {"shadowmountplus", "ShadowMountPlus", "1.7beta7", "Utilities & Tools",
             "Ein automatischer Mounter für Spiele-Abbilder im Hintergrund.", "1.7beta7", false,
             "mountsmith", true},
            {"pldmgr", "pldmgr", "v0.6.4", "Utilities & Tools",
             "Payloads verwalten und automatisch laden.", "v0.6.3", true, "loadkeeper", true},
            {"ps5upload", "ps5upload", "v8.4.0", "Networking & Servers",
             "Schnelle Übertragung vom Rechner auf die PS5 (braucht die PC-App).", "", false,
             "uplinker", true},
            {"ftpsrv", "ftpsrv", "v0.15", "Networking & Servers", "FTP-Server auf Port 2121.", "",
             false, "portwright", false},
            {"kstuff", "kstuff", "v1.6.7", "System & Jailbreak", "Kernel-Hilfen für den Jailbreak.",
             "", false, "", false},
            {"elfldr", "elfldr", "v0.21", "Loaders", "Der ELF-Loader auf Port 9021.", "", false,
             "portwright", false},
        };
        std::size_t seed = 3;
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
            if (!s.installed_version.empty())
                s.installed_file = s.filename;
            s.update = i.update;
            s.owner = i.owner;
            s.avatar = i.avatar;
            if (s.avatar)
                covers().deliver(avatar_key(s.owner), invented_cover(seed++));
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
            {"WARN", "fan_threshold_overridden",
             "Die Konsole hat die Lüftereinstellung auf 91 °C gesetzt. Zurückgestellt auf 80 °C."},
            {"INFO", "fan_threshold_applied",
             "An die Konsole übergeben: 78 °C – Durchschnitt 71,4 °C, gewünschte Drehzahl 46 %."},
            {"ERROR", "paystore_fetch_failed",
             "Die Liste der Payloads ließ sich nicht laden (Zeitüberschreitung)."},
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
            "[SceShellUI] I/PSM.UI : SceneQ : Loaded[0.00msec] : SplashScreen.FAKE86921 : "
            "SplashScreen",
            "[SceLncService] launchApp(PPSA02433)",
            "[AvControl] Waiting for the pstate event : done",
            "[Syscore Umd] Set memory pstate (hasRunningGame=1, hasRunningMiniApp=0, "
            "isSplashDisplayed=0)",
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
    float scan_left_ = 0.0f;
    std::deque<Notice> notices_;
    float clock_ = 0.0f;
    float second_ = 0.0f;
    int pending_target_ = 0;
    float target_at_ = 0.0f;
    std::string closing_;
    float close_at_ = 0.0f;
    float eject_at_ = 0.0f;
    std::map<std::string, std::vector<FsEntry>> fs_tree_;
    std::uint64_t fs_seq_ = 0;
    std::int64_t klog_n_ = 51400;
    std::uint64_t plan_ticket_ = 0;
    Tool plan_tool_ = Tool::copy;
    std::string plan_id_;
    float plan_at_ = 0.0f;
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
    // Civil date from days since 1970 (Howard Hinnant's algorithm): no time zone database on the
    // console.
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
