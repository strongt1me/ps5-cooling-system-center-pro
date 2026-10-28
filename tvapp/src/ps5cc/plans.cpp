// ps5cc - The game tools' plans: parsing, the rules that refuse a target, the bodies that start a job.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/plans.hpp"

#include "third_party/cjson/cJSON.h"

#include <cstdio>

namespace hui::ps5cc
{

namespace
{

// The room an image unpack keeps free beside the files (imgjob.c, IJ_RESERVE).
constexpr std::uint64_t kUnpackReserve = 256ull * 1024ull * 1024ull;

std::string str(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}

double num(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) && item->valuedouble > 0.0 ? item->valuedouble : 0.0;
}

std::uint64_t bytes(const cJSON *object, const char *name)
{
    return static_cast<std::uint64_t>(num(object, name));
}

bool flag(const cJSON *object, const char *name)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, name));
}

// The web app tests "enough_space === false": a missing field is not a refusal.
int tri(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsBool(item) ? (cJSON_IsTrue(item) ? 1 : 0) : -1;
}

PlanDest parse_dest(const cJSON *object, const char *path_key)
{
    PlanDest d;
    if (!cJSON_IsObject(object))
        return d;
    d.present = true;
    d.path = str(object, path_key);
    d.exists = flag(object, "exists");
    d.unfinished = flag(object, "unfinished");
    d.unfinished_bytes = bytes(object, "unfinished_bytes");
    d.enough_space = tri(object, "enough_space");
    d.path_too_long = flag(object, "path_too_long");
    return d;
}

