// ps5cc - Tests for Remote Play: reading the console's state, the PIN's words, the sentence of a
// pairing that failed, and the invented console of the preview.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/data.hpp"
#include "ps5cc/remoteplay.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{

using hui::ps5cc::RpPairing;
using hui::ps5cc::RpState;

// GET /api/v1/remoteplay as the console app answers it (docs of src/remoteplay.c).
constexpr const char *kState =
    R"({"ok":true,"available":true,"firmware":"12.00","per_user":true,)"
    R"("service":{"enabled":true},)"
    R"("users":[{"slot":1,"name":"Max","activated":true,"account_id":"AAAAAAcAAAA=","allowed":true},)"
    R"({"slot":3,"name":"Gast","activated":false,"account_id":"","allowed":null}],)"
    R"("pairing_user":{"slot":1,"via":"foreground"},)"
    R"("devices":[{"slot":1,"client_type":2,"user":"Max"},{"slot":4,"client_type":3,"user":""}],)"
    R"("pairing":{"state":"waiting","pin":"47291853","seconds_left":292,"code":"","err":"","busy":false},)"
    R"("module":{"state":"ready"},)"
    R"("backup":{"dir":"/data/PS5-Cooling-Center/remoteplay-backup","files":3,"last":"x.json"}})";

RpState parsed(const std::string &json)
{
    RpState s;
    EXPECT_TRUE(hui::ps5cc::parse_remoteplay(json, &s));
    return s;
}

TEST(Ps5ccRemotePlay, ReadsTheWholeState)
{
    const RpState s = parsed(kState);
    EXPECT_TRUE(s.loaded);
    EXPECT_TRUE(s.available);
    EXPECT_TRUE(s.per_user);
    EXPECT_EQ(s.firmware, "12.00");
    EXPECT_EQ(s.service, 1);
    ASSERT_EQ(s.users.size(), 2u);
    EXPECT_EQ(s.users[0].name, "Max");
    EXPECT_EQ(s.users[0].slot, 1);
    EXPECT_TRUE(s.users[0].activated);
    EXPECT_EQ(s.users[0].account_id, "AAAAAAcAAAA=");
    EXPECT_EQ(s.users[0].allowed, 1);
    EXPECT_FALSE(s.users[1].activated);
    EXPECT_EQ(s.users[1].account_id, "");
    EXPECT_EQ(s.users[1].allowed, -1); // null: the console could not read it
    EXPECT_EQ(s.pairing_slot, 1);
    ASSERT_EQ(s.devices.size(), 2u);
    EXPECT_EQ(s.devices[0].client_type, 2);
    EXPECT_EQ(s.devices[1].slot, 4);
    EXPECT_EQ(s.devices[1].user, "");
    EXPECT_EQ(s.pairing.state, "waiting");
    EXPECT_EQ(s.pairing.pin, "47291853");
    EXPECT_EQ(s.pairing.seconds_left, 292);
    EXPECT_EQ(s.module, "ready");
    EXPECT_EQ(s.backup_dir, "/data/PS5-Cooling-Center/remoteplay-backup");
}

TEST(Ps5ccRemotePlay, ServiceNullIsUnreadableNotOff)
{
    const RpState s =
        parsed(R"({"ok":true,"available":true,"service":{"enabled":null},"users":[]})");
    EXPECT_EQ(s.service, -1);
    const RpState off =
        parsed(R"({"ok":true,"available":true,"service":{"enabled":false},"users":[]})");
    EXPECT_EQ(off.service, 0);
}

