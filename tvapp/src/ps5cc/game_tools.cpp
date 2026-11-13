// ps5cc - The game tools of Spiele → Bibliothek: the action menu (□) and the plan dialog of each
// tool.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/game_tools.hpp"

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ui/glyphs.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ps5cc
{

namespace
{

constexpr int kExtrasTag = 200;
constexpr int kLibraryTag = 201;
constexpr int kRemountTag = 202; // + 0 remount, 1 unmount, 2 mount
constexpr int kCloseTag = 205;

using gfx::Color;
using gfx::Rect;

enum Row
{
    kRowMode = 1,
    kRowOp,
    kRowAmpr,
    kRowVerify,
    kRowName,
    kRowQueue,
    kRowQueueView,
    kRowDeleteSource,
    kRowStart,
    kRowCancel, // the delete question's "Abbrechen", where its focus opens
    kRowInfo = 20,
    kRowDrive = 100,
};

const Rect kPanel{200.0f, 92.0f, 1520.0f, 864.0f};

std::string quoted(const std::string &text)
{
    return "„" + text + "“";
}

std::string files_text(std::uint64_t n)
{
    return std::to_string(n) + (n == 1 ? " Datei" : " Dateien");
}

const char *op_title(int op, bool folder)
{
    switch (op)
    {
    case 0:
        return folder ? "ffpfsc · komprimiert (empfohlen)" : "ffpfsc · komprimiert";
    case 1:
        return "ffpkg (UFS2) · unkomprimiert";
    default:
        return "exFAT-Abbild · unkomprimiert";
    }
}

std::string op_text(const Plan &p, int op)
{
    switch (op)
    {
    case 0:
        return p.kind == "folder" ? "Der Ordner wird als exFAT-Abbild in einen komprimierten "
                                    "PFS-Container gepackt, in einem "
                                    "Durchgang ohne Zwischendatei."
                                  : "Das Abbild wird in einen komprimierten PFS-Container gepackt.";
    case 1:
        return "Ein UFS2-Abbild direkt aus dem Ordner mit 64-KiB-Blöcken, wie ShadowMountPlus es "
               "empfiehlt" +
               (p.ffpkg_bytes > 0 && p.ffpkg_error.empty()
                    ? ", " + format_bytes(p.ffpkg_bytes) + " groß."
                    : std::string("."));
    default:
        return "Ein exFAT-Abbild mit 64-KiB-Clustern, wie ShadowMountPlus es für seinen schnellen "
               "Weg braucht.";
    }
}

const char *delete_kind(const std::string &kind)
{
    if (kind == "installed")
        return "Installiertes Spiel";
    if (kind == "smp")
        return "Spiel von ShadowMountPlus";
    return "Spiel";
}

const char *loading_text(Tool tool)
{
    switch (tool)
    {
    case Tool::copy:
        return "Der Spielordner wird vermessen …";
    case Tool::convert:
        return "Das Spiel wird vermessen …";
    case Tool::move:
    case Tool::unpack:
        return "Ziele werden ermittelt …";
    case Tool::image_unpack:
        return "Das Abbild wird gezählt …";
    case Tool::erase:
        return "Die Konsole prüft, was gelöscht würde …";
    default:
        return "Das Abbild wird gelesen …";
    }
}

} // namespace

void GameTools::init(const ui::Theme &theme)
{
    theme_ = theme;
    menu_.style.theme = solid_theme(theme);
    menu_.style.width = 520.0f;
    menu_.style.scrim = 0.35f;
    menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});

    form_.style.theme = theme;
    form_.style.panel = false;
    form_.style.on_page = false;
    form_.style.row_height = 62.0f;
    form_.style.header_height = 46.0f;
    form_.style.label_size = 24.0f;
    form_.style.value_size = 21.0f;
    form_.style.description_size = 19.0f;
    form_.style.control_width = 520.0f;
    form_.style.label_ratio = 0.4f;
    form_.style.on_text = "An";
    form_.style.off_text = "Aus";
    form_.set_bounds({kPanel.x + 24.0f, kPanel.y + 130.0f, 880.0f, 650.0f});
    // Drives as radio buttons: the chosen one is filled, a drive that cannot be chosen says why in
    // its place.
    form_.trailing =
        [this](ui::Canvas &canvas, const Rect &control, const ui::FormRow &row, float focus)
    {
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        const float cy = control.y + control.h * 0.5f;
        float right = control.x + control.w;
        if (row.id >= kRowDrive)
        {
            ui::Look look;
            look.focus = focus;
            look.disabled = row.disabled;
            paint.radio({right - 30.0f, cy - 15.0f, 30.0f, 30.0f},
                        choice_.drive == row.id - kRowDrive ? 1.0f : 0.0f, look);
            right -= 46.0f;
        }
        if (row.text.empty())
            return;
        const Color ink = row.disabled && row.id >= kRowDrive ? kWarning : kMuted;
        paint.label(ui::fit_label(paint, row.text, 20.0f, right - control.x), right, cy + 7.0f,
                    20.0f, ink, gfx::Align::right);
    };

    // The name of a converted image is typed on the kit's keyboard (the template's braces are on its symbol page).
    prompt_.style.theme = solid_theme(theme);
    prompt_.style.max_length = 190;
    prompt_.style.allow_empty = true;
    prompt_.style.auto_capital = false;
    prompt_.style.frosted = false;
    prompt_.style.done_label = "Fertig";
    prompt_.style.cancel_label = "Abbrechen";
    prompt_.set_title("Name der Ausgabe");
    prompt_.field.set_placeholder("Name des Ordners");
    prompt_.keyboard.style.bindings = ui::KeyboardBindings::standard();
    prompt_.keyboard.style.space_label = "Leer";
    prompt_.keyboard.style.done_label = "Fertig";
    prompt_.keyboard.set_layouts(german_layouts());

    // The queue of conversions: rows like the Pakete page's queue.
    queue_list_.style.theme = theme;
    queue_list_.style.panel = true;
    queue_list_.style.row_height = 84.0f;
    queue_list_.style.title_size = 24.0f;
    queue_list_.style.subtitle_size = 18.0f;
    queue_list_.style.value_size = 20.0f;
    queue_list_.style.highlight.kind = ui::HighlightKind::bar;
    queue_list_.style.badge_column = true;
    queue_list_.set_bounds({kPanel.x + 40.0f, kPanel.y + 140.0f, kPanel.w - 80.0f, 620.0f});
}

