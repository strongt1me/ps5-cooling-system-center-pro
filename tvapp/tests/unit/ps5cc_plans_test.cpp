// ps5cc - Tests for the game tools' plans: parsing the console app's answers, the rules, the start
// bodies.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/plans.hpp"

#include <gtest/gtest.h>

namespace
{

using hui::ps5cc::CheckReply;
using hui::ps5cc::Game;
using hui::ps5cc::Plan;
using hui::ps5cc::PlanChoice;
using hui::ps5cc::PlanReply;
using hui::ps5cc::Tool;

PlanReply reply(Tool tool, const char *json)
{
    PlanReply r;
    r.tool = tool;
    r.id = "PPSA01325";
    r.ok = true;
    r.json = json;
    return r;
}

// gamecopy.c's answer, shortened: the internal SSD too small, the source's own USB disk, a free M.2
// and a FAT32 stick.
constexpr const char *kCopy = R"({"ok":true,"title_id":"PPSA01325","name":"ASTRO BOT",
  "source":"/mnt/usb0/homebrew/PPSA01325","kind":"folder","size_bytes":31200000000,"files":2312,
  "largest_bytes":6000000000,"skipped":2,"running":false,"busy":false,"other_busy":false,"targets":[
  {"mount":"/data","label":"Interne SSD","fs":"ufs","free_bytes":24000000000,"source_here":false,
   "fat32_too_big":false,"enough_space":false,
   "homebrew":{"path":"/data/homebrew/PPSA01325","exists":false,"enough_space":false},
   "backup":{"path":"/data/PS5-Sicherung/Spiele/PPSA01325","exists":false,"enough_space":false}},
  {"mount":"/mnt/usb0","label":"USB-Laufwerk 1","fs":"exfatfs","free_bytes":1210000000000,"source_here":true,
   "fat32_too_big":false,"enough_space":true,
   "homebrew":{"path":"/mnt/usb0/homebrew/PPSA01325","exists":true,"enough_space":true},
   "backup":{"path":"/mnt/usb0/PS5-Sicherung/Spiele/PPSA01325","exists":false,"enough_space":true}},
  {"mount":"/mnt/ext0","label":"M.2-Laufwerk","fs":"exfatfs","free_bytes":640000000000,"source_here":false,
   "fat32_too_big":false,"enough_space":true,
   "homebrew":{"path":"/mnt/ext0/homebrew/PPSA01325","exists":false,"enough_space":true},
   "backup":{"path":"/mnt/ext0/PS5-Sicherung/Spiele/PPSA01325","exists":false,"unfinished":true,
             "unfinished_bytes":2100000000,"enough_space":true}},
  {"mount":"/mnt/usb1","label":"USB-Stick","fs":"msdosfs","free_bytes":59000000000,"source_here":false,
   "fat32_too_big":true,"enough_space":true}]})";

TEST(Ps5ccPlans, CopyPlanIsParsed)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::copy, reply(Tool::copy, kCopy));
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.id, "PPSA01325");
    EXPECT_EQ(p.name, "ASTRO BOT");
    EXPECT_EQ(p.files, 2312u);
    EXPECT_EQ(p.skipped, 2u);
    ASSERT_EQ(p.drives.size(), 4u);
    EXPECT_EQ(p.drives[2].label, "M.2-Laufwerk");
    EXPECT_TRUE(p.drives[2].copy[1].unfinished);
    EXPECT_EQ(p.drives[2].copy[1].unfinished_bytes, 2100000000u);
    EXPECT_EQ(p.drives[1].copy[0].path, "/mnt/usb0/homebrew/PPSA01325");
    EXPECT_FALSE(p.drives[3].copy[0].present);
}

TEST(Ps5ccPlans, CopyRefusesAsTheWebApp)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::copy, reply(Tool::copy, kCopy));
    PlanChoice c;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[3]),
              "FAT32 nimmt keine Datei über 4 GB auf");
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[0]),
              "Zu wenig Platz: Die Kopie braucht 31,2 GB");
    // The drive is fine as such; only "playable" would land on the original.
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[1]), "");
    c.mode = 0;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[1]), "Hier liegt das Original");
    c.mode = 1;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[1]), "");
}

TEST(Ps5ccPlans, CopyStartsOnlyWithModeAndDrive)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::copy, reply(Tool::copy, kCopy));
    PlanChoice c = hui::ps5cc::first_choice(p, nullptr);
    EXPECT_EQ(c.drive, 2); // the first drive that is not the source's and has room
    EXPECT_EQ(c.mode, -1); // as on the web page, "playable or backup" is the person's choice
    EXPECT_FALSE(hui::ps5cc::ready(p, c));
    c.mode = 1;
    EXPECT_TRUE(hui::ps5cc::ready(p, c));
    EXPECT_EQ(hui::ps5cc::target_of(p, c), "/mnt/ext0/PS5-Sicherung/Spiele/PPSA01325");
    EXPECT_EQ(hui::ps5cc::start_body(p, c),
              R"({"id":"PPSA01325","target":"/mnt/ext0","mode":"backup"})");
}