TEST(Ps5ccRemotePlay, RefusesWhatIsNoState)
{
    RpState s;
    EXPECT_FALSE(hui::ps5cc::parse_remoteplay("", &s));
    EXPECT_FALSE(hui::ps5cc::parse_remoteplay("not json", &s));
    EXPECT_FALSE(hui::ps5cc::parse_remoteplay("[1,2]", &s));
    // the answer of a refused post, and the cheap answer about the PIN alone
    EXPECT_FALSE(hui::ps5cc::parse_remoteplay(
        R"({"ok":false,"code":"rp_write_failed","message":"Die Konsole hat die Änderung nicht übernommen."})",
        &s));
    EXPECT_FALSE(hui::ps5cc::parse_remoteplay(
        R"({"ok":true,"pairing":{"state":"idle","pin":"","seconds_left":0,"code":"","err":"","busy":false}})",
        &s));
    EXPECT_FALSE(s.loaded); // untouched
}

TEST(Ps5ccRemotePlay, MissingPartsTakeTheirQuietValues)
{
    const RpState s = parsed(R"({"ok":true,"available":false,"users":[]})");
    EXPECT_FALSE(s.available);
    EXPECT_EQ(s.service, -1);
    EXPECT_EQ(s.pairing_slot, -1);
    EXPECT_EQ(s.pairing.state, "idle");
    EXPECT_EQ(s.module, "idle");
    EXPECT_TRUE(s.devices.empty());
}

TEST(Ps5ccRemotePlay, ThePinIsGroupedInFours)
{
    using hui::ps5cc::pin_grouped;
    EXPECT_EQ(pin_grouped("47291853"), "4729 1853");
    EXPECT_EQ(pin_grouped("00000001"), "0000 0001");
    // anything that is not eight digits stays as it came
    EXPECT_EQ(pin_grouped(""), "");
    EXPECT_EQ(pin_grouped("1234567"), "1234567");
    EXPECT_EQ(pin_grouped("123456789"), "123456789");
    EXPECT_EQ(pin_grouped("1234abcd"), "1234abcd");
}

TEST(Ps5ccRemotePlay, TheCountdownReadsMinutesAndSeconds)
{
    using hui::ps5cc::countdown_text;
    EXPECT_EQ(countdown_text(292), "noch 4:52");
    EXPECT_EQ(countdown_text(300), "noch 5:00");
    EXPECT_EQ(countdown_text(65), "noch 1:05");
    EXPECT_EQ(countdown_text(9), "noch 0:09");
    EXPECT_EQ(countdown_text(0), "noch 0:00");
    EXPECT_EQ(countdown_text(-3), "noch 0:00");
}

TEST(Ps5ccRemotePlay, EveryCodeHasItsSentence)
{
    struct Case
    {
        const char *code;
        const char *text;
    };
    const Case cases[] = {
        {"pin_expired", "Die PIN ist abgelaufen, bevor ein Gerät gekoppelt wurde."},
        {"not_available", "Remote Play ist nicht verfügbar."},
        {"gen_failed", "Die Konsole konnte keine PIN erzeugen."},
        {"pin_invalid", "Das Koppeln ist fehlgeschlagen: Die PIN wurde falsch eingegeben."},
        {"account_invalid", "Das Koppeln ist fehlgeschlagen: Die Konto-ID wurde nicht angenommen."},
        {"aborted", "Das Koppeln ist fehlgeschlagen: Die Konsole hat die Anmeldung beendet."},
        {"refused", "Das Koppeln ist fehlgeschlagen: Die Konsole hat die Anmeldung abgelehnt."},
        {"no_account",
         "Kein Benutzer mit aktiviertem Konto. Aktiviere zuerst das Offline-Konto (Seite "
         "Profil)."},
        {"no_module", "Diese Firmware stellt Remote Play für Programme nicht bereit."},
        {"init_failed", "Remote Play ließ sich nicht starten."},
    };
    for (const Case &c : cases)
    {
        RpPairing p;
        p.state = "failed";
        p.code = c.code;
        p.err = "die Konsole sagt 0x80FC0003"; // the console's own sentence is not shown
        EXPECT_EQ(hui::ps5cc::pairing_note(p), c.text) << c.code;
        p.state = "timeout";
        EXPECT_EQ(hui::ps5cc::pairing_note(p), c.text) << c.code;
    }
}