std::string base_name(const std::string &path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string quoted(const std::string &text)
{
    return "„" + text + "“";
}

std::string json_text(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    std::string out = text != nullptr ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

bool chosen(const Plan &plan, const PlanChoice &choice)
{
    return choice.drive >= 0 && choice.drive < static_cast<int>(plan.drives.size());
}

} // namespace

bool is_image(const Game &game)
{
    const std::string &f = game.format;
    return !f.empty() && f != "folder" && f != "pkg" && f != "fpkg" && !game.path.empty() && game.path[0] == '/';
}

std::vector<Tool> tools_for(const Game &game)
{
    std::vector<Tool> out;
    if (game.can_copy)
        out.push_back(Tool::copy);
    if (game.can_convert)
        out.push_back(Tool::convert);
    if (game.can_move)
        out.push_back(Tool::move);
    if (game.can_unpack)
        out.push_back(Tool::unpack);
    if (is_image(game))
    {
        out.push_back(Tool::image_unpack);
        out.push_back(Tool::image_check);
        // Repaired is only a compressed image whose size disagrees with its header (imgjob.c).
        if (game.format == "ffpfsc")
            out.push_back(Tool::image_repair);
    }
    out.push_back(Tool::erase);
    return out;
}

const char *tool_label(Tool tool)
{
    switch (tool)
    {
    case Tool::copy:
        return "Kopieren";
    case Tool::convert:
        return "Konvertieren";
    case Tool::move:
        return "Verschieben";
    case Tool::unpack:
        return "Entpacken";
    case Tool::image_unpack:
        return "In einen Ordner entpacken";
    case Tool::image_check:
        return "Abbild prüfen";
    case Tool::image_repair:
        return "Abbild reparieren";
    case Tool::erase:
        return "Löschen";
    case Tool::count:
        break;
    }
    return "";
}

JobKind job_of(Tool tool)
{
    switch (tool)
    {
    case Tool::copy:
        return JobKind::copy;
    case Tool::convert:
        return JobKind::convert;
    case Tool::move:
    case Tool::unpack:
        return JobKind::storage;
    case Tool::erase:
        return JobKind::deletion;
    default:
        return JobKind::image;
    }
}

std::string fs_label(const std::string &fs)
{
    if (fs == "exfatfs" || fs == "exfat")
        return "exFAT";
    if (fs == "msdosfs")
        return "FAT32";
    if (fs == "ufs" || fs == "nullfs")
        return "UFS";
    return fs;
}

Plan parse_plan(Tool tool, const PlanReply &reply)
{
    Plan p;
    p.tool = tool;
    p.id = reply.id;
    if (!reply.ok)
    {
        p.error = reply.error.empty() ? "Die App hat nicht geantwortet." : reply.error;
        return p;
    }
    cJSON *root = cJSON_Parse(reply.json.c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        p.error = "Die Antwort der App ist nicht lesbar.";
        return p;
    }
    p.ok = true;
    const std::string id = str(root, "title_id");
    if (!id.empty())
        p.id = id;
    else if (!str(root, "id").empty())
        p.id = str(root, "id");
    p.name = str(root, "name");
    p.source = str(root, "source");
    p.kind = str(root, "kind");
    p.running = flag(root, "running");
    p.busy = flag(root, "busy");
    p.other_busy = flag(root, "other_busy");
    p.size_bytes = bytes(root, "size_bytes");
    p.files = bytes(root, "files");
    p.skipped = bytes(root, "skipped");

    p.ps4 = flag(root, "ps4");
    p.raw_bytes = bytes(root, "raw_bytes");
    p.ffpkg_bytes = bytes(root, "ffpkg_bytes");
    p.ffpkg_error = str(root, "ffpkg_error");
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "ops"))
    {
        // Only formats this screen knows by name: the value is text like any other.
        if (!cJSON_IsString(item) || item->valuestring == nullptr)
            continue;
        for (const char *key : kOpKeys)
        {
            if (std::string(item->valuestring) == key)
                p.ops.push_back(key);
        }
    }
    const cJSON *risk = cJSON_GetObjectItemCaseSensitive(root, "stream_risk");
    p.stream.risky = flag(risk, "risky");
    p.stream.big_data = flag(risk, "big_data");
    p.stream.data_bytes = bytes(risk, "data_bytes");
    p.stream.container_bytes = bytes(risk, "container_bytes");
    p.stream.stream_dir = str(risk, "stream_dir");
    p.stream.stream_dir_bytes = bytes(risk, "stream_dir_bytes");
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(risk, "containers"))
    {
        if (cJSON_IsString(item) && item->valuestring != nullptr)
            p.stream.containers.push_back(item->valuestring);
    }

    p.op = str(root, "op");
    p.image_type = str(root, "image_type");

    p.damaged = flag(root, "damaged");
    p.format = str(root, "format");
    p.why = str(root, "why");
    p.note = str(root, "note");
    p.can_check = flag(root, "can_check");
    p.can_unpack = flag(root, "can_unpack");
    p.image_bytes = bytes(root, "image_bytes");
    if (p.source.empty())
        p.source = str(root, "image");
    const cJSON *repair = cJSON_GetObjectItemCaseSensitive(root, "repair");
    if (cJSON_IsObject(repair))
    {
        p.repair_state = str(repair, "state");
        p.repair_why = str(repair, "why");
        p.repair_declared = bytes(repair, "declared");
        p.repair_actual = bytes(repair, "actual");
    }
    const cJSON *last = cJSON_GetObjectItemCaseSensitive(root, "last_check");
    if (cJSON_IsObject(last))
    {
        p.checked = true;
        p.check_clean = flag(last, "clean");
        p.check_when = static_cast<std::int64_t>(num(last, "when"));
        p.check_files = bytes(last, "files");
        p.check_bad_files = bytes(last, "bad_files");
        p.check_container = str(last, "container");
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(last, "bad"))
        {
            const std::string path = str(item, "path");
            if (!path.empty())
                p.check_bad.push_back(path);
        }
    }

    p.path = str(root, "path");
    p.version = str(root, "version");
    p.keeps = str(root, "keeps");
    p.confirm = str(root, "confirm");
    p.can_delete = flag(root, "can_delete");
    if (tool == Tool::erase)
    {
        // The delete plan says what kind of thing it is ("installed", "smp") and why it refuses.
        p.why = str(root, "why");
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "items"))
        {
            if (cJSON_IsString(item) && item->valuestring != nullptr)
                p.items.push_back(item->valuestring);
        }
    }

    // Copy, convert and the image unpack list drives ("targets"); move and unpack list folders ("destinations").
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "targets"))
    {
        PlanDrive d;
        d.mount = str(item, "mount");
        d.label = str(item, "label");
        if (d.label.empty())
            d.label = d.mount;
        d.fs = str(item, "fs");
        d.free_bytes = bytes(item, "free_bytes");
        d.enough_space = flag(item, "enough_space");
        d.fat32_too_big = flag(item, "fat32_too_big");
        const cJSON *ffpkg = cJSON_GetObjectItemCaseSensitive(item, "fat32_too_big_ffpkg");
        d.fat32_too_big_ffpkg = cJSON_IsBool(ffpkg) ? cJSON_IsTrue(ffpkg) : d.fat32_too_big;
        d.source_here = flag(item, "source_here");
        d.exists = flag(item, "exists");
        d.path_too_long = flag(item, "path_too_long");
        d.path = str(item, "path");
        for (int m = 0; m < kModes; ++m)
        {
            const cJSON *mode = cJSON_GetObjectItemCaseSensitive(item, kModeKeys[m]);
            if (tool == Tool::copy)
            {
                d.copy[static_cast<std::size_t>(m)] = parse_dest(mode, "path");
                continue;
            }
            for (int o = 0; o < kOps; ++o)
                d.convert[static_cast<std::size_t>(m)][static_cast<std::size_t>(o)] =
                    parse_dest(cJSON_GetObjectItemCaseSensitive(mode, kOpKeys[o]), "path");
        }
        if (!d.mount.empty())
            p.drives.push_back(std::move(d));
    }
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "destinations"))
    {
        PlanDrive d;
        d.mount = str(item, "path");
        d.label = d.mount;
        d.drive = str(item, "drive");
        d.fs = str(item, "fs");
        d.free_bytes = bytes(item, "free_bytes");
        d.exists = flag(item, "exists");
        d.here = flag(item, "here");
        d.enough_space = flag(item, "enough_space");
        d.fat32_too_big = flag(item, "fat32_too_big");
        d.target = str(item, "target");
        d.target_exists = flag(item, "target_exists");
        if (!d.mount.empty())
            p.drives.push_back(std::move(d));
    }
    cJSON_Delete(root);
    return p;
}