TEST(Ps5ccPlans, CopyRunRefusals)
{
    PlanReply r = reply(Tool::copy, R"({"title_id":"X","running":true,"busy":true})");
    EXPECT_EQ(
        hui::ps5cc::run_blocked(hui::ps5cc::parse_plan(Tool::copy, r)),
        "Das Spiel läuft gerade. Bitte erst beenden: Es schreibt womöglich in seinen Ordner.");
    r.json = R"({"title_id":"X","other_busy":true})";
    EXPECT_EQ(hui::ps5cc::run_blocked(hui::ps5cc::parse_plan(Tool::copy, r)),
              "Es läuft gerade eine Konvertierung. Kopieren, Konvertieren und Verschieben laufen "
              "nicht gleichzeitig.");
}

constexpr const char *kConvert = R"({"ok":true,"title_id":"CUSA05041","name":"Two Point Hospital",
  "source":"/mnt/usb0/homebrew/CUSA05041","kind":"folder","ps4":true,"stem":"CUSA05041","raw_bytes":12800000000,
  "stream_risk":{"risky":true,"data_bytes":52000000000,"big_data":true,"container_bytes":9000000000,
                 "containers":["a.pak","b.pak"],"stream_dir":"streaming","stream_dir_bytes":3000000000},
  "ffpkg_bytes":13100000000,"ffpkg_error":"Ein Ordner hat zu viele Einträge.","files":800,
  "ops":["ffpfsc","zip","ffpkg","exfat"],"running":false,"busy":false,"other_busy":false,"targets":[
  {"mount":"/mnt/usb1","label":"USB-Stick","fs":"msdosfs","free_bytes":59000000000,"source_here":false,
   "fat32_too_big":true,"fat32_too_big_ffpkg":true,"enough_space":true},
  {"mount":"/mnt/ext0","label":"M.2-Laufwerk","fs":"exfatfs","free_bytes":640000000000,"source_here":false,
   "fat32_too_big":false,"fat32_too_big_ffpkg":false,"enough_space":true,
   "homebrew":{"dir":"/mnt/ext0/homebrew","ffpfsc":{"path":"/mnt/ext0/homebrew/CUSA05041.ffpfsc","exists":true},
               "ffpkg":{"path":"/mnt/ext0/homebrew/CUSA05041.ffpkg","exists":false},
               "exfat":{"path":"/mnt/ext0/homebrew/CUSA05041.exfat","exists":false,"enough_space":true}},
   "backup":{"dir":"/mnt/ext0/PS5-Sicherung/Spiele",
             "ffpfsc":{"path":"/mnt/ext0/PS5-Sicherung/Spiele/CUSA05041.ffpfsc","exists":false,"enough_space":true}}}]})";

TEST(Ps5ccPlans, ConvertKeepsOnlyKnownFormatsAndSkipsABrokenFfpkg)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    ASSERT_TRUE(p.ok);
    EXPECT_TRUE(p.ps4);
    ASSERT_EQ(p.ops.size(), 3u);
    EXPECT_EQ(p.ops[1], "ffpkg");
    Game g;
    g.ampr_emu = true;
    const PlanChoice c = hui::ps5cc::first_choice(p, &g);
    EXPECT_EQ(c.op, 0);    // ffpfsc
    EXPECT_EQ(c.drive, 1); // the FAT32 stick is skipped
    EXPECT_TRUE(c.ampr);
}

TEST(Ps5ccPlans, ConvertRulesFollowTheFormat)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    PlanChoice c;
    c.mode = 0;
    c.op = 0;
    c.drive = 1;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[1]), "Dort gibt es diese Datei schon");
    c.op = 2;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[1]), "");
    EXPECT_EQ(hui::ps5cc::target_of(p, c), "/mnt/ext0/homebrew/CUSA05041.exfat");
    EXPECT_TRUE(hui::ps5cc::ready(p, c));
    c.op = 1; // ffpkg is known to fail for this folder
    EXPECT_FALSE(hui::ps5cc::ready(p, c));
}

TEST(Ps5ccPlans, ConvertBodyHasNoAmprIndexForAPs4Title)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    PlanChoice c;
    c.mode = 0;
    c.op = 2;
    c.drive = 1;
    c.ampr = true;
    EXPECT_EQ(
        hui::ps5cc::start_body(p, c),
        R"({"id":"CUSA05041","op":"exfat","target":"/mnt/ext0","mode":"homebrew","ampr_index":false,"verify":"full","name":""})");
}