TEST(Ps5ccRemotePlay, AnUnknownCodeShowsTheConsolesSentence)
{
    RpPairing p;
    p.state = "failed";
    p.code = "something_new";
    p.err = "Die Konsole meldet 0x1234.";
    EXPECT_EQ(hui::ps5cc::pairing_note(p), "Die Konsole meldet 0x1234.");
    p.err.clear();
    EXPECT_EQ(hui::ps5cc::pairing_note(p), "Das Koppeln ist fehlgeschlagen.");
}

TEST(Ps5ccRemotePlay, OnlyAFailedPairingHasANote)
{
    RpPairing p;
    p.code = "pin_invalid"; // a stale code does not make an idle pairing fail
    for (const char *state : {"idle", "waiting", "paired"})
    {
        p.state = state;
        EXPECT_EQ(hui::ps5cc::pairing_note(p), "") << state;
    }
}

TEST(Ps5ccRemotePlay, TheStatusSentenceFollowsTheState)
{
    RpPairing p;
    p.state = "idle";
    EXPECT_EQ(hui::ps5cc::pin_status_text(p),
              "Die PIN gilt 5 Minuten und steht auch auf dem Fernseher.");
    p.state = "waiting";
    EXPECT_EQ(hui::ps5cc::pin_status_text(p),
              "In chiaki-ng oder ps5upload bei „Registrieren“ eingeben. Die PIN steht auch auf "
              "dem Fernseher.");
    p.state = "paired";
    EXPECT_EQ(hui::ps5cc::pin_status_text(p), "Ein Gerät wurde gekoppelt.");
    p.state = "timeout";
    p.code = "pin_expired";
    EXPECT_EQ(hui::ps5cc::pin_status_text(p),
              "Die PIN ist abgelaufen, bevor ein Gerät gekoppelt wurde.");
}

TEST(Ps5ccRemotePlay, WhatKeepsItFromWorking)
{
    RpState s = parsed(kState);
    EXPECT_EQ(hui::ps5cc::state_warning(s), "");
    EXPECT_TRUE(hui::ps5cc::pin_possible(s));

    // no account at all: the sentence the web page says, and no PIN
    s.pairing_slot = -1;
    EXPECT_EQ(hui::ps5cc::state_warning(s),
              "Kein Benutzer hat ein aktiviertes Konto: ohne Konto lässt sich kein Gerät "
              "koppeln. Aktiviere es hier (Offline aktivieren) oder auf der Seite Profil.");
    EXPECT_FALSE(hui::ps5cc::pin_possible(s));

    // the firmware has no Remote Play for programs
    s = parsed(kState);
    s.module = "missing";
    EXPECT_EQ(hui::ps5cc::state_warning(s),
              "Diese Firmware stellt Remote Play für Programme nicht bereit.");
    EXPECT_FALSE(hui::ps5cc::pin_possible(s));

    // the registry out of reach (no jailbreak) comes first
    s = parsed(kState);
    s.available = false;
    s.module = "missing";
    s.pairing_slot = -1;
    EXPECT_EQ(hui::ps5cc::state_warning(s),
              "Die Registry der Konsole ist nicht erreichbar. Läuft der Jailbreak?");
    EXPECT_FALSE(hui::ps5cc::pin_possible(s));
}

TEST(Ps5ccRemotePlay, ThePinPollIsMergedIntoTheState)
{
    const std::string pin =
        R"({"ok":true,"pairing":{"state":"paired","pin":"","seconds_left":0,"code":"","err":"","busy":false}})";
    ASSERT_TRUE(hui::ps5cc::remoteplay_waiting(kState));
    const std::string merged = hui::ps5cc::merge_pairing(kState, pin);
    EXPECT_FALSE(hui::ps5cc::remoteplay_waiting(merged));
    const RpState s = parsed(merged);
    EXPECT_EQ(s.pairing.state, "paired");
    // everything else of the state stays
    EXPECT_EQ(s.users.size(), 2u);
    EXPECT_EQ(s.devices.size(), 2u);
    EXPECT_EQ(s.service, 1);
    EXPECT_EQ(s.backup_dir, "/data/PS5-Cooling-Center/remoteplay-backup");
}

