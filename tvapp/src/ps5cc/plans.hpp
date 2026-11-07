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
constexpr int kOps = 3;   // ffpfsc, ffpkg, exfat
constexpr const char *kModeKeys[kModes] = {"homebrew", "backup"};
constexpr const char *kOpKeys[kOps] = {"ffpfsc", "ffpkg", "exfat"};

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

// Plain ELF executables of a folder game (fsign.c): the console does not start them. A conversion, and packing into a new
// folder, write them as FSELF; the plan names them beforehand.
struct UnsignedExec
{
    std::uint64_t count = 0;        // the ones that can be signed
    std::uint64_t refused = 0;      // the ones with an odd header: left as they are
    std::vector<std::string> files; // the first few
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
    int drive = -1; // index into Plan::drives
    bool ampr = false;
    bool delete_source = false;
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
// The body of the start's POST, exactly as the web app sends it.
std::string start_body(const Plan &plan, const PlanChoice &choice);
// Why a compressed image may stutter, one sentence each; empty when nothing speaks against it.
std::vector<std::string> stream_reasons(const Plan &plan);
// The names of the unsigned executables, comma-separated, with "…" when there are more than shown; "" when none.
std::string unsigned_names(const UnsignedExec &u);
// "exFAT", "FAT32", "UFS" for the console's file system names.
std::string fs_label(const std::string &fs);

} // namespace hui::ps5cc
