// ps5cc - Tests for "Original danach": the plan for the original, the check of a folder, the fields
// of the start bodies, the notes and refusals, the status the job cards show.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/plans.hpp"

#include <gtest/gtest.h>

namespace
{

using hui::ps5cc::OrigCheck;
using hui::ps5cc::OrigChoice;
using hui::ps5cc::OrigPlan;
using hui::ps5cc::OrigReply;
using hui::ps5cc::OrigStatus;
using hui::ps5cc::Plan;
using hui::ps5cc::PlanChoice;
using hui::ps5cc::PlanDrive;
using hui::ps5cc::Tool;

OrigReply reply(const char *json)
{
    OrigReply r;
    r.ticket = 7;
    r.ok = true;
    r.json = json;
    return r;
}

// The answer of GET /api/v1/library/orig/plan: the original on the internal SSD, three drives.
constexpr const char *kPlan = R"({"ok":true,"id":"PPSA23000","name":"Double Dragon Revive",
  "source":{"path":"/data/homebrew/PPSA23000","kind":"folder","size_bytes":9400000000,"mount":"/data",
            "label":"Interne SSD"},
  "can_delete":true,"delete_why":"","can_move":true,"move_why":"","default_folder":"PS5-Sicherung/Spiele",
  "drives":[
   {"mount":"/data","label":"Interne SSD","kind":"internal","free_bytes":123000000000,
    "default_dest":"/data/PS5-Sicherung/Spiele"},
   {"mount":"/mnt/ext1","label":"M.2-Erweiterung","kind":"m2","free_bytes":5000000000,
    "default_dest":"/mnt/ext1/PS5-Sicherung/Spiele"},
   {"mount":"/mnt/usb0","label":"usb0","kind":"usb","free_bytes":1800000000000,
    "default_dest":"/mnt/usb0/PS5-Sicherung/Spiele"}]})";

OrigPlan plan()
{
    return hui::ps5cc::parse_orig_plan(reply(kPlan));
}

TEST(Ps5ccOrig, ThePlanIsParsed)
{
    const OrigPlan p = plan();
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.id, "PPSA23000");
    EXPECT_EQ(p.name, "Double Dragon Revive");
    EXPECT_EQ(p.path, "/data/homebrew/PPSA23000");
    EXPECT_EQ(p.kind, "folder");
    EXPECT_EQ(p.mount, "/data");
    EXPECT_EQ(p.size_bytes, 9400000000u);
    EXPECT_TRUE(p.can_delete);
    EXPECT_TRUE(p.can_move);
    EXPECT_EQ(p.default_folder, "PS5-Sicherung/Spiele");
    ASSERT_EQ(p.drives.size(), 3u);
    EXPECT_EQ(p.drives[1].label, "M.2-Erweiterung");
    EXPECT_EQ(p.drives[1].kind, "m2");
    EXPECT_EQ(p.drives[2].default_dest, "/mnt/usb0/PS5-Sicherung/Spiele");
}

TEST(Ps5ccOrig, AMissingDefaultDestIsMadeFromTheDefaultFolder)
{
    const OrigPlan p = hui::ps5cc::parse_orig_plan(reply(
        R"({"ok":true,"id":"X","source":{"path":"/data/x","mount":"/data"},"default_folder":"Sicherung/Spiele",
            "drives":[{"mount":"/mnt/usb0/","label":"usb0","kind":"usb","free_bytes":1}]})"));
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.drives.size(), 1u);
    EXPECT_EQ(p.drives[0].default_dest, "/mnt/usb0/Sicherung/Spiele");
}