void GameTools::open_menu(const Game &game, bool running, ui::Feedback &feedback)
{
    game_ = game;
    menu_tools_ = tools_for(game);
    std::vector<ui::MenuItem> items;
    // The running game can be closed from here (user, 31.10.2026), at once like the web app's
    // "Spiel beenden": what is not saved in it is lost.
    if (running || game.id == running_id_)
    {
        ui::MenuItem close;
        close.label = "Spiel beenden";
        close.tag = kCloseTag;
        close.shortcut = "sofort";
        items.push_back(close);
        ui::MenuItem line;
        line.separator = true;
        items.push_back(line);
    }
    for (const Tool tool : menu_tools_)
    {
        if (tool == Tool::erase && !items.empty() && !items.back().separator)
        {
            ui::MenuItem line;
            line.separator = true;
            items.push_back(line);
        }
        ui::MenuItem item;
        item.label = tool_label(tool);
        item.tag = static_cast<int>(tool);
        item.danger = tool == Tool::erase;
        if (tool == Tool::move || tool == Tool::unpack)
            item.shortcut = "ShadowMountPlus";
        else if (tool == Tool::image_unpack)
            item.shortcut = "mit Prüfung";
        items.push_back(item);
    }
    // the extras of the web app's per-game window, then the tools for the whole library
    ui::MenuItem line;
    line.separator = true;
    items.push_back(line);
    ui::MenuItem extras;
    // LZ4 and zstd only work for dump folders; for an image the entry names what is left there (10.11.2026)
    extras.label = game.format == "folder" ? "Extras: AMPR, PlayGo, LZ4, zstd …" : "Extras: AMPR, PlayGo …";
    extras.tag = kExtrasTag;
    // as in the web app: only for games with a folder or image of their own, not for installed
    // packages
    extras.disabled = game.path.empty() || game.format == "pkg" || game.format == "fpkg" ||
                      game.format == "ps4mount";
    items.push_back(extras);
    // Taken out of ShadowMountPlus and put back (30.10.2026): for a game that hangs or should be
    // found anew after copying, moving or deleting. Mounted: "Neu einhängen" and "Aushängen", else
    // "Einhängen". Never while it runs.
    if (game.can_remount)
    {
        const bool live = game.id == running_id_;
        const auto add = [&](const char *label, int offset)
        {
            ui::MenuItem m;
            m.label = label;
            m.tag = kRemountTag + offset;
            m.shortcut = live ? "läuft gerade" : "ShadowMountPlus";
            m.disabled = live;
            items.push_back(m);
        };
        if (game.smp_mounted)
        {
            add("Neu einhängen", 0);
            add("Aushängen", 1);
        }
        else
            add("Einhängen", 2);
    }
    ui::MenuItem library;
    library.label = "Bibliothek: Doppelte Abbilder …";
    library.tag = kLibraryTag;
    items.push_back(library);
    menu_.set_items(items);
    // Above the card's "□ Aktionen", so the game's cover and name stay in sight.
    menu_.open({kRightX + 360.0f, kBottom - 80.0f, 140.0f, 44.0f}, feedback);
}

void GameTools::open_tool(Tool tool, ui::Feedback &feedback)
{
    // A running game is never deleted (the console app refuses too): said at once, without asking
    // it.
    if (tool == Tool::erase && game_.id == running_id_)
    {
        feedback.play(audio::Cue::error);
        if (toast)
            toast(ui::StatusKind::danger, quoted(game_.name) + " läuft gerade",
                  "Ein laufendes Spiel wird nicht gelöscht. Bitte zuerst beenden.");
        return;
    }
    open_dialog(tool, feedback);
}

void GameTools::open_dialog(Tool tool, ui::Feedback &feedback)
{
    tool_ = tool;
    open_ = true;
    plan_ = Plan{};
    plan_.tool = tool;
    choice_ = PlanChoice{};
    check_ = ConvertCheck{};
    check_key_.clear();
    check_pending_ = false;
    check_ticket_ = 0;
    name_mode_ = 0;
    form_.clear();
    awaiting_ = source().request_plan(tool, game_.id);
    if (tool == Tool::convert)
        source().convert_queue(""); // how many wait, for the row that opens the queue
    feedback.play(audio::Cue::open);
}

void GameTools::close_dialog(ui::Feedback &feedback)
{
    open_ = false;
    awaiting_ = 0;
    feedback.play(audio::Cue::back);
}

void GameTools::absorb(const Snapshot &data)
{
    running_id_ = data.status.game_id;
    convert_job_ = data.jobs[static_cast<std::size_t>(JobKind::convert)];
    if (data.convert_queue_json != queue_json_)
    {
        queue_json_ = data.convert_queue_json;
        queue_ = parse_queue(queue_json_);
        if (open_ && tool_ == Tool::convert && plan_.ok)
            refresh();
    }
    if (queue_open_)
        rebuild_queue();
    if (open_ && tool_ == Tool::convert && check_pending_ && check_ticket_ != 0 &&
        data.convert_check.ticket == check_ticket_)
    {
        check_ = parse_check(data.convert_check);
        check_pending_ = false;
        refresh();
    }
    if (!open_ || awaiting_ == 0 || data.plan.ticket != awaiting_)
        return;
    awaiting_ = 0;
    plan_ = parse_plan(tool_, data.plan);
    if (plan_.name.empty())
        plan_.name = game_.name;
    choice_ = first_choice(plan_, &game_);
    build_form();
    // "Löschen" chosen in the menu used to delete at once (wish of the user, 27.10.2026). Since
    // 06.11.2026 the dialog stays: it names what goes, and only its red "Löschen" row starts it
    // (the focus opens on "Abbrechen"; △ is no shortcut here).
}

bool GameTools::modal() const
{
    return menu_.is_open() || open_ || queue_open_;
}

bool GameTools::visible() const
{
    return menu_.visible() || open_ || shown_.value > 0.01f || queue_open_ ||
           queue_shown_.value > 0.01f;
}

