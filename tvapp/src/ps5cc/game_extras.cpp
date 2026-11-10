// ps5cc - The game extras and the library tools (see game_extras.hpp).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/game_extras.hpp"

#include "ps5cc/look.hpp"
#include "ps5cc/playtime_rules.hpp"
#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <cstdio>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

const Rect kPanel{200.0f, 92.0f, 1520.0f, 864.0f};

enum Row
{
    kAmpr = 1,
    kEmuAmpr,
    kEmuPlaygo,
    kPlugins,
    kLz4Pack,
    kLz4Verify,
    kLz4Unpack,
    kZstPack,
    kTraceStart,
    kTraceCollect,
    kTraceGenerate,
    kTraceStop,
    kInstall = 40,
    kRestore,
    kRemove,
    kFetchNg,
    kTested = 60,
    kLevel,
    kMode,
    kTarget,
    kOriginals,
    kPlanText,
    kSignText,
    kSign,
    kPackStart,
    kStatus = 90,   // + section: the state, always in sight
    kVersion = 100, // + index
    kDup = 300,     // + index
};

// The parsed answer, deleted with the holder.
struct Json
{
    explicit Json(const std::string &text)
        : root(text.empty() ? nullptr : cJSON_Parse(text.c_str()))
    {
    }
    ~Json()
    {
        cJSON_Delete(root);
    }
    Json(const Json &) = delete;
    Json &operator=(const Json &) = delete;
    cJSON *root;
};

const cJSON *get(const cJSON *o, const char *name)
{
    return o != nullptr ? cJSON_GetObjectItemCaseSensitive(o, name) : nullptr;
}

std::string str(const cJSON *o, const char *name)
{
    const cJSON *v = get(o, name);
    return cJSON_IsString(v) && v->valuestring != nullptr ? v->valuestring : "";
}

double num(const cJSON *o, const char *name, double fallback = 0.0)
{
    const cJSON *v = get(o, name);
    return v != nullptr && cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

bool yes(const cJSON *o, const char *name)
{
    return cJSON_IsTrue(get(o, name));
}

std::string json_text(const std::string &s)
{
    std::string out = "\"";
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out + "\"";
}

const char *plugin_state(const std::string &s)
{
    if (s == "current")
        return "aktuell";
    if (s == "older")
        return "älter, Update da";
    if (s == "faulty")
        return "fehlerhaft";
    if (s == "missing")
        return "fehlt";
    if (s == "not_needed")
        return "nicht nötig";
    if (s == "noexec")
        return "nicht ausführbar";
    return "unbekannt";
}

std::string duration(double seconds)
{
    const int s = static_cast<int>(seconds);
    char text[32];
    if (s >= 3600)
        std::snprintf(text, sizeof(text), "%d Std. %02d Min.", s / 3600, s / 60 % 60);
    else
        std::snprintf(text, sizeof(text), "%d Min.", std::max(1, s / 60));
    return text;
}

} // namespace

void GameExtras::init(const ui::Theme &theme)
{
    theme_ = theme;
    form_.style.theme = theme;
    form_.style.panel = false;
    form_.style.on_page = false;
    form_.style.row_height = 60.0f;
    form_.style.header_height = 44.0f;
    form_.style.label_size = 23.0f;
    form_.style.value_size = 21.0f;
    form_.style.description_size = 18.0f;
    form_.style.control_width = 380.0f;
    form_.style.label_ratio = 0.52f;
    form_.style.on_text = "An";
    form_.style.off_text = "Aus";
    form_.set_bounds({kPanel.x + 24.0f, kPanel.y + 130.0f, 980.0f, 640.0f});
    ask_.style.theme = solid_theme(theme);
    ask_.style.frosted = false;
    ask_.style.scrim = 0.72f;
    ask_.style.width = 900.0f;
    ask_.style.body_lines = 10;
    ask_version_.style.theme = solid_theme(theme);
    ask_version_.style.frosted = false;
    ask_version_.style.scrim = 0.72f;
    ask_version_.style.width = 900.0f;
    ask_version_.style.body_lines = 10;
    ask_version_.style.buttons = ui::DialogButtons::stacked; // three answers with long words: one under the other
}

void GameExtras::open_game(const Game &game, ui::Feedback &feedback)
{
    game_ = game;
    extras_ = ExtrasState{};
    view_ = kMain;
    key_.clear();
    form_.clear();
    source().extras_open(game.id);
    feedback.play(audio::Cue::open);
}

void GameExtras::open_library(ui::Feedback &feedback)
{
    view_ = kLibrary;
    key_.clear();
    form_.clear();
    source().library_tools_fetch();
    feedback.play(audio::Cue::open);
}

void GameExtras::close(ui::Feedback &feedback)
{
    if (view_ == kVersions || view_ == kLz4)
    {
        view_ = kMain;
        key_.clear();
        build();
        feedback.play(audio::Cue::back);
        return;
    }
    if (view_ == kMain)
        source().extras_open({});
    view_ = kClosed;
    feedback.play(audio::Cue::back);
}

bool GameExtras::modal() const
{
    return view_ != kClosed;
}

bool GameExtras::visible() const
{
    return view_ != kClosed || shown_.value > 0.01f;
}