TEST(Ps5ccOrig, AFailedOrUnreadablePlanIsFriendly)
{
    OrigReply r;
    r.ok = false;
    r.error = "Das Spiel ist nicht in der Liste.";
    OrigPlan p = hui::ps5cc::parse_orig_plan(r);
    EXPECT_FALSE(p.ok);
    EXPECT_EQ(p.error, "Das Spiel ist nicht in der Liste.");
    // an older app: no message at all
    r.error.clear();
    EXPECT_EQ(hui::ps5cc::parse_orig_plan(r).error, "Die App hat nicht geantwortet.");
    EXPECT_EQ(hui::ps5cc::parse_orig_plan(reply("{nicht json")).error,
              "Die Antwort der App ist nicht lesbar.");
    p = hui::ps5cc::parse_orig_plan(reply(R"({"ok":false,"message":"Unbekannter Titel."})"));
    EXPECT_FALSE(p.ok);
    EXPECT_EQ(p.error, "Unbekannter Titel.");
    // nothing offered: a plan that says nothing allows nothing
    p = hui::ps5cc::parse_orig_plan(reply(R"({"ok":true,"id":"X"})"));
    EXPECT_TRUE(p.ok);
    EXPECT_FALSE(p.can_delete);
    EXPECT_FALSE(p.can_move);
    EXPECT_TRUE(p.drives.empty());
}

TEST(Ps5ccOrig, TheFirstChoiceKeepsAndPicksADriveWithRoom)
{
    const OrigPlan p = plan();
    const OrigChoice c = hui::ps5cc::first_orig_choice(p);
    EXPECT_EQ(c.after, hui::ps5cc::kOrigKeep);
    // the internal SSD is the original's own drive; M.2 has too little room; usb0 is the one
    EXPECT_EQ(c.drive, 2);
    EXPECT_TRUE(c.folder.empty());

    // nobody has room: the first drive that is not the original's own
    OrigPlan tight = p;
    tight.size_bytes = 9000000000000ull;
    EXPECT_EQ(hui::ps5cc::first_orig_choice(tight).drive, 1);
    // only the original's own drive: it is better than none
    OrigPlan only = p;
    only.drives.resize(1);
    EXPECT_EQ(hui::ps5cc::first_orig_choice(only).drive, 0);
    only.drives.clear();
    EXPECT_EQ(hui::ps5cc::first_orig_choice(only).drive, -1);
}

TEST(Ps5ccOrig, AWayTheAppRefusesSaysWhy)
{
    using hui::ps5cc::kOrigDelete;
    using hui::ps5cc::kOrigKeep;
    using hui::ps5cc::kOrigMove;
    OrigPlan p = plan();
    EXPECT_EQ(hui::ps5cc::orig_way_blocked(p, kOrigKeep), "");
    EXPECT_EQ(hui::ps5cc::orig_way_blocked(p, kOrigDelete), "");
    EXPECT_EQ(hui::ps5cc::orig_way_blocked(p, kOrigMove), "");
    p.can_delete = false;
    p.delete_why = "Das Spiel läuft gerade. Bitte erst beenden.";
    EXPECT_EQ(hui::ps5cc::orig_way_blocked(p, kOrigDelete),
              "Das Spiel läuft gerade. Bitte erst beenden.");
    p.can_move = false; // and no reason given: the app's words are replaced by ours
    EXPECT_EQ(hui::ps5cc::orig_way_blocked(p, kOrigMove),
              "Das Original lässt sich nicht verschieben.");
    // a move needs a drive
    p.can_move = true;
    p.drives.clear();
    EXPECT_EQ(hui::ps5cc::orig_way_blocked(p, kOrigMove),
              "Es gibt keinen Datenträger, auf den sich das Original verschieben ließe.");
}

TEST(Ps5ccOrig, LeftAndRightSkipTheWaysThatAreRefused)
{
    using hui::ps5cc::kOrigDelete;
    using hui::ps5cc::kOrigKeep;
    using hui::ps5cc::kOrigMove;
    OrigPlan p = plan();
    // everything open: the way wanted
    EXPECT_EQ(hui::ps5cc::orig_next_way(p, kOrigDelete, true), kOrigDelete);
    p.can_delete = false;
    // right from "Behalten" would be "Löschen": on to "Verschieben"
    EXPECT_EQ(hui::ps5cc::orig_next_way(p, kOrigDelete, true), kOrigMove);
    // left from "Verschieben" would be "Löschen": on to "Behalten"
    EXPECT_EQ(hui::ps5cc::orig_next_way(p, kOrigDelete, false), kOrigKeep);
    // nothing but "Behalten" is open
    p.can_move = false;
    EXPECT_EQ(hui::ps5cc::orig_next_way(p, kOrigDelete, true), kOrigKeep);
    EXPECT_EQ(hui::ps5cc::orig_next_way(p, kOrigMove, false), kOrigKeep);
}

