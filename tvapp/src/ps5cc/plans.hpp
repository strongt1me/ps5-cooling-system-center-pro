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

// "Original danach" (18.11.2026): what becomes of the game's original once a copy, a conversion or
// an unpack has been written and checked. The console app does it after the check, never before.
constexpr int kOrigKeep = 0; // the default: nothing is sent
constexpr int kOrigDelete = 1;
constexpr int kOrigMove = 2;
constexpr int kOrigWays = 3;
constexpr const char *kOrigKeys[kOrigWays] = {"keep", "delete", "move"};

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
    std::string
        fpkg_error; // no debug FPKG: packs that could not be looked at, or both kinds of packs
    // A game in AMPR packs ("zstd" / "lz4"): Kraken Mount takes no package with packs in it, so
    // Dump Forge decodes them while it writes the debug FPKG; the package then holds the unpacked
    // game, this big (0: not known, an image).
    std::string fpkg_unpack;
    std::uint64_t fpkg_unpack_bytes = 0;
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
    // unpack, only when the app has no "Original danach" plan (an older one): the old switch; else
    // the way of `orig` below takes its place
    bool delete_source = false;
    // copy, convert, unpack: what becomes of the original afterwards (kOrigKeep, kOrigDelete,
    // kOrigMove) and, for a move, the absolute folder it goes to (orig_dest_of)
    int orig = kOrigKeep;
    std::string orig_dest;
    // the original the person saw (OrigPlan::path): sent along, so that the app refuses when the
    // title lies somewhere else by the time the job starts
    std::string orig_path;
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

// ---- "Original danach" (18.11.2026) ----
// A drive the original may be moved to.
struct OrigDrive
{
    std::string mount; // "/mnt/usb0"
    std::string label; // "usb0", "Interne SSD", "M.2-Erweiterung"
    std::string kind;  // "internal", "m2", "usb"
    std::uint64_t free_bytes = 0;
    std::string default_dest; // the folder offered on this drive: "/mnt/usb0/PS5-Sicherung/Spiele"
};

// GET /api/v1/library/orig/plan?id=: where the original lies, which ways the app allows and the
// drives for a move.
struct OrigPlan
{
    bool ok = false;
    std::string error; // why there is no plan (an older app, an unknown title)
    std::string id, name;
    std::string path, kind; // the original: its path, "folder", "image", "installed" or "other"
    std::string mount, label;
    std::uint64_t size_bytes = 0;
    bool can_delete = false;
    std::string delete_why; // why not, when it cannot be
    bool can_move = false;
    std::string move_why;
    std::string default_folder; // "PS5-Sicherung/Spiele"
    std::vector<OrigDrive> drives;
};

// What the person chose in the rows "Original danach", "Datenträger" and "Ordner".
struct OrigChoice
{
    int after = kOrigKeep;
    int drive = -1; // index into OrigPlan::drives
    std::string
        folder; // typed: under the drive ("Sicherung/Spiele") or absolute; "": the drive's own
};

// GET /api/v1/library/orig/check?id=&dest=: is this folder fit for the original?
struct OrigCheck
{
    bool ok = false;   // an answer came
    std::string error; // why not
    std::string level; // "ok" or "error"
    bool can_move = false;
    std::string text; // one German sentence, for the check line
    std::string dest, target;
    std::uint64_t free_bytes = 0;
    std::uint64_t need_bytes = 0;
    bool smp_searches = false; // ShadowMountPlus searches this folder
};

// One entry of GET /api/v1/library/orig/status: kind is "convert", "copy" or "unpack", mode
// "delete" or "move", state "waiting", "running", "done", "failed" or "skipped".
struct OrigItem
{
    std::string id, name, kind, mode, state, message, target;
    std::int64_t updated_ms = 0;
};

struct OrigStatus
{
    bool ok = false;
    bool active = false;
    std::vector<OrigItem> items; // newest first
};

OrigPlan parse_orig_plan(const OrigReply &reply);
OrigCheck parse_orig_check(const OrigReply &reply);
OrigStatus parse_orig_status(const std::string &json);

// Keep, and for a move the first drive that is not the original's own and has room for it (else
// the first that is not its own, else the first).
OrigChoice first_orig_choice(const OrigPlan &plan);
// Why this way (kOrigDelete, kOrigMove) cannot be chosen: the plan's own words; "" when it can.
std::string orig_way_blocked(const OrigPlan &plan, int way);
// The way the person lands on after pressing left or right: the one wanted, or when the plan
// refuses it the next one in that direction that is allowed ("Behalten" always is).
int orig_next_way(const OrigPlan &plan, int wanted, bool forward);
// The folder the original would go to, absolute: the typed folder (under the drive, or absolute
// when it starts with a "/"), else the drive's own proposal; "" while no drive is chosen.
std::string orig_dest_of(const OrigPlan &plan, const OrigChoice &choice);
// The same as the row "Ordner" shows it: under the drive ("PS5-Sicherung/Spiele"), or the whole
// path when it lies elsewhere.
std::string orig_folder_text(const OrigPlan &plan, const OrigChoice &choice);
// True when the result of this tool and choice lands in a folder ShadowMountPlus searches: a copy
// or a conversion as "A · Spielbar" (the folder homebrew; the debug FPKG goes to FPKG-Games, which
// it does not search), an unpack always.
bool result_in_search_folder(Tool tool, const PlanChoice &choice);
// The note for "Behalten" when original and result both lie in a searched folder; "" otherwise
// (and for every other way).
std::string orig_keep_note(Tool tool, const PlanChoice &choice);
// Why the start (and the queue) must not go ahead because of the original; "" when nothing speaks
// against it. A check that has not arrived yet is the caller's business (it waits).
std::string orig_blocked(const OrigPlan &plan, const PlanChoice &choice, const OrigCheck &check);
// "Gelöscht wird „Name“ (9,4 GB) aus /data/homebrew/PPSA23000. Spielstände bleiben."
std::string orig_delete_warning(const OrigPlan &plan);
// The lines under the name in the question that comes before a delete: where it lies, what stays.
std::vector<std::string> orig_delete_notes(const OrigPlan &plan);
// The entry of the status that belongs to a job: the newest one of this kind ("copy", "convert",
// "unpack") for this title id (when the job knows it), else for this name; null when none.
const OrigItem *orig_status_for(const OrigStatus &status, const std::string &kind,
                                const std::string &id, const std::string &name);
// The words a job card adds for its original: the entry's message; "" when there is none, or while
// the job still runs and the entry only waits (the card has the progress then).
std::string orig_card_text(const OrigItem &item, bool job_active);

} // namespace hui::ps5cc