void GameExtras::absorb(const Snapshot &data)
{
    if (view_ == kClosed)
        return;
    const bool changed =
        data.extras.seq != extras_.seq || data.extras_versions.seq != versions_.seq ||
        data.extras_lz4_plan.seq != plan_.seq || data.library_tools.seq != library_.seq;
    if (data.extras.id == game_.id)
        extras_ = data.extras;
    // AMPR EMU NG built in: ShadowMountPlus must not refresh the fakelib, said once per job
    {
        Json a(extras_.ampr_job);
        const std::string warn = str(a.root, "warn");
        const std::string job = str(a.root, "id") + str(a.root, "state") + warn;
        if (str(a.root, "state") == "done" && num(a.root, "warn_level") >= 2 && job != warned_ &&
            toast)
        {
            warned_ = job;
            // the toast shows two lines: the advice short, the full sentence stays in the window
            const std::string key = str(a.root, "warn_key").empty() ? "update_emulators" : str(a.root, "warn_key");
            toast(ui::StatusKind::warning, "AMPR EMU NG eingebaut",
                  "In ShadowMountPlus " + key + "=0 setzen, sonst wird die Datei ersetzt.");
        }
    }
    versions_ = data.extras_versions;
    plan_ = data.extras_lz4_plan;
    library_ = data.library_tools;
    if (changed)
        build();
}

// Rebuilt only when what it shows changed (a refill restarts the form's entrance and loses the
// focus).
void GameExtras::build()
{
    const int keep = form_.focus_id();
    // each view only by what it shows: the LZ4 settings keep their choices while the plan comes and
    // goes
    std::string key = std::to_string(view_);
    if (view_ == kMain)
        key += extras_.ampr + extras_.emu_ampr + extras_.emu_playgo + extras_.lz4 + extras_.trace +
               extras_.plugins;
    else if (view_ == kVersions)
        key += versions_.json + (lib_ == "playgo" ? extras_.emu_playgo : extras_.emu_ampr);
    else if (view_ == kLz4)
        key += extras_.lz4;
    else
        key += library_.duplicates;
    if (key == key_)
    {
        if (view_ == kLz4)
        {
            form_.set_value_text(kPlanText, plan_text());
            refresh_sign();
        }
        return;
    }
    key_ = key;
    form_.clear();
    switch (view_)
    {
    case kMain:
        build_main();
        break;
    case kVersions:
        build_versions();
        break;
    case kLz4:
        build_lz4();
        break;
    case kLibrary:
        build_library();
        break;
    case kClosed:
        return;
    }
    form_.set_active(true);
    if (keep > 0 && form_.row(keep) != nullptr)
        form_.focus_row(keep);
}

