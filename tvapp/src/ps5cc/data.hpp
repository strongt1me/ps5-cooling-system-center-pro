// ps5cc - What the console app reports, and where it comes from.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ps5cc/backdrops.hpp"

#include <array>
#include <map>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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
    std::string
        cover; // where the app serves its cover ("/api/v1/library/cover?id=…"), "" when none
    std::string last_played; // ISO date as the app sends it, empty when never
    std::uint64_t size_bytes = 0;
    std::uint64_t played_s = 0;
    bool can_launch = true;
    // What the game tools may do with it (GET /api/v1/library): copy and convert are the console
    // app's own, move and unpack go through ShadowMountPlus.
    bool can_copy = false;
    bool can_convert = false;
    bool can_move = false;
    bool can_unpack = false;
    bool can_remount = false; // ShadowMountPlus can take it out and put it back (1.7beta10)
    bool smp_mounted = false; // ShadowMountPlus has it mounted now
    bool ampr_emu = false; // mods.ampr_emu: the conversion builds the AMPR index first by default
    std::string disc; // installed from disc: "in" (the disc is in the drive) or "out"; "" otherwise
    bool dump_mounted = false; // the game runs (mounted), so it can be dumped
    std::string dump;          // a dump of it lies on a drive: "done", "unfinished", ""
};

// The app's two tiles (and their ids before 1.74.1, src/titleid.h): titles of their own, never
// games to list or start. Both sources skip them, so the PC preview shows what the console shows.
inline bool is_own_tile(const std::string &id)
{
    return id == "FAKE86921" || id == "FAKE86922" || id == "PPSA86921" || id == "PPSA86922";
}

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
    std::string mode = "automatic";  // "automatic" or "observe"
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
    int tz_offset_min = 0;         // minutes east of UTC: the console's zone, else the web page's
    int target_temp_c = 68;        // what a game profile remembers
    int warning_cpu_c = 85;        // 40..110; the chip's own limit is 5 °C more (config.c)
    int warning_countdown_s = 120; // 30..900
    bool lightbar_enabled = false;
    int lightbar_warn_c = 70; // 55..90: yellow from here
    int lightbar_hot_c = 80;  // 56..95: red from here, always above yellow
    // The fullscreen app's language: "" follows the console, else de, en, es, fr, it, ru.
    std::string tv_language;
};

// GET /api/v1/games: a cooling profile kept for one game (Phase 8).
struct GameRule
{
    std::string title_id;
    std::string title_name;
    int target_c = 68;
    std::string profile = "comfort"; // comfort, balanced, cool
};

