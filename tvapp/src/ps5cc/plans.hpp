// ps5cc - The game tools' plans: what the console app answers before a copy, a conversion, a move,
// a check or a delete.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rules are the web app's (web/app.js: cpBlocked, cvBlocked, mvBlocked, the unpack and delete
// dialogs), so both faces refuse the same targets with the same words; the console app checks
// everything again at the start.

#pragma once

#include "ps5cc/data.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::ps5cc
{

// One destination file or folder on a drive (copy: per mode; convert: per mode and format).
struct PlanDest
{
    bool present = false;
    std::string path;
    bool exists = false;
    bool unfinished = false; // a leftover of this app, replaced at the start
    std::uint64_t unfinished_bytes = 0;
    int enough_space = -1; // -1: not said, 0 or 1
    bool path_too_long = false;
};

constexpr int kModes = 2; // homebrew, backup
constexpr int kOps = 4;   // ffpfsc, ffpkg, exfat, fpkg
constexpr const char *kModeKeys[kModes] = {"homebrew", "backup"};
constexpr const char *kOpKeys[kOps] = {"ffpfsc", "ffpkg", "exfat", "fpkg"};

// The debug FPKG (written by Dump Forge, 14.11.2026) is one format with four compression levels;
// the console app knows each level as an op of its own.
constexpr int kOpFpkg = 3;
constexpr int kLevels = 4;
constexpr const char *kLevelOps[kLevels] = {"fpkg", "fpkg-bal", "fpkg-min", "fpkg-raw"};
constexpr const char *kLevelNames[kLevels] = {"Schnell", "Ausgewogen", "Kleinste", "Unkomprimiert"};
constexpr int kLevelRaw = 3; // stored: nothing is compressed

// A place the plan offers: a drive (copy, convert, image unpack) or a folder of ShadowMountPlus
// (move, unpack).
struct PlanDrive
{
    std::string mount; // what the start is given: the mount, or for move and unpack the folder
    std::string label; // the drive's name, or the folder
    std::string drive; // move and unpack: the drive the folder is on
    std::string fs;
    std::uint64_t free_bytes = 0;
    bool enough_space = true;
    bool fat32_too_big = false;
    bool fat32_too_big_ffpkg = false;
    bool source_here = false;
    bool here = false;          // move: the game lies in this folder now
    bool exists = false;        // image unpack: the folder exists; move/unpack: the folder exists
    bool path_too_long = false; // image unpack
    std::string path;           // image unpack: the folder it would write
    std::string target;         // move and unpack: what the result would be called
    bool target_exists = false;
    std::array<PlanDest, kModes> copy{};
    std::array<std::array<PlanDest, kOps>, kModes> convert{};
};

struct StreamRisk
{
    bool risky = false;
    bool big_data = false;
    std::uint64_t data_bytes = 0;
    std::uint64_t container_bytes = 0;
    std::vector<std::string> containers;
    std::string stream_dir;
    std::uint64_t stream_dir_bytes = 0;
};

// Plain ELF executables of a folder game (fsign.c): the console does not start them. A conversion,
// and packing into a new folder, write them as FSELF; the plan names them beforehand.
struct UnsignedExec
{
    std::uint64_t count = 0;        // the ones that can be signed
    std::uint64_t refused = 0;      // the ones with an odd header: left as they are
    std::vector<std::string> files; // the first few
};

// Dump Forge, the engine of the debug FPKG: there (folder, version) or still to be fetched.
struct Engine
{
    bool known = false; // the plan said anything about it
    bool installed = false;
    bool running = false;
    std::string version;
    std::string folder;
};

struct Plan
{
    Tool tool = Tool::copy;
    bool ok = false;
    std::string error; // why there is no plan
    std::string id, name, source, kind;
    bool running = false, busy = false, other_busy = false;
    std::uint64_t size_bytes = 0;
    std::uint64_t files = 0;
    std::uint64_t skipped = 0;
    std::vector<PlanDrive> drives;
    // ---- convert ----
    bool ps4 = false;
    std::uint64_t raw_bytes = 0;
    std::uint64_t ffpkg_bytes = 0;
    std::string ffpkg_error;
    std::vector<std::string> ops; // the formats the console app knows by name, in its order
    StreamRisk stream;
    UnsignedExec unsigned_exec;
    Engine dumpforge; // the format "fpkg"
    // ---- move and unpack (ShadowMountPlus) ----
    std::string op, image_type;
    // ---- image unpack, check, repair ----
    bool damaged = false;
    std::string format, why, note;
    bool can_check = false, can_unpack = false;
    std::uint64_t image_bytes = 0;
    std::string repair_state = "na"; // na, fine, fixable, broken
    std::string repair_why;
    std::uint64_t repair_declared = 0, repair_actual = 0;
    bool checked = false; // a last check exists
    bool check_clean = false;
    std::int64_t check_when = 0;
    std::uint64_t check_files = 0, check_bad_files = 0;
    std::string check_container;
    std::vector<std::string> check_bad;
    // ---- delete ----
    std::string path, version, keeps, confirm;
    std::vector<std::string> items;
    bool can_delete = false;
};

// What the person chose in a plan's dialog.
struct PlanChoice
{
    int mode = -1;  // copy and convert: 0 homebrew, 1 backup, -1 not chosen yet
    int op = -1;    // convert: index into kOpKeys
    int level = 0;  // convert, format fpkg: index into kLevelOps
    int drive = -1; // index into Plan::drives
    bool ampr = false;
    bool quick =
        false; // convert: check the result the quick way (structure and a sample), no checksum file
    bool delete_source = false;
    // convert: the template of the result's name ({titel} {id} {version} {quelle}); empty: the name
    // of the source
    std::string name;
};

// The template the name row offers (what the console names a game: title, id, version).
constexpr const char *kNameTemplate = "{titel} [{id}] v{version}";

// One line of the check before a conversion starts (POST /api/v1/library/convert/check): level is
// "ok", "warn" or "error".
struct CheckRow
{
    std::string id, level, text, hint;
};

// The whole answer: red rows stop the start and the queue, a yellow "busy" row only the start.
struct ConvertCheck
{
    bool ok = false;
    std::string error; // why there is no answer
    std::vector<CheckRow> rows;
    bool can_start = false;
    bool can_queue = false;
    std::string target; // the full path of the result
    std::string stem;   // its name without the ending
};

// One entry of the queue of conversions (GET /api/v1/library/convert/queue): state is "waiting",
// "running", "done", "failed" or "cancelled".
struct QueueItem
{
    int qid = 0;
    std::string id, name, op, mode, mount, target, state, message;
    std::uint64_t output_bytes = 0;
    double elapsed_s = 0.0;
};

struct ConvertQueue
{
    bool ok = false;
    bool running = false; // the queue starts entries
    int running_id = 0;
    std::vector<QueueItem> items;
    int open = 0; // waiting or running
    int done = 0;
    int failed = 0; // failed or cancelled
};

// A step of a conversion as the job card shows it: the name and what stands under it ("16:06 · 51
// %", "noch offen").
struct StageText
{
    std::string label, detail, state;
};

// The tools a game offers, in menu order, from the library's flags.
std::vector<Tool> tools_for(const Game &game);
const char *tool_label(Tool tool);
// True when the game lies as an image file the console app can read itself.
bool is_image(const Game &game);
// The job card a started tool shows up on.
JobKind job_of(Tool tool);

// The answer of a plan route; a reply that failed becomes ok = false with its message.
Plan parse_plan(Tool tool, const PlanReply &reply);

// Why this drive cannot be chosen ("" when it can).
std::string drive_blocked(const Plan &plan, const PlanChoice &choice, const PlanDrive &drive);
// Why nothing can start at all ("" when something can).
std::string run_blocked(const Plan &plan);
// Where the result lands with this choice ("" when it is not complete).
std::string target_of(const Plan &plan, const PlanChoice &choice);
// The chosen drive's own destination entry, or null.
const PlanDest *dest_of(const Plan &plan, const PlanChoice &choice);
// True when the start may be pressed.
bool ready(const Plan &plan, const PlanChoice &choice);
// The choice the dialog opens with, as the web app picks it.
PlanChoice first_choice(const Plan &plan, const Game *game);
// The op the console app is given for this choice ("ffpfsc", "fpkg-min" ...); "" when none is
// chosen.
std::string op_key(const PlanChoice &choice);
// True for the console app's ops of the debug FPKG ("fpkg", "fpkg-bal", "fpkg-min", "fpkg-raw").
bool is_fpkg_op(const std::string &op);
// True when the result gets smaller than the game (ffpfsc and a compressed FPKG): the step is
// "Komprimieren".
bool squeezes(const std::string &op);
// The op in words for the queue: "FPKG · schnell", the others as they are.
std::string op_label(const std::string &op);
// The body of the start's POST, exactly as the web app sends it.
std::string start_body(const Plan &plan, const PlanChoice &choice);
// Why a compressed image may stutter, one sentence each; empty when nothing speaks against it.
std::vector<std::string> stream_reasons(const Plan &plan);
// The names of the unsigned executables, comma-separated, with "…" when there are more than shown;
// "" when none.
std::string unsigned_names(const UnsignedExec &u);
// "exFAT", "FAT32", "UFS" for the console's file system names.
std::string fs_label(const std::string &fs);

// ---- the check before a conversion, the queue and the steps (13.11.2026) ----
// The answer of the check; a reply that failed becomes ok = false with its message.
ConvertCheck parse_check(const CheckReply &reply);
// The body of the check's POST for this choice; "" while format, kind or drive is not chosen yet.
std::string check_body(const Plan &plan, const PlanChoice &choice);
// The body that puts the conversion into the queue (as the start's, plus the action).
std::string queue_add_body(const Plan &plan, const PlanChoice &choice);
// {"action": ..., "qid": ...} for remove, retry, start, stop and clear (qid only where it is not
// 0).
std::string queue_action_body(const std::string &action, int qid = 0);
ConvertQueue parse_queue(const std::string &json);
// "0:42", "16:06", "1:02:11" (the time of a step).
std::string duration_text(double ms);
// The steps of a conversion's job with their times; empty when the app sent none.
std::vector<StageText> stage_texts(const Job &job);
// The words under the "Warteschlange" line: "2 offen · 1 fertig · 1 mit Fehler".
std::string queue_summary(const ConvertQueue &queue);

} // namespace hui::ps5cc
