// ps5cc - Remote Play: the console's state and the page's words (see remoteplay.hpp).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/remoteplay.hpp"

#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace hui::ps5cc
{

namespace
{

const cJSON *member(const cJSON *object, const char *name)
{
    return object != nullptr ? cJSON_GetObjectItemCaseSensitive(object, name) : nullptr;
}

std::string text_of(const cJSON *object, const char *name)
{
    const cJSON *item = member(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

int int_of(const cJSON *object, const char *name, int fallback = 0)
{
    const cJSON *item = member(object, name);
    return cJSON_IsNumber(item) ? static_cast<int>(item->valuedouble) : fallback;
}

// true, false, or null / missing as -1.
int tri_of(const cJSON *object, const char *name)
{
    const cJSON *item = member(object, name);
    if (cJSON_IsTrue(item))
        return 1;
    if (cJSON_IsFalse(item))
        return 0;
    return -1;
}

RpPairing pairing_of(const cJSON *p)
{
    RpPairing out;
    const std::string state = text_of(p, "state");
    if (!state.empty())
        out.state = state;
    out.pin = text_of(p, "pin");
    out.seconds_left = std::max(int_of(p, "seconds_left"), 0);
    out.code = text_of(p, "code");
    out.err = text_of(p, "err");
    out.busy = cJSON_IsTrue(member(p, "busy"));
    return out;
}

struct Parsed
{
    explicit Parsed(const std::string &text)
        : root(text.empty() ? nullptr : cJSON_Parse(text.c_str()))
    {
    }
    ~Parsed()
    {
        cJSON_Delete(root);
    }
    Parsed(const Parsed &) = delete;
    Parsed &operator=(const Parsed &) = delete;
    cJSON *root;
};

// The console's codes for a pairing that did not end well, as the web page words them.
struct CodeText
{
    const char *code;
    const char *text;
};
constexpr CodeText kCodeTexts[] = {
    {"pin_expired", "Die PIN ist abgelaufen, bevor ein Gerät gekoppelt wurde."},
    {"not_available", "Remote Play ist nicht verfügbar."},
    {"gen_failed", "Die Konsole konnte keine PIN erzeugen."},
    {"pin_invalid", "Das Koppeln ist fehlgeschlagen: Die PIN wurde falsch eingegeben."},
    {"account_invalid", "Das Koppeln ist fehlgeschlagen: Die Konto-ID wurde nicht angenommen."},
    {"aborted", "Das Koppeln ist fehlgeschlagen: Die Konsole hat die Anmeldung beendet."},
    {"refused", "Das Koppeln ist fehlgeschlagen: Die Konsole hat die Anmeldung abgelehnt."},
    {"no_account", "Kein Benutzer mit aktiviertem Konto. Aktiviere zuerst das Offline-Konto (Seite "
                   "Profil)."},
    {"no_module", "Diese Firmware stellt Remote Play für Programme nicht bereit."},
    {"init_failed", "Remote Play ließ sich nicht starten."},
};

} // namespace

bool parse_remoteplay(const std::string &json, RpState *out)
{
    Parsed parsed(json);
    const cJSON *root = parsed.root;
    if (!cJSON_IsObject(root) || cJSON_IsFalse(member(root, "ok")))
        return false;
    // an answer without any of the state's parts is no state (the cheap PIN poll has only
    // "pairing")
    if (member(root, "available") == nullptr && member(root, "users") == nullptr)
        return false;
    RpState s;
    s.loaded = true;
    s.available = cJSON_IsTrue(member(root, "available"));
    s.per_user = cJSON_IsTrue(member(root, "per_user"));
    s.firmware = text_of(root, "firmware");
    s.service = tri_of(member(root, "service"), "enabled");
    const cJSON *u = nullptr;
    cJSON_ArrayForEach(u, member(root, "users"))
    {
        RpUser user;
        user.slot = int_of(u, "slot");
        user.name = text_of(u, "name");
        user.activated = cJSON_IsTrue(member(u, "activated"));
        user.account_id = text_of(u, "account_id");
        user.allowed = tri_of(u, "allowed");
        s.users.push_back(std::move(user));
    }
    s.pairing_slot = int_of(member(root, "pairing_user"), "slot", -1);
    const cJSON *d = nullptr;
    cJSON_ArrayForEach(d, member(root, "devices"))
    {
        RpDevice device;
        device.slot = int_of(d, "slot");
        device.client_type = int_of(d, "client_type");
        device.user = text_of(d, "user");
        s.devices.push_back(std::move(device));
    }
    s.pairing = pairing_of(member(root, "pairing"));
    const std::string module = text_of(member(root, "module"), "state");
    if (!module.empty())
        s.module = module;
    s.backup_dir = text_of(member(root, "backup"), "dir");
    *out = std::move(s);
    return true;
}

std::string pin_grouped(const std::string &pin)
{
    if (pin.size() != 8)
        return pin;
    for (const char c : pin)
    {
        if (c < '0' || c > '9')
            return pin;
    }
    return pin.substr(0, 4) + " " + pin.substr(4);
}

std::string countdown_text(int seconds_left)
{
    const int s = std::max(seconds_left, 0);
    char text[32];
    std::snprintf(text, sizeof(text), "noch %d:%02d", s / 60, s % 60);
    return text;
}

std::string pairing_note(const RpPairing &pairing)
{
    if (pairing.state != "failed" && pairing.state != "timeout")
        return {};
    for (const CodeText &c : kCodeTexts)
    {
        if (pairing.code == c.code)
            return c.text;
    }
    return pairing.err.empty() ? "Das Koppeln ist fehlgeschlagen." : pairing.err;
}

std::string pin_status_text(const RpPairing &pairing)
{
    if (pairing.state == "waiting")
        return "In chiaki-ng oder ps5upload bei „Registrieren“ eingeben. Die PIN steht auch auf "
               "dem Fernseher.";
    if (pairing.state == "paired")
        return "Ein Gerät wurde gekoppelt.";
    if (pairing.state == "failed" || pairing.state == "timeout")
        return pairing_note(pairing);
    return "Die PIN gilt 5 Minuten und steht auch auf dem Fernseher.";
}

std::string state_warning(const RpState &state)
{
    if (!state.available)
        return "Die Registry der Konsole ist nicht erreichbar. Läuft der Jailbreak?";
    if (state.module == "missing")
        return "Diese Firmware stellt Remote Play für Programme nicht bereit.";
    if (state.pairing_slot < 0)
        return "Kein Benutzer hat ein aktiviertes Konto: ohne Konto lässt sich kein Gerät koppeln. "
               "Aktiviere es hier (Offline aktivieren) oder auf der Seite Profil.";
    return {};
}

bool pin_possible(const RpState &state)
{
    return state.available && state.pairing_slot > 0 && state.module != "missing";
}

std::string merge_pairing(const std::string &state_json, const std::string &pin_json)
{
    Parsed state(state_json);
    Parsed pin(pin_json);
    if (!cJSON_IsObject(state.root) || !cJSON_IsObject(pin.root) ||
        cJSON_IsFalse(member(pin.root, "ok")))
        return state_json;
    cJSON *fresh = cJSON_DetachItemFromObjectCaseSensitive(pin.root, "pairing");
    if (!cJSON_IsObject(fresh))
    {
        cJSON_Delete(fresh);
        return state_json;
    }
    if (cJSON_HasObjectItem(state.root, "pairing"))
        cJSON_ReplaceItemInObjectCaseSensitive(state.root, "pairing", fresh);
    else
        cJSON_AddItemToObject(state.root, "pairing", fresh);
    char *text = cJSON_PrintUnformatted(state.root);
    if (text == nullptr)
        return state_json;
    std::string merged = text;
    cJSON_free(text);
    return merged;
}

bool remoteplay_waiting(const std::string &json)
{
    Parsed parsed(json);
    return text_of(member(parsed.root, "pairing"), "state") == "waiting";
}

} // namespace hui::ps5cc