TEST(Ps5ccOrig, TheFolderIsTheDrivesOwnUntilTheyTypeOne)
{
    const OrigPlan p = plan();
    OrigChoice c = hui::ps5cc::first_orig_choice(p);
    EXPECT_EQ(hui::ps5cc::orig_dest_of(p, c), "/mnt/usb0/PS5-Sicherung/Spiele");
    EXPECT_EQ(hui::ps5cc::orig_folder_text(p, c), "PS5-Sicherung/Spiele");
    // typed under the drive: it goes with the drive
    c.folder = "Meine Spiele/Alt/";
    EXPECT_EQ(hui::ps5cc::orig_dest_of(p, c), "/mnt/usb0/Meine Spiele/Alt");
    EXPECT_EQ(hui::ps5cc::orig_folder_text(p, c), "Meine Spiele/Alt");
    c.drive = 1;
    EXPECT_EQ(hui::ps5cc::orig_dest_of(p, c), "/mnt/ext1/Meine Spiele/Alt");
    // typed whole: it stays where it is, whatever the drive
    c.folder = "  /mnt/usb0/Archiv ";
    EXPECT_EQ(hui::ps5cc::orig_dest_of(p, c), "/mnt/usb0/Archiv");
    EXPECT_EQ(hui::ps5cc::orig_folder_text(p, c), "/mnt/usb0/Archiv");
    c.drive = 2;
    EXPECT_EQ(hui::ps5cc::orig_folder_text(p, c), "Archiv");
    // emptied: the drive's own again
    c.folder = " / ";
    EXPECT_EQ(hui::ps5cc::orig_dest_of(p, c), "/mnt/usb0/PS5-Sicherung/Spiele");
    // no drive chosen: no folder
    c.drive = -1;
    EXPECT_EQ(hui::ps5cc::orig_dest_of(p, c), "");
    EXPECT_EQ(hui::ps5cc::orig_folder_text(p, c), "");
    c.drive = 9;
    EXPECT_EQ(hui::ps5cc::orig_dest_of(p, c), "");
}

TEST(Ps5ccOrig, TheCheckIsParsedAndAnUnknownLevelHoldsTheStartBack)
{
    OrigCheck c = hui::ps5cc::parse_orig_check(reply(
        R"({"ok":true,"level":"ok","can_move":true,"text":"/mnt/usb0/PS5-Sicherung/Spiele · Platz reicht",
            "dest":"/mnt/usb0/PS5-Sicherung/Spiele","target":"/mnt/usb0/PS5-Sicherung/Spiele/PPSA23000",
            "free_bytes":1800000000000,"need_bytes":9400000000,"smp_searches":false})"));
    ASSERT_TRUE(c.ok);
    EXPECT_EQ(c.level, "ok");
    EXPECT_TRUE(c.can_move);
    EXPECT_EQ(c.target, "/mnt/usb0/PS5-Sicherung/Spiele/PPSA23000");
    EXPECT_EQ(c.need_bytes, 9400000000u);
    EXPECT_FALSE(c.smp_searches);

    c = hui::ps5cc::parse_orig_check(reply(
        R"({"ok":true,"level":"error","can_move":false,"text":"ShadowMountPlus würde das Spiel wieder finden.",
            "smp_searches":true})"));
    EXPECT_EQ(c.level, "error");
    EXPECT_TRUE(c.smp_searches);

    // a level this screen does not know is read as a refusal
    c = hui::ps5cc::parse_orig_check(reply(R"({"ok":true,"level":"warn","can_move":true})"));
    EXPECT_EQ(c.level, "error");
    c = hui::ps5cc::parse_orig_check(reply(R"({"ok":true,"can_move":true})"));
    EXPECT_EQ(c.level, "error");

    // no answer
    OrigReply failed;
    failed.error = "Die App hat nicht rechtzeitig geantwortet.";
    c = hui::ps5cc::parse_orig_check(failed);
    EXPECT_FALSE(c.ok);
    EXPECT_EQ(c.error, "Die App hat nicht rechtzeitig geantwortet.");
    c = hui::ps5cc::parse_orig_check(
        reply(R"({"ok":false,"message":"Dieser Ordner gehört nicht dazu."})"));
    EXPECT_FALSE(c.ok);
    EXPECT_EQ(c.error, "Dieser Ordner gehört nicht dazu.");
}

