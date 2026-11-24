// ps5cc - Remote Play: the console's state as the app answers it, and the words the page uses.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GET /api/v1/remoteplay (src/remoteplay.c of the console app, 24.11.2026) and the answers of its
// four POSTs (service, user, pin, pin/cancel) are the same object. The source passes it on as the
// text it came as (RemotePlayData::state); the Remote Play page reads it with parse_remoteplay().
// The PIN, its countdown and the sentence for a failed pairing are the web app's (web/app.js,
// "Remote Play (remoteplay.c, 24.11.2026)"). No OpenGL here: the unit tests use it.

#pragma once

#include <string>
#include <vector>

namespace hui::ps5cc
{

// One user of the console as the app lists them (users[]).
struct RpUser
{
    int slot = 0;
    std::string name;
    bool activated = false; // the account is activated (offline account or real one)
    std::string account_id; // base64 of the 8-byte id, "" when none
    int allowed = -1;       // Remote Play allowed for this user: 1, 0, or -1 (null: unreadable)
};

// One paired device (devices[]); the console keeps only the user and the kind, no name.
struct RpDevice
{
    int slot = 0;
    int client_type = 0;
    std::string user; // "" when the user is not known
};

// The pairing with a PIN (pairing). state: idle, waiting, paired, failed or timeout.
struct RpPairing
{
    std::string state = "idle";
    std::string pin; // 8 digits while waiting
    int seconds_left = 0;
    std::string code;  // why it failed or ran out (see pairing_note)
    std::string err;   // the console's own sentence for it, with numbers
    bool busy = false; // the console's worker is inside the system
};

struct RpState
{
    bool loaded = false;    // a state came and was read
    bool available = false; // the console's registry can be reached (jailbreak)
    bool per_user = false;  // the firmware knows the permission per user (from 10.00)
    std::string firmware;
    int service = -1; // the service: 1 on, 0 off, -1 unreadable (null)
    std::vector<RpUser> users;
    int pairing_slot = -1; // the user whose account the PIN is made for; -1: none has one
    std::vector<RpDevice> devices;
    RpPairing pairing;
    std::string module = "idle"; // libSceRemoteplay: idle, loading, ready or missing
    std::string backup_dir;      // where the app saves what it changes
};

// Reads the console's answer. False (and *out untouched) when the text is not a state: empty, not
// JSON, or {"ok":false,...}.
bool parse_remoteplay(const std::string &json, RpState *out);

// "47291853" -> "4729 1853"; anything that is not eight digits comes back as it is.
std::string pin_grouped(const std::string &pin);

// 292 -> "noch 4:52" (a pattern the dictionaries have: "noch {0}:{1}"); negative counts as 0.
std::string countdown_text(int seconds_left);

// The sentence for a pairing that failed or ran out (state failed or timeout): the console sends a
// code and a sentence with numbers, this shows the code's own. An unknown code shows the console's
// sentence, else "Das Koppeln ist fehlgeschlagen.". "" for every other state.
std::string pairing_note(const RpPairing &pairing);

// What the page says under the PIN's action while nothing happens or something ended: waiting ->
// the hint, paired -> "Ein Gerät wurde gekoppelt.", failed or timeout -> pairing_note, else the
// sentence about the 5 minutes.
std::string pin_status_text(const RpPairing &pairing);

// Why Remote Play cannot be used as it is: the registry is out of reach, the firmware offers no
// Remote Play to programs, no user has an activated account; "" when none of that.
std::string state_warning(const RpState &state);

// A PIN can be asked for: the registry is there, the module is not missing, and a user's account
// can be used for it.
bool pin_possible(const RpState &state);

// The state with its "pairing" replaced by the one of GET /api/v1/remoteplay/pin ({"ok":true,
// "pairing":{...}}), so the cheap poll of every second keeps the one full state current. Returns
// the state text unchanged when either is not what it should be.
std::string merge_pairing(const std::string &state_json, const std::string &pin_json);

// Whether the text is a state whose pairing waits for a device.
bool remoteplay_waiting(const std::string &json);

} // namespace hui::ps5cc
