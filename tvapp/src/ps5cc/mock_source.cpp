// ps5cc - Invented data for the PC preview and the unit tests, and the source registry.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/covers.hpp"
#include "ps5cc/data.hpp"
#include "ps5cc/plans.hpp"
#include "third_party/cjson/cJSON.h"

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
            const std::string format = m.format;
            const bool folder = format == "folder";
            const bool installed = format == "fpkg";
            g.path = installed ? std::string("/user/app/") + m.id
                               : std::string("/mnt/usb0/homebrew/") + m.id + (folder ? "" : "." + format);
            // The library's rules (library.c): copy a folder or an image file, convert a folder or an uncompressed
            // image, move and unpack through ShadowMountPlus.
            g.can_copy = !installed;
            g.can_convert = folder || format == "exfat" || format == "ffpkg" || format == "ffpfs";
            g.can_move = !installed;
            g.can_unpack = !installed && !folder;
            g.ampr_emu = std::string(m.id) == "PPSA01325";
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
        // A move by ShadowMountPlus that crawls through the whole preview; the app's own kinds stay free for the tools.
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
        fill_backdrops();
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
                // The move of the opening screen crawls; the jobs started from the tools finish within the preview.
                const bool crawl = i == static_cast<std::size_t>(JobKind::storage) && job.name == "Maneater";
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

    std::uint64_t request_plan(Tool tool, const std::string &id) override
    {
        plan_ticket_ += 1;
        plan_tool_ = tool;
        plan_id_ = id;
        // As long as a small game takes to measure on the console, so the "wird vermessen" state shows too.
        plan_at_ = clock_ + 0.6f;
        return plan_ticket_;
    }

    void start_tool(Tool tool, const std::string &id, const std::string &body) override
    {
        (void)body;
        const Game *game = find(id);
        const std::string name = game != nullptr ? game->name : id;
        const JobKind kind = tool == Tool::copy      ? JobKind::copy
                             : tool == Tool::convert ? JobKind::convert
                             : tool == Tool::move || tool == Tool::unpack ? JobKind::storage
                             : tool == Tool::erase                        ? JobKind::deletion
                                                                          : JobKind::image;
        Job &job = data_.jobs[static_cast<std::size_t>(kind)];
        if (job.active)
        {
            // What the console app answers with 409 while one of its kind runs.
            notice(Notice::Kind::error, "Nicht gestartet", "Es läuft schon ein Auftrag dieser Art. Bitte danach noch einmal.");
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

    void set_backdrop(const std::string &area, BackdropMode mode) override
    {
        for (BackdropSetting &b : data_.backdrops)
        {
            if (b.id != area)
                continue;
            if (mode == BackdropMode::custom && !b.custom)
            {
                notice(Notice::Kind::error, "Kein eigenes Bild",
                       "Lade es zuerst in der Browser-App hoch: System → Hintergründe der Vollbild-App.");
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
    // One of each case the dialogs know: too little room, the source's own drive, a good one, FAT32.
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
        cJSON_AddStringToObject(root, tool == Tool::erase || tool >= Tool::image_unpack ? "id" : "title_id",
                                g->id.c_str());
        cJSON_AddStringToObject(root, "name", g->name.c_str());
        cJSON_AddBoolToObject(root, "running", running);
        const auto job_active = [this](JobKind kind) { return data_.jobs[static_cast<std::size_t>(kind)].active; };
        const auto drive_base = [&](cJSON *d, const MockDrive &m)
        {
            cJSON_AddStringToObject(d, "mount", m.mount);
            cJSON_AddStringToObject(d, "label", m.label);
            cJSON_AddStringToObject(d, "base", m.mount);
            cJSON_AddStringToObject(d, "fs", m.fs);
            cJSON_AddNumberToObject(d, "free_bytes", m.free_gb * 1e9);
            cJSON_AddBoolToObject(d, "source_here", g->path.rfind(std::string(m.mount) + "/", 0) == 0);
        };
        const auto fat32 = [](const MockDrive &m, double file) { return std::string(m.fs) == "msdosfs" && file > 4.29e9; };
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
            // The preview starts every kind beside the others; the console app keeps copy and convert apart.
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
                    const std::string path = std::string(m.mount) + (mode == 0 ? "/homebrew/" : "/PS5-Sicherung/Spiele/") + leaf;
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
                // A PS4 title from the console's own storage brings it down: the app lists only USB and M.2.
                if (ps4 && std::string(m.mount) == "/data")
                    continue;
                cJSON *d = cJSON_CreateObject();
                drive_base(d, m);
                cJSON_AddBoolToObject(d, "fat32_too_big", fat32(m, raw));
                cJSON_AddBoolToObject(d, "fat32_too_big_ffpkg", fat32(m, ffpkg > 0.0 ? ffpkg : raw));
                cJSON_AddBoolToObject(d, "enough_space", m.free_gb * 1e9 >= raw);
                for (int mode = 0; mode < kModes; ++mode)
                {
                    cJSON *o = cJSON_AddObjectToObject(d, kModeKeys[mode]);
                    const std::string dir = std::string(m.mount) + (mode == 0 ? "/homebrew" : "/PS5-Sicherung/Spiele");
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
            cJSON_AddStringToObject(root, "image_type", g->format == "ffpfsc" ? "pfsc" : g->format.c_str());
            cJSON_AddNumberToObject(root, "size_bytes", size);
            cJSON *dests = cJSON_AddArrayToObject(root, "destinations");
            for (const MockDrive &m : kDrives)
            {
                cJSON *d = cJSON_CreateObject();
                const std::string path = std::string(m.mount) + "/homebrew";
                const std::string target = path + "/" + (unpack ? g->id : g->path.substr(slash + 1));
                cJSON_AddStringToObject(d, "path", path.c_str());
                cJSON_AddStringToObject(d, "drive", m.label);
                cJSON_AddStringToObject(d, "fs", m.fs);
                cJSON_AddNumberToObject(d, "free_bytes", m.free_gb * 1e9);
                cJSON_AddBoolToObject(d, "exists", 1);
                cJSON_AddBoolToObject(d, "here", path == here);
                cJSON_AddBoolToObject(d, "enough_space", path == here && !unpack ? 1 : m.free_gb * 1e9 >= size);
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
            const bool installed = g->format == "fpkg";
            cJSON_AddStringToObject(root, "kind", installed ? "installed" : "smp");
            cJSON_AddStringToObject(root, "path", g->path.c_str());
            cJSON_AddStringToObject(root, "format", g->format.c_str());
            cJSON_AddStringToObject(root, "version", g->version.c_str());
            cJSON_AddNumberToObject(root, "size_bytes", size);
            cJSON *items = cJSON_AddArrayToObject(root, "items");
            const std::string what = (folder ? "Der Spielordner " : "Die Abbilddatei ") + g->path;
            cJSON_AddItemToArray(items, cJSON_CreateString(installed ? "Das installierte Spiel (über die Deinstallation "
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
        s.soc_c = static_cast<int>(std::lround((playing ? 64.0 : 47.0) + 1.2 * std::sin(t * 0.17 + 1.0)));
        s.gpu_c = static_cast<int>(std::lround((playing ? 66.0 : 46.0) + 1.4 * std::sin(t * 0.13 + 2.0)));
        s.hottest_c = std::max(s.cpu_c, std::max(s.soc_c, s.gpu_c));
        s.fan_pct = static_cast<float>(std::round(((playing ? 46.3 : 20.4) + 2.2 * std::sin(t * 0.09)) * 10.0) / 10.0);
        s.automatic = true;
    }

    // Every area on the tile's own picture; Spiele has one of the user's as well (set aside).
    void fill_backdrops()
    {
        static const char *const kLabels[kBackdropAreas] = {"Kühlung", "Spiele", "Dateien", "Payloads",
                                                            "System",  "Profil", "Protokoll"};
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
            const char *owner; // invented accounts: a picture for some, none for others
            bool avatar;
        };
        static constexpr Item kItems[] = {
            {"shadowmountplus", "ShadowMountPlus", "1.7beta7", "Utilities & Tools",
             "Ein automatischer Mounter für Spiele-Abbilder im Hintergrund.", "1.7beta7", false, "mountsmith", true},
            {"pldmgr", "pldmgr", "v0.6.4", "Utilities & Tools", "Payloads verwalten und automatisch laden.", "v0.6.3",
             true, "loadkeeper", true},
            {"ps5upload", "ps5upload", "v8.4.0", "Networking & Servers",
             "Schnelle Übertragung vom Rechner auf die PS5 (braucht die PC-App).", "", false, "uplinker", true},
            {"ftpsrv", "ftpsrv", "v0.15", "Networking & Servers", "FTP-Server auf Port 2121.", "", false, "portwright",
             false},
            {"kstuff", "kstuff", "v1.6.7", "System & Jailbreak", "Kernel-Hilfen für den Jailbreak.", "", false, "", false},
            {"elfldr", "elfldr", "v0.21", "Loaders", "Der ELF-Loader auf Port 9021.", "", false, "portwright", false},
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
            "[SceShellUI] I/PSM.UI : SceneQ : Loaded[0.00msec] : SplashScreen.FAKE86921 : SplashScreen",
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