// GET /api/v1/fan/selftest
struct SelfTest
{
    std::string state = "idle";   // idle, requested, running, finished
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

// GET /api/v1/payload-profiles: a named sequence of payload files with pauses ("!2000" = 2 s).
struct PayloadProfile
{
    std::string id; // a-z0-9-_
    std::string name;
    std::vector<std::string> items;
};

// GET /api/v1/payload-profiles/status
struct ProfileRunResult
{
    std::string item;
    bool ok = false;
    std::string message;
};
struct ProfileRun
{
    bool running = false;
    std::string id, name;
    int step = 0, total = 0;
    std::vector<ProfileRunResult> results;
};

// GET /api/v1/payloads
struct RunningPayload
{
    int pid = 0;
    std::string name;
    int memory_mb = 0;
    bool self = false;
    std::string file; // the file it was started from, when the console app started it ("" unknown)
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
    std::string installed_file;    // the downloaded file's name
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

// One package of the last search (GET /api/v1/packages).
struct Package
{
    std::string id; // valid for the last search only
    std::string name;
    std::string file;
    std::string drive;       // the drive's mount ("/mnt/usb0", "/user")
    std::string drive_label; // "USB-Speicher 1", from the list's drives
    std::string title_id;
    std::string content_id;
    std::string version;
    std::string kind; // base, update, dlc
    int plat = 0;     // 5 PS5, 4 PS4, 0 unknown
    std::uint64_t size = 0;
    std::uint64_t total = 0; // a split package: all of it (size: the parts found)
    int parts_total = 0;     // 0 for a single file
    int parts_found = 0;
    bool has_icon = false;
};

// The texture key of a package's picture in covers().
inline std::string package_key(const std::string &id)
{
    return "pkg:" + id;
}

struct PackageScan
{
    bool loaded = false;
    bool ever = false; // searched at least once since the app started
    bool scanning = false;
    int parsed = 0;
    std::string current; // the file being read
};

// GET /api/v1/packages: a drive a package can be split onto.
struct PackageDrive
{
    std::string mount;
    std::string label;
    std::uint64_t free_bytes = 0;
};

// GET /api/v1/packages/job: splitting a package into parts (pkgsplit.c).
struct PackageSplit
{
    std::string state = "idle"; // idle, splitting, verifying, done, failed, cancelled
    bool active = false;
    bool can_cancel = false;
    std::string phase;
    std::string name;
    int parts = 0;
    int part = 0;
    int percent = 0;
    std::string error;
    std::string note;
};

// GET /api/v1/packages/install/job
struct PackageInstall
{
    std::string state =
        "idle"; // idle, preparing, starting, installing, finishing, done, failed, cancelled
    bool active = false;
    bool can_cancel = false;
    bool cancelling = false;
    std::string name;
    std::string version;
    std::string error;
    std::string note;
    double bytes_total = 0.0;
    double bytes_done = 0.0;
    int percent = -1;  // -1 unknown
    int remain_s = -1; // -1 unknown
};

struct PackageQueueItem
{
    std::string id;
    std::string name;
    std::string version;
    std::string kind;
    std::string state; // waiting, running, done, failed, skipped, cancelled
    std::string message;
    std::uint64_t size = 0;
};

// GET /api/v1/packages/queue
struct PackageQueue
{
    bool running = false;
    bool pause_requested = false;
    bool halted = false;
    std::string halt_reason;
    int waiting = 0;
    int done = 0;
    int failed = 0; // failed, skipped and cancelled
    std::vector<PackageQueueItem> items;
};

// One game of PS4Mount (GET /api/v1/ps4mount).
struct Ps4Game
{
    std::string title_id;
    std::string name;
    std::string version;
    std::string state; // ready, running, pending, removed, internal, duplicate, addons
    std::string mount;
    std::string update_issue;
    bool has_patch = false;
    std::uint64_t size = 0;
    int dlc = 0;       // add-ons in use
    int dlc_total = 0; // add-ons found
    int updates = 0;   // updates found
};

// The texture key of a PS4Mount game's picture in covers().
inline std::string ps4_key(const std::string &title_id)
{
    return "p4m:" + title_id;
}

struct Ps4MountConfig
{
    bool valid =
        false; // a busy PS4Mount answers without its settings: never save defaults over them
    bool batch = true;
    bool dlc = true;
    bool dlc_copy = false;
    bool fast_reg = false;
    int batch_size = 5;
    int restore_batch = 5;
    int max_restore_batch = 10;
    std::vector<std::string> paths;
};

// ---- Phase 4 (29.10.2026): Spielzeit, Spielstände, Sicherungen, as the web app's tabs of the same
// names ----

// One game session (GET /api/v1/playtime, "sessions"); -1 = not measured.
struct PlaySession
{
    std::string id;
    std::string name;       // from "titles", or the id
    std::int64_t start = 0; // s since 1970
    std::int64_t end = 0;
    int play = 0; // seconds in front
    int cpu_max = -1;
    int soc_max = -1;
    int fan_avg = -1;
    int flags = 0; // 1 safety mode, 2 warning, 4 end estimated
};

struct Playtime
{
    bool loaded = false;
    bool enabled = true;
    std::int64_t now = 0;              // the console's clock, s
    int total = 0;                     // sessions ever recorded
    int kept = 0;                      // how many are kept
    bool live = false;                 // a game runs now
    PlaySession current;               // its session so far
    std::string live_state;            // front, back, gone
    std::vector<PlaySession> sessions; // oldest first
};

// A save folder of one game of one user (GET /api/v1/saves).
struct SaveTitle
{
    std::string uid;
    std::string id;
    std::string name;
    std::string platform; // PS5, PS4
    bool installed = true;
    std::uint64_t bytes = 0;
    std::int64_t mtime = 0;
};

struct SaveUser
{
    std::string uid;
    std::string name;
    std::uint64_t bytes = 0;
    std::vector<SaveTitle> titles;
};

struct SaveDrive
{
    std::string mount;
    std::string label;
    std::uint64_t free_bytes = 0;
};

struct SaveBackup
{
    std::int64_t created = 0;
    std::string path;
    std::string label; // the drive
    std::string kind;  // backup, undo (the state before a restore)
    std::uint64_t bytes = 0;
    bool this_console = true;
    std::vector<std::string> user_names;
    std::vector<SaveTitle> titles;
};

struct SaveJob
{
    bool active = false;
    bool can_cancel = false;
    std::string state; // idle scanning copying verifying applying done failed cancelled
    std::string kind;  // backup verify restore delete
    std::string phase;
    int percent = 0;
    int files_done = 0;
    int files_total = 0;
    std::string current;
    std::string error;
    std::string note;
    std::string path;
    std::string undo;
    int ok_files = 0;
    int elapsed_s = 0;
    int finished_ago_s = -1;
};

struct Saves
{
    bool loaded = false;
    std::string error;
    bool game_running = false;
    std::string game_id;
    int unfinished = 0;
    std::vector<SaveUser> users;
    std::vector<SaveDrive> drives;
    std::vector<SaveBackup> backups; // newest first
    SaveJob job;
};

// A game backup of the app (GET /api/v1/library/backups, folder PS5-Sicherung/Spiele).
struct GameBackup
{
    std::string path;
    std::string name;
    std::string drive;
    std::string type;
    std::uint64_t size_bytes = 0;
    std::int64_t modified = 0;
    bool unfinished = false;
    bool sums = false;
};

struct GameBackups
{
    bool loaded = false;
    std::string error;
    std::vector<GameBackup> backups;
};

// ---- Phase 6 (29.10.2026): the game extras of the web app's per-game window (AMPR index, APR-EMU,
// PlayGo, plugins, LZ4 packs, recording), the library's duplicate images and cover cache, the
// kernel-log recording. The answers are kept as the console sent them (JSON) and read by the dialog
// itself: there are many fields, and most are shown once.

struct ExtrasState
{
    std::string id;        // the game the window is open for
    std::uint64_t seq = 0; // grows with every answer
    std::string ampr_job, image_job, ampr, image, emu_ampr, emu_playgo, lz4, trace, plugins;
};

struct ExtrasVersions
{
    std::uint64_t seq = 0;
    std::string lib;  // ampr, playgo
    std::string json; // GET /api/v1/ampr-emu or /api/v1/playgo
};

struct ExtrasLz4Plan
{
    std::uint64_t seq = 0;
    std::string json; // GET /api/v1/library/lz4/plan
};

// The System area (Phase 7, 30.10.2026): the console app's answers as they come, read by the page
// with cJSON. Only fetched while System is open.
struct SystemData
{
    std::uint64_t seq = 0;
    std::string system;   // GET /api/v1/system
    std::string status;   // GET /api/v1/status (the same answer the dashboard reads)
    std::string health;   // GET /api/v1/cooling-health
    std::string config;   // GET /api/v1/config (probe_mask, probe_revert_*)
    std::string channels; // GET /api/v1/channels
    std::string diag;     // GET /api/v1/controller/diag
    std::string drives;   // GET /api/v1/drives, "" while the drive probe is off
    std::string qr;       // GET /api/v1/qr: url and svg
    std::string risky;    // GET /api/v1/sensors/risky, on "Jetzt messen"
};

struct LibraryTools
{
    std::uint64_t seq = 0;
    std::string duplicates; // GET /api/v1/library/duplicates
    std::string cache;      // GET /api/v1/library/cache
};

// GET /api/v1/klog/files: what lies in the console's kernel-log folder.
struct KlogFile
{
    std::string name;
    std::string kind; // "rec" (Aufnahme) or "save" (Auszug)
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    bool active = false; // the recording that runs
};

struct KlogRecord
{
    bool loaded = false;
    bool recording = false;
    std::string file;
    std::string reason;
    std::uint64_t bytes = 0;
    std::uint64_t max_bytes = 0;
    int lines = 0;
    int elapsed_s = 0;
};

// ---- Phase 5 (29.10.2026): Dateien, as the web app's file manager (src/filemgr.c,
// /api/v1/files/*) ---- Changes only below the drives and /data (the console says so with
// writable); looking is allowed everywhere.

struct FsPlace
{
    std::string label;
    std::string path;
    std::int64_t free_bytes = -1;
    bool writable = false;
};

struct FsEntry
{
    std::string name;
    std::string type; // dir, file, dirlink, link, other
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    bool writable = false;
};

struct FsList
{
    bool loaded = false;
    std::uint64_t seq = 0; // grows with every answer
    std::string path;      // "" = the places
    std::string parent;
    std::string error;
    bool writable = false;
    std::int64_t free_bytes = -1;
    int total = 0;
    bool truncated = false;
    std::vector<FsEntry> entries;
};

struct FsJob
{
    bool active = false;
    std::string state; // idle counting copying deleting done failed cancelled
    std::string kind;  // copy move delete
    std::string dest;
    std::string current;
    std::string error;
    std::string note;
    int percent = 0;
    std::uint64_t bytes_total = 0;
    std::uint64_t bytes_done = 0;
    int files_total = 0;
    int files_done = 0;
    int finished_ago_s = -1;
    std::uint64_t rate_bps = 0; // bytes per second of a copy or move, 0 when not known yet
    int eta_s = 0;              // seconds left at that speed, 0 when not known
};

// Names the target of a copy or move already has (the app refused with them, 409 "exists"): the
// screen asks what to do (replace, keep both, skip) and the answer goes back through
// Source::files_conflict.
struct FsConflict
{
    std::uint64_t seq = 0; // goes up with every new question and every answer
    std::vector<std::string> names;
    bool move = false;
};

// A text file's beginning (GET /api/v1/files/view).
struct FsView
{
    std::uint64_t seq = 0;
    std::string path;
    std::string text;
    std::string error;
    bool truncated = false;
    std::uint64_t size = 0;
};

// A folder measured (GET /api/v1/files/size): at most 8 s of counting, partial = at least this
// much.
struct FsSize
{
    std::uint64_t bytes = 0;
    int files = 0;
    bool partial = false;
    bool failed = false;
};

// A drive of the console (GET /api/v1/system, "volumes"); USB drives can be ejected (29.10.2026).
struct Volume
{
    std::string label;
    std::string path;
    std::uint64_t total_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t free_bytes = 0;
    bool ejectable = false;
};

// The last eject through ShadowMountPlus (GET /api/v1/storage/eject): only while no job of the app
// runs.
struct UsbEject
{
    bool loaded = false;
    bool available = false; // ShadowMountPlus 1.7beta10 or newer runs
    bool running = false;
    std::string state; // "" never, running, done, failed
    std::string mount;
    std::string message;
};

// A pkg on a drive that the search leaves where it is, and why ("skipped" in
// /api/v1/ps4mount, 29.10.2026).
struct Ps4Skipped
{
    std::string path;
    std::string mount;    // "/mnt/usb0"
    std::string title_id; // empty when the header could not be read
    std::string name;
    std::string
        why; // internal, restored, not_ps4, not_fake, dlc_foreign, dlc_off, duplicate, second_game
};

struct Ps4Mount
{
    bool loaded = false;
    bool enabled = false;
    bool running = false;
    bool busy = false;
    std::string standalone; // why it waits: the stand-alone ps4mount.elf runs
    std::string version;
    Ps4MountConfig config;
    std::vector<Ps4Game> games;
    std::vector<Ps4Skipped> skipped;
};

struct Ps4Update
{
    std::string file;
    std::string version;
    std::string name;
    bool active = false;
};

struct Ps4Dlc
{
    std::string content_id;
    std::string name;
    bool on = false;
    bool data = false; // with data, or only an unlock
    std::string copy;  // "", missing, installing, installed, store, problem
    std::string reason;
};

// GET /api/v1/ps4mount/game?id=… : what one game can choose.
struct Ps4GameDetail
{
    std::string id;
    std::uint64_t seq =
        0; // new with every answer, so a dialog never takes an older one for its own
    bool loaded = false;
    std::string error;
    int dlc_limit = 29;
    std::vector<Ps4Update> updates;
    std::vector<Ps4Dlc> dlcs;
};

// The drive and its images (GET /api/v1/disc, disc.c).
struct DiscImage
{
    std::string name;
    std::string serial;
    std::string crc32;
    std::uint64_t size = 0;
    bool verified = false;
};

struct DiscState
{
    bool loaded = false;
    bool has_drive = true; // false: the console has no drive, the Disc tab is not shown
    bool enabled = false;
    bool auto_dump = false;
    std::string drive; // ok, none, unknown
    bool present = false;
    bool has_title = false;
    bool dumpable = false;
    std::string type; // "PS2-DVD", "PS5-Disc", ...
    std::string serial;
    std::string name;
    std::string title_state; // ok, generic, wait, none
    std::uint64_t image_bytes = 0;
    // the image being made
    std::string job_state = "idle"; // idle, running, verifying, done, failed, cancelled
    std::string job_name;
    std::string job_error;
    std::string job_note;
    double job_done = 0.0;
    double job_total = 0.0;
    int job_eta_s = 0;
    std::vector<std::string> events;
    std::vector<DiscImage> images;
    std::string dir;
    bool redump = false; // a usable Redump list lies on the console
};

struct DumpTarget
{
    std::string mount;
    std::string label;
    std::string fs;
    std::uint64_t free_bytes = 0;
    bool console = false;
};

// The dump settings of GET /api/v1/dump.
struct DumpSettings
{
    bool valid = false;
    std::string target;
    std::string subdir;
    bool folder_titles = true;
    bool dump_dlc = true;
    bool verify = true;
    bool decrypt = true;
    bool fself = true;
    bool backport = false;
    bool decrypt_supported = true;
    int ps4_split = 3; // 3 separate, 1 only the game, 2 only the update, 0 in one folder
    int ps4_backport_level = 4;
    int ps5_backport_level = 1;
};

struct DumpQueueItem
{
    std::string id;
    std::string name;
    std::string state; // pending, waiting_disc, launching, settling, dumping, done, failed, skipped
    std::string message;
    bool disc_out = false; // a disc game whose disc is not in the drive
    bool custom = false;   // settings of its own
    bool dumped = false;   // a finished dump of it lies on a drive
};

struct DumpQueue
{
    bool active = false;
    int current = -1;
    int delay = 30;
    int wait = 0;
    int max = 64;
    bool replace = true;
    bool interrupted = false;
    std::vector<DumpQueueItem> items;
};

struct DumpJob
{
    std::string state = "idle"; // idle, running, done, failed, cancelled
    bool active = false;
    bool verify = false;
    std::string name;
    std::string title_id;
    std::string stage;
    std::string kind; // game, dlc
    std::string drive;
    std::string dest;
    std::string folder;
    std::string error;
    std::string note;
    double done_bytes = 0.0;
    double total_bytes = 0.0;
    double checked_bytes = 0.0;
    double rate = 0.0;
    int eta_s = 0;
};

// GET /api/v1/dump
struct DumpState
{
    bool loaded = false;
    DumpSettings settings;
    std::vector<DumpTarget> targets;
    DumpQueue queue;
    DumpJob job;
};

// One dump of GET /api/v1/dumps: only those the Disc tab made.
struct DumpEntry
{
    std::string mount;
    std::string dir;
    std::string folder;
    std::string title_id;
    std::string title;
    std::string state; // done, running, failed, aborted, ""
    std::string kind;  // game, dlc, patch
    std::uint64_t bytes = 0;
    bool decrypt = false;
    bool fself = false;
    bool backport = false;
};

struct DumpList
{
    bool loaded = false;
    bool scanning = false;
    bool ever = false;
    std::vector<DumpEntry> dumps;
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
    packages,
    ps4mount,
    disc,
    storage,
    playtime,
    saves,
    backups,
    files,
    profile,
};

// GET /api/v1/system "user" and the console: the Profil page.
struct ProfileInfo
{
    bool loaded = false;
    bool valid = false; // a user is signed in
    std::string username;
    std::string uid_hex;
    std::string console_name;
    std::string model;
    int name_max = 16;
    bool can_rename = false;
};

// GET /api/v1/profile/account/status: the offline account and the fake sign-in of the signed-in
// user.
// One user of the console, as OffAct lists them (slots 1..16 with a name): the offline account is
// activated for one of them.
struct AccountSlot
{
    int slot = 0;
    std::string name;
    bool active = false; // account id and type "np" are set
};

struct AccountInfo
{
    bool loaded = false;
    bool activated = false;  // one of the users is activated as the offline account
    bool fakesignin = false; // auth.dat exists
    std::string username;    // the name of the first activated one
    std::vector<AccountSlot> slots;
};

// /avatars/index.json: a built-in profile picture (web/avatars, thumbnails in /avatars/t).
struct AvatarChoice
{
    std::string file;
    std::string label;
};

// A picture of one's own on the console (JPG or PNG in /data/PS5-Cooling-Center/Avatars, at the
// root of a USB drive or in its Avatars folder), offered as a profile picture.
struct OwnPicture
{
    std::string path; // the full path on the console
    std::string name;
    std::string where; // "usb0", "Konsole"
    std::uint64_t size = 0;
};

inline std::string own_picture_key(const std::string &path)
{
    return "ownpic:" + path;
}

// The texture keys of the Profil page's pictures in covers().
inline std::string avatar_choice_key(const std::string &file)
{
    return "avatarpick:" + file;
}
inline std::string credit_key(const std::string &file)
{
    return "credit:" + file;
}
inline std::string profile_picture_key(int revision)
{
    return "profilepic:" + std::to_string(revision);
}

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
    // The language the screen is in ("de" until a dictionary arrived) and its dictionary: the
    // browser's /lang/<xx>.json from the app, then the fullscreen app's own
    // /app0/assets/lang/<xx>.json. A new pointer means a new language; it stays the same while the
    // language does.
    std::string language = "de";
    std::shared_ptr<const std::vector<std::pair<std::string, std::string>>> dictionary;
    int console_language = -1; // locale.language of /api/v1/system; -1 until it was read
    Status status;
    std::vector<Game> games;
    bool games_loaded = false;
    std::array<Job, static_cast<std::size_t>(JobKind::count)> jobs{};
    std::vector<HistoryEntry> history;
    Config config;
    std::vector<GameRule> game_rules;
    std::string active_rule; // the title id whose profile is in force, "" for none
    bool rules_loaded = false;
    SelfTest selftest;
    std::vector<PayloadFile> payload_files;
    std::vector<PayloadProfile> payload_profiles;
    std::string profile_startup; // the id run a little after the app has started, "" for none
    bool profiles_loaded = false;
    ProfileRun profile_run;
    bool payload_files_loaded = false;
    std::vector<RunningPayload> running;
    std::vector<StoreItem> store;
    bool store_loaded = false;
    bool store_fetching = false;
    std::string store_error;
    StoreJob store_job;
    std::vector<LogEntry> log;  // oldest first
    std::vector<KlogLine> klog; // oldest first, at most kKlogKeep
    bool klog_lost = false;     // lines went by faster than they were fetched
    PlanReply plan;             // the last plan that came back
    std::vector<Package> packages;
    PackageScan package_scan;
    PackageInstall package_install;
    std::vector<PackageDrive> package_drives;
    PackageSplit package_split;
    PackageQueue package_queue;
    Ps4Mount ps4mount;
    Ps4GameDetail ps4_detail; // the game last asked for with ps4mount_fetch("game", id)
    std::string ps4_log;      // the end of PS4Mount's log, after ps4mount_fetch("log")
    std::uint64_t ps4_log_seq = 0;
    DiscState disc;
    DumpState dump;
    DumpList dumps;
    ExtrasState extras;
    ExtrasVersions extras_versions;
    ExtrasLz4Plan extras_lz4_plan;
    LibraryTools library_tools;
    SystemData system_data;
    KlogRecord klog_record;
    ProfileInfo profile;
    AccountInfo account;
    std::vector<AvatarChoice> avatars;
    std::vector<OwnPicture> own_pictures;
    bool own_pictures_loaded = false;
    int avatar_revision = 0; // goes up with every picture applied: a new texture key
    bool avatar_busy = false;
    std::vector<KlogFile> klog_files;
    bool klog_files_loaded = false;
    std::vector<FsPlace> fs_places;
    FsList fs_list;
    FsJob fs_job;
    FsConflict fs_conflict;
    FsView fs_view;
    std::map<std::string, FsSize> fs_sizes; // by path
    Playtime playtime;
    Saves saves;
    GameBackups game_backups;
    std::vector<Volume> volumes;
    bool volumes_loaded = false;
    UsbEject usb_eject;
    std::vector<BackdropSetting> backdrops; // GET /api/v1/backdrops, in the sidebar's order
    bool backdrops_loaded = false;
    std::uint64_t serial = 0; // grows whenever anything above changes
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
    // One game "unmount", "mount" or "remount" through ShadowMountPlus; the answer comes as a
    // notice.
    virtual void remount_game(const std::string &id, const std::string &action) = 0;
    virtual void cancel_job(JobKind kind) = 0;
    virtual void open_web_ui() = 0;
    virtual void reload() = 0;
    virtual void retry() = 0;
    // The page that is open: its data is asked for often (jobs every second, the logs while they
    // are read).
    virtual void watch(Watch page) = 0;
    // PUT /api/v1/config with these fields only ({"profile":"cool"}).
    virtual void save_config(const std::string &patch) = 0;
    // PUT /api/v1/games: keep the current setting for a game, or forget a game's profile.
    virtual void save_game_rule(const GameRule &rule) = 0;
    virtual void remove_game_rule(const std::string &title_id) = 0;
    virtual void selftest(bool start) = 0;
    // POST /api/v1/power: "standby", "reboot", "off" or "safemode".
    virtual void power(const std::string &action) = 0;
    virtual void start_payload(const PayloadFile &file) = 0;
    // POST /api/v1/payload-files/copy (a USB file into the internal folder, never overwriting) and
    // /delete (an internal file).
    virtual void copy_payload_file(const PayloadFile &file) = 0;
    virtual void delete_payload_file(const PayloadFile &file) = 0;
    // POST /api/v1/payload-profiles: the whole document; /run and /stop.
    virtual void save_payload_profiles(const std::vector<PayloadProfile> &profiles,
                                       const std::string &startup) = 0;
    virtual void run_payload_profile(const std::string &id) = 0;
    virtual void stop_payload_profile() = 0;
    virtual void kill_payload(int pid) = 0;
    // POST /api/v1/paystore: "refresh", "install", "start", "cancel" (id where it needs one).
    virtual void store_action(const std::string &action, const std::string &id) = 0;
    // A game tool's plan for one game; it arrives as Snapshot::plan with the ticket returned here.
    // Plans can take a minute (the console app measures the whole game), so they never wait behind
    // the status.
    virtual std::uint64_t request_plan(Tool tool, const std::string &id) = 0;
    // Starts the tool with this POST body (ps5cc/plans.hpp builds it). Accepted: a job_started
    // notice; refused: an error notice with the app's message.
    virtual void start_tool(Tool tool, const std::string &id, const std::string &body) = 0;
    // The Pakete page: "scan", "install" (id), "install_cancel", "queue_add" / "queue_remove" (id),
    // "queue_start", "queue_pause", "queue_retry", "queue_clear" (the finished ones).
    virtual void package_action(const std::string &action, const std::string &id) = 0;
    // POST /api/v1/packages/split: into parts of part_mb on the drive target ("split_cancel" as a
    // package_action stops it).
    virtual void split_package(const std::string &id, const std::string &target, int part_mb) = 0;
    // PS4 von USB: a JSON body for POST /api/v1/ps4mount (route ""), /ps4mount/update ("update") or
    // /ps4mount/dlc
    // ("dlc"); the answer's message comes back as a notice.
    virtual void ps4mount_post(const std::string &route, const std::string &json) = 0;
    // "game" (with the title id): Snapshot::ps4_detail; "log": Snapshot::ps4_log.
    virtual void ps4mount_fetch(const std::string &what, const std::string &id) = 0;
    // The Disc page: a JSON body for POST /api/v1/<route>, route "disc", "dump", "dumps/scan" or
    // "dumps/delete"; the answer's message comes back as a notice.
    virtual void disc_post(const std::string &route, const std::string &json) = 0;
    // A USB drive ejected through ShadowMountPlus ("/mnt/usb0"); the app refuses while one of its
    // jobs runs.
    virtual void usb_eject(const std::string &mount) = 0;
    // Spielzeit: the recorded sessions forgotten (the running one keeps counting).
    virtual void playtime_reset() = 0;
    // Spielstände: POST /api/v1/saves/<route> (backup, delete, verify, restore, cancel) with a JSON
    // body.
    virtual void saves_post(const std::string &route, const std::string &json) = 0;
    // Sicherungen: a game backup deleted at once (plan, then the delete with its one-time word).
    virtual void backup_delete(const std::string &path) = 0;
    // Dateien: open a folder ("" = the places), POST /api/v1/files/<route> (mkdir, rename, copy,
    // move, job/cancel), delete paths at once (plan, then the delete with its one-time word), view
    // a text file, measure a folder. Extras of a game: open the window ("" closes it; while open
    // the console is asked again and again), POST /api/v1/<route> with a JSON body, the versions of
    // APR-EMU (ampr) or PlayGo, the plan of an LZ4 pack.
    virtual void extras_open(const std::string &id) = 0;
    virtual void extras_post(const std::string &route, const std::string &json) = 0;
    virtual void extras_versions(const std::string &lib) = 0;
    virtual void extras_lz4_plan(const std::string &id, int level, const std::string &profile) = 0;
    // The library's duplicate images and cover cache: fetch both, or POST /api/v1/<route>.
    virtual void library_tools_fetch() = 0;
    // System: a POST of the console app ("cooling-health") and one reading of the risky sensors.
    virtual void system_post(const std::string &route, const std::string &json) = 0;
    virtual void risky_measure() = 0;
    virtual void library_tools_post(const std::string &route, const std::string &json) = 0;
    // The kernel log recorded into a file (on) or not (off).
    virtual void klog_record(bool on) = 0;
    // POST /api/v1/profile/username; a built-in picture (/avatars/<file>) made into the console's
    // files, sent and applied as the web page does; the picture before it back.
    virtual void set_username(const std::string &name) = 0;
    virtual void apply_avatar(const std::string &file) = 0;
    // The same with a picture of one's own, by its path on the console.
    virtual void apply_own_avatar(const std::string &path) = 0;
    virtual void restore_avatar() = 0;
    // POST /api/v1/profile/account/<action>: "activate" (needs the name), "fakesignin" (the app
    // sends np-fake-signin to the loader) and "restore" (everything back).
    virtual void account_action(const std::string &action, const std::string &name) = 0;
    // POST /api/v1/klog/save (the text as it is, a few MB at most) and /klog/files/clear (all but
    // the recording that runs).
    virtual void klog_save(const std::string &text) = 0;
    virtual void klog_files_clear() = 0;
    virtual void files_open(const std::string &path) = 0;
    virtual void files_post(const std::string &route, const std::string &json) = 0;
    // The answer to a FsConflict: "replace", "keep", "skip", or "" to give the copy or move up.
    virtual void files_conflict(const std::string &choice) = 0;
    virtual void files_delete(const std::vector<std::string> &paths) = 0;
    virtual void files_view(const std::string &path) = 0;
    virtual void files_size(const std::string &path) = 0;
    // POST /api/v1/backdrops: which picture an area shows. "custom" needs the user's picture
    // (uploaded in the browser).
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
// "82 MB/s", "6,4 MB/s", "340 KB/s": the speed of a copy.
std::string format_rate(std::uint64_t bytes_per_second);
// "40 Sek.", "12 Min.", "1 Std. 05 Min.": what is left.
std::string format_eta(int seconds);
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
// Appends the lines newer than the last one in `ring`, keeps at most `keep`; true when anything was
// added.
bool klog_append(std::vector<KlogLine> &ring, const std::vector<KlogLine> &fresh, std::size_t keep);

} // namespace hui::ps5cc