void GameExtras::build_main()
{
    if (extras_.seq == 0)
        return;
    // AMPR index
    {
        Json j(extras_.ampr);
        form_.add_header("AMPR-INDEX");
        const bool has = yes(j.root, "has_index");
        const std::string state =
            has ? "vorhanden, " + std::to_string(static_cast<int>(num(j.root, "files"))) +
                      " Dateien, Stand " +
                      play_date(static_cast<std::int64_t>(num(j.root, "mtime")), 0)
                : "fehlt";
        form_.add_value(kStatus, "Zustand", state);
        ui::FormRow &r = form_.add_action(kAmpr, has ? "Neu bauen" : "Index bauen");
        r.description = "Der Index sagt APR-EMU, wo jede Datei des Spiels im Abbild liegt; ohne "
                        "ihn lädt ein gepacktes "
                        "oder per APR-EMU gestartetes Spiel langsam.";
        if (!yes(j.root, "can_build"))
        {
            r.disabled = true;
            r.description = str(j.root, "why");
        }
    }
    // APR-EMU and PlayGo in the game
    const auto emu_row = [&](int id, const char *title, const std::string &text)
    {
        Json j(text);
        form_.add_header(title);
        const cJSON *inst = get(j.root, "installed");
        std::string state = !yes(inst, "present") ? "nicht eingebaut"
                            : yes(inst, "known")  ? "eingebaut: " + str(inst, "label")
                            : yes(inst, "ng") ? "AMPR EMU NG eingebaut · " + str(inst, "ng_version")
                                              : "eine unbekannte Fassung ist eingebaut";
        if (str(j.root, "place") == "backport")
            state += " · im Backport-Ordner";
        if (yes(inst, "has_orig"))
            state += " · Original gesichert";
        if (yes(j.root, "packed"))
            state += " · Spiel ist LZ4-gepackt";
        if (yes(j.root, "packed_zstd"))
            state += " · Spiel ist zstd-gepackt";
        form_.add_value(kStatus + id, "Zustand", state);
        ui::FormRow &r = form_.add_action(id, "Version wählen …");
        r.description = "Zeigt die Fassungen im Ordner der App; die gewählte wird sofort "
                        "eingebaut, das Original bleibt "
                        "gesichert.";
        if (!yes(j.root, "can_change"))
        {
            r.disabled = true;
            r.description = str(j.root, "why");
        }
    };
    emu_row(kEmuAmpr, "APR-EMU IM SPIEL", extras_.emu_ampr);
    emu_row(kEmuPlaygo, "PLAYGO IM SPIEL", extras_.emu_playgo);
    // plugins: not shown for games that are no folder (the web app hides them too)
    {
        Json j(extras_.plugins);
        const std::string why = str(j.root, "why");
        if (why.rfind("Nur für Spiele", 0) != 0)
        {
            form_.add_header("PLUGINS");
            std::string about;
            const cJSON *f = nullptr;
            cJSON_ArrayForEach(f, get(j.root, "files"))
            {
                about += (about.empty() ? "" : " · ") + str(f, "project") + ": " +
                         plugin_state(str(f, "state"));
            }
            const bool update = str(j.root, "state") == "update";
            form_.add_value(kStatus + kPlugins, "Zustand",
                            about.empty() ? "keine Plugins im Spiel" : about);
            ui::FormRow &r = form_.add_action(kPlugins, update ? "Aktualisieren" : "Prüfen");
            r.description = update ? "Legt die neueren Fassungen der Plugins ins Spiel."
                                   : "Prüft die Plugins des Spiels.";
            if (!yes(j.root, "can"))
            {
                r.disabled = true;
                r.description = why;
            }
        }
    }
    // LZ4 packs
    {
        Json j(extras_.lz4);
        const bool zst = str(get(j.root, "pack"), "kind") == "zstd"; // the packs are zstd ones (AMPR EMU NG)
        form_.add_header(zst ? "ZSTD-ASSET-PACKS" : "LZ4-ASSET-PACKS");
        if (yes(j.root, "packed"))
        {
            const cJSON *p = get(j.root, "pack");
            const double logical = num(p, "logical_bytes"), stored = num(p, "stored_bytes");
            char text[96];
            std::snprintf(text, sizeof(text), "gepackt: %s → %s (%d %%)",
                          format_bytes(static_cast<std::uint64_t>(logical)).c_str(),
                          format_bytes(static_cast<std::uint64_t>(stored)).c_str(),
                          logical > 0 ? static_cast<int>(stored * 100.0 / logical + 0.5) : 0);
            form_.add_value(kStatus + kLz4Pack, "Zustand", text);
            form_.add_action(kLz4Verify, "Pakete prüfen").description =
                "Liest jedes Paket und vergleicht es mit den Originaldateien.";
            ui::FormRow &u = form_.add_action(kLz4Unpack, zst ? "zstd entpacken" : "LZ4 entpacken");
            u.description = "Legt die Dateien wieder ungepackt ab.";
            u.disabled = num(j.root, "free_bytes") < logical + 256.0 * 1024 * 1024;
            if (u.disabled)
                u.description = "Zu wenig Platz zum Entpacken.";
        }
        else
        {
            form_.add_value(kStatus + kLz4Pack, "Zustand", "nicht gepackt");
            ui::FormRow &r = form_.add_action(kLz4Pack, "Mit LZ4 packen …");
            r.description =
                "Öffnet die Einstellungen: Stufe, Profil und ob im Ordner selbst oder als Kopie.";
            // zstd is a group of its own, as in the web app: one list under one heading looked as if
            // the two formats belonged together (10.11.2026)
            form_.add_header("ZSTD-ASSET-PACKS");
            ui::FormRow &z = form_.add_action(kZstPack, "Mit zstd packen …");
            z.description = "Nur für AMPR EMU NG: packt alles außer den Systemdateien. Öffnet die "
                            "Einstellungen: Stufe und ob im Ordner selbst oder als Kopie.";
            if (!yes(j.root, "can"))
            {
                r.disabled = true;
                r.description = str(j.root, "why");
                z.disabled = true;
                z.description = str(j.root, "why");
            }
        }
    }
    // optimisation by recording
    {
        Json j(extras_.trace);
        form_.add_header("OPTIMIERUNG PER MITSCHNITT");
        const int sessions = static_cast<int>(num(j.root, "sessions"));
        form_.add_value(kStatus + kTraceStart, "Zustand",
                        !yes(j.root, "enabled")
                            ? "aus"
                            : "läuft · " + std::to_string(sessions) + " Sitzung(en) gesammelt");
        if (!yes(j.root, "can"))
        {
            ui::FormRow &r = form_.add_action(kTraceStart, "Mitschnitt starten");
            r.disabled = true;
            r.description = str(j.root, "why");
        }
        else if (!yes(j.root, "enabled"))
            form_.add_action(kTraceStart, "Mitschnitt starten").description =
                "Danach das Spiel spielen; die App zeichnet auf, welche Dateien es wann liest.";
        else
        {
            ui::FormRow &c = form_.add_action(kTraceCollect, "Sitzung einsammeln");
            c.disabled = !yes(j.root, "journal_new") || yes(j.root, "game_running");
            c.description = yes(j.root, "game_running")  ? "Erst das Spiel beenden."
                            : yes(j.root, "journal_new") ? "Eine neue Sitzung liegt bereit."
                                                         : "Noch keine neue Sitzung.";
            ui::FormRow &g = form_.add_action(kTraceGenerate, "Profil erzeugen");
            g.disabled = sessions <= 0;
            g.description =
                std::to_string(sessions) + " Sitzung(en) gesammelt · " + str(j.root, "profile");
            form_.add_action(kTraceStop, "Mitschnitt beenden").description =
                "Nimmt die Aufzeichnung wieder aus dem Spiel.";
        }
    }
}