void GameTools::build_form()
{
    form_.clear();
    op_index_.clear();
    if (!plan_.ok)
        return;
    const auto add_drives = [this](const char *header)
    {
        form_.add_header(header);
        for (std::size_t i = 0; i < plan_.drives.size(); ++i)
            form_.add_action(kRowDrive + static_cast<int>(i), plan_.drives[i].label);
        if (plan_.drives.empty())
            form_.add_value(
                kRowInfo,
                plan_.ps4 ? "Kein USB- oder M.2-Laufwerk gefunden" : "Kein Laufwerk gefunden", "");
    };
    const auto add_mode = [this](const char *header)
    {
        form_.add_header(header);
        form_
            .add_choice(kRowMode, "Ziel", {"bitte wählen", "A · Spielbar", "B · Sicherung"},
                        choice_.mode + 1)
            .description =
            "A: homebrew, ShadowMountPlus bindet es ein · B: PS5-Sicherung/Spiele, nur aufbewahrt";
    };
    int first = kRowStart;
    switch (tool_)
    {
    case Tool::copy:
        add_mode("1. Als was?");
        add_drives("2. Wohin?");
        first = kRowMode;
        break;
    case Tool::convert:
    {
        form_.add_header("1. Was soll entstehen?");
        std::vector<std::string> options;
        int index = 0;
        for (const std::string &op : plan_.ops)
        {
            for (int o = 0; o < kOps; ++o)
            {
                if (op != kOpKeys[o])
                    continue;
                if (o == choice_.op)
                    index = static_cast<int>(op_index_.size());
                op_index_.push_back(o);
                options.emplace_back(op_title(o, plan_.kind == "folder"));
            }
        }
        if (!options.empty())
            form_.add_choice(kRowOp, "Format", options, index).description =
                choice_.op >= 0 ? op_text(plan_, choice_.op) : std::string();
        if (plan_.kind == "folder" && !plan_.ps4)
            form_.add_toggle(kRowAmpr, "AMPR-Index vorher bauen", choice_.ampr).description =
                "Für Spiele mit APR-EMU: ampr_emu.index kommt mit ins Abbild.";
        add_mode("2. Als was?");
        add_drives("3. Wohin?");
        // 4. How the result is checked afterwards; the .ffpkg (PS4) quick check is one read instead of two, the others a sample
        form_.add_header("4. Prüfung danach");
        form_.add_choice(kRowVerify, "Prüfung", {"Gründlich (empfohlen)", "Schnell"}, choice_.quick ? 1 : 0)
            .description = choice_.quick
                               ? "Aufbau und Stichprobe, ohne Prüfsummen-Datei. Spart bei großen Spielen viel Zeit."
                               : "Liest das ganze Ergebnis noch einmal und legt die Prüfsummen-Datei an.";
        // 5. The name of the result: the source's own, the template of the console's names, or typed on the keyboard
        form_.add_header("5. Name der Ausgabe");
        form_.add_choice(kRowName, "Name",
                         {"Name des Ordners", "Titel [ID] vVersion", "Eigener Name …"}, name_mode_)
            .description = "Felder: {titel} {id} {version} {quelle}. Leer: der Name des Ordners.";
        form_.add_action(kRowQueueView, "Warteschlange ansehen").chevron = true;
        form_.add_action(kRowQueue, "Zur Warteschlange");
        first = options.empty() ? kRowMode : kRowOp;
        break;
    }
    case Tool::move:
    case Tool::unpack:
    case Tool::image_unpack:
        add_drives(tool_ == Tool::move ? "Wohin?" : "Wohin entpacken?");
        if (tool_ == Tool::unpack)
            form_.add_toggle(kRowDeleteSource, "Abbild danach löschen", choice_.delete_source)
                .description = "Erst wenn das Entpacken fertig und fehlerfrei ist.";
        first = choice_.drive >= 0 ? kRowDrive + choice_.drive : kRowStart;
        break;
    case Tool::image_check:
    case Tool::image_repair:
    {
        form_.add_header("Abbild");
        form_.add_value(kRowInfo, "Format", plan_.format);
        form_.add_value(kRowInfo + 1, "Größe",
                        plan_.image_bytes > 0 ? format_bytes(plan_.image_bytes) : "–");
        if (tool_ == Tool::image_check)
        {
            std::string last = "noch nie";
            if (plan_.checked)
                last = format_date(plan_.check_when) +
                       (plan_.check_clean ? " · in Ordnung" : " · Fehler gefunden");
            form_.add_value(kRowInfo + 2, "Zuletzt geprüft", last);
        }
        else
        {
            const std::string state =
                plan_.repair_state == "fixable"
                    ? "Größe stimmt nicht: " + format_bytes(plan_.repair_actual) + " statt " +
                          format_bytes(plan_.repair_declared)
                : plan_.repair_state == "broken" ? std::string("nicht reparierbar")
                : plan_.repair_state == "fine"   ? std::string("nichts zu reparieren")
                                                 : std::string("–");
            form_.add_value(kRowInfo + 2, "Zustand", state);
        }
        break;
    }
    case Tool::erase:
        form_.add_header("Spiel");
        form_.add_value(kRowInfo, "Art", delete_kind(plan_.kind));
        form_.add_value(kRowInfo + 1, "Titel-ID", plan_.id);
        if (!plan_.version.empty())
            form_.add_value(kRowInfo + 2, "Version", plan_.version);
        if (plan_.size_bytes > 0)
            form_.add_value(kRowInfo + 3, "Größe", format_bytes(plan_.size_bytes));
        break;
    case Tool::count:
        break;
    }
    if (tool_ == Tool::erase)
    {
        form_.add_action(kRowCancel, "Abbrechen");
        first = kRowCancel;
    }
    ui::FormRow &start_row = form_.add_action(kRowStart, start_label());
    start_row.danger = tool_ == Tool::erase;
    refresh();
    form_.set_active(true);
    form_.focus_row(first);
    form_.enter();
}