TEST(Ps5ccPlans, ConvertBodyAsksForTheQuickCheck)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    PlanChoice c;
    c.mode = 0;
    c.op = 2;
    c.drive = 1;
    c.quick = true;
    EXPECT_EQ(
        hui::ps5cc::start_body(p, c),
        R"({"id":"CUSA05041","op":"exfat","target":"/mnt/ext0","mode":"homebrew","ampr_index":false,"verify":"quick","name":""})");
}

TEST(Ps5ccPlans, ConvertBodyCarriesTheNameTemplate)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    PlanChoice c;
    c.mode = 0;
    c.op = 2;
    c.drive = 1;
    c.name = hui::ps5cc::kNameTemplate;
    EXPECT_EQ(
        hui::ps5cc::start_body(p, c),
        R"({"id":"CUSA05041","op":"exfat","target":"/mnt/ext0","mode":"homebrew","ampr_index":false,"verify":"full","name":"{titel} [{id}] v{version}"})");
    // the check asks with the same choice
    EXPECT_EQ(
        hui::ps5cc::check_body(p, c),
        R"({"id":"CUSA05041","op":"exfat","target":"/mnt/ext0","mode":"homebrew","name":"{titel} [{id}] v{version}"})");
    // and the queue takes the start's body plus its action
    const std::string add = hui::ps5cc::queue_add_body(p, c);
    EXPECT_NE(add.find(R"("action":"add")"), std::string::npos);
    EXPECT_NE(add.find(R"("op":"exfat")"), std::string::npos);
    EXPECT_NE(add.find(R"("name":"{titel} [{id}] v{version}")"), std::string::npos);
}

TEST(Ps5ccPlans, NoCheckUntilFormatKindAndDriveAreChosen)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    PlanChoice c;
    EXPECT_EQ(hui::ps5cc::check_body(p, c), "");
    c.op = 2;
    c.drive = 1;
    EXPECT_EQ(hui::ps5cc::check_body(p, c), "") << "the kind (mode) is missing";
    c.mode = 1;
    EXPECT_NE(hui::ps5cc::check_body(p, c), "");
    c.drive = 9;
    EXPECT_EQ(hui::ps5cc::check_body(p, c), "") << "a drive that is not in the plan";
}

TEST(Ps5ccPlans, ConvertIsNotBlockedByARunningGameOrJobHere)
{
    // These are the check's rows now (a busy job still lets the conversion wait in the queue).
    Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    p.running = true;
    p.busy = true;
    p.other_busy = true;
    EXPECT_EQ(hui::ps5cc::run_blocked(p), "");
    p.ops.clear();
    EXPECT_EQ(hui::ps5cc::run_blocked(p), "Für dieses Spiel bietet die App kein Format an.");
}

TEST(Ps5ccPlans, TheCheckIsParsedWithItsLevels)
{
    CheckReply r;
    r.ok = true;
    r.json =
        R"({"ok":true,"can_start":false,"can_queue":true,"target":"/mnt/usb0/homebrew/X.ffpfsc","stem":"X",
      "checks":[{"id":"source","level":"ok","text":"Quelle lesbar: 12481 Dateien"},
                {"id":"exists","level":"warn","text":"Gelb","hint":"Ein Hinweis"},
                {"id":"fat32","level":"error","text":"Rot"},
                {"id":"x","level":"weird","text":"Unbekannte Stufe"},
                {"id":"empty","level":"ok","text":""}]})";
    const hui::ps5cc::ConvertCheck c = hui::ps5cc::parse_check(r);
    ASSERT_TRUE(c.ok);
    EXPECT_FALSE(c.can_start);
    EXPECT_TRUE(c.can_queue);
    EXPECT_EQ(c.target, "/mnt/usb0/homebrew/X.ffpfsc");
    ASSERT_EQ(c.rows.size(), 4u) << "a row without text is dropped";
    EXPECT_EQ(c.rows[1].level, "warn");
    EXPECT_EQ(c.rows[1].hint, "Ein Hinweis");
    EXPECT_EQ(c.rows[2].level, "error");
    EXPECT_EQ(c.rows[3].level, "ok") << "a level the screen does not know is read as a note";

    CheckReply bad;
    bad.error = "Die App hat nicht rechtzeitig geantwortet.";
    const hui::ps5cc::ConvertCheck f = hui::ps5cc::parse_check(bad);
    EXPECT_FALSE(f.ok);
    EXPECT_EQ(f.error, "Die App hat nicht rechtzeitig geantwortet.");
    EXPECT_TRUE(f.rows.empty());
    EXPECT_FALSE(f.can_start);

    CheckReply garbage;
    garbage.ok = true;
    garbage.json = "nicht json";
    EXPECT_FALSE(hui::ps5cc::parse_check(garbage).ok);
}