void GameExtras::build_versions()
{
    Json j(versions_.json);
    Json emu(lib_ == "playgo" ? extras_.emu_playgo : extras_.emu_ampr);
    const cJSON *inst = get(emu.root, "installed");
    const std::string sha = str(inst, "sha256");
    const bool packed = yes(emu.root, "packed");
    const bool packed_zstd = yes(emu.root, "packed_zstd"); // only AMPR EMU NG reads zstd packs
    version_names_.clear();
    version_ng_.clear();
    // the answer of the app says whether ShadowMountPlus refreshes the fakelib (update_emulators, on by default), which
    // line of its config.ini the app changes (update_emulators; update_enabled from 1.7beta12) and whether the person said
    // which version runs (an older app has no such field: then there is nothing to ask)
    const cJSON *emus = get(j.root, "emus");
    smp_update_on_ = yes(emus, "update_emulators");
    smp_key_ = str(emus, "key").empty() ? "update_emulators" : str(emus, "key");
    smp_format_ = str(emus, "format");
    smp_asked_ = get(emus, "smp_version") == nullptr || !str(emus, "smp_version").empty();
    // three groups with a title of their own (AMPR EMU only; PlayGo has one list): AMPR EMU NG by quer3q (every NG build, later
    // ones too), AMPR EMU by drakmor (the rest) and the builds that only serve LZ4 packs (test-pack, test-debug-pack)
    const bool playgo = lib_ == "playgo";
    auto group_of = [&](const cJSON *x)
    {
        if (playgo)
            return 1;
        if (yes(x, "ng"))
            return 0;
        return yes(x, "pack") ? 2 : 1;
    };
    int counts[3] = {0, 0, 0};
    const cJSON *v = nullptr;
    cJSON_ArrayForEach(v, get(j.root, "versions")) { ++counts[group_of(v)]; }
    int i = 0;
    // the rows go in the order of the groups; version_names_ and version_ng_ follow the same order, so row i is entry i
    auto add_versions = [&](int group, int want_debug)
    {
        const cJSON *e = nullptr;
        cJSON_ArrayForEach(e, get(j.root, "versions"))
        {
            if (group_of(e) != group)
                continue;
            if (want_debug >= 0 && yes(e, "debug") != (want_debug == 1))
                continue;
            std::string label = str(e, "label");
            if (!sha.empty() && str(e, "sha256") == sha)
                label += " (eingebaut)";
            ui::FormRow &r = form_.add_action(kVersion + i, label.empty() ? str(e, "name") : label);
            std::string about;
            if (yes(e, "ng"))
                about += "AMPR EMU NG · ";
            if (yes(e, "pack"))
                about += "LZ4 · ";
            if (yes(e, "ng")) // AMPR EMU NG reads zstd packs as well
                about += "ZSTD · ";
            if (yes(e, "debug"))
                about += "mit Protokoll · ";
            if (yes(e, "bundled"))
                about += "mitgeliefert · ";
            about +=
                !sha.empty() && str(e, "sha256") == sha ? "so eingebaut" : "X-Taste baut diese ein";
            r.description = about;
            r.disabled =
                !yes(e, "valid") || (packed && !yes(e, "pack")) || (packed_zstd && !yes(e, "ng"));
            if (packed && !yes(e, "pack"))
                r.description = "Das Spiel ist gepackt: nur eine LZ4-Fassung passt.";
            if (packed_zstd && !yes(e, "ng"))
                r.description = "Das Spiel liegt in zstd-Paketen: nur AMPR EMU NG passt.";
            version_names_.push_back(str(e, "name"));
            version_ng_.push_back(yes(e, "ng"));
            ++i;
        }
    };
    static const char *const kGroupTitles[3] = {"AMPR EMU NG @quer3q", "AMPR EMU @Drakmor",
                                                "LZ4-ASSET-PACK-VERSION"};
    if (playgo || counts[0] + counts[1] + counts[2] == 0)
        form_.add_header("VERSIONEN");
    for (int g = 0; g < 3; ++g)
    {
        if (counts[g] == 0)
            continue;
        if (!playgo)
            form_.add_header(kGroupTitles[g]);
        if (g == 2)
        {
            add_versions(g, 0); // without the log first
            add_versions(g, 1);
        }
        else
        {
            add_versions(g, -1);
        }
    }
    if (i == 0)
        form_.add_value(
            kVersion, versions_.json.empty() ? "Wird geladen …" : "Noch keine Versionen im Ordner",
            "");
    form_.add_header("ORDNER UND ORIGINAL");
    form_.add_action(kInstall, "Mitgelieferte Versionen in den Ordner legen");
    const cJSON *ng = get(j.root, "ng_release");
    if (lib_ == "ampr" && cJSON_IsObject(ng))
    {
        const cJSON *missing = get(ng, "missing");
        const bool want = cJSON_IsNumber(missing) && missing->valueint > 0;
        ui::FormRow &f = form_.add_action(kFetchNg, "AMPR EMU NG von GitHub laden");
        f.disabled = !want || yes(ng, "busy");
        f.description = want ? "Weiterentwicklung von quer3q, liest LZ4- und zstd-Pakete; "
                               "Prüfsumme wird geprüft."
                             : "Schon im Ordner oder mitgeliefert.";
    }
    if (yes(inst, "has_orig"))
        form_.add_action(kRestore, "Original wiederherstellen");
    else if (yes(inst, "present"))
        form_.add_action(kRemove, "Entfernen").description =
            "Nimmt die eingebaute Fassung wieder heraus.";
}