std::string drive_blocked(const Plan &p, const PlanChoice &c, const PlanDrive &d)
{
    switch (p.tool)
    {
    case Tool::copy:
    {
        const std::string room = "Zu wenig Platz: Die Kopie braucht " + format_bytes(p.size_bytes);
        if (d.fat32_too_big)
            return "FAT32 nimmt keine Datei über 4 GB auf";
        if (!d.enough_space)
            return room;
        // The drive-wide figure is the best case over both destinations; what counts is the one chosen (its own
        // unfinished leftover is removed first and gives its room back only to it).
        if (c.mode < 0 || c.mode >= kModes)
            return "";
        const PlanDest &dest = d.copy[static_cast<std::size_t>(c.mode)];
        if (!dest.present)
            return "";
        if (dest.enough_space == 0)
            return room;
        if (dest.path_too_long)
            return "Ein Pfad im Spiel würde dort zu lang";
        if (dest.path == p.source)
            return "Hier liegt das Original";
        if (dest.exists)
            return "Dort gibt es diesen Ordner schon";
        return "";
    }
    case Tool::convert:
    {
        const bool ffpkg = c.op == 1;
        const std::uint64_t need = ffpkg && p.ffpkg_bytes > 0 ? p.ffpkg_bytes : p.raw_bytes;
        const std::string room = "Zu wenig Platz: Nötig sind bis zu " + format_bytes(need);
        if (ffpkg ? d.fat32_too_big_ffpkg : d.fat32_too_big)
            return "FAT32 nimmt keine Datei über 4 GB auf";
        if (!d.enough_space)
            return room;
        if (c.mode < 0 || c.mode >= kModes || c.op < 0 || c.op >= kOps)
            return "";
        const PlanDest &t = d.convert[static_cast<std::size_t>(c.mode)][static_cast<std::size_t>(c.op)];
        if (!t.present)
            return "";
        if (t.enough_space == 0)
            return room;
        if (t.exists)
            return "Dort gibt es diese Datei schon";
        return "";
    }
    case Tool::move:
    case Tool::unpack:
    {
        const bool unpack = p.tool == Tool::unpack;
        if (!unpack && d.here)
            return "Hier liegt das Spiel schon";
        if (d.fat32_too_big)
            return "FAT32 nimmt keine Datei über 4 GB auf";
        if (!d.enough_space)
            return "Zu wenig Platz: Nötig sind mindestens " + format_bytes(p.size_bytes);
        if (d.target_exists)
            return unpack ? "Dort gibt es den Ordner " + p.id + " schon"
                          : "Dort gibt es schon " + quoted(base_name(p.source));
        return "";
    }
    case Tool::image_unpack:
        if (d.exists)
            return "Dort gibt es den Ordner schon.";
        if (!d.enough_space)
            return "Nicht genug Platz: nötig sind " + format_bytes(p.size_bytes + kUnpackReserve) + ".";
        if (d.fat32_too_big)
            return "FAT32 nimmt keine Dateien über 4 GB.";
        if (d.path_too_long)
            return "Ein Pfad im Abbild wäre dort zu lang.";
        return "";
    default:
        return "";
    }
}