TEST(Ps5ccPlans, TheQueueIsParsedAndCounted)
{
    const hui::ps5cc::ConvertQueue q = hui::ps5cc::parse_queue(
        R"({"ok":true,"running":true,"running_id":2,"items":[
      {"qid":1,"id":"PPSA01","name":"A","op":"ffpfsc","mode":"homebrew","mount":"/mnt/usb0","target":"/mnt/usb0/homebrew/A.ffpfsc","state":"done","output_bytes":21700000000,"elapsed_s":2460},
      {"qid":2,"id":"PPSA02","name":"B","op":"ffpfsc","state":"running"},
      {"qid":3,"id":"PPSA03","name":"C","op":"exfat","state":"waiting"},
      {"qid":4,"id":"PPSA04","name":"D","op":"ffpkg","state":"failed","message":"Die Quelle hat sich geändert."},
      {"qid":5,"id":"PPSA05","name":"E","op":"ffpkg","state":"cancelled"},
      {"qid":0,"id":"X","name":"ohne qid","state":"waiting"}]})");
    ASSERT_TRUE(q.ok);
    EXPECT_TRUE(q.running);
    EXPECT_EQ(q.running_id, 2);
    ASSERT_EQ(q.items.size(), 5u) << "an entry without a number is dropped";
    EXPECT_EQ(q.open, 2);
    EXPECT_EQ(q.done, 1);
    EXPECT_EQ(q.failed, 2) << "failed and cancelled count together";
    EXPECT_EQ(q.items[0].output_bytes, 21700000000ull);
    EXPECT_EQ(q.items[3].message, "Die Quelle hat sich geändert.");
    EXPECT_EQ(hui::ps5cc::queue_summary(q), "2 offen · 1 fertig · 2 mit Fehler");
    EXPECT_FALSE(hui::ps5cc::parse_queue("kaputt").ok);
    EXPECT_EQ(hui::ps5cc::queue_summary(hui::ps5cc::ConvertQueue{}), "0 offen");
}

TEST(Ps5ccPlans, QueueActionsAreBuiltAsTheAppExpectsThem)
{
    EXPECT_EQ(hui::ps5cc::queue_action_body("start"), R"({"action":"start"})");
    EXPECT_EQ(hui::ps5cc::queue_action_body("remove", 7), R"({"action":"remove","qid":7})");
    EXPECT_EQ(hui::ps5cc::queue_action_body("clear"), R"({"action":"clear"})");
}

TEST(Ps5ccPlans, StepTimesAreWrittenAsMinutesAndHours)
{
    EXPECT_EQ(hui::ps5cc::duration_text(0), "0:00");
    EXPECT_EQ(hui::ps5cc::duration_text(-5), "0:00");
    EXPECT_EQ(hui::ps5cc::duration_text(42000), "0:42");
    EXPECT_EQ(hui::ps5cc::duration_text(41500), "0:42") << "rounded to the second";
    EXPECT_EQ(hui::ps5cc::duration_text(966000), "16:06");
    EXPECT_EQ(hui::ps5cc::duration_text(3599000), "59:59");
    EXPECT_EQ(hui::ps5cc::duration_text(3731000), "1:02:11");
}

TEST(Ps5ccPlans, TheStepsOfAJobHaveNamesTimesAndShare)
{
    hui::ps5cc::Job job;
    job.op = "ffpfsc";
    job.total = 200.0;
    job.done = 102.0;
    job.stages = {{"scan", "done", 42000.0},
                  {"write", "now", 966000.0},
                  {"verify", "todo", 0.0},
                  {"rename", "todo", 0.0}};
    std::vector<hui::ps5cc::StageText> t = hui::ps5cc::stage_texts(job);
    ASSERT_EQ(t.size(), 4u);
    EXPECT_EQ(t[0].label, "Vermessen");
    EXPECT_EQ(t[0].detail, "0:42");
    EXPECT_EQ(t[1].label, "Komprimieren");
    EXPECT_EQ(t[1].detail, "16:06 · 51 %");
    EXPECT_EQ(t[2].label, "Prüfen");
    EXPECT_EQ(t[2].detail, "noch offen");
    EXPECT_EQ(t[3].label, "Umbenennen");
    // an uncompressed format writes, it does not compress
    job.op = "exfat";
    EXPECT_EQ(hui::ps5cc::stage_texts(job)[1].label, "Schreiben");
    // where the job ended
    job.stages = {{"scan", "done", 42000.0},
                  {"write", "done", 1870000.0},
                  {"verify", "stopped", 91000.0},
                  {"rename", "todo", 0.0}};
    t = hui::ps5cc::stage_texts(job);
    EXPECT_EQ(t[1].detail, "31:10");
    EXPECT_EQ(t[2].detail, "1:31 · hier beendet");
    // the app sent none (an older console app): no steps
    job.stages.clear();
    EXPECT_TRUE(hui::ps5cc::stage_texts(job).empty());
}