void GameExtras::build_lz4()
{
    Json j(extras_.lz4);
    if (zstd_)
    {
        // zstd: level 1 to 22 (four anchors), everything but the system files goes in, no profile. The game is always packed in its
        // own folder and the originals go once every file has been compared with its pack (a failure leaves everything as it was),
        // so there is nothing to choose about them; that the game ran unpacked is still confirmed first.
        form_.add_header("VORAUSSETZUNG");
        form_.add_toggle(kTested, "Ungepackt mit AMPR EMU NG getestet", false).description =
            "Das Spiel muss so schon gelaufen sein; erst dann lohnt das Packen.";
        form_.add_header("EINSTELLUNGEN");
        form_.add_choice(kLevel, "Stufe",
                         {"5 (empfohlen)", "1 (am schnellsten)", "9 (kleiner)", "19 (am kleinsten)"}, 0);
        profile_ids_.clear();
        form_.add_value(kOriginals, "Originale", "entfernt nach der Prüfung");
        form_.add_value(kPlanText, "Plan", plan_text());
        form_.add_value(kSignText, "Programmdateien", sign_text());
        form_.add_toggle(kSign, "Programmdateien signieren", true).description =
            "Ersetzt unsignierte Programmdateien im Spielordner durch FSELF-Fassungen.";
        form_.set_disabled(kSign, !has_unsigned());
        ui::FormRow &zgo = form_.add_action(kPackStart, "Packen starten");
        zgo.disabled = true;
        zgo.description = "Erst die Voraussetzung oben bestätigen.";
        return;
    }
    form_.add_header("VORAUSSETZUNG");
    form_.add_toggle(kTested, "Ungepackt mit APR-EMU (test-pack) getestet", false).description =
        "Das Spiel muss so schon gelaufen sein; erst dann lohnt das Packen.";
    form_.add_header("EINSTELLUNGEN");
    form_.add_choice(kLevel, "Stufe", {"9 (empfohlen)", "3 (schneller)", "12 (kleiner)"}, 0);
    profile_ids_.clear();
    std::vector<std::string> modes;
    const cJSON *suggest = get(j.root, "suggest");
    if (cJSON_IsObject(suggest))
    {
        modes.push_back("Getestetes Profil");
        profile_ids_.push_back(str(suggest, "id"));
    }
    modes.push_back("Automatisch");
    profile_ids_.push_back("");
    const cJSON *p = nullptr;
    cJSON_ArrayForEach(p, get(j.root, "profiles"))
    {
        if (str(p, "where") == "builtin")
            continue;
        modes.push_back("Profil: " + str(p, "name"));
        profile_ids_.push_back(str(p, "id"));
    }
    form_.add_choice(kMode, "Art", modes, 0);
    form_.add_choice(kTarget, "Wohin", {"Im Ordner selbst", "Als Kopie (-lz4)"}, 0).description =
        "Eine Kopie legt einen zweiten Ordner mit derselben Titel-ID an.";
    form_.add_value(kPlanText, "Plan", plan_text());
    form_.add_value(kSignText, "Programmdateien", sign_text());
    ui::FormRow &go = form_.add_action(kPackStart, "Packen starten");
    go.disabled = true;
    go.description = "Erst die Voraussetzung oben bestätigen.";
}

void GameExtras::build_library()
{
    Json d(library_.duplicates);
    paths_.clear();
    aside_.clear();
    form_.add_header("DOPPELTE ABBILDER");
    int rows = 0;
    const cJSON *g = nullptr;
    cJSON_ArrayForEach(g, get(d.root, "groups"))
    {
        const cJSON *s = nullptr;
        cJSON_ArrayForEach(s, get(g, "sources"))
        {
            const std::string path = str(s, "path");
            const bool folder = yes(s, "folder");
            const std::string name =
                str(g, "name") + " · " + path.substr(path.find_last_of('/') + 1);
            std::string about =
                path + " · " + format_bytes(static_cast<std::uint64_t>(num(s, "size")));
            if (yes(s, "smp_uses"))
                about += " · nimmt ShadowMountPlus gerade";
            if (yes(s, "mounted"))
                about += " · eingehängt";
            if (folder)
                form_.add_value(kDup + rows, name, "Ordner").description = about;
            else
            {
                ui::FormRow &r = form_.add_action(kDup + rows, "Deaktivieren: " + name);
                r.description = about;
                r.disabled = yes(s, "mounted");
            }
            paths_.push_back(path);
            aside_.push_back(false);
            ++rows;
        }
    }
    const cJSON *a = nullptr;
    cJSON_ArrayForEach(a, get(d.root, "aside"))
    {
        const std::string path = str(a, "path");
        form_.add_action(kDup + rows, "Aktivieren: " + path.substr(path.find_last_of('/') + 1))
            .description = "beiseitegelegt · " + path;
        paths_.push_back(path);
        aside_.push_back(true);
        ++rows;
    }
    if (rows == 0)
        form_.add_value(
            kDup, library_.duplicates.empty() ? "Wird gesucht …" : "Keine doppelten Abbilder", "");
    // Covers & Metadaten live under System → Speicher (user, 29.10.2026).
}

std::string GameExtras::plan_text() const
{
    Json plan(plan_.json);
    if (plan.root == nullptr)
        return "Wird berechnet …";
    if (!yes(plan.root, "valid"))
        return str(plan.root, "error");
    return std::to_string(static_cast<int>(num(plan.root, "pack_files"))) + " Dateien (" +
           format_bytes(static_cast<std::uint64_t>(num(plan.root, "pack_bytes"))) + ") · etwa " +
           duration(num(plan.root, "est_s"));
}

bool GameExtras::has_unsigned() const
{
    Json plan(plan_.json);
    const cJSON *u = get(plan.root, "unsigned_exec");
    return yes(plan.root, "valid") && num(u, "count") > 0;
}

// The "Programmdateien" row and the switch beside it follow the plan and the choices.
void GameExtras::refresh_sign()
{
    form_.set_value_text(kSignText, sign_text());
    if (zstd_)
        form_.set_disabled(kSign, !has_unsigned());
}

std::string GameExtras::sign_text() const
{
    Json plan(plan_.json);
    if (plan.root == nullptr)
        return "Wird geprüft …";
    if (!yes(plan.root, "valid"))
        return "–";
    const cJSON *u = get(plan.root, "unsigned_exec");
    const int count = static_cast<int>(num(u, "count"));
    const int refused = static_cast<int>(num(u, "refused"));
    if (count == 0 && refused == 0)
        return "Alle signiert";
    // into a copy the new folder gets them as FSELF; in the game's own folder they stay as they are (short: the value column is narrow)
    const bool copy = !zstd_ && form_.choice_index(kTarget) == 1;
    std::string text;
    if (count > 0)
        text = std::to_string(count) + " unsigniert";
    // zstd signs in the game's own folder when the switch is on; into a copy (LZ4) the new folder gets FSELF
    const bool signs = copy || (zstd_ && form_.toggle_value(kSign));
    if (refused > 0)
        text += (text.empty() ? "" : " · ") + std::to_string(refused) + " nicht signierbar";
    else
        text += copy ? " · FSELF in der Kopie" : signs ? " · werden zu FSELF" : " · bleiben unverändert";
    return text;
}