PlanChoice moving(const OrigPlan &p)
{
    PlanChoice c;
    c.orig = hui::ps5cc::kOrigMove;
    c.orig_dest = hui::ps5cc::orig_dest_of(p, hui::ps5cc::first_orig_choice(p));
    return c;
}

TEST(Ps5ccOrig, AnErrorOfTheCheckStopsTheStart)
{
    const OrigPlan p = plan();
    OrigCheck ok;
    ok.ok = true;
    ok.level = "ok";
    ok.can_move = true;
    ok.text = "Platz reicht";
    OrigCheck error = ok;
    error.level = "error";
    error.can_move = false;
    error.text = "Dorthin wird nicht verschoben: ShadowMountPlus würde das Spiel wieder finden.";

    // keeping asks nothing of anyone
    PlanChoice keep;
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, keep, OrigCheck{}), "");
    // a move with a good check goes
    PlanChoice move = moving(p);
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, move, ok), "");
    // a red check says why, in the app's words
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, move, error), error.text);
    // "can_move" false is red even when the level says ok
    OrigCheck odd = ok;
    odd.can_move = false;
    odd.text.clear();
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, move, odd),
              "Dorthin lässt sich das Original nicht verschieben.");
    // no answer yet or at all: held back
    EXPECT_FALSE(hui::ps5cc::orig_blocked(p, move, OrigCheck{}).empty());
    OrigCheck silent;
    silent.error = "Die App hat nicht rechtzeitig geantwortet.";
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, move, silent), silent.error);
    // no folder named
    PlanChoice nowhere = move;
    nowhere.orig_dest.clear();
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, nowhere, ok),
              "Wähle Datenträger und Ordner für das Original.");
    // a plan that refuses the move
    OrigPlan no = p;
    no.can_move = false;
    no.move_why = "Das Spiel läuft gerade. Bitte erst beenden.";
    EXPECT_EQ(hui::ps5cc::orig_blocked(no, move, ok),
              "Das Spiel läuft gerade. Bitte erst beenden.");
    // no plan, no say
    EXPECT_EQ(hui::ps5cc::orig_blocked(OrigPlan{}, move, error), "");
}

TEST(Ps5ccOrig, ADeleteIsHeldBackOnlyWhenThePlanRefusesIt)
{
    OrigPlan p = plan();
    PlanChoice c;
    c.orig = hui::ps5cc::kOrigDelete;
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, c, OrigCheck{}), "");
    p.can_delete = false;
    EXPECT_EQ(hui::ps5cc::orig_blocked(p, c, OrigCheck{}),
              "Das Original lässt sich nicht löschen.");
}

// A plan for a copy, a conversion and an unpack, as far as the start bodies need it.
Plan tool_plan(Tool tool)
{
    Plan p;
    p.tool = tool;
    p.ok = true;
    p.id = "PPSA23000";
    p.kind = "folder";
    PlanDrive d;
    d.mount = tool == Tool::unpack ? "/mnt/ext0/homebrew" : "/mnt/ext0";
    p.drives.push_back(d);
    return p;
}