// The debug FPKG (Dump Forge): one format, four levels; the engine may be missing.
constexpr const char *kConvertFpkg =
    R"({"ok":true,"title_id":"PPSA02664","name":"Alex Kidd in Miracle World DX",
  "source":"/mnt/ext1/etaHEN/games/Alex Kidd.exfat","kind":"image","ps4":false,"raw_bytes":4871608285,
  "files":1,"ops":["ffpfsc","fpkg"],"dumpforge":{"ok":true,"installed":false,"folder":"/data/PS5-Cooling-Center/dumpforge","running":false},
  "running":false,"busy":false,"other_busy":false,"targets":[
  {"mount":"/mnt/usb1","label":"USB-Stick","fs":"msdosfs","free_bytes":59000000000,"source_here":false,
   "fat32_too_big":true,"fat32_too_big_ffpkg":true,"enough_space":true,
   "homebrew":{"dir":"/mnt/usb1/homebrew","fpkg":{"path":"/mnt/usb1/FPKG-Games/Alex.pkg","exists":false,"enough_space":true}}},
  {"mount":"/mnt/ext0","label":"M.2-Laufwerk","fs":"exfatfs","free_bytes":640000000000,"source_here":false,
   "fat32_too_big":false,"fat32_too_big_ffpkg":false,"enough_space":true,
   "homebrew":{"dir":"/mnt/ext0/homebrew","fpkg":{"path":"/mnt/ext0/FPKG-Games/Alex.pkg","exists":false,"enough_space":true}},
   "backup":{"dir":"/mnt/ext0/PS5-Sicherung/Spiele","fpkg":{"path":"/mnt/ext0/PS5-Sicherung/Spiele/Alex.pkg","exists":true,"enough_space":true}}}]})";

TEST(Ps5ccPlans, TheDebugFpkgIsAFormatWithFourLevelsAndAnEngine)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvertFpkg));
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.ops.size(), 2u);
    EXPECT_EQ(p.ops[1], "fpkg");
    EXPECT_TRUE(p.dumpforge.known);
    EXPECT_FALSE(p.dumpforge.installed);
    EXPECT_EQ(p.dumpforge.folder, "/data/PS5-Cooling-Center/dumpforge");

    // each level is an op of its own for the console app
    PlanChoice c;
    c.mode = 0;
    c.op = hui::ps5cc::kOpFpkg;
    c.drive = 1;
    EXPECT_EQ(hui::ps5cc::op_key(c), "fpkg");
    c.level = 1;
    EXPECT_EQ(hui::ps5cc::op_key(c), "fpkg-bal");
    c.level = 2;
    EXPECT_EQ(hui::ps5cc::op_key(c), "fpkg-min");
    c.level = 3;
    EXPECT_EQ(hui::ps5cc::op_key(c), "fpkg-raw");
    c.op = 0;
    EXPECT_EQ(hui::ps5cc::op_key(c), "ffpfsc");
    c.op = -1;
    EXPECT_EQ(hui::ps5cc::op_key(c), "");

    EXPECT_TRUE(hui::ps5cc::is_fpkg_op("fpkg-min"));
    EXPECT_FALSE(hui::ps5cc::is_fpkg_op("ffpkg"));
    EXPECT_TRUE(hui::ps5cc::squeezes("ffpfsc"));
    EXPECT_TRUE(hui::ps5cc::squeezes("fpkg"));
    EXPECT_TRUE(hui::ps5cc::squeezes("fpkg-bal"));
    EXPECT_FALSE(hui::ps5cc::squeezes("fpkg-raw"));
    EXPECT_FALSE(hui::ps5cc::squeezes("exfat"));
    EXPECT_EQ(hui::ps5cc::op_label("fpkg-bal"), "FPKG · ausgewogen");
    EXPECT_EQ(hui::ps5cc::op_label("fpkg-raw"), "FPKG · unkomprimiert");
    EXPECT_EQ(hui::ps5cc::op_label("exfat"), "exfat") << "the other ops stay as the app names them";
}

TEST(Ps5ccPlans, TheDebugFpkgBodyCarriesTheLevelAndNoAmprIndex)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvertFpkg));
    PlanChoice c;
    c.mode = 0;
    c.op = hui::ps5cc::kOpFpkg;
    c.level = 2;
    c.drive = 1;
    c.ampr = true;
    c.quick = true;
    EXPECT_EQ(
        hui::ps5cc::start_body(p, c),
        R"({"id":"PPSA02664","op":"fpkg-min","target":"/mnt/ext0","mode":"homebrew","ampr_index":false,"verify":"quick","name":""})");
    EXPECT_EQ(
        hui::ps5cc::check_body(p, c),
        R"({"id":"PPSA02664","op":"fpkg-min","target":"/mnt/ext0","mode":"homebrew","name":""})");
    EXPECT_EQ(hui::ps5cc::target_of(p, c), "/mnt/ext0/FPKG-Games/Alex.pkg");
    EXPECT_NE(hui::ps5cc::queue_add_body(p, c).find(R"("op":"fpkg-min")"), std::string::npos);
}