void GameTools::refresh()
{
    for (std::size_t i = 0; i < plan_.drives.size(); ++i)
    {
        ui::FormRow *row = form_.row(kRowDrive + static_cast<int>(i));
        if (row == nullptr)
            continue;
        const PlanDrive &d = plan_.drives[i];
        const std::string why = drive_blocked(plan_, choice_, d);
        if (!why.empty() && choice_.drive == static_cast<int>(i))
            choice_.drive = -1;
        std::string info = format_bytes(d.free_bytes) + " frei";
        if (!d.fs.empty())
            info += " · " + fs_label(d.fs);
        if (!d.drive.empty())
            info = d.drive + " · " + info;
        if (d.source_here)
            info += tool_ == Tool::image_unpack ? " · hier liegt das Abbild"
                                                : " · hier liegt das Original";
        if (d.here && tool_ == Tool::move)
            info += " · hier liegt das Spiel";
        row->disabled = !why.empty();
        row->text = why.empty() ? info : why;
        // Focused, a refused drive says all of it: the reason may not fit beside the name.
        row->description = why.empty() ? std::string() : why + " · " + info;
    }
    if (tool_ == Tool::convert)
    {
        request_check();
        if (ui::FormRow *row = form_.row(kRowQueue))
            row->disabled = !queue_ready();
        if (ui::FormRow *row = form_.row(kRowQueueView))
            row->text = queue_.open > 0 ? queue_summary(queue_) : std::string();
    }
    if (ui::FormRow *row = form_.row(kRowStart))
    {
        row->label = start_label();
        row->disabled = !start_ready();
    }
}

void GameTools::request_check()
{
    const std::string body = check_body(plan_, choice_);
    if (body == check_key_)
        return;
    check_key_ = body;
    check_ = ConvertCheck{};
    check_pending_ = !body.empty();
    check_ticket_ = body.empty() ? 0 : source().convert_check(body);
}

bool GameTools::start_ready() const
{
    if (tool_ != Tool::convert)
        return ready(plan_, choice_);
    return ready(plan_, choice_) && check_.ok && !check_pending_ && check_.can_start;
}

bool GameTools::queue_ready() const
{
    return tool_ == Tool::convert && ready(plan_, choice_) && check_.ok && !check_pending_ &&
           check_.can_queue;
}

std::string GameTools::start_label() const
{
    switch (tool_)
    {
    case Tool::copy:
        return "Kopieren starten";
    case Tool::convert:
        return "Konvertieren starten";
    case Tool::move:
        return "Verschieben starten";
    case Tool::unpack:
    case Tool::image_unpack:
        return "Entpacken starten";
    case Tool::image_check:
        return plan_.checked ? "Noch einmal prüfen" : "Prüfen";
    case Tool::image_repair:
        return "Reparieren";
    case Tool::erase:
        return "Löschen";
    case Tool::count:
        break;
    }
    return "Starten";
}

std::string GameTools::title() const
{
    const std::string name = quoted(plan_.name.empty() ? game_.name : plan_.name);
    switch (tool_)
    {
    case Tool::copy:
        return name + " kopieren";
    case Tool::convert:
        return name + " konvertieren";
    case Tool::move:
        return name + " verschieben";
    case Tool::unpack:
        return name + " entpacken";
    case Tool::image_unpack:
        return name + " in einen Ordner entpacken";
    case Tool::image_check:
        return "Abbild prüfen · " + name;
    case Tool::image_repair:
        return "Abbild reparieren · " + name;
    case Tool::erase:
        return name + " löschen";
    case Tool::count:
        break;
    }
    return name;
}

void GameTools::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (menu_.is_open())
    {
        if (menu_.handle(input, feedback) == ui::Event::activated)
        {
            const int focus = menu_.focus();
            if (focus >= 0 && focus < static_cast<int>(menu_.items().size()))
            {
                const int tag = menu_.items()[static_cast<std::size_t>(focus)].tag;
                if (tag == kCloseTag)
                {
                    // nothing asked twice; the status line says when it is gone
                    feedback.play(audio::Cue::select);
                    source().close_game(game_.id);
                }
                else if (tag == kExtrasTag)
                {
                    if (on_extras)
                        on_extras(game_, feedback);
                }
                else if (tag == kLibraryTag)
                {
                    if (on_library)
                        on_library(feedback);
                }
                else if (tag >= kRemountTag && tag <= kRemountTag + 2)
                {
                    // at once, nothing asked twice; the answer comes as a notice
                    static const char *const kActions[3] = {"remount", "unmount", "mount"};
                    feedback.play(audio::Cue::select);
                    source().remount_game(game_.id, kActions[tag - kRemountTag]);
                }
                else
                    open_tool(static_cast<Tool>(tag), feedback);
            }
        }
        return;
    }
    if (queue_open_)
    {
        handle_queue(input, feedback);
        return;
    }
    if (!open_)
        return;
    if (prompt_.is_open())
    {
        const ui::Event typed = prompt_.handle(input, feedback);
        if (typed == ui::Event::activated)
        {
            choice_.name = prompt_.text();
            name_mode_ = choice_.name.empty() ? 0 : choice_.name == kNameTemplate ? 1 : 2;
            form_.set_choice(kRowName, name_mode_);
            refresh();
        }
        else if (typed == ui::Event::cancelled)
            form_.set_choice(kRowName, name_mode_);
        return;
    }
    if (input.is_pressed(Action::back))
    {
        close_dialog(feedback);
        return;
    }
    if (!plan_.ok)
        return; // loading, or the error with only "Schließen"
    if (input.is_pressed(Action::north) && tool_ != Tool::erase)
    {
        press_start(feedback);
        return;
    }
    if (input.is_pressed(Action::west) && tool_ == Tool::convert)
    {
        press_queue(feedback);
        return;
    }
    const ui::Event event = form_.handle(input, feedback);
    const int id = form_.changed_id();
    if (event == ui::Event::changed)
    {
        if (id == kRowMode)
            choice_.mode = form_.choice_index(kRowMode) - 1;
        else if (id == kRowOp)
        {
            const int index = form_.choice_index(kRowOp);
            choice_.op = index >= 0 && index < static_cast<int>(op_index_.size())
                             ? op_index_[static_cast<std::size_t>(index)]
                             : -1;
            if (ui::FormRow *row = form_.row(kRowOp))
                row->description = choice_.op >= 0 ? op_text(plan_, choice_.op) : std::string();
        }
        else if (id == kRowName)
        {
            const int mode = form_.choice_index(kRowName);
            if (mode == 2)
            {
                // The keyboard opens with what stands there; Done keeps it, back leaves the row as it was.
                prompt_.open(feedback, choice_.name.empty() ? std::string(kNameTemplate) : choice_.name);
                return;
            }
            name_mode_ = mode;
            choice_.name = mode == 1 ? kNameTemplate : "";
        }
        else if (id == kRowAmpr)
            choice_.ampr = form_.toggle_value(kRowAmpr);
        else if (id == kRowVerify)
        {
            choice_.quick = form_.choice_index(kRowVerify) == 1;
            if (ui::FormRow *row = form_.row(kRowVerify))
                row->description =
                    choice_.quick
                        ? "Aufbau und Stichprobe, ohne Prüfsummen-Datei. Spart bei großen Spielen viel Zeit."
                        : "Liest das ganze Ergebnis noch einmal und legt die Prüfsummen-Datei an.";
        }
        else if (id == kRowDeleteSource)
            choice_.delete_source = form_.toggle_value(kRowDeleteSource);
        refresh();
    }
    else if (event == ui::Event::activated)
    {
        if (id >= kRowDrive)
        {
            choice_.drive = id - kRowDrive;
            refresh();
        }
        else if (id == kRowCancel)
        {
            close_dialog(feedback);
        }
        else if (id == kRowStart)
        {
            press_start(feedback);
        }
        else if (id == kRowQueue)
        {
            press_queue(feedback);
        }
        else if (id == kRowQueueView)
        {
            open_queue(feedback);
        }
    }
}

