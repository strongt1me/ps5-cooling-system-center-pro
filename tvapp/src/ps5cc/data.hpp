// ps5cc - What the console app reports, and where it comes from.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::ps5cc
{

// Whether the app on the console (the payload with its web server on port
// 8086) answers. The tile starts it when it does not.
enum class Link : std::uint8_t
{
    starting, // not answering yet: the payload was sent, waiting for it
    online,
    failed, // did not answer within the time allowed
};

struct Status
{
    bool valid = false;
    int cpu_c = -1;
    int soc_c = -1;
    int gpu_c = -1; // -1 when the console has no reading (only the Pro has one)
    int hottest_c = -1;
    float fan_pct = -1.0f; // one decimal; negative without a reading
    bool automatic = true;
    bool safety = false;
    int guard = 0; // 0 idle, 1 over-temperature, 2 regulation silent
    bool night = false;
    int target_c = 0; // the target in force (a game's rule or the night target included)
    std::string game_state;
    std::string game_name;
    std::string game_id;
    bool game_foreground = false;
};

struct Game
{
    std::string id;
    std::string name;
    std::string platform;
    std::string version;
    std::string format;
    std::string path;
    std::string cover;       // where the app serves its cover ("/api/v1/library/cover?id=…"), "" when none
    std::string last_played; // ISO date as the app sends it, empty when never
    std::uint64_t size_bytes = 0;
    std::uint64_t played_s = 0;
    bool can_launch = true;
};

enum class JobKind : std::uint8_t
{
    copy,
    convert,
    image,
    count,
};

struct Job
{
    bool active = false;   // running now
    bool finished = false; // ended in this session: done, failed or cancelled
    std::string state;     // as the app says it ("scanning", "running", "done", ...)
    std::string name;      // the game
    std::string detail;    // where to, or what is being done
    double done = 0.0;
    double total = 0.0; // 0: unknown
    std::string error;
};

struct HistoryEntry
{
    std::int64_t t = 0; // seconds since 1970
    std::string kind;
    std::string title;
    std::string detail;
    std::string result; // done, failed, cancelled
    std::uint64_t bytes = 0;
};

// Something an action came back with, for the screen to show once.
struct Notice
{
    enum class Kind : std::uint8_t
    {
        info,
        success,
        error,
        other_running, // launch refused: another game runs (running_* / target_*)
        leaving,       // launch booked for after this app has ended: the screen ends the app
    };
    Kind kind = Kind::info;
    std::string title;
    std::string body;
    std::string running_id, running_name, target_id, target_name;
};

// Everything the screen shows, copied out of the source in one piece.
struct Snapshot
{
    Link link = Link::starting;
    std::string link_detail; // why it failed, or what it is waiting for
    Status status;
    std::vector<Game> games;
    bool games_loaded = false;
    std::array<Job, static_cast<std::size_t>(JobKind::count)> jobs{};
    std::vector<HistoryEntry> history;
    std::uint64_t serial = 0; // grows whenever anything above changes
};

// Where the data comes from: the console app over HTTP on the PS5, invented
// data on the PC (previews, tests). Every call returns at once; the work
// happens elsewhere and shows up in the next snapshot.
class Source
{
  public:
    virtual ~Source() = default;
    // Copies the data when it changed since `seen` (and updates it).
    virtual bool fetch(Snapshot &out, std::uint64_t &seen) = 0;
    // One notice per call, oldest first.
    virtual bool poll_notice(Notice &out) = 0;
    // Advances invented data; the HTTP source runs on its own thread.
    virtual void tick(float dt)
    {
        (void)dt;
    }
    virtual void set_target(int celsius) = 0;
    virtual void launch(const std::string &id) = 0;
    virtual void close_game(const std::string &id) = 0;
    virtual void cancel_job(JobKind kind) = 0;
    virtual void open_web_ui() = 0;
    virtual void reload() = 0;
    virtual void retry() = 0;
    // The jobs page is open: ask for jobs every second instead of rarely.
    virtual void watch_jobs(bool watching) = 0;
    // Ends the app on the console (the preview only says so).
    virtual void quit_app() = 0;
    // Preview only: steps the link through starting / failed / online so
    // the tour can photograph those screens.
    virtual void debug_cycle_link()
    {
    }
};

// The source the screen uses. The PS5 entry point installs the HTTP source;
// without one the invented data is used (PC preview, unit tests).
Source &source();
void set_source(Source *source);

// "47,8 GB", "512 MB" (German decimal comma).
std::string format_bytes(std::uint64_t bytes);
// "2,9 h", "35 min", "–" for nothing.
std::string format_playtime(std::uint64_t seconds);
// "46,3" for 46.3.
std::string format_decimal(float value, int decimals);

} // namespace hui::ps5cc