std::string run_blocked(const Plan &p)
{
    if (!p.ok)
        return p.error;
    switch (p.tool)
    {
    case Tool::copy:
        if (p.running)
            return "Das Spiel läuft gerade. Bitte erst beenden: Es schreibt womöglich in seinen Ordner.";
        if (p.busy)
            return "Es läuft schon eine Kopie.";
        if (p.other_busy)
            return "Es läuft gerade eine Konvertierung. Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig.";
        return "";
    case Tool::convert:
        if (p.running)
            return "Das Spiel läuft gerade. Bitte erst beenden.";
        if (p.busy)
            return "Es läuft schon eine Konvertierung.";
        if (p.other_busy)
            return "Es läuft gerade eine Kopie. Kopieren, Konvertieren und Verschieben laufen nicht gleichzeitig.";
        if (p.ops.empty())
            return "Für dieses Spiel bietet die App kein Format an.";
        return "";
    case Tool::move:
    case Tool::unpack:
        return p.running ? "Das Spiel läuft gerade. Bitte erst beenden." : "";
    case Tool::image_unpack:
        if (p.damaged)
            return "Das Abbild ist beschädigt: ein Ordner darin lässt sich nicht lesen. Zuerst „Abbild prüfen“.";
        if (p.busy)
            return "Es läuft schon ein Vorgang an einem Abbild.";
        return "";
    case Tool::image_check:
        if (!p.can_check)
            return !p.why.empty() ? p.why : !p.note.empty() ? p.note : "Dieses Abbild lässt sich nicht prüfen.";
        return "";
    case Tool::image_repair:
        if (p.repair_state == "fixable")
            return "";
        if (p.repair_state == "fine")
            return "Nichts zu reparieren: Die Größe der Datei stimmt.";
        if (p.repair_state == "broken")
            return "Nicht reparierbar" + (p.repair_why.empty() ? std::string(".") : ": " + p.repair_why);
        return !p.why.empty() ? p.why : "Repariert wird nur ein komprimiertes Abbild (ffpfsc).";
    case Tool::erase:
        return p.can_delete ? "" : (p.why.empty() ? "Das lässt sich gerade nicht löschen." : p.why);
    case Tool::count:
        break;
    }
    return "";
}

const PlanDest *dest_of(const Plan &p, const PlanChoice &c)
{
    if (!chosen(p, c) || c.mode < 0 || c.mode >= kModes)
        return nullptr;
    const PlanDrive &d = p.drives[static_cast<std::size_t>(c.drive)];
    const PlanDest *dest = nullptr;
    if (p.tool == Tool::copy)
        dest = &d.copy[static_cast<std::size_t>(c.mode)];
    else if (p.tool == Tool::convert && c.op >= 0 && c.op < kOps)
        dest = &d.convert[static_cast<std::size_t>(c.mode)][static_cast<std::size_t>(c.op)];
    return dest != nullptr && dest->present ? dest : nullptr;
}

std::string target_of(const Plan &p, const PlanChoice &c)
{
    switch (p.tool)
    {
    case Tool::copy:
    case Tool::convert:
    {
        const PlanDest *dest = dest_of(p, c);
        return dest != nullptr ? dest->path : "";
    }
    case Tool::move:
    case Tool::unpack:
    case Tool::image_unpack:
    {
        if (!chosen(p, c))
            return "";
        const PlanDrive &d = p.drives[static_cast<std::size_t>(c.drive)];
        if (p.tool == Tool::image_unpack)
            return d.path;
        if (p.tool == Tool::unpack)
            return d.target;
        return d.mount + "/" + base_name(p.source);
    }
    default:
        return "";
    }
}

bool ready(const Plan &p, const PlanChoice &c)
{
    if (!run_blocked(p).empty())
        return false;
    switch (p.tool)
    {
    case Tool::copy:
    case Tool::convert:
    case Tool::move:
    case Tool::unpack:
    case Tool::image_unpack:
        if (!chosen(p, c) || !drive_blocked(p, c, p.drives[static_cast<std::size_t>(c.drive)]).empty())
            return false;
        if (p.tool == Tool::convert && (c.op < 0 || (c.op == 1 && !p.ffpkg_error.empty())))
            return false;
        return !target_of(p, c).empty();
    case Tool::erase:
        return !p.confirm.empty();
    default:
        return true;
    }
}