void GameExtras::ask_plan()
{
    if (zstd_)
    {
        static const int kZstLevels[4] = {5, 1, 9, 19};
        source().extras_lz4_plan(game_.id, kZstLevels[std::clamp(form_.choice_index(kLevel), 0, 3)],
                                 std::string(), "zstd");
        return;
    }
    static const int kLevels[3] = {9, 3, 12};
    const int level = kLevels[std::clamp(form_.choice_index(kLevel), 0, 2)];
    const int mode = form_.choice_index(kMode);
    const std::string profile = mode >= 0 && mode < static_cast<int>(profile_ids_.size())
                                    ? profile_ids_[static_cast<std::size_t>(mode)]
                                    : std::string();
    source().extras_lz4_plan(game_.id, level, profile, "lz4");
}

void GameExtras::activate(int id, ui::Feedback &feedback)
{
    const std::string gid = json_text(game_.id);
    feedback.play(audio::Cue::select);
    if (id >= kDup)
    {
        const std::size_t i = static_cast<std::size_t>(id - kDup);
        if (i < paths_.size())
            source().library_tools_post("library/duplicates",
                                        "{\"path\":" + json_text(paths_[i]) +
                                            ",\"undo\":" + (aside_[i] ? "true" : "false") + "}");
        return;
    }
    if (id >= kVersion)
    {
        const std::size_t i = static_cast<std::size_t>(id - kVersion);
        if (i >= version_names_.size())
            return;
        // AMPR EMU NG while ShadowMountPlus still refreshes the fakelib (06.11.2026): it would put its own file back
        // at the start of a game. The question comes first: "Ja" switches the setting off and puts the version in,
        // "Abbrechen" (or back) leaves everything as it was.
        if (lib_ == "ampr" && i < version_ng_.size() && version_ng_[i] && smp_update_on_)
        {
            pending_version_ = version_names_[i];
            if (!smp_asked_)
            {
                // the person never said which version of ShadowMountPlus runs: asked once (09.11.2026), the answer is
                // kept in the settings; "back" leaves everything as it was
                ui::DialogContent content;
                content.icon = ui::StatusKind::question;
                content.title = "Welche Version von ShadowMountPlus nutzt du?";
                content.body =
                    "Ab Version 1.7beta12 hat ShadowMountPlus eine neue config.ini (mit Abschnitten statt einer "
                    "flachen Liste). Die App ändert darin gleich eine Zeile und muss wissen, welche Version bei dir "
                    "läuft.\n\n"
                    "Du kannst das später jederzeit unter System im Reiter ShadowMountPlus ändern. Mit „Zurück“ "
                    "bleibt alles, wie es ist, und NG wird nicht eingebaut.";
                content.buttons = {
                    {"Ab 1.7beta12", ui::ButtonKind::primary}, {"Vor 1.7beta12"}, {"Automatisch erkennen"}};
                ask_version_.open(content, feedback);
                return;
            }
            ask_switch(feedback);
            return;
        }
        apply_version(version_names_[i]);
        return;
    }
    switch (id)
    {
    case kAmpr:
        source().extras_post("library/ampr", "{\"id\":" + gid + "}");
        break;
    case kEmuAmpr:
    case kEmuPlaygo:
        lib_ = id == kEmuAmpr ? "ampr" : "playgo";
        view_ = kVersions;
        versions_ = ExtrasVersions{};
        key_.clear();
        source().extras_versions(lib_);
        build();
        break;
    case kPlugins:
        source().extras_post("library/plugins", "{\"id\":" + gid + ",\"action\":\"apply\"}");
        break;
    case kLz4Pack:
    case kZstPack:
        zstd_ = id == kZstPack;
        view_ = kLz4;
        plan_ = ExtrasLz4Plan{};
        key_.clear();
        build();
        ask_plan();
        break;
    case kLz4Verify:
        source().extras_post("library/lz4", "{\"id\":" + gid + ",\"kind\":\"verify\"}");
        break;
    case kLz4Unpack:
        source().extras_post("library/lz4", "{\"id\":" + gid + ",\"kind\":\"unpack\"}");
        break;
    case kTraceStart:
    case kTraceCollect:
    case kTraceGenerate:
    case kTraceStop:
    {
        const char *action = id == kTraceStart      ? "start"
                             : id == kTraceCollect  ? "collect"
                             : id == kTraceGenerate ? "generate"
                                                    : "stop";
        source().extras_post("library/trace", "{\"id\":" + gid + ",\"action\":\"" + action + "\"}");
        break;
    }
    case kInstall:
        source().extras_post(lib_ == "playgo" ? "playgo/install" : "ampr-emu/install", "{}");
        source().extras_versions(lib_);
        break;
    case kFetchNg:
        source().extras_post("ampr-emu/fetch-ng", "{}");
        source().extras_versions(lib_);
        break;
    case kRestore:
    case kRemove:
        source().extras_post("library/emu", "{\"id\":" + gid + ",\"lib\":\"" + lib_ +
                                                "\",\"action\":\"" +
                                                (id == kRestore ? "restore" : "remove") + "\"}");
        view_ = kMain;
        key_.clear();
        build();
        break;
    case kPackStart:
    {
        static const int kLevels[3] = {9, 3, 12};
        static const int kZstLevels[4] = {5, 1, 9, 19};
        const int mode = form_.choice_index(kMode);
        const std::string profile = !zstd_ && mode >= 0 && mode < static_cast<int>(profile_ids_.size())
                                        ? profile_ids_[static_cast<std::size_t>(mode)]
                                        : std::string();
        const int level = zstd_ ? kZstLevels[std::clamp(form_.choice_index(kLevel), 0, 3)]
                                : kLevels[std::clamp(form_.choice_index(kLevel), 0, 2)];
        source().extras_post(
            "library/lz4",
            "{\"id\":" + gid + ",\"kind\":\"" +
                (!zstd_ && form_.choice_index(kTarget) == 1 ? "copy" : "pack") + "\",\"level\":" +
                std::to_string(level) + ",\"profile\":" + json_text(profile) + ",\"format\":\"" +
                (zstd_ ? "zstd" : "lz4") + "\",\"sign\":" +
                (zstd_ && form_.toggle_value(kSign) && has_unsigned() ? "true" : "false") + "}");
        view_ = kMain;
        key_.clear();
        build();
        break;
    }
    default:
        break;
    }
}