void GameTools::press_queue(ui::Feedback &feedback)
{
    if (!queue_ready())
    {
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        return;
    }
    // At once, nothing asked: the queue deletes nothing, and the conversion starts by itself when it is its turn.
    feedback.play(audio::Cue::select);
    source().convert_queue(queue_add_body(plan_, choice_));
    open_ = false;
    awaiting_ = 0;
    open_queue(feedback);
}

void GameTools::open_queue(ui::Feedback &feedback)
{
    open_ = false;
    awaiting_ = 0;
    prompt_.dismiss();
    queue_open_ = true;
    queue_poll_ = 0.0f;
    queue_key_.clear();
    rebuild_queue();
    queue_list_.set_active(true);
    queue_list_.enter();
    source().convert_queue("");
    feedback.play(audio::Cue::open);
}

void GameTools::close_queue(ui::Feedback &feedback)
{
    queue_open_ = false;
    feedback.play(audio::Cue::back);
}

std::string GameTools::queue_line(const QueueItem &item) const
{
    const std::string what = item.op + " · " + item.target;
    if (item.state == "running")
    {
        if (convert_job_.queue_id != item.qid)
            return "wird gestartet …";
        for (const StageText &s : stage_texts(convert_job_))
        {
            if (s.state == "now")
                return s.label + " " + s.detail;
        }
        return what;
    }
    if (item.state == "done")
        return "Fertig und geprüft" +
               (item.output_bytes > 0 ? ": " + format_bytes(item.output_bytes) : std::string()) +
               (item.elapsed_s > 0.0 ? " in " + duration_text(item.elapsed_s * 1000.0) : std::string());
    if (!item.message.empty())
        return item.message;
    return what;
}

void GameTools::rebuild_queue()
{
    std::string key;
    for (const QueueItem &q : queue_.items)
        key += std::to_string(q.qid) + q.state + ";";
    if (key != queue_key_)
    {
        queue_key_ = key;
        std::vector<ui::ListItem> items;
        for (const QueueItem &q : queue_.items)
        {
            ui::ListItem item;
            item.title = q.name;
            item.tag = q.qid;
            item.swatch = q.state == "done"      ? kGood
                          : q.state == "failed"  ? kCritical
                          : q.state == "cancelled" ? kWarning
                          : q.state == "running" ? Color::rgb(0x6aa2ff)
                                                 : kMuted;
            item.badge = q.state == "waiting"     ? "WARTET"
                         : q.state == "running"   ? "LÄUFT"
                         : q.state == "done"      ? "FERTIG"
                         : q.state == "cancelled" ? "ABGEBROCHEN"
                                                  : "FEHLER";
            items.push_back(item);
        }
        const int focus = queue_list_.focus();
        queue_list_.set_items(items);
        queue_list_.set_focus(std::clamp(focus, 0, std::max(static_cast<int>(items.size()) - 1, 0)), true);
    }
    // The words under the names change every second while a conversion runs: written in place, so the list does not start over.
    for (std::size_t i = 0; i < queue_.items.size() && i < queue_list_.items().size(); ++i)
        queue_list_.item(static_cast<int>(i)).subtitle = queue_line(queue_.items[i]);
}

void GameTools::handle_queue(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back))
    {
        close_queue(feedback);
        return;
    }
    const std::size_t at = static_cast<std::size_t>(std::max(queue_list_.focus(), 0));
    const QueueItem *item = at < queue_.items.size() ? &queue_.items[at] : nullptr;
    // Nothing here asks twice: removing takes an entry off the list (the queue deletes no file), the others are plain switches.
    if (input.is_pressed(Action::north))
    {
        if (!queue_.running && queue_.open == 0)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        feedback.play(audio::Cue::select);
        source().convert_queue(queue_action_body(queue_.running ? "stop" : "start"));
        return;
    }
    if (input.is_pressed(Action::west))
    {
        if (item == nullptr || item->state == "running")
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        feedback.play(audio::Cue::select);
        source().convert_queue(queue_action_body("remove", item->qid));
        return;
    }
    if (input.is_pressed(Action::page_next))
    {
        if (queue_.done + queue_.failed == 0)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        feedback.play(audio::Cue::select);
        source().convert_queue(queue_action_body("clear"));
        return;
    }
    if (queue_list_.handle(input, feedback) == ui::Event::activated)
    {
        if (item != nullptr && (item->state == "failed" || item->state == "cancelled"))
        {
            feedback.play(audio::Cue::select);
            source().convert_queue(queue_action_body("retry", item->qid));
        }
        else
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    }
}

void GameTools::press_start(ui::Feedback &feedback)
{
    if (!start_ready())
    {
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        return;
    }
    // No ffpfsc question here: the stutter warning stands in the dialog's side text (nothing is
    // asked twice).
    feedback.play(audio::Cue::select);
    start();
}

void GameTools::start()
{
    source().start_tool(tool_, plan_.id, start_body(plan_, choice_));
    open_ = false;
    awaiting_ = 0;
}

void GameTools::update(float dt)
{
    clock_ += dt;
    menu_.update(dt);
    form_.update(dt);
    prompt_.update(dt);
    queue_list_.update(dt);
    shown_.target = open_ ? 1.0f : 0.0f;
    shown_.update(dt, 18.0f);
    queue_shown_.target = queue_open_ ? 1.0f : 0.0f;
    queue_shown_.update(dt, 18.0f);
    if (queue_open_)
    {
        // The list is asked for again every two seconds while it is on screen.
        queue_poll_ += dt;
        if (queue_poll_ >= 2.0f)
        {
            queue_poll_ = 0.0f;
            source().convert_queue("");
        }
    }
}

