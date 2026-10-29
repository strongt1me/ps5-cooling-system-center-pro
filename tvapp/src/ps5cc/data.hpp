// ps5cc - What the console app reports, and where it comes from.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ps5cc/backdrops.hpp"

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
    // What the game tools may do with it (GET /api/v1/library): copy and convert are the console app's own, move
    // and unpack go through ShadowMountPlus.
    bool can_copy = false;
    bool can_convert = false;
    bool can_move = false;
    bool can_unpack = false;
    bool ampr_emu = false; // mods.ampr_emu: the conversion builds the AMPR index first by default
};

enum class JobKind : std::uint8_t
{
    copy,
    convert,
    image,
    storage,  // a move or unpack by ShadowMountPlus
    deletion, // a game being deleted
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
    bool cancellable = true; // a delete cannot be stopped; ShadowMountPlus says whether its job can
};

// The game tools of Spiele → Bibliothek, in the order of their menu.
enum class Tool : std::uint8_t
{
    copy,
    convert,
    move,         // ShadowMountPlus
    unpack,       // ShadowMountPlus: an image into a dump folder
    image_unpack, // the console app's own reader: an image into a folder
    image_check,
    image_repair,
    erase,
    count,
};

// The answer to a plan request, as it came: parsed by ps5cc/plans.hpp.
struct PlanReply
{
    std::uint64_t ticket = 0; // the number request_plan() returned
    Tool tool = Tool::copy;
    std::string id;
    bool ok = false;
    std::string error; // the app's message when ok is false
    std::string json;
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

// The cooling settings of GET /api/v1/config that the Settings page edits.
struct Config
{
    bool valid = false;
    std::string mode = "automatic"; // "automatic" or "observe"
    std::string profile = "comfort"; // "comfort" (Leise), "balanced", "cool"
    int target_min_c = 50;
    int target_max_c = 91;
    int safety_temp_c = 78;
    int safety_min_c = 72;
    int safety_max_c = 95;
    bool night_enabled = false;
    int night_start_min = 23 * 60;
    int night_end_min = 7 * 60;
    int night_target_c = 75;
    int tz_offset_min = 0; // minutes east of UTC, as the web page last told the app
};

// GET /api/v1/fan/selftest
struct SelfTest
{
    std::string state = "idle";  // idle, requested, running, finished
    std::string verdict = "none"; // none, responds, no_response, aborted, not_possible
    std::string why;
    int baseline_pct = 0;
    int peak_pct = 0;
    int rise_pct = 0;
    int threshold_c = 0;
    int hottest_c = 0;
    int elapsed_s = 0;
    int limit_s = 0;
    std::vector<int> samples; // fan speed, one per second from the start
};

// One .elf the app can send to the loader (GET /api/v1/payload-files).
struct PayloadFile
{
    std::string source; // "internal" or "usb"
    std::string mount;  // usb: "/mnt/usb0"
    std::string dir;    // "" or "payloads"
    std::string name;
    std::string where; // "Intern", "USB-Speicher 1"
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    bool valid = true;
    std::string why; // why it is not valid
};

// GET /api/v1/payloads
struct RunningPayload
{
    int pid = 0;
    std::string name;
    int memory_mb = 0;
    bool self = false;
};

// GET /api/v1/paystore
struct StoreItem
{
    std::string id;
    std::string name;
    std::string filename;
    std::string version;
    std::string category;
    std::string description;
    std::string installed_version; // "" when not downloaded
    bool update = false;           // a newer version than the one downloaded
    bool name_taken = false;       // a file of that name exists that the store did not load
    std::string owner;             // GitHub account of the source, "" when it is not on GitHub
    bool avatar = false;           // the console app has that account's picture
};

// The texture key of a developer picture in covers() (games use their title id).
inline std::string avatar_key(const std::string &owner)
{
    return "avatar:" + owner;
}

struct StoreJob
{
    std::string state = "idle"; // idle, running, done, failed, cancelled
    std::string id;
    std::string name;
    std::string error;
    double done = 0.0;
    double total = 0.0;
};

// GET /api/v1/logs/tail
struct LogEntry
{
    std::int64_t t_ms = 0;
    std::string level; // INFO, WARN, ERROR
    std::string code;
    std::string message;
};

// GET /api/v1/klog
struct KlogLine
{
    std::int64_t n = 0; // the line's number; never given out twice
    std::int64_t t_ms = 0;
    std::string text;
};

// Which page is open, so the HTTP source asks for its data often (and for nothing else's).
enum class Watch : std::uint8_t
{
    none,
    jobs,
    settings,
    payloads,
    logs,
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
        job_started,   // a game tool's job was accepted: `job` names its card
    };
    Kind kind = Kind::info;
    std::string title;
    std::string body;
    std::string running_id, running_name, target_id, target_name;
    JobKind job = JobKind::count;
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
    Config config;
    SelfTest selftest;
    std::vector<PayloadFile> payload_files;
    bool payload_files_loaded = false;
    std::vector<RunningPayload> running;
    std::vector<StoreItem> store;
    bool store_loaded = false;
    bool store_fetching = false;
    std::string store_error;
    StoreJob store_job;
    std::vector<LogEntry> log; // oldest first
    std::vector<KlogLine> klog; // oldest first, at most kKlogKeep
    bool klog_lost = false;     // lines went by faster than they were fetched
    PlanReply plan;             // the last plan that came back
    std::vector<BackdropSetting> backdrops; // GET /api/v1/backdrops, in the sidebar's order
    bool backdrops_loaded = false;
    std::uint64_t serial = 0;   // grows whenever anything above changes
};

constexpr std::size_t kKlogKeep = 1000;

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
    // The page that is open: its data is asked for often (jobs every second, the logs while they are read).
    virtual void watch(Watch page) = 0;
    // PUT /api/v1/config with these fields only ({"profile":"cool"}).
    virtual void save_config(const std::string &patch) = 0;
    virtual void selftest(bool start) = 0;
    // POST /api/v1/power: "standby", "reboot", "off" or "safemode".
    virtual void power(const std::string &action) = 0;
    virtual void start_payload(const PayloadFile &file) = 0;
    virtual void kill_payload(int pid) = 0;
    // POST /api/v1/paystore: "refresh", "install", "start", "cancel" (id where it needs one).
    virtual void store_action(const std::string &action, const std::string &id) = 0;
    // A game tool's plan for one game; it arrives as Snapshot::plan with the ticket returned here. Plans can take a
    // minute (the console app measures the whole game), so they never wait behind the status.
    virtual std::uint64_t request_plan(Tool tool, const std::string &id) = 0;
    // Starts the tool with this POST body (ps5cc/plans.hpp builds it). Accepted: a job_started notice; refused: an
    // error notice with the app's message.
    virtual void start_tool(Tool tool, const std::string &id, const std::string &body) = 0;
    // POST /api/v1/backdrops: which picture an area shows. "custom" needs the user's picture (uploaded in the browser).
    virtual void set_backdrop(const std::string &area, BackdropMode mode) = 0;
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
// "23:00" for 1380 minutes after midnight (wraps at a day).
std::string format_clock(int minutes);
// "14:03:07" for a time in ms since 1970, shifted by tz_offset_min.
std::string format_time_of_day(std::int64_t ms, int tz_offset_min);
// "27.10.2026" for seconds since 1970.
std::string format_date(std::int64_t seconds);
// Appends the lines newer than the last one in `ring`, keeps at most `keep`; true when anything was added.
bool klog_append(std::vector<KlogLine> &ring, const std::vector<KlogLine> &fresh, std::size_t keep);

} // namespace hui::ps5cc