TEST(Ps5ccOrig, TheStartBodiesCarryTheOriginalOnlyWhenItIsNotKept)
{
    const OrigPlan op = plan();
    PlanChoice keep;
    keep.drive = 0;
    keep.mode = 1;
    PlanChoice del = keep;
    del.orig = hui::ps5cc::kOrigDelete;
    PlanChoice move = keep;
    move.orig = hui::ps5cc::kOrigMove;
    move.orig_dest = hui::ps5cc::orig_dest_of(op, hui::ps5cc::first_orig_choice(op));

    const Plan copy = tool_plan(Tool::copy);
    EXPECT_EQ(hui::ps5cc::start_body(copy, keep),
              R"({"id":"PPSA23000","target":"/mnt/ext0","mode":"backup"})");
    EXPECT_EQ(hui::ps5cc::start_body(copy, del),
              R"({"id":"PPSA23000","target":"/mnt/ext0","mode":"backup","orig_after":"delete"})");
    EXPECT_EQ(
        hui::ps5cc::start_body(copy, move),
        R"({"id":"PPSA23000","target":"/mnt/ext0","mode":"backup","orig_after":"move","orig_dest":"/mnt/usb0/PS5-Sicherung/Spiele"})");

    // the original the person saw goes along (the app refuses when the title lies elsewhere by
    // then); never for "keep"
    PlanChoice seen_keep = keep;
    seen_keep.orig_path = "/mnt/usb0/homebrew/PPSA23000";
    EXPECT_EQ(hui::ps5cc::start_body(copy, seen_keep),
              R"({"id":"PPSA23000","target":"/mnt/ext0","mode":"backup"})");
    PlanChoice seen_del = del;
    seen_del.orig_path = "/mnt/usb0/homebrew/PPSA23000";
    EXPECT_EQ(
        hui::ps5cc::start_body(copy, seen_del),
        R"({"id":"PPSA23000","target":"/mnt/ext0","mode":"backup","orig_after":"delete","orig_path":"/mnt/usb0/homebrew/PPSA23000"})");
    PlanChoice seen_move = move;
    seen_move.orig_path = "/mnt/usb0/homebrew/PPSA23000";
    EXPECT_EQ(
        hui::ps5cc::start_body(copy, seen_move),
        R"({"id":"PPSA23000","target":"/mnt/ext0","mode":"backup","orig_after":"move","orig_dest":"/mnt/usb0/PS5-Sicherung/Spiele","orig_path":"/mnt/usb0/homebrew/PPSA23000"})");

    const Plan convert = tool_plan(Tool::convert);
    keep.op = del.op = move.op = 0; // ffpfsc
    keep.mode = del.mode = move.mode = 0;
    EXPECT_EQ(
        hui::ps5cc::start_body(convert, keep),
        R"({"id":"PPSA23000","op":"ffpfsc","target":"/mnt/ext0","mode":"homebrew","ampr_index":false,"verify":"full","name":""})");
    EXPECT_EQ(
        hui::ps5cc::start_body(convert, del),
        R"({"id":"PPSA23000","op":"ffpfsc","target":"/mnt/ext0","mode":"homebrew","ampr_index":false,"verify":"full","name":"","orig_after":"delete"})");
    // the queue takes the same fields, plus its action
    EXPECT_EQ(
        hui::ps5cc::queue_add_body(convert, move),
        R"({"id":"PPSA23000","op":"ffpfsc","target":"/mnt/ext0","mode":"homebrew","ampr_index":false,"verify":"full","name":"","orig_after":"move","orig_dest":"/mnt/usb0/PS5-Sicherung/Spiele","action":"add"})");

    const Plan unpack = tool_plan(Tool::unpack);
    EXPECT_EQ(
        hui::ps5cc::start_body(unpack, keep),
        R"({"id":"PPSA23000","op":"unpack","target":"/mnt/ext0/homebrew","delete_source":false})");
    EXPECT_EQ(
        hui::ps5cc::start_body(unpack, del),
        R"({"id":"PPSA23000","op":"unpack","target":"/mnt/ext0/homebrew","delete_source":false,"orig_after":"delete"})");
    EXPECT_EQ(
        hui::ps5cc::start_body(unpack, move),
        R"({"id":"PPSA23000","op":"unpack","target":"/mnt/ext0/homebrew","delete_source":false,"orig_after":"move","orig_dest":"/mnt/usb0/PS5-Sicherung/Spiele"})");
    // an app without the plan for the original keeps the old switch
    PlanChoice old = keep;
    old.delete_source = true;
    EXPECT_EQ(
        hui::ps5cc::start_body(unpack, old),
        R"({"id":"PPSA23000","op":"unpack","target":"/mnt/ext0/homebrew","delete_source":true})");

    // ShadowMountPlus's own move has no original of ours to deal with
    const Plan mover = tool_plan(Tool::move);
    EXPECT_EQ(hui::ps5cc::start_body(mover, del),
              R"({"id":"PPSA23000","op":"move","target":"/mnt/ext0","delete_source":false})");
}

