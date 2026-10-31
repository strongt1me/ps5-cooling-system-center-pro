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
    kTraceStart,
    kTraceCollect,
    kTraceGenerate,
    kTraceStop,
    kInstall = 40,
    kRestore,
    kRemove,
    kTested = 60,
    kLevel,
    kMode,
    kTarget,
    kPlanText,
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
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
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
            form_.set_value_text(kPlanText, plan_text());
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
                                                  : "eine unbekannte Fassung ist eingebaut";
        if (str(j.root, "place") == "backport")
            state += " · im Backport-Ordner";
        if (yes(inst, "has_orig"))
            state += " · Original gesichert";
        if (yes(j.root, "packed"))
            state += " · Spiel ist LZ4-gepackt";
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
        form_.add_header("LZ4-ASSET-PACKS");
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
            ui::FormRow &u = form_.add_action(kLz4Unpack, "LZ4 entpacken");
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
            if (!yes(j.root, "can"))
            {
                r.disabled = true;
                r.description = str(j.root, "why");
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
    version_names_.clear();
    form_.add_header("VERSIONEN");
    int i = 0;
    const cJSON *v = nullptr;
    cJSON_ArrayForEach(v, get(j.root, "versions"))
    {
        std::string label = str(v, "label");
        if (!sha.empty() && str(v, "sha256") == sha)
            label += " (eingebaut)";
        ui::FormRow &r = form_.add_action(kVersion + i, label.empty() ? str(v, "name") : label);
        std::string about;
        if (yes(v, "pack"))
            about += "LZ4 · ";
        if (yes(v, "debug"))
            about += "mit Protokoll · ";
        if (yes(v, "bundled"))
            about += "mitgeliefert · ";
        about +=
            !sha.empty() && str(v, "sha256") == sha ? "so eingebaut" : "X-Taste baut diese ein";
        r.description = about;
        r.disabled = !yes(v, "valid") || (packed && !yes(v, "pack"));
        if (packed && !yes(v, "pack"))
            r.description = "Das Spiel ist gepackt: nur eine LZ4-Fassung passt.";
        version_names_.push_back(str(v, "name"));
        ++i;
    }
    if (i == 0)
        form_.add_value(
            kVersion, versions_.json.empty() ? "Wird geladen …" : "Noch keine Versionen im Ordner",
            "");
    form_.add_header("ORDNER UND ORIGINAL");
    form_.add_action(kInstall, "Mitgelieferte Versionen in den Ordner legen");
    if (yes(inst, "has_orig"))
        form_.add_action(kRestore, "Original wiederherstellen");
    else if (yes(inst, "present"))
        form_.add_action(kRemove, "Entfernen").description =
            "Nimmt die eingebaute Fassung wieder heraus.";
}

void GameExtras::build_lz4()
{
    Json j(extras_.lz4);
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

void GameExtras::ask_plan()
{
    static const int kLevels[3] = {9, 3, 12};
    const int level = kLevels[std::clamp(form_.choice_index(kLevel), 0, 2)];
    const int mode = form_.choice_index(kMode);
    const std::string profile = mode >= 0 && mode < static_cast<int>(profile_ids_.size())
                                    ? profile_ids_[static_cast<std::size_t>(mode)]
                                    : std::string();
    source().extras_lz4_plan(game_.id, level, profile);
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
        if (i < version_names_.size())
            source().extras_post("library/emu", "{\"id\":" + gid + ",\"lib\":\"" + lib_ +
                                                    "\",\"action\":\"apply\",\"name\":" +
                                                    json_text(version_names_[i]) + "}");
        view_ = kMain;
        key_.clear();
        build();
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
        const int mode = form_.choice_index(kMode);
        const std::string profile = mode >= 0 && mode < static_cast<int>(profile_ids_.size())
                                        ? profile_ids_[static_cast<std::size_t>(mode)]
                                        : std::string();
        source().extras_post(
            "library/lz4",
            "{\"id\":" + gid + ",\"kind\":\"" +
                (form_.choice_index(kTarget) == 1 ? "copy" : "pack") + "\",\"level\":" +
                std::to_string(kLevels[std::clamp(form_.choice_index(kLevel), 0, 2)]) +
                ",\"profile\":" + json_text(profile) + "}");
        view_ = kMain;
        key_.clear();
        build();
        break;
    }
    default:
        break;
    }
}

void GameExtras::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (view_ == kClosed)
        return;
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
    }
}

void GameExtras::update(float dt)
{
    clock_ += dt;
    form_.update(dt);
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
        : view_ == kLz4 ? "Mit LZ4 packen · " + game_.name
                        : "Extras · " + game_.name;
    ui::text(list, fonts.display, ui::fit_label(paint, title, 36.0f, panel.w - 80.0f),
             panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
    ui::text(list, fonts.regular,
             view_ == kLibrary ? "für alle Spiele"
                               : "AMPR-Index, APR-EMU, PlayGo, Plugins, LZ4 und Mitschnitt",
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
}

} // namespace hui::ps5cc
