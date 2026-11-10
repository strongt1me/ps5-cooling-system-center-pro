// ps5cc - The game tools of Spiele → Bibliothek: the action menu (□) and the plan dialog of each
// tool.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/game_tools.hpp"

#include "ps5cc/look.hpp"
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

const char *op_format(int op)
{
    switch (op)
    {
    case 0:
        return "ffpfsc (komprimiertes PFS)";
    case 1:
        return "ffpkg (UFS2-Abbild)";
    default:
        return "exFAT-Abbild (.exfat)";
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
    extras.label = "Extras: AMPR, PlayGo, LZ4, zstd …";
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
    form_.clear();
    awaiting_ = source().request_plan(tool, game_.id);
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
    return menu_.is_open() || open_;
}

bool GameTools::visible() const
{
    return menu_.visible() || open_ || shown_.value > 0.01f;
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
            form_.add_choice(kRowOp, "Format", options, index);
        if (plan_.kind == "folder" && !plan_.ps4)
            form_.add_toggle(kRowAmpr, "AMPR-Index vorher bauen", choice_.ampr).description =
                "Für Spiele mit APR-EMU: ampr_emu.index kommt mit ins Abbild.";
        add_mode("2. Als was?");
        add_drives("3. Wohin?");
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
    if (ui::FormRow *row = form_.row(kRowStart))
    {
        row->label = start_label();
        row->disabled = !ready(plan_, choice_);
    }
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
    if (!open_)
        return;
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
        }
        else if (id == kRowAmpr)
            choice_.ampr = form_.toggle_value(kRowAmpr);
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
    }
}

void GameTools::press_start(ui::Feedback &feedback)
{
    if (!ready(plan_, choice_))
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
    shown_.target = open_ ? 1.0f : 0.0f;
    shown_.update(dt, 18.0f);
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
        para(plan_.source, kInk, 2);
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
                 kWarning, 4);
            para(unsigned_names(plan_.unsigned_exec), kMuted, 2);
        }
        if (plan_.unsigned_exec.refused > 0)
            para(std::to_string(plan_.unsigned_exec.refused) +
                     " Programmdateien sind nicht signiert und ließen sich nicht signieren "
                     "(ungewöhnlicher Aufbau). Sie bleiben, wie sie sind; das Spiel startet damit "
                     "womöglich nicht.",
                 kCritical, 4);
        if (choice_.op >= 0)
        {
            para(op_text(plan_, choice_.op), kMuted, 3);
            if (choice_.op == 1 && !plan_.ffpkg_error.empty())
                para("Bei diesem Spiel nicht möglich: " + plan_.ffpkg_error, kCritical, 3);
            if (choice_.op == 0 && !stream_reasons(plan_).empty())
                para("Achtung: Bei diesem Spiel sind Ruckler möglich (viele Daten oder "
                     "Texture-Streaming). Ein "
                     "komprimiertes Abbild muss beim Spielen ständig entpackt werden – exFAT oder "
                     "ffpkg vermeiden das.",
                     kWarning, 5);
        }
        if (choice_.mode == 0)
            para("ShadowMountPlus findet das Spiel danach zweimal (Original und neues Abbild). "
                 "Lösche das Original erst, "
                 "wenn das neue Abbild startet.",
                 kWarning, 3);
        if (!target.empty())
        {
            heading(std::string("Ziel · ") + op_format(choice_.op), kMuted);
            para(target, kInk, 2);
        }
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
            if (shortcut)
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
}

} // namespace hui::ps5cc