TEST(Ps5ccOrig, KeepingWarnsOnlyWhereTheResultIsSearched)
{
    using hui::ps5cc::kOpFpkg;
    PlanChoice c;
    const char *note =
        "Ergebnis und Original liegen beide in einem Ordner, den ShadowMountPlus durchsucht: Er "
        "meldet dasselbe Spiel zweimal.";
    // a copy as "A · Spielbar" lands in homebrew; as "B · Sicherung" it does not
    c.mode = 0;
    EXPECT_TRUE(hui::ps5cc::result_in_search_folder(Tool::copy, c));
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::copy, c), note);
    c.mode = 1;
    EXPECT_FALSE(hui::ps5cc::result_in_search_folder(Tool::copy, c));
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::copy, c), "");
    c.mode = -1; // not chosen yet
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::copy, c), "");

    // a conversion the same; the debug FPKG goes to FPKG-Games, which is not searched
    c.mode = 0;
    c.op = 0;
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::convert, c), note);
    c.op = kOpFpkg;
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::convert, c), "");
    c.mode = 1;
    c.op = 2;
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::convert, c), "");

    // an unpack always writes into a folder of ShadowMountPlus
    c = PlanChoice{};
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::unpack, c), note);

    // deleting or moving the original leaves nothing to find twice
    c.orig = hui::ps5cc::kOrigDelete;
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::unpack, c), "");
    c.orig = hui::ps5cc::kOrigMove;
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::unpack, c), "");

    // the other tools have no such note
    c = PlanChoice{};
    c.mode = 0;
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::move, c), "");
    EXPECT_EQ(hui::ps5cc::orig_keep_note(Tool::erase, c), "");
}

TEST(Ps5ccOrig, TheDeleteQuestionNamesWhatGoes)
{
    const OrigPlan p = plan();
    EXPECT_EQ(hui::ps5cc::orig_delete_warning(p),
              "Gelöscht wird „Double Dragon Revive“ (8,8 GB) aus /data/homebrew/PPSA23000. "   /* binary units since 21.11.2026 */
              "Spielstände bleiben.");
    const std::vector<std::string> notes = hui::ps5cc::orig_delete_notes(p);
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_EQ(notes[0], "/data/homebrew/PPSA23000");
    EXPECT_EQ(notes[1], "Spielstände bleiben erhalten.");
    // what the app did not say is left out
    OrigPlan bare;
    bare.name = "X";
    EXPECT_EQ(hui::ps5cc::orig_delete_warning(bare), "Gelöscht wird „X“. Spielstände bleiben.");
    ASSERT_EQ(hui::ps5cc::orig_delete_notes(bare).size(), 1u);
}

constexpr const char *kStatus = R"({"ok":true,"active":true,"items":[
  {"id":"PPSA23000","name":"Double Dragon Revive","kind":"convert","mode":"move","state":"running",
   "message":"Das Original wird verschoben …","target":"/mnt/usb0/PS5-Sicherung/Spiele","updated_ms":3000},
  {"id":"PPSA23000","name":"Double Dragon Revive","kind":"copy","mode":"delete","state":"done",
   "message":"Original gelöscht: 9,4 GB sind frei.","updated_ms":2000},
  {"id":"PPSA01325","name":"ASTRO BOT","kind":"convert","mode":"delete","state":"failed",
   "message":"Original nicht gelöscht: Der Ordner ist schreibgeschützt.","updated_ms":1000},
  {"id":"PPSA09999","name":"Altes Spiel","kind":"unpack","mode":"delete","state":"waiting","message":"","updated_ms":900},
  {"kind":"copy","mode":"delete","state":"done","message":"ohne Namen und Nummer"}]})";