float GameTools::draw_paragraph(ui::Canvas &canvas, const std::string &text, float x, float y,
                                float width, float size, Color ink, int max_lines) const
{
    int lines = 0;
    for (const std::string &line : canvas.fonts.regular.font->wrap(text, size, width))
    {
        if (++lines > max_lines)
            break;
        ui::text(canvas.list, canvas.fonts.regular, line, x, y, size, ink);
        y += size * 1.34f;
    }
    return y + size * 0.5f;
}

void GameTools::draw_side(ui::Canvas &canvas, const Rect &box) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const float x = box.x;
    const float w = box.w;
    float y = box.y + 26.0f;
    const auto heading = [&](const std::string &label, Color ink)
    {
        ui::text(list, fonts.semibold, label, x, y, 21.0f, ink);
        y += 30.0f;
    };
    const auto para = [&](const std::string &text, Color ink, int max_lines = 6)
    { y = draw_paragraph(canvas, text, x, y, w, 20.0f, ink, max_lines); };

    const std::string blocked = run_blocked(plan_);
    const std::string target = target_of(plan_, choice_);
    switch (tool_)
    {
    case Tool::copy:
    {
        heading("Quelle", kMuted);
        para(plan_.source, kInk, 2);
        para(format_bytes(plan_.size_bytes) + " in " + files_text(plan_.files) +
                 (plan_.skipped > 0 ? " · " + std::to_string(plan_.skipped) +
                                          " Verknüpfungen oder Sonderdateien bleiben weg"
                                    : ""),
             kMuted, 2);
        if (!target.empty())
        {
            heading("Ziel", kMuted);
            para(target, kInk, 2);
            const PlanDest *dest = dest_of(plan_, choice_);
            if (dest != nullptr && dest->unfinished)
                para("Dort liegt eine abgebrochene Kopie dieser App, sie wird ersetzt" +
                         (dest->unfinished_bytes > 0
                              ? " (" + format_bytes(dest->unfinished_bytes) + " werden dabei frei)."
                              : std::string(".")),
                     kWarning, 3);
        }
        if (choice_.mode == 0)
        {
            heading("ShadowMountPlus findet das Spiel danach zweimal", kWarning);
            para("Das Original und die Kopie haben beide die Titel-ID " + plan_.id +
                     ". Das kann beim Einbinden Probleme machen. Wolltest du das Spiel "
                     "verschieben, lösche nach der Kopie "
                     "das Original.",
                 kWarning, 4);
        }
        if (!blocked.empty())
            para(blocked, kCritical, 3);
        if (!target.empty())
            para("Jede Datei wird nach dem Schreiben zurückgelesen und verglichen; daneben "
                 "entsteht eine .sha256 mit den "
                 "Prüfsummen.",
                 kMuted, 3);
        break;
    }
    case Tool::convert:
    {
        heading("Quelle", kMuted);
        para(plan_.source, kInk, 1);
        para(plan_.kind == "folder"
                 ? files_text(plan_.files) + ", als exFAT-Abbild " + format_bytes(plan_.raw_bytes)
                 : format_bytes(plan_.raw_bytes),
             kMuted, 1);
        if (plan_.ps4)
        {
            heading("PS4-Spiel", kWarning);
            para("Ziel ist nur ein USB- oder M.2-Laufwerk: Ein PS4-Titel aus dem Speicher der "
                 "Konsole bringt sie zum "
                 "Absturz. Kein AMPR-Index. Keine Trophäen aus Abbildern, manche Titel hängen bei "
                 "„Bitte warten“: "
                 "Beides geht nur installiert (Pakete).",
                 kWarning, 5);
        }
        if (plan_.unsigned_exec.count > 0)
        {
            heading("Unsignierte Programmdateien", kWarning);
            para(std::to_string(plan_.unsigned_exec.count) +
                     " Programmdateien dieses Spiels sind einfache ELF-Dateien, die die Konsole "
                     "nicht startet. Das Abbild bekommt sie fake-signiert als FSELF; der "
                     "Spielordner bleibt, wie er ist.",
                 kWarning, 3);
            para(unsigned_names(plan_.unsigned_exec), kMuted, 1);
        }
        if (plan_.unsigned_exec.refused > 0)
            para(std::to_string(plan_.unsigned_exec.refused) +
                     " Programmdateien sind nicht signiert und ließen sich nicht signieren "
                     "(ungewöhnlicher Aufbau). Sie bleiben, wie sie sind; das Spiel startet damit "
                     "womöglich nicht.",
                 kCritical, 3);
        // The check before the start: everything that speaks for or against it in one list.
        heading("Prüfung vor dem Start", kMuted);
        y = draw_checks(canvas, x, y, w);
        if (!check_.target.empty())
            para(check_.target, kInk, 2);
        if (choice_.op == 1 && !plan_.ffpkg_error.empty())
            para("Bei diesem Spiel nicht möglich: " + plan_.ffpkg_error, kCritical, 3);
        if (choice_.op == 0 && !stream_reasons(plan_).empty())
            para("Achtung: Bei diesem Spiel sind Ruckler möglich (viele Daten oder "
                 "Texture-Streaming). Ein "
                 "komprimiertes Abbild muss beim Spielen ständig entpackt werden – exFAT oder "
                 "ffpkg vermeiden das.",
                 kWarning, 5);
        if (choice_.mode == 0)
            para("ShadowMountPlus findet das Spiel danach zweimal (Original und neues Abbild). "
                 "Lösche das Original erst, wenn das neue Abbild startet.",
                 kWarning, 3);
        if (!blocked.empty())
            para(blocked, kCritical, 3);
        break;
    }
    case Tool::move:
    case Tool::unpack:
    {
        heading("Jetzt", kMuted);
        para(plan_.source, kInk, 2);
        para(format_bytes(plan_.size_bytes), kMuted, 1);
        para(tool_ == Tool::unpack
                 ? "ShadowMountPlus entpackt das Abbild in einen Dump-Ordner namens " + plan_.id +
                       " im gewählten Ziel." +
                       (plan_.image_type == "pfsc"
                            ? " Ein komprimiertes Abbild wird dabei deutlich größer als die Datei."
                            : "")
                 : std::string(
                       "ShadowMountPlus verschiebt das Spiel selbst: Es kopiert die Daten, trägt "
                       "das Spiel am "
                       "neuen Ort ein und löscht es am alten erst, wenn alles angekommen ist."),
             kMuted, 5);
        if (!target.empty())
        {
            heading("Ziel", kMuted);
            para(target, kInk, 2);
            if (tool_ == Tool::move)
                para("Nach dem Verschieben gibt es das Spiel am alten Ort nicht mehr.", kWarning,
                     2);
            else if (choice_.delete_source)
                para("Die Abbild-Datei wird nach dem Entpacken gelöscht.", kWarning, 2);
        }
        if (!blocked.empty())
            para(blocked, kCritical, 3);
        break;
    }
    case Tool::image_unpack:
        heading("Abbild", kMuted);
        para(plan_.source, kInk, 2);
        para(files_text(plan_.files) + ", " + format_bytes(plan_.size_bytes) + " entpackt", kMuted,
             1);
        if (!target.empty())
        {
            heading("Ziel", kMuted);
            para(target, kInk, 2);
        }
        if (!blocked.empty())
            para(blocked, kCritical, 3);
        para("Jede Datei wird geschrieben, zurückgelesen und verglichen; erst wenn alles stimmt, "
             "bekommt der Ordner seinen "
             "Namen. Das Abbild bleibt unberührt.",
             kMuted, 4);
        break;
    case Tool::image_check:
        para(plan_.format == "ffpfsc"
                 ? "Liest den ganzen Container, jeden Block, und prüft die Dateien darin, ohne das "
                   "Abbild einzuhängen."
                 : "Liest jede Datei des Abbilds vollständig, ohne es einzuhängen.",
             kMuted, 3);
        para("Findet es etwas Defektes, nennt es die betroffenen Dateien beim Namen.", kMuted, 2);
        if (plan_.checked)
        {
            heading("Letzte Prüfung · " + format_date(plan_.check_when),
                    plan_.check_clean ? kGood : kCritical);
            if (plan_.check_clean)
                para("In Ordnung · " + files_text(plan_.check_files), kInk, 1);
            else
            {
                para(plan_.check_container.empty() ? "Im Abbild wurden Fehler gefunden."
                                                   : plan_.check_container,
                     kCritical, 3);
                for (std::size_t i = 0; i < plan_.check_bad.size() && i < 4; ++i)
                    para("• " + plan_.check_bad[i], kInk, 1);
            }
        }
        if (!plan_.note.empty())
            para(plan_.note, kMuted, 3);
        if (!blocked.empty())
            para(blocked, kCritical, 3);
        break;
    case Tool::image_repair:
        para("Nur, wo es sicher geht: Stimmt die Größe der Datei nicht mit dem Kopf des Abbilds "
             "überein (Daten "
             "vollständig, überzählige Bytes leer), setzt die App sie richtig und prüft danach den "
             "ganzen Container.",
             kMuted, 5);
        para("Defekte Blöcke lassen sich ohne Original nicht wiederherstellen.", kMuted, 2);
        if (!blocked.empty())
            para(blocked, kCritical, 3);
        break;
    case Tool::erase:
    {
        if (!plan_.items.empty())
        {
            heading("Gelöscht wird:", kInk);
            for (const std::string &item : plan_.items)
                para("• " + item, kInk, 2);
        }
        if (!plan_.keeps.empty())
            para("Bleibt liegen: " + plan_.keeps + " (die Spieldaten, auf die das Spiel verweist).",
                 kMuted, 3);
        heading("Deine Spielstände bleiben erhalten.", kGood);
        y += 6.0f;
        // The strong warning: a red plate, not only red text.
        const Rect plate{x - 14.0f, y - 30.0f, w + 14.0f, 92.0f};
        list.rounded_rect(plate, 12.0f, kCritical.with_alpha(0.16f));
        list.rounded_rect({plate.x, plate.y, 5.0f, plate.h}, 2.5f, kCritical);
        ui::text(list, fonts.semibold, "Das Löschen ist endgültig.", x + 8.0f, y, 22.0f, kCritical);
        y += 30.0f;
        ui::text(list, fonts.regular, "Es lässt sich nicht rückgängig machen.", x + 8.0f, y, 20.0f,
                 kInk);
        y += 54.0f;
        if (!blocked.empty())
            para(blocked, kCritical, 3);
        break;
    }
    case Tool::count:
        break;
    }
}