TEST(Ps5ccPlans, ACompressedFpkgMayGoToAFat32DriveButAnUncompressedOneMayNot)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvertFpkg));
    PlanChoice c;
    c.mode = 0;
    c.op = hui::ps5cc::kOpFpkg;
    c.drive = 0;
    // it gets smaller than the game: the check names the drive, the list does not turn it away
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[0]), "");
    c.level = hui::ps5cc::kLevelRaw;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[0]),
              "FAT32 nimmt keine Datei über 4 GB auf");
    // ffpfsc stays as it was
    c.op = 0;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[0]),
              "FAT32 nimmt keine Datei über 4 GB auf");
    // a target that exists is refused for the FPKG as for the others
    c.op = hui::ps5cc::kOpFpkg;
    c.level = 0;
    c.mode = 1;
    c.drive = 1;
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, c, p.drives[1]), "Dort gibt es diese Datei schon");
}

TEST(Ps5ccPlans, TheStepOfACompressedFpkgIsCalledCompressing)
{
    hui::ps5cc::Job job;
    job.total = 100.0;
    job.stages = {{"scan", "done", 1000.0},
                  {"write", "now", 5000.0},
                  {"verify", "todo", 0.0},
                  {"rename", "todo", 0.0}};
    job.op = "fpkg";
    EXPECT_EQ(hui::ps5cc::stage_texts(job)[1].label, "Komprimieren");
    job.op = "fpkg-raw";
    EXPECT_EQ(hui::ps5cc::stage_texts(job)[1].label, "Schreiben");
}

TEST(Ps5ccPlans, StreamRiskReasonsInWords)
{
    const Plan p = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    const std::vector<std::string> why = hui::ps5cc::stream_reasons(p);
    ASSERT_EQ(why.size(), 3u);
    EXPECT_EQ(why[0], "Es hat sehr viele Daten (52,0 GB).");
    EXPECT_EQ(why[1], "Es enthält große Streaming-Container (a.pak, b.pak, zusammen 9,0 GB).");
    EXPECT_EQ(why[2], "Es hat einen Streaming-Ordner („streaming“, 3,0 GB).");
}

TEST(Ps5ccPlans, UnsignedExecutablesAreReadAndNamed)
{
    // an answer without the field (an older console app): nothing to say
    const Plan none = hui::ps5cc::parse_plan(Tool::convert, reply(Tool::convert, kConvert));
    EXPECT_EQ(none.unsigned_exec.count, 0u);
    EXPECT_EQ(none.unsigned_exec.refused, 0u);
    EXPECT_EQ(hui::ps5cc::unsigned_names(none.unsigned_exec), "");

    const Plan p = hui::ps5cc::parse_plan(
        Tool::convert,
        reply(
            Tool::convert,
            R"({"ok":true,"title_id":"PPSA01325","name":"X","kind":"folder","ops":["ffpfsc"],"targets":[],
                  "unsigned_exec":{"count":6,"refused":1,"capped":false,"bytes":148000000,
                  "files":["eboot.bin","sce_module/a.prx","sce_module/b.prx","fakelib/c.sprx","d.sprx","e.sprx","f.sprx","g.sprx"]}})"));
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.unsigned_exec.count, 6u);
    EXPECT_EQ(p.unsigned_exec.refused, 1u);
    EXPECT_EQ(p.unsigned_exec.files.size(), 8u);
    // four names, then "…" when there are more executables than named
    EXPECT_EQ(hui::ps5cc::unsigned_names(p.unsigned_exec),
              "eboot.bin, sce_module/a.prx, sce_module/b.prx, fakelib/c.sprx, …");
    Plan few = p;
    few.unsigned_exec.count = 4;
    EXPECT_EQ(hui::ps5cc::unsigned_names(few.unsigned_exec),
              "eboot.bin, sce_module/a.prx, sce_module/b.prx, fakelib/c.sprx");
    // only executables that cannot be signed: no names, no "…"
    Plan odd;
    odd.unsigned_exec.refused = 2;
    EXPECT_EQ(hui::ps5cc::unsigned_names(odd.unsigned_exec), "");
}