TEST(Ps5ccOrig, TheStatusIsParsedNewestFirstAndKeepsTheOrder)
{
    const OrigStatus s = hui::ps5cc::parse_orig_status(kStatus);
    ASSERT_TRUE(s.ok);
    EXPECT_TRUE(s.active);
    // the entry that names neither a title nor a name is dropped
    ASSERT_EQ(s.items.size(), 4u);
    EXPECT_EQ(s.items[0].kind, "convert");
    EXPECT_EQ(s.items[0].state, "running");
    EXPECT_EQ(s.items[0].target, "/mnt/usb0/PS5-Sicherung/Spiele");
    EXPECT_EQ(s.items[0].updated_ms, 3000);
    EXPECT_EQ(s.items[2].state, "failed");

    EXPECT_FALSE(hui::ps5cc::parse_orig_status("").ok);
    EXPECT_FALSE(hui::ps5cc::parse_orig_status("{nicht json").ok);
    // never more than the app's eight
    std::string many = R"({"ok":true,"items":[)";
    for (int i = 0; i < 12; ++i)
        many += std::string(i == 0 ? "" : ",") + R"({"id":"X)" + std::to_string(i) +
                R"(","kind":"copy","state":"done","message":"m"})";
    many += "]}";
    EXPECT_EQ(hui::ps5cc::parse_orig_status(many).items.size(), 8u);
}

TEST(Ps5ccOrig, AJobCardFindsItsOwnOriginal)
{
    const OrigStatus s = hui::ps5cc::parse_orig_status(kStatus);
    // the newest entry of this kind for this title
    const hui::ps5cc::OrigItem *a = hui::ps5cc::orig_status_for(s, "convert", "PPSA23000", "");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->state, "running");
    // the same title as a copy is another entry
    const hui::ps5cc::OrigItem *b = hui::ps5cc::orig_status_for(s, "copy", "PPSA23000", "");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->state, "done");
    // a title with no entry of this kind
    EXPECT_EQ(hui::ps5cc::orig_status_for(s, "unpack", "PPSA23000", ""), nullptr);
    // a job that does not know its title id is found by its name
    const hui::ps5cc::OrigItem *c = hui::ps5cc::orig_status_for(s, "convert", "", "ASTRO BOT");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->state, "failed");
    // a known id beats the name: another game's entry is never shown
    EXPECT_EQ(hui::ps5cc::orig_status_for(s, "convert", "PPSA00000", "ASTRO BOT"), nullptr);
    // nothing known about the job: nothing shown
    EXPECT_EQ(hui::ps5cc::orig_status_for(s, "convert", "", ""), nullptr);
    EXPECT_EQ(hui::ps5cc::orig_status_for(OrigStatus{}, "convert", "PPSA23000", ""), nullptr);
}

TEST(Ps5ccOrig, TheCardShowsTheMessageButNotWhileTheJobStillRunsAndTheEntryWaits)
{
    const OrigStatus s = hui::ps5cc::parse_orig_status(kStatus);
    EXPECT_EQ(hui::ps5cc::orig_card_text(s.items[0], false), "Das Original wird verschoben …");
    EXPECT_EQ(hui::ps5cc::orig_card_text(s.items[0], true), "Das Original wird verschoben …");
    hui::ps5cc::OrigItem waiting = s.items[0];
    waiting.state = "waiting";
    waiting.message = "Das Original wird nach dem Prüfen des Ergebnisses verschoben.";
    EXPECT_EQ(hui::ps5cc::orig_card_text(waiting, true), "");
    EXPECT_EQ(hui::ps5cc::orig_card_text(waiting, false), waiting.message);
    // no message, no words
    EXPECT_EQ(hui::ps5cc::orig_card_text(s.items[3], false), "");
}

} // namespace