// "ShadowMountPlus umstellen?": yes switches its automatic fakelib update off and puts the waiting version in.
void GameExtras::ask_switch(ui::Feedback &feedback)
{
    ui::DialogContent content;
    content.icon = ui::StatusKind::warning;
    content.title = "ShadowMountPlus umstellen?";
    content.body =
        "Die automatische Fakelib-Aktualisierung von ShadowMountPlus ist eingeschaltet. "
        "Dann ersetzt ShadowMountPlus die NG-Datei beim Spielstart wieder durch die Fassung "
        "aus seinem Emulator-Ordner.\n\n"
        "Soll die automatische Fakelib-Aktualisierung in ShadowMountPlus deaktiviert "
        "werden?\n\n"
        "Die App ändert dafür nur die Zeile " +
        smp_key_ + " in /data/shadowmount/config.ini. Mit „Abbrechen“ bleibt alles, wie es ist, und NG wird nicht eingebaut.";
    content.buttons = {{"Ja", ui::ButtonKind::primary}, {"Abbrechen"}};
    ask_.open(content, feedback);
}

// The version goes into the game; the window returns to the game's page.
void GameExtras::apply_version(const std::string &name)
{
    source().extras_post("library/emu", "{\"id\":" + json_text(game_.id) + ",\"lib\":\"" + lib_ +
                                            "\",\"action\":\"apply\",\"name\":" +
                                            json_text(name) + "}");
    view_ = kMain;
    key_.clear();
    build();
}

void GameExtras::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (view_ == kClosed)
        return;
    if (ask_version_.is_open())
    {
        // the version of ShadowMountPlus, asked once: kept in the settings (the request queues before the one that changes
        // its config.ini, so the named version is in force when that is written), then the question about switching off
        const ui::Event event = ask_version_.handle(input, feedback);
        if (event == ui::Event::activated)
        {
            static const char *const kWords[3] = {"v2", "old", "auto"};
            const int choice = std::clamp(ask_version_.choice(), 0, 2);
            source().save_config(std::string("{\"smp_version\":\"") + kWords[choice] + "\"}");
            smp_asked_ = true;
            // a file with content decides which line it has; for one without, the named version does
            if (smp_format_ == "none" || smp_format_.empty())
                smp_key_ = choice == 0 ? "update_enabled" : "update_emulators";
            ask_switch(feedback);
        }
        else if (!ask_version_.is_open())
            pending_version_.clear();
        return;
    }
    if (ask_.is_open())
    {
        // "Ja" switches the automatic fakelib update off in ShadowMountPlus, then the version goes in; "Abbrechen" and
        // back change nothing (the requests queue in this order, so the setting is written before the file goes in)
        const ui::Event event = ask_.handle(input, feedback);
        if (event == ui::Event::activated && ask_.choice() == 0)
        {
            source().extras_post("ampr-emu/smp-update", "{\"enabled\":false}");
            smp_update_on_ = false;
            apply_version(pending_version_);
            pending_version_.clear();
        }
        else if (!ask_.is_open())
            pending_version_.clear();
        return;
    }
    if (input.is_pressed(Action::back))
    {
        close(feedback);
        return;
    }
    const ui::Event event = form_.handle(input, feedback);
    const int id = form_.changed_id();
    if (event == ui::Event::activated)
        activate(id, feedback);
    else if (event == ui::Event::changed)
    {
        if (id == kTested)
        {
            const bool ok = form_.toggle_value(kTested);
            form_.set_disabled(kPackStart, !ok);
            if (ui::FormRow *r = form_.row(kPackStart))
                r->description = ok ? "Startet sofort; der Auftrag läuft auf der Konsole."
                                    : "Erst die Voraussetzung oben bestätigen.";
        }
        else if (id == kLevel || id == kMode)
            ask_plan();
        else if (id == kTarget || id == kSign)
            refresh_sign();
    }
}

void GameExtras::update(float dt)
{
    clock_ += dt;
    form_.update(dt);
    ask_.update(dt);
    ask_version_.update(dt);
    shown_.target = view_ != kClosed ? 1.0f : 0.0f;
    shown_.update(dt, 14.0f);
}

// The job of the window's game: the AMPR job (index, APR-EMU) or the image job (LZ4), whichever
// runs.
std::string GameExtras::job_line(float *fraction) const
{
    *fraction = -1.0f;
    Json a(extras_.ampr_job);
    Json i(extras_.image_job);
    if (yes(i.root, "active"))
    {
        const double total = num(i.root, "work_total"), done = num(i.root, "work_done");
        *fraction = total > 0 ? static_cast<float>(done / total) : 0.0f;
        std::string line = str(i.root, "phase");
        if (num(i.root, "eta_s") > 0)
            line += " · noch " + duration(num(i.root, "eta_s"));
        return line;
    }
    if (yes(a.root, "active"))
    {
        *fraction = 0.0f;
        return str(a.root, "phase").empty() ? "läuft …" : str(a.root, "phase");
    }
    const std::string err =
        !str(a.root, "error").empty() ? str(a.root, "error") : str(i.root, "error");
    if (!err.empty())
        return "Fehler: " + err;
    if (str(a.root, "state") == "done" && !str(a.root, "warn").empty())
        return str(a.root, "warn");
    return {};
}