PlanChoice first_choice(const Plan &p, const Game *game)
{
    PlanChoice c;
    if (p.tool == Tool::convert)
    {
        for (const std::string &op : p.ops)
        {
            if (op == "ffpkg" && !p.ffpkg_error.empty())
                continue;
            for (int o = 0; o < kOps; ++o)
            {
                if (op == kOpKeys[o] && c.op < 0)
                    c.op = o;
            }
        }
        c.ampr = game != nullptr && game->ampr_emu;
    }
    // Copy and convert as the web app: the first drive that is not the source's and has room; convert then any with
    // room. The others start on the first drive that can be chosen at all.
    for (std::size_t i = 0; i < p.drives.size() && c.drive < 0; ++i)
    {
        const PlanDrive &d = p.drives[i];
        if (p.tool == Tool::copy || p.tool == Tool::convert)
        {
            if (!d.source_here && d.enough_space && !d.fat32_too_big)
                c.drive = static_cast<int>(i);
        }
        else if (drive_blocked(p, c, d).empty())
        {
            c.drive = static_cast<int>(i);
        }
    }
    for (std::size_t i = 0; i < p.drives.size() && c.drive < 0 && p.tool == Tool::convert; ++i)
    {
        if (p.drives[i].enough_space && !p.drives[i].fat32_too_big)
            c.drive = static_cast<int>(i);
    }
    return c;
}

std::string start_body(const Plan &p, const PlanChoice &c)
{
    cJSON *root = cJSON_CreateObject();
    const std::string drive = chosen(p, c) ? p.drives[static_cast<std::size_t>(c.drive)].mount : "";
    const char *mode = c.mode >= 0 && c.mode < kModes ? kModeKeys[c.mode] : "";
    switch (p.tool)
    {
    case Tool::copy:
        cJSON_AddStringToObject(root, "id", p.id.c_str());
        cJSON_AddStringToObject(root, "target", drive.c_str());
        cJSON_AddStringToObject(root, "mode", mode);
        break;
    case Tool::convert:
        cJSON_AddStringToObject(root, "id", p.id.c_str());
        cJSON_AddStringToObject(root, "op", c.op >= 0 && c.op < kOps ? kOpKeys[c.op] : "");
        cJSON_AddStringToObject(root, "target", drive.c_str());
        cJSON_AddStringToObject(root, "mode", mode);
        // The AMPR index is a PS5 thing and only goes into an image built from a folder.
        cJSON_AddBoolToObject(root, "ampr_index", p.kind == "folder" && !p.ps4 && c.ampr);
        break;
    case Tool::move:
    case Tool::unpack:
        cJSON_AddStringToObject(root, "id", p.id.c_str());
        cJSON_AddStringToObject(root, "op", p.tool == Tool::unpack ? "unpack" : "move");
        cJSON_AddStringToObject(root, "target", drive.c_str());
        cJSON_AddBoolToObject(root, "delete_source", p.tool == Tool::unpack && c.delete_source);
        break;
    case Tool::image_unpack:
    case Tool::image_check:
    case Tool::image_repair:
        cJSON_AddStringToObject(root, "kind", p.tool == Tool::image_unpack  ? "unpack"
                                              : p.tool == Tool::image_check ? "check"
                                                                            : "repair");
        cJSON_AddStringToObject(root, "id", p.id.c_str());
        cJSON_AddStringToObject(root, "target", p.tool == Tool::image_unpack ? drive.c_str() : "");
        break;
    case Tool::erase:
        cJSON_AddStringToObject(root, "kind", "game");
        cJSON_AddStringToObject(root, "id", p.id.c_str());
        cJSON_AddStringToObject(root, "confirm", p.confirm.c_str());
        break;
    case Tool::count:
        break;
    }
    return json_text(root);
}

std::vector<std::string> stream_reasons(const Plan &p)
{
    std::vector<std::string> out;
    if (!p.stream.risky)
        return out;
    if (p.stream.big_data)
        out.push_back("Es hat sehr viele Daten (" + format_bytes(p.stream.data_bytes) + ").");
    if (p.stream.container_bytes > 0)
    {
        std::string names;
        for (const std::string &n : p.stream.containers)
            names += (names.empty() ? "" : ", ") + n;
        out.push_back("Es enthält große Streaming-Container (" + names + ", zusammen " +
                      format_bytes(p.stream.container_bytes) + ").");
    }
    if (!p.stream.stream_dir.empty())
        out.push_back("Es hat einen Streaming-Ordner (" + quoted(p.stream.stream_dir) + ", " +
                      format_bytes(p.stream.stream_dir_bytes) + ").");
    return out;
}

} // namespace hui::ps5cc