TEST(Ps5ccRemotePlay, AMergeThatCannotBeDoneChangesNothing)
{
    const std::string pin = R"({"ok":true,"pairing":{"state":"idle"}})";
    EXPECT_EQ(hui::ps5cc::merge_pairing("", pin), "");
    EXPECT_EQ(hui::ps5cc::merge_pairing(kState, ""), kState);
    EXPECT_EQ(hui::ps5cc::merge_pairing(kState, R"({"ok":false})"), kState);
    EXPECT_EQ(hui::ps5cc::merge_pairing(kState, R"({"ok":true})"), kState);
    EXPECT_FALSE(hui::ps5cc::remoteplay_waiting(""));
    EXPECT_FALSE(hui::ps5cc::remoteplay_waiting("{}"));
}

// The preview's invented console: a switch, the user's permission, a PIN that counts down and ends
// in an error, and a second one that pairs. It stands in for the console in the page's tour, so it
// has to behave like the contract.
TEST(Ps5ccRemotePlay, ThePreviewConsoleFollowsTheContract)
{
    using namespace hui::ps5cc;
    Source &src = source();
    Snapshot snap;
    std::uint64_t seen = 0;
    const auto state = [&]
    {
        src.fetch(snap, seen);
        RpState s;
        EXPECT_TRUE(parse_remoteplay(snap.remoteplay.state, &s));
        return s;
    };
    // whatever an earlier test left: no PIN under way, then a quiet start
    src.remoteplay_post("pin/cancel", "{}");
    for (int i = 0; i < 200; ++i)
        src.tick(0.01f);
    src.remoteplay_post("pin/cancel", "{}");
    ASSERT_TRUE(state().available);
    EXPECT_EQ(state().pairing.state, "idle");

    src.remoteplay_post("service", R"({"enabled":true})");
    EXPECT_EQ(state().service, 1);
    src.remoteplay_post("service", R"({"enabled":false})");
    EXPECT_EQ(state().service, 0);

    // the permission of a user with an activated account
    const RpState before = state();
    ASSERT_FALSE(before.users.empty());
    const int slot = before.users[0].slot;
    const int was = before.users[0].allowed;
    ASSERT_GE(was, 0);
    src.remoteplay_post("user", "{\"slot\":" + std::to_string(slot) +
                                    ",\"enabled\":" + (was == 1 ? "false" : "true") + "}");
    EXPECT_EQ(state().users[0].allowed, was == 1 ? 0 : 1);

    // a PIN takes a moment (the post is pending), then waits with 8 digits and counts down
    src.remoteplay_post("pin", "{}");
    src.fetch(snap, seen);
    EXPECT_EQ(snap.remoteplay.pending, 1);
    for (int i = 0; i < 150; ++i)
        src.tick(0.01f); // 1.5 s
    const RpState waiting = state();
    EXPECT_EQ(snap.remoteplay.pending, 0);
    EXPECT_EQ(waiting.pairing.state, "waiting");
    EXPECT_EQ(waiting.pairing.pin.size(), 8u);
    EXPECT_GT(waiting.pairing.seconds_left, 290);
    EXPECT_LE(waiting.pairing.seconds_left, 300);
    EXPECT_EQ(waiting.service,
              1); // making a PIN switches the service on first, as the console does
    for (int i = 0; i < 100; ++i)
        src.tick(0.01f); // 1 s more
    EXPECT_LT(state().pairing.seconds_left, waiting.pairing.seconds_left);

    src.remoteplay_post("pin/cancel", "{}");
    EXPECT_EQ(state().pairing.state, "idle");
}

} // namespace