std::string GameExtras::side_text() const
{
    switch (view_)
    {
    case kVersions:
        return lib_ == "playgo" ? "PlayGo im Spiel: die Fassung von libScePlayGo, die das Spiel "
                                  "lädt. Eine andere gewählt, "
                                  "wird sie sofort eingebaut; das Original bleibt gesichert."
                                : "APR-EMU im Spiel: die Fassung von libSceAmpr, die das Spiel "
                                  "lädt. Ein gepacktes Spiel "
                                  "braucht eine LZ4-Fassung (test-pack). Neue Fassungen vom PC "
                                  "kommen über die Browser-App.";
    case kLz4:
        if (zstd_)
            return "Packt das ganze Spiel außer den Systemdateien in zstd-Asset-Packs. Das spart "
                   "Platz; gelesen wird sie nur von AMPR EMU NG, das die App dafür einbaut. Die "
                   "Originale werden entfernt, sobald jede Datei Byte für Byte geprüft ist.";
        return "Packt die großen Dateien des Spiels in LZ4-Asset-Packs. Das spart Platz; gelesen "
               "wird über APR-EMU "
               "(test-pack). Eigene Profile (TOML) lädt man über die Browser-App hoch.";
    case kLibrary:
        return "Doppelte Abbilder: Liegt ein Spiel mehrmals auf den Laufwerken, nimmt "
               "ShadowMountPlus eins davon. "
               "Deaktivieren legt eine Datei beiseite (.off), Aktivieren holt sie zurück.";
    default:
        break;
    }
    return form_.help_text();
}

void GameExtras::draw_overlay(ui::Canvas &canvas) const
{
    const float amount = shown_.value;
    if (amount <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, kNavy.with_alpha(0.78f * amount));
    list.push_opacity(amount);
    const Rect panel{kPanel.x, kPanel.y + 24.0f * (1.0f - amount), kPanel.w, kPanel.h};
    ui::Painter paint(list, fonts, theme_);
    solid_floor(list, panel, theme_.radius_card);
    paint.panel(panel);
    const std::string title =
        view_ == kLibrary ? "Bibliothek: Doppelte Abbilder"
        : view_ == kVersions
            ? std::string(lib_ == "playgo" ? "PlayGo" : "APR-EMU") + " · " + game_.name
        : view_ == kLz4 ? std::string(zstd_ ? "Mit zstd packen · " : "Mit LZ4 packen · ") + game_.name
                        : "Extras · " + game_.name;
    ui::text(list, fonts.display, ui::fit_label(paint, title, 36.0f, panel.w - 80.0f),
             panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
    ui::text(list, fonts.regular,
             view_ == kLibrary ? "für alle Spiele"
                               : "AMPR-Index, APR-EMU, PlayGo, Plugins, LZ4, zstd und Mitschnitt",
             panel.x + 40.0f, panel.y + 104.0f, 21.0f, kMuted);
    if (view_ == kMain && extras_.seq == 0)
        ui::text(list, fonts.semibold, "Wird geladen …", panel.x + 520.0f, panel.y + 420.0f, 30.0f,
                 kInk, gfx::Align::center);
    form_.draw(canvas);
    list.rounded_rect({panel.x + 1030.0f, panel.y + 140.0f, 1.5f, 630.0f}, 0.0f,
                      Color::rgb(0x3880ff, 0.18f));
    // the focused row's text, then the job
    float y = panel.y + 170.0f;
    int lines = 0;
    for (const std::string &line : fonts.regular.font->wrap(side_text(), 21.0f, panel.w - 1100.0f))
    {
        if (++lines > 12)
            break;
        ui::text(list, fonts.regular, line, panel.x + 1060.0f, y, 21.0f, kMuted);
        y += 29.0f;
    }
    float fraction = -1.0f;
    const std::string job = view_ == kLibrary ? std::string() : job_line(&fraction);
    if (!job.empty())
    {
        const float jy = panel.y + 640.0f;
        ui::text(list, fonts.semibold, "Auftrag", panel.x + 1060.0f, jy, 22.0f, kInk);
        ui::text(list, fonts.regular, ui::fit_label(paint, job, 19.0f, panel.w - 1100.0f),
                 panel.x + 1060.0f, jy + 32.0f, 19.0f, fraction < 0.0f ? kWarning : kMuted);
        if (fraction >= 0.0f)
        {
            const Rect bar{panel.x + 1060.0f, jy + 50.0f, panel.w - 1100.0f, 12.0f};
            list.rounded_rect(bar, 6.0f, Color::rgb(0x3880ff, 0.18f));
            list.rounded_rect({bar.x, bar.y, bar.w * std::clamp(fraction, 0.0f, 1.0f), bar.h}, 6.0f,
                              kBlue);
        }
    }
    ui::HintLayout layout;
    layout.size = 34.0f;
    layout.text_size = 21.0f;
    layout.cy = panel.y + panel.h - 42.0f;
    const ui::Hint hints[] = {
        {ui::Button::cross, "Wählen"},
        {ui::Button::circle, view_ == kVersions || view_ == kLz4 ? "Zurück" : "Schließen"}};
    ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 2, panel.x + 40.0f, false, layout);
    list.pop_opacity();
    ask_version_.draw(canvas); // the version of ShadowMountPlus, once
    ask_.draw(canvas);         // "ShadowMountPlus umstellen?" on top of everything
}

} // namespace hui::ps5cc