float GameTools::draw_checks(ui::Canvas &canvas, float x, float y, float width) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    constexpr float kSize = 19.0f;
    constexpr float kHintSize = 17.0f;
    if (check_key_.empty() || check_pending_ || (!check_.ok && check_.error.empty()))
    {
        ui::text(list, fonts.regular,
                 check_key_.empty() ? "Wähle Format, Art und Ziel, dann erscheint die Prüfung."
                                    : "Die Prüfung läuft …",
                 x, y, 20.0f, kMuted);
        return y + 40.0f;
    }
    if (!check_.ok)
        return draw_paragraph(canvas, check_.error, x, y, width, 20.0f, kCritical, 3);
    for (const CheckRow &row : check_.rows)
    {
        // A coloured dot, not a glyph: the check marks are not in the font.
        const Color tone = row.level == "error" ? kCritical : row.level == "warn" ? kWarning : kGood;
        list.rounded_rect({x, y - 14.0f, 12.0f, 12.0f}, 6.0f, tone);
        const int max_lines = row.level == "ok" ? 2 : 3;
        int lines = 0;
        for (const std::string &line : fonts.regular.font->wrap(row.text, kSize, width - 28.0f))
        {
            if (++lines > max_lines)
                break;
            ui::text(list, fonts.regular, line, x + 28.0f, y, kSize,
                     row.level == "error" ? kCritical : row.level == "warn" ? kWarning : kInk);
            y += kSize * 1.3f;
        }
        lines = 0;
        for (const std::string &line : fonts.regular.font->wrap(row.hint, kHintSize, width - 28.0f))
        {
            if (row.hint.empty() || ++lines > 2)
                break;
            ui::text(list, fonts.regular, line, x + 28.0f, y, kHintSize, kMuted);
            y += kHintSize * 1.3f;
        }
        y += 5.0f;
    }
    return y + 4.0f;
}