constexpr const char *kStorage = R"({"ok":true,"op":"move","title_id":"PPSA01862",
  "source":"/mnt/usb0/homebrew/PPSA01862.ffpfs","source_dir":"/mnt/usb0/homebrew","source_type":"image",
  "image_type":"pfs","size_bytes":22000000000,"running":false,"destinations":[
  {"path":"/mnt/usb0/homebrew","drive":"USB-Laufwerk 1","fs":"exfatfs","free_bytes":1,"exists":true,"here":true,
   "enough_space":true,"fat32_too_big":false,"target":"/mnt/usb0/homebrew/PPSA01862.ffpfs","target_exists":true},
  {"path":"/mnt/ext0/homebrew","drive":"M.2-Laufwerk","fs":"exfatfs","free_bytes":640000000000,"exists":true,
   "here":false,"enough_space":true,"fat32_too_big":false,"target":"/mnt/ext0/homebrew/PPSA01862.ffpfs",
   "target_exists":false},
  {"path":"/data/homebrew","drive":"Interne SSD","fs":"ufs","free_bytes":1000,"exists":false,"here":false,
   "enough_space":false,"fat32_too_big":false,"target":"/data/homebrew/PPSA01862.ffpfs","target_exists":false}]})";

TEST(Ps5ccPlans, MoveAndUnpackRules)
{
    const Plan move = hui::ps5cc::parse_plan(Tool::move, reply(Tool::move, kStorage));
    ASSERT_EQ(move.drives.size(), 3u);
    EXPECT_EQ(move.drives[1].mount, "/mnt/ext0/homebrew");
    EXPECT_EQ(move.drives[1].drive, "M.2-Laufwerk");
    PlanChoice c = hui::ps5cc::first_choice(move, nullptr);
    EXPECT_EQ(c.drive, 1);
    EXPECT_EQ(hui::ps5cc::drive_blocked(move, c, move.drives[0]), "Hier liegt das Spiel schon");
    EXPECT_EQ(hui::ps5cc::drive_blocked(move, c, move.drives[2]),
              "Zu wenig Platz: Nötig sind mindestens 22,0 GB");
    EXPECT_EQ(hui::ps5cc::target_of(move, c), "/mnt/ext0/homebrew/PPSA01862.ffpfs");
    c.delete_source = true; // only an unpack deletes its source
    EXPECT_EQ(
        hui::ps5cc::start_body(move, c),
        R"({"id":"PPSA01862","op":"move","target":"/mnt/ext0/homebrew","delete_source":false})");

    const Plan unpack = hui::ps5cc::parse_plan(Tool::unpack, reply(Tool::unpack, kStorage));
    EXPECT_EQ(hui::ps5cc::drive_blocked(unpack, c, unpack.drives[0]),
              "Dort gibt es den Ordner PPSA01862 schon");
    EXPECT_EQ(
        hui::ps5cc::start_body(unpack, c),
        R"({"id":"PPSA01862","op":"unpack","target":"/mnt/ext0/homebrew","delete_source":true})");
}

TEST(Ps5ccPlans, ImageUnpackPlanAndInfo)
{
    const char *plan =
        R"({"ok":true,"id":"PPSA03860","name":"Lantern Pass","image":"/mnt/usb0/homebrew/L.ffpfsc",
      "size_bytes":18500000000,"files":900,"largest_bytes":1,"damaged":true,"busy":false,"targets":[
      {"mount":"/mnt/ext0","label":"M.2","fs":"exfatfs","path":"/mnt/ext0/homebrew/PPSA03860","free_bytes":9,
       "exists":false,"enough_space":false,"fat32_too_big":false,"path_too_long":false,"source_here":false}]})";
    const Plan p = hui::ps5cc::parse_plan(Tool::image_unpack, reply(Tool::image_unpack, plan));
    EXPECT_EQ(p.source, "/mnt/usb0/homebrew/L.ffpfsc");
    EXPECT_EQ(hui::ps5cc::run_blocked(p), "Das Abbild ist beschädigt: ein Ordner darin lässt sich "
                                          "nicht lesen. Zuerst „Abbild prüfen“.");
    EXPECT_EQ(hui::ps5cc::drive_blocked(p, PlanChoice{}, p.drives[0]),
              "Nicht genug Platz: nötig sind 18,8 GB.");

    const char *info =
        R"({"ok":true,"id":"PPSA03860","name":"Lantern Pass","format":"ffpfsc","image_bytes":5,
      "can_check":true,"can_unpack":true,"repair":{"state":"fixable","declared":100,"actual":120},
      "last_check":{"when":1792800000,"clean":false,"files":3,"bad_files":1,"container":"Block 7 defekt.",
                    "bad":[{"path":"data/a.bin","offset":4096}]}})";
    const Plan repair = hui::ps5cc::parse_plan(Tool::image_repair, reply(Tool::image_repair, info));
    EXPECT_EQ(repair.repair_state, "fixable");
    EXPECT_EQ(repair.repair_actual, 120u);
    EXPECT_TRUE(hui::ps5cc::ready(repair, PlanChoice{}));
    EXPECT_EQ(hui::ps5cc::start_body(repair, PlanChoice{}),
              R"({"kind":"repair","id":"PPSA03860","target":""})");
    const Plan check = hui::ps5cc::parse_plan(Tool::image_check, reply(Tool::image_check, info));
    EXPECT_TRUE(check.checked);
    EXPECT_FALSE(check.check_clean);
    ASSERT_EQ(check.check_bad.size(), 1u);
    EXPECT_EQ(check.check_bad[0], "data/a.bin");
}