void GameTools::draw_queue(ui::Canvas &canvas) const
{
    const float amount = queue_shown_.value;
    if (amount <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, kNavy.with_alpha(0.78f * amount));
    list.push_opacity(amount);
    const float rise = 24.0f * (1.0f - amount);
    const Rect panel{kPanel.x, kPanel.y + rise, kPanel.w, kPanel.h};
    ui::Painter paint(list, fonts, theme_);
    solid_floor(list, panel, theme_.radius_card);
    solid_floor(list, panel, theme_.radius_card);
    paint.panel(panel);
    ui::text(list, fonts.display, "Warteschlange", panel.x + 40.0f, panel.y + 66.0f, 36.0f, kInk);
    ui::text(list, fonts.regular,
             queue_.items.empty()
                 ? std::string("Konvertierungen, die nacheinander laufen")
                 : queue_summary(queue_) + (queue_.running ? " · läuft" : " · angehalten"),
             panel.x + 40.0f, panel.y + 104.0f, 21.0f, queue_.running ? kMuted : kWarning);
    if (queue_.items.empty())
    {
        ui::text(list, fonts.semibold, "Die Warteschlange ist leer.", panel.x + panel.w * 0.5f,
                 panel.y + 380.0f, 30.0f, kInk, gfx::Align::center);
        ui::text(list, fonts.regular,
                 "Im Fenster „Konvertieren“ eines Spiels stellst du es mit „Zur Warteschlange“ hinein.",
                 panel.x + panel.w * 0.5f, panel.y + 428.0f, 22.0f, kMuted, gfx::Align::center);
    }
    else
        queue_list_.draw(canvas);
    ui::HintLayout layout;
    layout.size = 34.0f;
    layout.text_size = 21.0f;
    layout.cy = panel.y + panel.h - 42.0f;
    const ui::Hint hints[] = {{ui::Button::cross, "Erneut versuchen"},
                              {ui::Button::square, "Entfernen"},
                              {ui::Button::triangle,
                               queue_.running ? "Warteschlange stoppen" : "Warteschlange starten"},
                              {ui::Button::r1, "Fertige entfernen"},
                              {ui::Button::circle, "Schließen"}};
    ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 5, panel.x + 40.0f, false, layout);
    list.pop_opacity();
}

void GameTools::draw_overlay(ui::Canvas &canvas) const
{
    // The menu under the dialog: it is still leaving while the dialog arrives.
    menu_.draw(canvas);
    const float amount = shown_.value;
    if (amount > 0.01f)
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, kNavy.with_alpha(0.78f * amount));
        list.push_opacity(amount);
        const float rise = 24.0f * (1.0f - amount);
        const Rect panel{kPanel.x, kPanel.y + rise, kPanel.w, kPanel.h};
        ui::Painter paint(list, fonts, theme_);
        solid_floor(list, panel, theme_.radius_card);
        solid_floor(list, panel, theme_.radius_card);
        paint.panel(panel);
        if (tool_ == Tool::erase)
            list.rounded_rect({panel.x, panel.y + 26.0f, 6.0f, 76.0f}, 3.0f, kCritical);
        ui::text(list, fonts.display, ui::fit_label(paint, title(), 36.0f, panel.w - 80.0f),
                 panel.x + 40.0f, panel.y + 66.0f, 36.0f, tool_ == Tool::erase ? kCritical : kInk);
        std::string about = game_.id;
        if (!game_.platform.empty())
            about += " · " + game_.platform;
        if (!game_.format.empty())
            about += " · " + game_.format;
        if (game_.size_bytes > 0)
            about += " · " + format_bytes(game_.size_bytes);
        ui::text(list, fonts.regular, about, panel.x + 40.0f, panel.y + 104.0f, 21.0f, kMuted);

        ui::HintLayout layout;
        layout.size = 34.0f;
        layout.text_size = 21.0f;
        layout.cy = panel.y + panel.h - 42.0f;
        if (awaiting_ != 0)
        {
            const float pulse = 0.6f + 0.4f * std::sin(clock_ * 4.0f);
            ui::text(list, fonts.semibold, loading_text(tool_), panel.x + panel.w * 0.5f,
                     panel.y + 420.0f, 30.0f, kInk.with_alpha(pulse), gfx::Align::center);
            ui::text(list, fonts.regular, "Bei großen Spielen dauert das bis zu einer Minute.",
                     panel.x + panel.w * 0.5f, panel.y + 466.0f, 21.0f, kMuted, gfx::Align::center);
            const ui::Hint hints[] = {{ui::Button::circle, "Abbrechen"}};
            ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 1, panel.x + 40.0f, false,
                           layout);
        }
        else if (!plan_.ok)
        {
            ui::text(list, fonts.semibold, "Nicht möglich", panel.x + 40.0f, panel.y + 200.0f,
                     28.0f, kCritical);
            draw_paragraph(canvas, plan_.error, panel.x + 40.0f, panel.y + 246.0f, panel.w - 80.0f,
                           22.0f, kInk, 5);
            const ui::Hint hints[] = {{ui::Button::circle, "Schließen"}};
            ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 1, panel.x + 40.0f, false,
                           layout);
        }
        else
        {
            list.rounded_rect({panel.x + 920.0f, panel.y + 140.0f, 1.5f, 630.0f}, 0.0f,
                              Color::rgb(0x3880ff, 0.18f));
            form_.draw(canvas);
            draw_side(canvas, {panel.x + 950.0f, panel.y + 130.0f, panel.w - 990.0f, 650.0f});
            const std::string start = start_label();
            // a delete has no triangle shortcut: only its red row starts it
            const bool shortcut = tool_ != Tool::erase;
            const ui::Hint hints[] = {{ui::Button::cross, "Wählen"},
                                      {ui::Button::triangle, start.c_str()},
                                      {ui::Button::circle, "Abbrechen"}};
            if (tool_ == Tool::convert)
            {
                const ui::Hint four[] = {hints[0], hints[1],
                                         {ui::Button::square, "Zur Warteschlange"}, hints[2]};
                ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), four, 4, panel.x + 40.0f, false,
                               layout);
            }
            else if (shortcut)
                ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 3, panel.x + 40.0f,
                               false, layout);
            else
            {
                const ui::Hint two[] = {hints[0], hints[2]};
                ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), two, 2, panel.x + 40.0f, false,
                               layout);
            }
        }
        list.pop_opacity();
    }
    draw_queue(canvas);
    prompt_.draw(canvas);
}

} // namespace hui::ps5cc