TEST(Ps5ccPlans, RepairRefusesWhatIsFine)
{
    const Plan p = hui::ps5cc::parse_plan(
        Tool::image_repair,
        reply(Tool::image_repair, R"({"id":"X","format":"ffpfsc","repair":{"state":"fine"}})"));
    EXPECT_EQ(hui::ps5cc::run_blocked(p), "Nichts zu reparieren: Die Größe der Datei stimmt.");
    EXPECT_FALSE(hui::ps5cc::ready(p, PlanChoice{}));
}

TEST(Ps5ccPlans, DeletePlanCarriesItsToken)
{
    const char *json =
        R"({"ok":true,"kind":"smp","id":"PPSA01325","name":"ASTRO BOT","path":"/mnt/usb0/x.ffpkg",
      "version":"01.008.000","size_bytes":31200000000,"items":["Die Abbilddatei","Der Eintrag"],"keeps":"",
      "can_delete":true,"confirm":"4f9c2a17"})";
    const Plan p = hui::ps5cc::parse_plan(Tool::erase, reply(Tool::erase, json));
    ASSERT_EQ(p.items.size(), 2u);
    EXPECT_EQ(p.kind, "smp");
    EXPECT_TRUE(hui::ps5cc::ready(p, PlanChoice{}));
    EXPECT_EQ(hui::ps5cc::start_body(p, PlanChoice{}),
              R"({"kind":"game","id":"PPSA01325","confirm":"4f9c2a17"})");

    const Plan no = hui::ps5cc::parse_plan(
        Tool::erase,
        reply(
            Tool::erase,
            R"({"id":"X","can_delete":false,"why":"Das Spiel läuft gerade. Bitte erst beenden.","confirm":""})"));
    EXPECT_EQ(hui::ps5cc::run_blocked(no), "Das Spiel läuft gerade. Bitte erst beenden.");
    EXPECT_FALSE(hui::ps5cc::ready(no, PlanChoice{}));
}

TEST(Ps5ccPlans, AFailedReplyKeepsTheMessage)
{
    PlanReply r;
    r.tool = Tool::copy;
    r.ok = false;
    r.error = "Das Spiel ist nicht in der Liste.";
    const Plan p = hui::ps5cc::parse_plan(Tool::copy, r);
    EXPECT_FALSE(p.ok);
    EXPECT_EQ(hui::ps5cc::run_blocked(p), "Das Spiel ist nicht in der Liste.");

    r.ok = true;
    r.json = "{nicht json";
    EXPECT_EQ(hui::ps5cc::parse_plan(Tool::copy, r).error, "Die Antwort der App ist nicht lesbar.");
}

TEST(Ps5ccPlans, TheMenuShowsWhatTheGameAllows)
{
    Game installed;
    installed.format = "pkg";
    installed.path = "/user/app/X";
    ASSERT_EQ(hui::ps5cc::tools_for(installed).size(), 1u);
    EXPECT_EQ(hui::ps5cc::tools_for(installed)[0], Tool::erase);

    Game folder;
    folder.format = "folder";
    folder.path = "/mnt/usb0/homebrew/X";
    folder.can_copy = folder.can_convert = folder.can_move = true;
    const std::vector<Tool> f = hui::ps5cc::tools_for(folder);
    ASSERT_EQ(f.size(), 4u);
    EXPECT_EQ(f[0], Tool::copy);
    EXPECT_EQ(f[3], Tool::erase);

    Game image;
    image.format = "ffpfsc";
    image.path = "/mnt/usb0/homebrew/X.ffpfsc";
    image.can_copy = image.can_unpack = true;
    const std::vector<Tool> i = hui::ps5cc::tools_for(image);
    const std::vector<Tool> want = {Tool::copy,        Tool::unpack,       Tool::image_unpack,
                                    Tool::image_check, Tool::image_repair, Tool::erase};
    EXPECT_EQ(i, want);
    EXPECT_EQ(hui::ps5cc::job_of(Tool::unpack), hui::ps5cc::JobKind::storage);
    EXPECT_EQ(hui::ps5cc::job_of(Tool::erase), hui::ps5cc::JobKind::deletion);
}

} // namespace
