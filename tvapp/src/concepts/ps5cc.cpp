// ps5cc - The console screen of PS5 Cooling & System Center: cooling, games, jobs, settings, payloads, logs.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built from the components of ps5-homebrew-ui (BlackBearReloaded, GPL-3.0-or-later).
// The data comes from the app on the console through ps5cc::source(): HTTP on
// the PS5, invented values on the PC.

#include "concepts/concepts.hpp"

#include "core/tween.hpp"
#include "ps5cc/backdrops.hpp"
#include "ps5cc/covers.hpp"
#include "ps5cc/data.hpp"
#include "ps5cc/game_tools.hpp"
#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "ui/components.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;
using ps5cc::JobKind;
using ps5cc::Link;

using ps5cc::kBlue;
using ps5cc::kBottom;
using ps5cc::kCritical;
using ps5cc::kGood;
using ps5cc::kInk;
using ps5cc::kLeftW;
using ps5cc::kLeftX;
using ps5cc::kMargin;
using ps5cc::kMuted;
using ps5cc::kNavy;
using ps5cc::kNavyHigh;
using ps5cc::kRailW;
using ps5cc::kRightW;
using ps5cc::kRightX;
using ps5cc::kTabH;
using ps5cc::kTabY;
using ps5cc::kTop;
using ps5cc::kWarning;
constexpr int kTargetMin = 50;
constexpr int kTargetMax = 91;
constexpr float kApplyDelay = 0.6f;
constexpr float kSwitchWaitSeconds = 30.0f; // a game asked to close is gone in seconds; past this the start is dropped
constexpr int kChartPoints = 25; // one every 5 s: two minutes
constexpr float kChartStep = 5.0f;
constexpr float kPi = 3.14159265f;

// The sidebar's pages, in the order of the browser app's sidebar.
enum Area
{
    kAreaCooling = 0,
    kAreaGames = 1,
    kAreaFiles = 2,
    kAreaPayloads = 3,
    kAreaSystem = 4,
    kAreaProfile = 5,
    kAreaLogs = 6,
    kAreas = 7,
};

// What the content area shows: an area and, where it has sub-tabs, which one.
enum View
{
    kCooling,
    kSettings,
    kGames,
    kJobs,
    kPayloads,
    kLogs,
    kPlaceholder, // Dateien, System, Profil until their pages exist
};

// The pages that live in src/ps5cc/*_page.cpp.
enum PageIndex
{
    kPageSettings = 0,
    kPagePayloads = 1,
    kPageLogs = 2,
    kPageCount = 3,
};

// The sidebar's icons, drawn with the kit's shapes (no artwork files): one per area.
void draw_area_icon(gfx::DrawList &list, int area, const Rect &box, Color ink)
{
    const float s = std::min(box.w, box.h);
    const float x = box.cx() - s * 0.5f;
    const float y = box.cy() - s * 0.5f;
    const float cx = box.cx();
    const float cy = box.cy();
    const float t = std::max(2.0f, s * 0.09f);
    const Color clear{ink.r, ink.g, ink.b, 0.0f};
    switch (area)
    {
    case kAreaCooling: // a fan: hub, three blades, housing
        list.ring(cx, cy, s * 0.46f, t * 0.8f, ink);
        for (int i = 0; i < 3; ++i)
            list.arc(cx, cy, s * 0.22f, s * 0.2f, static_cast<float>(i) * kPi * 2.0f / 3.0f, kPi * 0.42f, ink);
        list.circle(cx, cy, s * 0.08f, ink);
        break;
    case kAreaGames: // a controller: body, d-pad, two buttons
        list.bordered_rect({x + s * 0.02f, y + s * 0.24f, s * 0.96f, s * 0.54f}, s * 0.27f, clear, t, ink);
        list.line(x + s * 0.2f, cy + s * 0.01f, x + s * 0.4f, cy + s * 0.01f, t, ink);
        list.line(x + s * 0.3f, cy - s * 0.09f, x + s * 0.3f, cy + s * 0.11f, t, ink);
        list.circle(x + s * 0.68f, cy - s * 0.05f, s * 0.06f, ink);
        list.circle(x + s * 0.8f, cy + s * 0.07f, s * 0.06f, ink);
        break;
    case kAreaFiles: // a folder: tab and body
        list.rounded_rect({x + s * 0.04f, y + s * 0.16f, s * 0.42f, s * 0.2f}, s * 0.06f, ink);
        list.rounded_rect({x + s * 0.04f, y + s * 0.28f, s * 0.92f, s * 0.58f}, s * 0.1f, ink);
        break;
    case kAreaPayloads: // a box with a play mark: something that runs
        list.bordered_rect({x + s * 0.06f, y + s * 0.06f, s * 0.88f, s * 0.88f}, s * 0.18f, clear, t, ink);
        list.triangle({x + s * 0.36f, y + s * 0.3f, s * 0.36f, s * 0.4f}, ink, 0.0f, kPi * 0.5f);
        break;
    case kAreaSystem: // a chip with pins
        list.bordered_rect({x + s * 0.2f, y + s * 0.2f, s * 0.6f, s * 0.6f}, s * 0.08f, clear, t, ink);
        list.rounded_rect({x + s * 0.38f, y + s * 0.38f, s * 0.24f, s * 0.24f}, s * 0.04f, ink);
        for (int i = 0; i < 3; ++i)
        {
            const float p = s * (0.32f + 0.18f * static_cast<float>(i));
            list.line(x + p, y + s * 0.02f, x + p, y + s * 0.2f, t * 0.8f, ink);
            list.line(x + p, y + s * 0.8f, x + p, y + s * 0.98f, t * 0.8f, ink);
            list.line(x + s * 0.02f, y + p, x + s * 0.2f, y + p, t * 0.8f, ink);
            list.line(x + s * 0.8f, y + p, x + s * 0.98f, y + p, t * 0.8f, ink);
        }
        break;
    case kAreaProfile: // a person
        list.circle(cx, y + s * 0.3f, s * 0.2f, ink);
        list.rounded_rect({x + s * 0.12f, y + s * 0.58f, s * 0.76f, s * 0.36f}, s * 0.18f, ink);
        break;
    default: // the log: lines with a dot each
        for (int i = 0; i < 3; ++i)
        {
            const float ly = y + s * (0.22f + 0.28f * static_cast<float>(i));
            list.circle(x + s * 0.1f, ly, s * 0.07f, ink);
            list.line(x + s * 0.28f, ly, x + s * 0.96f, ly, t, ink);
        }
        break;
    }
}

enum MenuTag
{
    kMenuWeb = 1,
    kMenuReload = 2,
    kMenuQuit = 3,
    kMenuStandby = 4,
    kMenuReboot = 5,
    kMenuOff = 6,
    kMenuSafe = 7,
};

constexpr const char *kTechniques[] = {
    "Live temperatures and fan speed from the app's API",
    "Target temperature with the D-pad, applied after a short pause",
    "Games list with start and details, jobs with progress and cancel",
};

const char *job_title(JobKind kind)
{
    switch (kind)
    {
    case JobKind::copy:
        return "Kopieren";
    case JobKind::convert:
        return "Konvertieren";
    case JobKind::image:
        return "Abbild";
    case JobKind::storage:
        return "Verschieben · Entpacken";
    case JobKind::deletion:
        return "Löschen";
    case JobKind::count:
        break;
    }
    return "";
}

const char *history_kind(const std::string &kind)
{
    if (kind == "copy")
        return "Kopieren";
    if (kind == "convert")
        return "Konvertieren";
    if (kind == "image")
        return "Abbild";
    if (kind == "move")
        return "Verschieben";
    if (kind == "unpack")
        return "Entpacken";
    if (kind == "delete")
        return "Löschen";
    if (kind == "install")
        return "Installieren";
    return "Auftrag";
}

const char *format_label(const std::string &format)
{
    if (format == "folder")
        return "Ordner";
    if (format == "exfat")
        return "exFAT";
    if (format == "ffpkg")
        return "ffpkg";
    if (format == "ffpfs")
        return "ffpfs";
    if (format == "ffpfsc")
        return "ffpfsc";
    if (format == "fpkg")
        return "Installiert";
    return format.c_str();
}

// "2026-10-27T10:29:26Z" -> "27.10.2026"
std::string german_date(const std::string &iso)
{
    if (iso.size() < 10)
        return "noch nie";
    return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
}

Color temp_color(int celsius)
{
    if (celsius >= 85)
        return kCritical;
    if (celsius >= 75)
        return kWarning;
    return kInk;
}

ui::Theme make_theme()
{
    ui::Theme t = ui::default_theme();
    t.id = "ps5cc";
    t.name = "PS5CC";
    t.family = "PS5 Cooling & System Center";
    t.summary = "Deep navy panels with a blue edge, as in the web app";
    t.backdrop.mode = gfx::BackdropMode::gradient;
    t.backdrop.colors[0] = kNavy;
    t.backdrop.colors[1] = kNavyHigh;
    t.page = kNavy;
    t.page_text = kInk;
    t.page_text_muted = kMuted;
    // A little of the area's picture shows through the panels (29.10.2026); the list rows and focus stay solid enough.
    t.surface = Color::rgb(0x0f1a38, 0.78f);
    t.surface_high = Color::rgb(0x1a2850, 0.88f);
    t.text = kInk;
    t.text_muted = kMuted;
    t.primary = kBlue;
    t.on_primary = Color::rgb(0xffffff);
    t.secondary = Color::rgb(0x1a2850);
    t.on_secondary = kInk;
    t.accent = kBlue;
    t.outline = Color::rgb(0x3880ff, 0.34f);
    t.focus = Color::rgb(0x6aa2ff);
    t.shadow = Color::rgb(0x000000, 0.55f);
    t.light = Color::rgb(0x9fbfff, 0.10f);
    t.danger = kCritical;
    t.success = kGood;
    t.warning = kWarning;
    t.style = ui::SurfaceStyle::soft;
    t.corner = ui::Corner::round;
    t.radius = 12.0f;
    t.radius_card = 20.0f;
    t.border = 1.5f;
    t.pill_chips = true;
    t.heading = ui::FontRole::display;
    t.label = ui::FontRole::semibold;
    t.caps = false;
    t.sounds = audio::SoundSet::glass;
    t.dark = true;
    return t;
}

class Ps5cc final : public app::Concept
{
  public:
    explicit Ps5cc(app::Context &context) : context_(context), theme_(make_theme())
    {
        // The sidebar of the browser app, as a rail: icons only while the content has the focus, icons and labels
        // (drawn over the content, which stays where it is) while it is being steered.
        nav_.style.theme = theme_;
        nav_.style.collapsed_width = kRailW;
        nav_.style.expanded_width = 330.0f;
        nav_.style.row_height = 76.0f;
        nav_.style.gap = 10.0f;
        nav_.style.icon_size = 34.0f;
        nav_.style.text_size = 25.0f;
        nav_.style.expanded = false;
        nav_.icon = [](ui::Canvas &canvas, const Rect &box, const ui::NavEntry &entry, int, float, Color ink)
        { draw_area_icon(canvas.list, entry.tag, box, ink); };
        {
            const char *const labels[kAreas] = {"Kühlung", "Spiele", "Dateien", "Payloads", "System", "Profil",
                                                "Protokoll"};
            std::vector<ui::NavEntry> entries;
            for (int i = 0; i < kAreas; ++i)
            {
                ui::NavEntry e;
                e.label = labels[i];
                e.tag = i;
                entries.push_back(e);
            }
            nav_.set_entries(entries);
        }
        nav_.set_bounds({kMargin, kTabY, 0.0f, kBottom - kTabY});
        nav_.set_current(kAreaCooling, true);
        nav_.set_focus(kAreaCooling, true);
        nav_.set_focused(false);

        ps5cc::style_subtabs(cooling_tabs_, theme_, 2);
        cooling_tabs_.set_tabs({{"Übersicht"}, {"Einstellungen"}});
        cooling_tabs_.set_active(0, true);
        ps5cc::style_subtabs(games_tabs_, theme_, 2);
        games_tabs_.set_tabs({{"Bibliothek"}, {"Aufträge"}});
        games_tabs_.set_active(0, true);

        pages_ = {ps5cc::make_settings_page(), ps5cc::make_payloads_page(), ps5cc::make_logs_page()};
        for (const std::unique_ptr<ps5cc::Page> &page : pages_)
            page->init(theme_);

        gauge_.style.theme = theme_;
        gauge_.style.sweep = 4.712389f;
        gauge_.style.start_angle = -2.3561945f;
        gauge_.style.thickness = 22.0f;
        gauge_.style.percent = false;
        gauge_.style.status = ui::Status::accent;
        gauge_.center = [this](ui::Canvas &canvas, const Rect &inner, float value)
        {
            (void)value;
            const std::string pct = fan_text();
            ui::text(canvas.list, canvas.fonts.display, pct, inner.x + inner.w * 0.5f,
                     inner.y + inner.h * 0.5f + 22.0f, 64.0f, kInk, gfx::Align::center);
            ui::text(canvas.list, canvas.fonts.regular, "Lüfterdrehzahl", inner.x + inner.w * 0.5f,
                     inner.y + inner.h * 0.5f + 66.0f, 22.0f, kMuted, gfx::Align::center);
        };

        chart_.style.theme = theme_;
        chart_.style.readout = false;
        chart_.style.legend = ui::LegendPlacement::top;
        chart_.style.axis.ticks = 4;
        chart_.style.axis.from_zero = false;
        // Up to the highest target (91 °C): a scale fitted to the data put 72 °C at the top and looked too hot.
        chart_.style.axis.include_low = 20.0f;
        chart_.style.axis.include_high = 91.0f;
        chart_.title = "Verlauf der letzten 2 Minuten";
        std::vector<std::string> labels(kChartPoints);
        labels.front() = "–2 min";
        labels[kChartPoints / 2] = "–1 min";
        labels.back() = "jetzt";
        chart_.set_labels(labels);
        chart_.set_active(false);
        chart_.set_bounds({kLeftX, kTop + 270.0f, kLeftW, kBottom - kTop - 270.0f});
        gauge_.set_bounds({kRightX + kRightW * 0.5f - 150.0f, kTop + 80.0f, 300.0f, 300.0f});

        games_.style.theme = theme_;
        games_.style.row_height = 84.0f;
        games_.style.leading_width = 76.0f;
        games_.style.title_size = 26.0f;
        games_.style.subtitle_size = 19.0f;
        games_.style.value_size = 22.0f;
        games_.style.panel = true;
        games_.style.highlight.kind = ui::HighlightKind::bar;
        games_.leading = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item,
                                int index, float focus)
        {
            (void)focus;
            const std::string id = index >= 0 && index < static_cast<int>(data_.games.size())
                                       ? data_.games[static_cast<std::size_t>(index)].id
                                       : std::string();
            draw_cover(canvas, {row.x + 18.0f, row.y + 12.0f, 60.0f, 60.0f}, id, item.title, 24.0f);
        };
        games_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        game_info_.style.theme = theme_;
        game_info_.style.panel = false;
        game_info_.style.label_size = 20.0f;
        game_info_.style.value_size = 22.0f;
        game_info_.style.row_height = 44.0f;
        game_info_.set_bounds({kRightX + 36.0f, kTop + 250.0f, kRightW - 72.0f, 300.0f});

        details_.style.theme = theme_;
        details_.style.panel = false;
        // One line per value: wrapped, the path put the content right at the height that needs a scrollbar, the scrollbar
        // narrowed the rows, the path rewrapped, and the sheet jumped up and down every frame (console, 27.10.2026).
        details_.style.max_lines = 1;
        sheet_.style.theme = theme_;
        sheet_.style.size = 720.0f;
        sheet_.style.footer = 120.0f;
        sheet_.content = [this](ui::Canvas &canvas, const Rect &area, float opacity)
        {
            (void)area;
            (void)opacity;
            details_.draw(canvas);
        };

        for (std::size_t i = 0; i < bars_.size(); ++i)
        {
            bars_[i].style.theme = theme_;
            bars_[i].style.height = 16.0f;
            bars_[i].style.sheen = true;
            bars_[i].style.text_size = 22.0f;
            const Rect r = job_rect(static_cast<int>(i));
            bars_[i].set_bounds({r.x + 32.0f, r.y + 62.0f, r.w - 64.0f, 52.0f});
        }
        tools_.init(theme_);
        tools_.toast = [this](ui::StatusKind kind, const std::string &title, const std::string &body)
        { toasts_.push(kind, title, body); };
        job_focus_amount_.snap(job_rect(0));
        history_.style.theme = theme_;
        history_.style.row_height = 78.0f;
        history_.style.title_size = 22.0f;
        history_.style.subtitle_size = 18.0f;
        history_.style.value_size = 20.0f;
        history_.style.panel = true;
        history_.style.highlight.kind = ui::HighlightKind::none;
        history_.style.entrance_step = 0.0f;
        history_.set_active(false);
        history_.set_bounds({kRightX, kTop + 54.0f, kRightW, kBottom - kTop - 54.0f});

        menu_.style.theme = theme_;
        menu_.style.width = 560.0f;
        menu_.style.scrim = 0.35f;
        {
            ui::MenuItem web;
            web.label = "Weboberfläche im Browser öffnen";
            web.tag = kMenuWeb;
            ui::MenuItem reload;
            reload.label = "Neu laden";
            reload.tag = kMenuReload;
            ui::MenuItem line;
            line.separator = true;
            ui::MenuItem quit;
            quit.label = "App beenden";
            quit.tag = kMenuQuit;
            // Power-Optionen: fired directly (do_power).
            const auto power = [](const char *label, int tag, bool danger)
            {
                ui::MenuItem item;
                item.label = label;
                item.tag = tag;
                item.danger = danger;
                item.shortcut = tag == kMenuStandby ? "Power-Optionen" : "";
                return item;
            };
            menu_.set_items({web, reload, quit, line, power("Ruhemodus", kMenuStandby, false),
                             power("Neu starten", kMenuReboot, false), power("Ausschalten", kMenuOff, true),
                             power("Abgesicherter Modus", kMenuSafe, true)});
        }

        link_dialog_.style.theme = theme_;
        link_dialog_.style.dismissable = false;
        link_dialog_.style.frosted = false;
        link_dialog_.style.scrim = 0.85f;
        toasts_.style.theme = theme_;
        toasts_.style.width = 560.0f;
        toasts_.style.anchor = ui::ToastAnchor::bottom_right;
        toasts_.style.margin = 0.0f;
        // Above the hint row, inside the safe area: the toasts never cover the page's titles.
        toasts_.set_bounds({kMargin, kTop, 1824.0f - kMargin, 940.0f - kTop});
        menu_.set_bounds({kMargin, 40.0f, 1824.0f - kMargin, 920.0f});

        loader_.style.theme = theme_;
        loader_.style.layout = ui::LoadingLayout::center;
        loader_.style.indicator = ui::LoadingIndicator::bar;
        loader_.style.tip_label = "Hinweis";
        loader_.title = "PS5 Cooling & System Center";
        loader_.subtitle = "Die App wird gestartet …";
        loader_.set_tips({"Die Oberfläche holt alle Werte von der App auf der Konsole (Port 8086).",
                          "Die volle Weboberfläche gibt es weiter am Handy oder PC.",
                          "L1 und R1 wechseln die Seite, L2 und R2 die Ansicht; das Touchpad öffnet die Seitenleiste.",
                          "Options öffnet das Menü mit der Weboberfläche und den Power-Optionen."});

        for (ui::Chip &chip : chips_)
        {
            chip.style.theme = theme_;
            chip.style.leading_dot = true;
            chip.style.height = 42.0f;
            chip.style.text_size = 20.0f;
        }
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "ps5cc",
            "PS5 Cooling & System Center",
            "Cooling, games and jobs of the console app, full screen with the controller",
            "src/concepts/ps5cc.cpp",
            audio::SoundSet::glass,
            kBlue,
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
        games_.enter();
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        age_ += dt;
        clock_ += dt;
        placeholder_age_ += dt;
        ps5cc::Source &src = ps5cc::source();
        src.tick(dt);
        if (src.fetch(data_, seen_))
            absorb();
        sample_chart(dt);

        ps5cc::Notice notice;
        while (src.poll_notice(notice))
            show_notice(notice, feedback);
        // The console app launches the game once this app has gone (it is a title of its own and would count as the
        // running game); a moment for the toast first.
        if (switch_wait_ > 0.0f)
        {
            switch_wait_ -= dt;
            if (data_.status.valid && data_.status.game_id != running_id_)
            {
                switch_wait_ = 0.0f;
                src.launch(switch_target_);
            }
            else if (switch_wait_ <= 0.0f)
            {
                toasts_.push(ui::StatusKind::danger, "Das laufende Spiel ist nicht beendet",
                             "Bitte über die PS-Taste schließen und dann noch einmal starten.");
            }
        }
        if (leave_in_ > 0.0f)
        {
            leave_in_ -= dt;
            if (leave_in_ <= 0.0f)
                src.quit_app();
        }

        if (input.is_pressed(Action::r3))
            src.debug_cycle_link();
        update_link(input, feedback);

        if (data_.link == Link::online)
            route(input, feedback);
        // The worker fetches the cover of the game in focus before the others.
        if (const ps5cc::Game *g = focused_game())
            ps5cc::covers().prefer(g->id);

        if (pending_target_ > 0)
        {
            apply_in_ -= dt;
            if (apply_in_ <= 0.0f)
                apply_target();
        }

        nav_.update(dt);
        cooling_tabs_.update(dt);
        games_tabs_.update(dt);
        nav_veil_.target = nav_focus_ ? 1.0f : 0.0f;
        nav_veil_.update(dt, 16.0f);
        gauge_.update(dt);
        chart_.update(dt);
        games_.update(dt);
        game_info_.update(dt);
        details_.update(dt);
        sheet_.update(dt);
        tools_.update(dt);
        for (ui::ProgressBar &bar : bars_)
            bar.update(dt);
        history_.update(dt);
        for (const std::unique_ptr<ps5cc::Page> &page : pages_)
            page->update(dt);
        menu_.update(dt);
        link_dialog_.update(dt);
        toasts_.update(dt, feedback);
        loader_.update(dt);
        for (ui::Chip &chip : chips_)
            chip.update(dt);
        job_focus_amount_.target(job_rect(job_focus_));
        job_focus_amount_.update(dt, 18.0f);
        target_bump_.update(dt, 8.0f);
        target_refusal_.update(dt, 9.0f);
    }

    void draw(app::Frame &frame) const override
    {
        frame.backdrop = theme_.backdrop;
        frame.backdrop.time = clock_;
        const ps5cc::Page *page = current_page();
        frame.glass = sheet_.visible() || link_dialog_.visible() || tools_.visible() ||
                      (page != nullptr && page->modal());

        ui::Canvas canvas{frame.scene, context_.fonts, 0, clock_};
        draw_area_picture(canvas);
        draw_header(canvas);
        switch (view())
        {
        case kCooling:
            draw_cooling(canvas);
            break;
        case kGames:
            draw_games(canvas);
            break;
        case kJobs:
            draw_jobs(canvas);
            break;
        case kPlaceholder:
            draw_placeholder(canvas);
            break;
        default:
            if (page != nullptr)
                page->draw(canvas);
            break;
        }
        draw_nav(canvas);
        draw_footer(canvas);

        ui::Canvas top{frame.overlay, context_.fonts, frame.glass_texture, clock_};
        if (page != nullptr)
            page->draw_overlay(top);
        sheet_.draw(top);
        tools_.draw_overlay(top);
        menu_.draw(top);
        toasts_.draw(top);
        if (loader_.visible())
            loader_.draw(top);
        link_dialog_.draw(top);
    }

    std::span<const app::TourStep> tour() const override
    {
        static constexpr app::TourStep kTour[] = {
            {1.4f, 0, Direction::none, "kuehlung"},
            {0.25f, 0, Direction::right},
            {0.25f, 0, Direction::right},
            {0.2f, 0, Direction::none, "ziel-aendern"},
            {0.2f, action_bit(Action::confirm)},
            {0.9f, 0, Direction::none, "ziel-uebernommen"},
            {0.5f, action_bit(Action::jump_next)},
            {1.0f, 0, Direction::none, "einstellungen"},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::right},
            {0.9f, 0, Direction::none, "einstellung-geaendert"},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.3f, action_bit(Action::confirm)},
            {4.0f, 0, Direction::none, "selbsttest"},
            // The area pictures: Kühlung switched off (its picture goes) and back to the standard one.
            {0.25f, 0, Direction::down},
            {0.8f, 0, Direction::none, "hintergruende"},
            {0.25f, 0, Direction::right},
            {1.0f, 0, Direction::none, "hintergrund-aus"},
            {0.25f, 0, Direction::left},
            {0.6f},
            {0.3f, action_bit(Action::page_next)},
            {0.9f, 0, Direction::down},
            {0.35f, 0, Direction::down},
            {0.8f, 0, Direction::none, "spiele"},
            {0.2f, action_bit(Action::north)},
            {0.9f, 0, Direction::none, "details"},
            {0.2f, action_bit(Action::back)},
            {0.6f, action_bit(Action::confirm)},
            {0.9f, 0, Direction::none, "spielwechsel"},
            {0.2f, action_bit(Action::back)},
            // The game tools on ASTRO BOT (an ffpkg image): the menu, then each plan.
            {0.4f, action_bit(Action::west)},
            {0.7f, 0, Direction::none, "aktionen-menue"},
            {0.2f, action_bit(Action::confirm)},
            {0.4f, 0, Direction::none, "plan-laedt"},
            {0.5f, 0, Direction::right},
            {0.7f, 0, Direction::none, "plan-kopieren"},
            {0.2f, action_bit(Action::back)},
            {0.4f, action_bit(Action::west)},
            {0.3f, 0, Direction::down},
            {0.2f, action_bit(Action::confirm)},
            {0.8f, 0, Direction::down},
            {0.2f, 0, Direction::right},
            {0.8f, 0, Direction::none, "plan-konvertieren"},
            {0.2f, action_bit(Action::north)},
            {1.2f, 0, Direction::none, "auftrag-laeuft"},
            {0.3f, action_bit(Action::jump_prev)},
            {0.5f, action_bit(Action::west)},
            {0.3f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, action_bit(Action::confirm)},
            {1.0f, 0, Direction::none, "plan-verschieben"},
            {0.2f, action_bit(Action::back)},
            {0.4f, action_bit(Action::west)},
            {0.3f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, action_bit(Action::confirm)},
            {1.0f, 0, Direction::none, "plan-entpacken"},
            {0.2f, action_bit(Action::back)},
            {0.4f, action_bit(Action::west)},
            {0.3f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, action_bit(Action::confirm)},
            {1.0f, 0, Direction::none, "abbild-pruefen"},
            {0.2f, action_bit(Action::back)},
            {0.4f, action_bit(Action::west)},
            {0.3f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, action_bit(Action::confirm)},
            {1.2f, 0, Direction::none, "loeschen-gestartet"},
            {0.3f, action_bit(Action::jump_prev)},
            // A PS4 title (Two Point Hospital, a folder): converting lists only USB and M.2.
            {0.4f, 0, Direction::down},
            {0.15f, 0, Direction::down},
            {0.15f, 0, Direction::down},
            {0.15f, 0, Direction::down},
            {0.15f, 0, Direction::down},
            {0.15f, 0, Direction::down},
            {0.15f, 0, Direction::down},
            {0.4f, action_bit(Action::west)},
            {0.3f, 0, Direction::down},
            {0.2f, action_bit(Action::confirm)},
            {1.0f, 0, Direction::none, "plan-ps4"},
            {0.2f, action_bit(Action::back)},
            {0.5f, action_bit(Action::jump_next)},
            {1.0f, 0, Direction::none, "auftraege"},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, action_bit(Action::west)},
            {0.8f, 0, Direction::none, "abgebrochen"},
            {0.5f, action_bit(Action::menu)},
            {0.8f, 0, Direction::none, "optionen"},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.2f, 0, Direction::down},
            {0.3f, action_bit(Action::confirm)},
            {0.8f, 0, Direction::none, "ausschalten"},
            {0.5f, action_bit(Action::page_next)},
            {0.9f, 0, Direction::none, "dateien"},
            {0.5f, action_bit(Action::page_next)},
            {1.0f, 0, Direction::none, "payloads"},
            {0.4f, action_bit(Action::jump_next)},
            {0.2f, 0, Direction::down},
            {0.8f, 0, Direction::none, "payloads-laufend"},
            {0.2f, action_bit(Action::west)},
            {0.8f, 0, Direction::none, "payload-beendet"},
            {0.4f, action_bit(Action::jump_next)},
            {0.2f, 0, Direction::down},
            {0.3f, action_bit(Action::confirm)},
            {1.2f, 0, Direction::none, "payload-store"},
            {0.5f, action_bit(Action::page_next)},
            {0.9f, 0, Direction::none, "system"},
            {0.5f, action_bit(Action::page_next)},
            {0.5f, action_bit(Action::page_next)},
            {1.0f, 0, Direction::none, "protokoll"},
            {0.4f, action_bit(Action::jump_next)},
            {1.5f, 0, Direction::none, "kernel-log"},
            {0.2f, 0, Direction::up},
            {0.2f, 0, Direction::up},
            {0.6f, 0, Direction::none, "kernel-log-angehalten"},
            {0.4f, action_bit(Action::touch)},
            {0.2f, 0, Direction::up},
            {0.2f, 0, Direction::up},
            {0.8f, 0, Direction::none, "seitenleiste"},
            {0.4f, action_bit(Action::confirm)},
            {0.9f, 0, Direction::none, "seitenleiste-gewaehlt"},
            {0.4f, action_bit(Action::page_next)},
            {0.5f, action_bit(Action::r3)},
            {1.3f, 0, Direction::none, "laden"},
            {0.2f, action_bit(Action::r3)},
            {1.0f, 0, Direction::none, "fehler"},
            {0.2f, action_bit(Action::r3)},
            {0.6f, action_bit(Action::page_next)},
            {0.8f},
        };
        return kTour;
    }

  private:
    // ---------------------------------------------------------------- data

    void absorb()
    {
        const ps5cc::Status &s = data_.status;
        if (s.valid && s.fan_pct >= 0.0f)
            gauge_.set_value(std::clamp(s.fan_pct / 100.0f, 0.0f, 1.0f), !gauge_started_);
        gauge_started_ = gauge_started_ || s.valid;
        if (pending_target_ == 0 && s.target_c > 0)
            target_ = s.target_c;

        if (data_.games_loaded && (data_.games.size() != shown_games_ || s.game_id != shown_running_))
            rebuild_games();
        rebuild_jobs();
        rebuild_history();
        rebuild_chips();
        if (sheet_.is_open())
            fill_details();
        tools_.absorb(data_);
        for (const std::unique_ptr<ps5cc::Page> &page : pages_)
            page->absorb(data_);
    }

    View view() const
    {
        switch (nav_.current())
        {
        case kAreaCooling:
            return cooling_tabs_.active() == 0 ? kCooling : kSettings;
        case kAreaGames:
            return games_tabs_.active() == 0 ? kGames : kJobs;
        case kAreaPayloads:
            return kPayloads;
        case kAreaLogs:
            return kLogs;
        default:
            return kPlaceholder;
        }
    }

    // The sub-tabs the screen itself owns (Payloads and Protokoll keep theirs on their page).
    const ui::TabBar *own_subtabs() const
    {
        if (nav_.current() == kAreaCooling)
            return &cooling_tabs_;
        if (nav_.current() == kAreaGames)
            return &games_tabs_;
        return nullptr;
    }

    ps5cc::Page *current_page() const
    {
        switch (view())
        {
        case kSettings:
            return pages_[kPageSettings].get();
        case kPayloads:
            return pages_[kPagePayloads].get();
        case kLogs:
            return pages_[kPageLogs].get();
        default:
            return nullptr;
        }
    }

    void sample_chart(float dt)
    {
        if (!data_.status.valid)
            return;
        chart_clock_ += dt;
        if (!temp_points_.empty() && chart_clock_ < kChartStep)
            return;
        chart_clock_ = 0.0f;
        const float temp = static_cast<float>(data_.status.hottest_c);
        const float fan = std::max(0.0f, data_.status.fan_pct);
        if (temp_points_.empty())
        {
            // Nothing measured before the app opened: a flat start, not invented history.
            temp_points_.assign(kChartPoints, temp);
            fan_points_.assign(kChartPoints, fan);
        }
        else
        {
            temp_points_.pop_front();
            temp_points_.push_back(temp);
            fan_points_.pop_front();
            fan_points_.push_back(fan);
        }
        ui::ChartSeries t{"Temperatur °C", {temp_points_.begin(), temp_points_.end()}, kWarning};
        ui::ChartSeries f{"Lüfter %", {fan_points_.begin(), fan_points_.end()}, kBlue};
        chart_.set_series({t, f}, !chart_started_);
        chart_started_ = true;
    }

    void rebuild_games()
    {
        const int keep = games_.focus();
        std::vector<ui::ListItem> items;
        for (const ps5cc::Game &g : data_.games)
        {
            ui::ListItem item;
            item.title = g.name;
            item.subtitle = g.platform + " · v" + g.version + " · " + format_label(g.format) + " · " +
                            ps5cc::format_bytes(g.size_bytes);
            item.value = ps5cc::format_playtime(g.played_s);
            if (g.id == data_.status.game_id)
                item.badge = "LÄUFT";
            item.disabled = !g.can_launch;
            items.push_back(item);
        }
        if (items.empty())
        {
            ui::ListItem none;
            none.title = "Keine Spiele gefunden";
            none.subtitle = "Die App hat keine Spiele in der Bibliothek der Konsole gefunden.";
            none.disabled = true;
            items.push_back(none);
        }
        games_.set_items(items);
        games_.set_focus(std::clamp(keep, 0, static_cast<int>(items.size()) - 1));
        shown_games_ = data_.games.size();
        shown_running_ = data_.status.game_id;
        fill_game_info();
    }

    const ps5cc::Game *focused_game() const
    {
        const int i = games_.focus();
        if (i < 0 || i >= static_cast<int>(data_.games.size()))
            return nullptr;
        return &data_.games[static_cast<std::size_t>(i)];
    }

    void fill_game_info()
    {
        const ps5cc::Game *g = focused_game();
        if (g == nullptr)
        {
            game_info_.set_items({});
            return;
        }
        std::vector<ui::DetailItem> items;
        items.push_back({"Titel-ID", g->id});
        items.push_back({"Version", g->version});
        items.push_back({"Format", format_label(g->format)});
        items.push_back({"Größe", ps5cc::format_bytes(g->size_bytes)});
        items.push_back({"Spielzeit", ps5cc::format_playtime(g->played_s)});
        game_info_.set_items(items);
    }

    void fill_details()
    {
        const ps5cc::Game *g = focused_game();
        if (g == nullptr)
            return;
        sheet_.set_title(g->name);
        std::vector<ui::DetailItem> items;
        items.push_back({"Titel-ID", g->id});
        items.push_back({"Plattform", g->platform});
        items.push_back({"Version", g->version});
        items.push_back({"Format", format_label(g->format)});
        items.push_back({"Größe", ps5cc::format_bytes(g->size_bytes)});
        items.push_back({"Spielzeit", ps5cc::format_playtime(g->played_s)});
        items.push_back({"Zuletzt gespielt", german_date(g->last_played)});
        items.push_back({"Speicherort", g->path});
        items.push_back({"Starten", g->can_launch ? "möglich" : "nicht möglich"});
        // The data changes every second (temperatures); refilling unchanged rows reset the list each time.
        std::string key = g->name;
        for (const ui::DetailItem &item : items)
            key += '\n' + item.label + '\t' + item.value;
        if (key == details_key_)
            return;
        details_key_ = std::move(key);
        details_.set_items(items);
        details_.set_bounds(sheet_.content_rect());
    }

    void rebuild_jobs()
    {
        for (std::size_t i = 0; i < bars_.size(); ++i)
        {
            const ps5cc::Job &job = data_.jobs[i];
            ui::ProgressBar &bar = bars_[i];
            const bool known = job.total > 0.0;
            bar.style.mode = job.active && !known ? ui::ProgressMode::indeterminate
                                                  : ui::ProgressMode::determinate;
            bar.style.percent = known;
            bar.style.status = job.state == "failed"      ? ui::Status::danger
                               : job.state == "cancelled" ? ui::Status::warning
                               : job.active               ? ui::Status::accent
                                                          : ui::Status::success;
            bar.label = job_state_text(job);
            bar.set_value(known ? static_cast<float>(std::clamp(job.done / job.total, 0.0, 1.0)) : 0.0f);
        }
    }

    static std::string job_state_text(const ps5cc::Job &job)
    {
        if (job.active)
        {
            if (job.state == "scanning")
                return "wird vermessen";
            if (job.state == "verifying")
                return "wird geprüft";
            if (job.total > 0.0)
                return ps5cc::format_bytes(static_cast<std::uint64_t>(job.done)) + " von " +
                       ps5cc::format_bytes(static_cast<std::uint64_t>(job.total));
            return "läuft";
        }
        if (job.state == "done")
            return "fertig";
        if (job.state == "failed")
            return job.error.empty() ? "gescheitert" : "gescheitert: " + job.error;
        if (job.state == "cancelled")
            return "abgebrochen";
        return "";
    }

    void rebuild_history()
    {
        std::vector<ui::ListItem> items;
        for (const ps5cc::HistoryEntry &e : data_.history)
        {
            if (items.size() >= 12)
                break;
            ui::ListItem item;
            item.title = std::string(history_kind(e.kind)) + " · " + e.title;
            item.subtitle = e.detail;
            item.value = e.result == "done"        ? "fertig"
                         : e.result == "cancelled" ? "abgebrochen"
                                                   : "gescheitert";
            item.swatch = e.result == "done" ? kGood : e.result == "cancelled" ? kWarning : kCritical;
            items.push_back(item);
        }
        if (items.empty())
        {
            ui::ListItem none;
            none.title = "Noch keine Aufträge";
            none.subtitle = "Kopieren, Konvertieren und Prüfen erscheinen hier.";
            items.push_back(none);
        }
        history_.set_items(items);
    }

    void rebuild_chips()
    {
        const ps5cc::Status &s = data_.status;
        chips_[0].label = s.automatic ? "Automatik" : "Beobachten";
        chips_[0].style.dot = s.automatic ? ui::Status::success : ui::Status::neutral;
        chips_[1].label = s.safety ? "Sicherheitsmodus aktiv" : "Sicherheitsmodus aus";
        chips_[1].style.dot = s.safety ? ui::Status::danger : ui::Status::neutral;
        chips_[2].label = s.guard == 1   ? "Wächter: volle Kühlung"
                          : s.guard == 2 ? "Wächter: Regelung reagiert nicht"
                                         : "Wächter bereit";
        chips_[2].style.dot = s.guard != 0 ? ui::Status::danger : ui::Status::success;
        chips_[3].label = s.night ? "Nachtmodus aktiv" : "Nachtmodus aus";
        chips_[3].style.dot = s.night ? ui::Status::accent : ui::Status::neutral;
    }

    std::string fan_text() const
    {
        if (!data_.status.valid || data_.status.fan_pct < 0.0f)
            return "–";
        return ps5cc::format_decimal(data_.status.fan_pct, 1) + " %";
    }

    // --------------------------------------------------------------- input

    void update_link(const InputFrame &input, app::Feedback &feedback)
    {
        if (data_.link != shown_link_)
        {
            if (data_.link == Link::starting)
            {
                link_dialog_.dismiss();
                loader_.show(feedback);
                loader_.set_progress(-1.0f);
            }
            else if (data_.link == Link::failed)
            {
                loader_.hide();
                ui::DialogContent content;
                content.icon = ui::StatusKind::danger;
                content.title = "Die App antwortet nicht";
                content.body = data_.link_detail +
                               "\nIst der Jailbreak mit ELF-Loader (Port 9021) geladen?";
                content.buttons = {{"Erneut versuchen", ui::ButtonKind::primary}, {"Beenden"}};
                link_dialog_.open(content, feedback);
            }
            else
            {
                loader_.hide();
                link_dialog_.dismiss();
            }
            shown_link_ = data_.link;
        }
        if (data_.link == Link::starting)
            loader_.subtitle = data_.link_detail.empty() ? "Die App wird gestartet …" : data_.link_detail;
        if (link_dialog_.is_open() && link_dialog_.handle(input, feedback) == ui::Event::activated)
        {
            if (link_dialog_.choice() == 0)
                ps5cc::source().retry();
            else
                ps5cc::source().quit_app();
        }
    }

    void route(const InputFrame &input, app::Feedback &feedback)
    {
        ps5cc::Source &src = ps5cc::source();
        if (tools_.modal())
        {
            tools_.handle(input, feedback);
            return;
        }
        if (sheet_.is_open())
        {
            sheet_.handle(input, feedback);
            return;
        }
        ps5cc::Page *page = current_page();
        if (page != nullptr && page->modal())
        {
            page->handle(input, feedback);
            return;
        }
        if (menu_.is_open())
        {
            if (menu_.handle(input, feedback) == ui::Event::activated)
            {
                const int tag = menu_.items()[static_cast<std::size_t>(menu_.focus())].tag;
                if (tag == kMenuWeb)
                    src.open_web_ui();
                else if (tag == kMenuReload)
                    src.reload();
                else if (tag == kMenuQuit)
                    src.quit_app();
                else if (tag >= kMenuStandby && tag <= kMenuSafe)
                    do_power(tag, feedback);
            }
            return;
        }
        if (input.is_pressed(Action::menu))
        {
            menu_.open({1824.0f - 60.0f, 44.0f, 60.0f, 64.0f}, feedback);
            return;
        }
        if (input.is_pressed(Action::page_prev) || input.is_pressed(Action::page_next))
        {
            step_area(input.is_pressed(Action::page_next) ? 1 : -1, input, feedback);
            return;
        }
        if (input.is_pressed(Action::touch))
        {
            set_nav_focus(!nav_focus_, feedback);
            return;
        }
        if (nav_focus_)
        {
            route_nav(input, feedback);
            return;
        }
        // The screen's own sub-tabs; Payloads and Protokoll switch theirs on the page.
        if (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
        {
            ui::TabBar *tabs = const_cast<ui::TabBar *>(own_subtabs());
            if (tabs != nullptr)
            {
                if (tabs->step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) == ui::Event::changed)
                    switched();
                return;
            }
        }
        // Left from a page that has no use for it opens the sidebar (Kühlung and Einstellungen turn values with it).
        const View v = view();
        if (input.nav == Direction::left && !input.nav_repeat && v != kCooling && v != kSettings)
        {
            set_nav_focus(true, feedback);
            return;
        }

        switch (v)
        {
        case kCooling:
            route_cooling(input, feedback);
            break;
        case kGames:
            route_games(input, feedback);
            break;
        case kJobs:
            route_jobs(input, feedback);
            break;
        case kPlaceholder:
            if (input.is_pressed(Action::confirm))
                src.open_web_ui();
            break;
        default:
            if (page != nullptr)
                page->handle(input, feedback);
            break;
        }
    }

    void set_nav_focus(bool focus, app::Feedback &feedback)
    {
        if (focus == nav_focus_)
            return;
        nav_focus_ = focus;
        nav_.set_focused(focus);
        nav_.set_expanded(focus);
        if (focus)
            nav_.set_focus(nav_.current(), true);
        feedback.play(focus ? audio::Cue::open : audio::Cue::back);
    }

    void route_nav(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.nav == Direction::right && !input.nav_repeat)
        {
            set_nav_focus(false, feedback);
            return;
        }
        const int before = nav_.current();
        const ui::Event event = nav_.handle(input, feedback);
        if (event == ui::Event::activated)
        {
            set_nav_focus(false, feedback);
            if (nav_.current() != before)
                switched();
        }
        else if (event == ui::Event::cancelled)
        {
            set_nav_focus(false, feedback);
        }
    }

    // L1/R1: the previous or next page of the sidebar, from anywhere.
    void step_area(int direction, const InputFrame &input, app::Feedback &feedback)
    {
        const int next = nav_.current() + direction;
        if (next < 0 || next >= kAreas)
        {
            if (!input.nav_repeat)
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        nav_.set_current(next);
        nav_.set_focus(next, !nav_focus_);
        feedback.play(audio::Cue::tab);
        switched();
    }

    void switched()
    {
        const View v = view();
        ps5cc::source().watch(v == kJobs       ? ps5cc::Watch::jobs
                              : v == kSettings ? ps5cc::Watch::settings
                              : v == kPayloads ? ps5cc::Watch::payloads
                              : v == kLogs     ? ps5cc::Watch::logs
                                               : ps5cc::Watch::none);
        if (ps5cc::Page *page = current_page())
            page->enter();
        if (v == kGames)
            games_.enter();
        if (v == kCooling)
            chart_.enter();
        placeholder_age_ = 0.0f;
    }

    void route_cooling(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int next = target_ + (input.nav == Direction::right ? 1 : -1);
            if (next < kTargetMin || next > kTargetMax)
            {
                if (!input.nav_repeat)
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                target_refusal_.trigger();
                return;
            }
            target_ = next;
            pending_target_ = next;
            apply_in_ = kApplyDelay;
            target_bump_.trigger();
            feedback.play(audio::Cue::slider, 1.0f, 0.0f, 0.8f + 0.01f * static_cast<float>(next - 50));
            return;
        }
        if (input.is_pressed(Action::confirm) && pending_target_ > 0)
        {
            apply_target();
            feedback.play(audio::Cue::select);
        }
    }

    void apply_target()
    {
        ps5cc::source().set_target(pending_target_);
        applied_target_ = pending_target_;
        pending_target_ = 0;
    }

    void route_games(const InputFrame &input, app::Feedback &feedback)
    {
        const int before = games_.focus();
        const ui::Event event = games_.handle(input, feedback);
        if (games_.focus() != before)
            fill_game_info();
        const ps5cc::Game *g = focused_game();
        if (event == ui::Event::activated && g != nullptr)
        {
            ps5cc::source().launch(g->id);
            return;
        }
        if (input.is_pressed(Action::north) && g != nullptr)
        {
            fill_details();
            sheet_.open(feedback);
            return;
        }
        // □: the game tools, as the buttons under each card of the web app (□ is the kit's "more for this item").
        if (input.is_pressed(Action::west) && g != nullptr)
            tools_.open_menu(*g, g->id == data_.status.game_id, feedback);
    }

    void route_jobs(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = job_focus_ + (input.nav == Direction::down ? 1 : -1);
            if (next >= 0 && next < static_cast<int>(JobKind::count))
            {
                job_focus_ = next;
                feedback.play(audio::Cue::focus);
            }
            else if (!input.nav_repeat)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            }
            return;
        }
        if (input.is_pressed(Action::west))
        {
            const ps5cc::Job &job = data_.jobs[static_cast<std::size_t>(job_focus_)];
            if (!job.active)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            if (!job.cancellable)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                toasts_.push(ui::StatusKind::info, "Lässt sich nicht abbrechen",
                             static_cast<JobKind>(job_focus_) == JobKind::deletion
                                 ? "Ein Löschen, das begonnen hat, läuft bis zum Ende."
                                 : "ShadowMountPlus lässt diesen Auftrag gerade nicht abbrechen.");
                return;
            }
            // No second question (user, 27.10.2026): □ cancels at once.
            feedback.play(audio::Cue::select);
            ps5cc::source().cancel_job(static_cast<JobKind>(job_focus_));
        }
    }

    // Power-Optionen fire at once – nothing is asked twice (user, 27.10.2026).
    void do_power(int tag, app::Feedback &feedback)
    {
        const char *action = tag == kMenuStandby ? "standby" : tag == kMenuReboot ? "reboot" : tag == kMenuOff ? "off" : "safemode";
        const char *what = tag == kMenuStandby ? "Ruhemodus" : tag == kMenuReboot ? "Neustart" : tag == kMenuOff ? "Ausschalten"
                                                                                                                : "Abgesicherter Modus";
        feedback.play(audio::Cue::select);
        toasts_.push(ui::StatusKind::info, what, "Die Konsole bekommt den Auftrag.");
        ps5cc::source().power(action);
    }

    void show_notice(const ps5cc::Notice &n, app::Feedback &feedback)
    {
        if (n.kind == ps5cc::Notice::Kind::other_running)
        {
            // A start the user asked for while another game runs: that one is closed and the new one started, without
            // a question (user, 27.10.2026). The start follows once the status no longer names the closed game.
            running_id_ = n.running_id;
            switch_target_ = n.target_id;
            switch_wait_ = kSwitchWaitSeconds;
            ps5cc::source().close_game(n.running_id);
            toasts_.push(ui::StatusKind::info, "Spiel wird gewechselt",
                         "„" + n.running_name + "“ wird beendet, danach startet „" + n.target_name + "“.");
            feedback.play(audio::Cue::select);
            return;
        }
        if (n.kind == ps5cc::Notice::Kind::job_started)
        {
            // Straight to the job's card, as the web app's dialog turns into the job's progress: what runs, how far,
            // and □ to cancel. Only from the library itself; elsewhere the toast says where to look.
            const bool here = view() == kGames;
            toasts_.push(ui::StatusKind::success, n.title,
                         n.body + (here ? "" : " · Fortschritt unter Spiele → Aufträge"));
            if (here && n.job != JobKind::count)
            {
                games_tabs_.set_active(1);
                job_focus_ = static_cast<int>(n.job);
                feedback.play(audio::Cue::tab);
                switched();
            }
            return;
        }
        if (n.kind == ps5cc::Notice::Kind::leaving)
        {
            toasts_.push(ui::StatusKind::success, n.title, n.body);
            if (leave_in_ <= 0.0f)
                leave_in_ = 1.2f;
            return;
        }
        const ui::StatusKind kind = n.kind == ps5cc::Notice::Kind::success ? ui::StatusKind::success
                                    : n.kind == ps5cc::Notice::Kind::error ? ui::StatusKind::danger
                                                                           : ui::StatusKind::info;
        toasts_.push(kind, n.title, n.body);
    }

    // ---------------------------------------------------------------- draw

    void draw_initials(ui::Canvas &canvas, const Rect &box, const std::string &name, float size) const
    {
        const Color fill = ui::Avatar::color_of(name, 0.55f, 0.55f);
        canvas.list.rounded_rect(box, box.w * 0.22f, fill);
        ui::text(canvas.list, canvas.fonts.semibold, ui::Avatar::initials_of(name), box.x + box.w * 0.5f,
                 box.y + box.h * 0.5f + size * 0.36f, size, Color::rgb(0xffffff), gfx::Align::center);
    }

    // The game's cover once the worker has brought it, faded in over the initials it replaces.
    void draw_cover(ui::Canvas &canvas, const Rect &box, const std::string &id, const std::string &name,
                    float size) const
    {
        const ps5cc::CoverShown cover = id.empty() ? ps5cc::CoverShown{} : ps5cc::covers().texture_of(id);
        const float alpha = cover.texture == 0 ? 0.0f : context_.settings.reduced_motion ? 1.0f : cover.alpha;
        if (alpha < 1.0f)
            draw_initials(canvas, box, name, size);
        if (alpha > 0.0f)
            canvas.list.image(cover.texture, box, gfx::kFullUv, Color{1.0f, 1.0f, 1.0f, alpha}, box.w * 0.22f);
    }

    // The area's picture behind everything (user, 29.10.2026), dimmed so text stays readable: darker on the left
    // (sidebar, titles) and at the top and bottom (title line, hint row), the picture showing most on the right.
    void draw_area_picture(ui::Canvas &canvas) const
    {
        const std::uint32_t tex = ps5cc::backdrops().texture(nav_.current());
        if (tex == 0)
            return;
        gfx::DrawList &list = canvas.list;
        const Rect screen{0.0f, 0.0f, 1920.0f, 1080.0f};
        list.image(tex, screen, gfx::kFullUv, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f);
        list.gradient_rect_h(screen, 0.0f, kNavy.with_alpha(0.80f), kNavy.with_alpha(0.12f));
        list.gradient_rect({0.0f, 0.0f, 1920.0f, 200.0f}, 0.0f, kNavy.with_alpha(0.75f), kNavy.with_alpha(0.0f));
        list.gradient_rect({0.0f, 860.0f, 1920.0f, 220.0f}, 0.0f, kNavy.with_alpha(0.0f), kNavy.with_alpha(0.85f));
    }

    void panel(ui::Canvas &canvas, const Rect &r) const
    {
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        paint.panel(r);
    }

    void draw_header(ui::Canvas &canvas) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        // The app's name heads the sidebar column; the page's name follows it on the same line.
        const float brand = ui::text(list, fonts.semibold, "PS5 COOLING & SYSTEM CENTER", kMargin, 52.0f, 18.0f,
                                     kBlue, gfx::Align::left, 3.0f);
        const float pro = ui::text(list, fonts.display, "Pro", kMargin + brand + 14.0f, 53.0f, 22.0f, kInk);
        const int area = nav_.current();
        if (area >= 0 && area < static_cast<int>(nav_.entries().size()))
        {
            const float x = kMargin + brand + 14.0f + pro + 22.0f;
            list.rounded_rect({x, 38.0f, 2.0f, 20.0f}, 1.0f, Color::rgb(0x3880ff, 0.4f));
            ui::text(list, fonts.semibold, nav_.entries()[static_cast<std::size_t>(area)].label, x + 22.0f, 53.0f,
                     22.0f, kInk);
        }

        if (const ui::TabBar *tabs = own_subtabs())
        {
            tabs->draw(canvas);
            ps5cc::draw_subtab_glyphs(canvas, *tabs);
        }

        // What runs on the console right now, top right on the title line.
        const ps5cc::Status &s = data_.status;
        ui::Painter paint(list, fonts, theme_);
        const std::string name = ui::fit_label(paint, s.game_name.empty() ? "Kein Spiel" : s.game_name, 22.0f, 640.0f);
        const float w = ui::text(list, fonts.semibold, name, 1824.0f, 53.0f, 22.0f, kInk, gfx::Align::right);
        ui::text(list, fonts.regular, s.game_name.empty() ? "Startbildschirm ·" : "Im Spiel ·", 1824.0f - w - 10.0f,
                 53.0f, 18.0f, kMuted, gfx::Align::right);
    }

    void draw_cooling(ui::Canvas &canvas) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const ps5cc::Status &s = data_.status;

        struct TempTile
        {
            const char *label;
            const char *note;
            int value;
        };
        const TempTile tiles[] = {
            {"Prozessor", "CPU", s.cpu_c},
            {"Hauptchip", "SoC", s.soc_c},
            {"Grafik", s.gpu_c < 0 ? "nur PS5 Pro" : "GPU", s.gpu_c},
        };
        const float tile_w = (kLeftW - 2.0f * 24.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float in = tween::stagger(age_, i);
            const Rect r{kLeftX + static_cast<float>(i) * (tile_w + 24.0f), kTop + 10.0f + 30.0f * (1.0f - in),
                         tile_w, 236.0f};
            list.push_opacity(in);
            panel(canvas, r);
            ui::text(list, fonts.semibold, tiles[i].label, r.x + 30.0f, r.y + 50.0f, 24.0f, kInk);
            ui::text(list, fonts.regular, tiles[i].note, r.x + r.w - 30.0f, r.y + 50.0f, 20.0f, kMuted,
                     gfx::Align::right);
            char value[16];
            if (tiles[i].value >= 0)
                std::snprintf(value, sizeof(value), "%d", tiles[i].value);
            else
                std::snprintf(value, sizeof(value), "–");
            const Color ink = tiles[i].value >= 0 ? temp_color(tiles[i].value) : kMuted;
            const float w = ui::text(list, fonts.display, value, r.x + 30.0f, r.y + 168.0f, 104.0f, ink);
            if (tiles[i].value >= 0)
                ui::text(list, fonts.display, "°C", r.x + 40.0f + w, r.y + 168.0f, 40.0f, kMuted);
            // How close to 100 °C, as a thin line along the bottom.
            const float level = tiles[i].value >= 0 ? std::clamp(static_cast<float>(tiles[i].value) / 100.0f, 0.0f, 1.0f) : 0.0f;
            const Rect track{r.x + 30.0f, r.y + r.h - 34.0f, r.w - 60.0f, 8.0f};
            list.rounded_rect(track, 4.0f, Color::rgb(0x1a2850));
            list.rounded_rect({track.x, track.y, track.w * level, track.h}, 4.0f, ink);
            list.pop_opacity();
        }

        chart_.draw(canvas);

        // ---- right column: fan gauge, target, states ----
        const Rect fan_box{kRightX, kTop + 10.0f, kRightW, 380.0f};
        panel(canvas, fan_box);
        ui::text(list, fonts.semibold, "Lüfter", fan_box.x + 30.0f, fan_box.y + 50.0f, 24.0f, kInk);
        ui::text(list, fonts.regular, s.automatic ? "Automatik" : "Beobachten", fan_box.x + fan_box.w - 30.0f,
                 fan_box.y + 50.0f, 20.0f, kMuted, gfx::Align::right);
        gauge_.draw(canvas);

        const Rect target_box{kRightX, kTop + 414.0f, kRightW, 214.0f};
        panel(canvas, target_box);
        ui::text(list, fonts.semibold, "Zieltemperatur", target_box.x + 30.0f, target_box.y + 50.0f, 24.0f,
                 kInk);
        const char *state = pending_target_ > 0                                         ? "wird übernommen …"
                            : data_.status.target_c > 0 && data_.status.target_c != target_ ? "wartet auf die App"
                                                                                          : "übernommen";
        ui::text(list, fonts.regular, state, target_box.x + target_box.w - 30.0f, target_box.y + 50.0f, 20.0f,
                 pending_target_ > 0 ? kWarning : kMuted, gfx::Align::right);
        char target[16];
        std::snprintf(target, sizeof(target), "%d °C", target_);
        const float cx = target_box.x + target_box.w * 0.5f + ui::shake(target_refusal_.value, clock_);
        const float pop = 1.0f + 0.06f * target_bump_.value;
        ui::text(list, fonts.display, target, cx, target_box.y + 150.0f, 76.0f * pop, kInk, gfx::Align::center);
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        ui::draw_button(list, fonts, glyphs, ui::Button::dpad, target_box.x + 40.0f, target_box.y + 124.0f,
                        46.0f);
        ui::text(list, fonts.regular, "50 – 91", target_box.x + target_box.w - 30.0f, target_box.y + 134.0f,
                 20.0f, kMuted, gfx::Align::right);

        // States as chips, wrapped into rows under the target.
        const Rect states{kRightX, kTop + 652.0f, kRightW, kBottom - kTop - 652.0f};
        panel(canvas, states);
        ui::Painter paint(list, fonts, theme_);
        float x = states.x + 26.0f;
        float y = states.y + 24.0f;
        for (const ui::Chip &chip : chips_)
        {
            const float w = chip.width(paint);
            if (x + w > states.x + states.w - 26.0f)
            {
                x = states.x + 26.0f;
                y += 54.0f;
            }
            ui::Chip &c = const_cast<ui::Chip &>(chip);
            c.set_bounds({x, y, w, 42.0f});
            chip.draw(canvas);
            x += w + 12.0f;
        }
    }

    void draw_games(ui::Canvas &canvas) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        if (!data_.games_loaded)
        {
            panel(canvas, games_.bounds());
            ui::text(list, fonts.semibold, "Spiele werden geladen …", kLeftX + 40.0f, kTop + 90.0f, 28.0f,
                     kMuted);
        }
        else
        {
            games_.draw(canvas);
        }

        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        panel(canvas, card);
        const ps5cc::Game *g = focused_game();
        if (g == nullptr)
            return;
        draw_cover(canvas, {card.x + 36.0f, card.y + 36.0f, 150.0f, 150.0f}, g->id, g->name, 56.0f);
        float y = card.y + 70.0f;
        const float text_x = card.x + 212.0f;
        const float text_w = card.w - 248.0f;
        int lines = 0;
        for (const std::string &line : fonts.display.font->wrap(g->name, 34.0f, text_w))
        {
            if (++lines > 3)
                break;
            ui::text(list, fonts.display, line, text_x, y, 34.0f, kInk);
            y += 44.0f;
        }
        const bool running = g->id == data_.status.game_id;
        ui::text(list, fonts.semibold, running ? "Läuft gerade" : g->platform, text_x, y + 8.0f, 22.0f,
                 running ? kGood : kMuted);
        game_info_.draw(canvas);

        const ui::Hint hints[] = {{ui::Button::cross, running ? "Nach vorn holen" : "Starten"},
                                  {ui::Button::triangle, "Details"},
                                  {ui::Button::square, "Aktionen"}};
        ui::HintLayout layout;
        layout.size = 36.0f;
        layout.text_size = 22.0f;
        layout.cy = card.y + card.h - 44.0f;
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 3, card.x + 36.0f, false, layout);
    }

    // Five cards, one per kind of job: copy, convert, image, ShadowMountPlus, delete.
    static Rect job_rect(int index)
    {
        constexpr float kGap = 16.0f;
        const float h = (kBottom - kTop - 10.0f - 4.0f * kGap) / 5.0f;
        return {kLeftX, kTop + 10.0f + static_cast<float>(index) * (h + kGap), kLeftW, h};
    }

    void draw_jobs(ui::Canvas &canvas) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        // The focus: a ring that glides between the job panels.
        const Rect ring = job_focus_amount_.value();
        list.bordered_rect({ring.x - 5.0f, ring.y - 5.0f, ring.w + 10.0f, ring.h + 10.0f}, 22.0f,
                           Color::rgb(0x3880ff, 0.10f), 3.0f, Color::rgb(0x6aa2ff, 0.9f));
        ui::Painter paint(list, fonts, theme_);
        for (int i = 0; i < static_cast<int>(JobKind::count); ++i)
        {
            const Rect r = job_rect(i);
            const ps5cc::Job &job = data_.jobs[static_cast<std::size_t>(i)];
            panel(canvas, r);
            // Title and game on one line, the bar under them: five cards fit the page.
            const float w = ui::text(list, fonts.semibold, job_title(static_cast<JobKind>(i)), r.x + 32.0f,
                                     r.y + 46.0f, 24.0f, kInk);
            const float x = r.x + 32.0f + w + 22.0f;
            if (!job.active && !job.finished)
            {
                ui::text(list, fonts.regular, "Kein Auftrag", x, r.y + 46.0f, 22.0f, kMuted);
                continue;
            }
            ui::text(list, fonts.regular,
                     ui::fit_label(paint, job.name + (job.detail.empty() ? "" : "  " + job.detail), 22.0f,
                                   r.x + r.w - 32.0f - x),
                     x, r.y + 46.0f, 22.0f, kMuted);
            bars_[static_cast<std::size_t>(i)].draw(canvas);
        }

        ui::text(list, fonts.semibold, "Verlauf", kRightX, kTop + 36.0f, 26.0f, kInk);
        history_.draw(canvas);
    }

    // Dateien, System, Profil: their pages come later; until then the web interface has them.
    void draw_placeholder(ui::Canvas &canvas) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const float in = context_.settings.reduced_motion ? 1.0f : tween::stagger(placeholder_age_, 0, 0.0f, 0.35f);
        const Rect card{kLeftX, kTop + 10.0f + 24.0f * (1.0f - in), 1824.0f - kLeftX, kBottom - kTop - 10.0f};
        list.push_opacity(in);
        panel(canvas, card);
        const int area = nav_.current();
        const float cx = card.x + card.w * 0.5f;
        draw_area_icon(list, area, {cx - 60.0f, card.y + 150.0f, 120.0f, 120.0f}, Color::rgb(0x3880ff, 0.85f));
        const char *title = area == kAreaFiles ? "Dateien" : area == kAreaSystem ? "System" : "Profil";
        const char *what = area == kAreaFiles    ? "Ordner der Konsole und der Laufwerke ansehen, kopieren, verschieben und löschen."
                           : area == kAreaSystem ? "Konsole, Speicherplatz, Netzwerk, Langzeitverlauf und Expertenansichten."
                                                 : "Anzeigename, Profilbild und die Sicherung der Einstellungen.";
        ui::text(list, fonts.display, title, cx, card.y + 350.0f, 48.0f, kInk, gfx::Align::center);
        ui::text(list, fonts.regular, what, cx, card.y + 400.0f, 24.0f, kMuted, gfx::Align::center);
        ui::text(list, fonts.semibold, "Kommt in einer der nächsten Fassungen – bis dahin in der Weboberfläche.", cx,
                 card.y + 480.0f, 26.0f, kInk, gfx::Align::center);
        const ui::Hint hints[] = {{ui::Button::cross, "Weboberfläche im Browser öffnen"}};
        ui::HintLayout layout;
        layout.size = 40.0f;
        layout.text_size = 24.0f;
        layout.cy = card.y + 560.0f;
        const float hw = ui::measure_hints(fonts, hints, 1, layout);
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 1, cx - hw * 0.5f, false, layout);
        ui::text(list, fonts.regular, "Oder jederzeit: Options → „Weboberfläche im Browser öffnen“, am Handy oder PC die "
                                      "Adresse der Konsole mit Port 8086.",
                 cx, card.y + 640.0f, 20.0f, kMuted, gfx::Align::center);
        list.pop_opacity();
    }

    // The rail; expanded over the content while it has the focus, with a veil that sets the content back.
    void draw_nav(ui::Canvas &canvas) const
    {
        if (nav_veil_.value > 0.01f)
            canvas.list.rounded_rect({kMargin + kRailW, kTabY - 10.0f, 1824.0f - kMargin - kRailW, kBottom - kTabY + 10.0f},
                                     0.0f, kNavy.with_alpha(0.72f * nav_veil_.value));
        nav_.draw(canvas);
    }

    void draw_footer(ui::Canvas &canvas) const
    {
        gfx::DrawList &list = canvas.list;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        ui::Hint hints[6];
        int count = 0;
        if (nav_focus_)
        {
            hints[count++] = {ui::Button::dpad, "Seite wählen"};
            hints[count++] = {ui::Button::cross, "Öffnen"};
            hints[count++] = {ui::Button::circle, "Zurück"};
        }
        else
        {
            hints[count++] = {ui::Button::l1, "Seiten", ui::Button::r1};
            if (own_subtabs() != nullptr)
                hints[count++] = {ui::Button::l2, "Ansicht", ui::Button::r2};
            switch (view())
            {
            case kCooling:
                hints[count++] = {ui::Button::dpad, "Zieltemperatur"};
                hints[count++] = {ui::Button::cross, "Sofort übernehmen"};
                break;
            case kGames:
                hints[count++] = {ui::Button::cross, "Starten"};
                hints[count++] = {ui::Button::triangle, "Details"};
                hints[count++] = {ui::Button::square, "Aktionen"};
                break;
            case kJobs:
                hints[count++] = {ui::Button::dpad, "Auswählen"};
                hints[count++] = {ui::Button::square, "Abbrechen"};
                break;
            case kPlaceholder:
                hints[count++] = {ui::Button::cross, "Weboberfläche"};
                break;
            default:
                if (const ps5cc::Page *page = current_page())
                    count += page->hints(hints + count);
                break;
            }
            if (count < 6)
                hints[count++] = {ui::Button::touchpad, "Seitenleiste"};
        }
        list.line(kMargin, 966.0f, 1824.0f, 966.0f, 1.0f, Color::rgb(0x3880ff, 0.18f));
        ui::draw_hints(list, canvas.fonts, glyphs, hints, count, kMargin, false);
        const ui::Hint menu[] = {{ui::Button::options, "Menü"}};
        ui::draw_hints(list, canvas.fonts, glyphs, menu, 1, 1824.0f, true);
    }

    app::Context &context_;
    ui::Theme theme_;
    ps5cc::Snapshot data_;
    std::uint64_t seen_ = 0;
    Link shown_link_ = Link::online;
    float age_ = 0.0f;
    float clock_ = 0.0f;

    ui::SideNav nav_;
    bool nav_focus_ = false;
    tween::Spring nav_veil_;
    ui::TabBar cooling_tabs_;
    ui::TabBar games_tabs_;
    float placeholder_age_ = 0.0f;
    ui::ProgressRing gauge_;
    bool gauge_started_ = false;
    ui::LineChart chart_;
    bool chart_started_ = false;
    float chart_clock_ = 0.0f;
    std::deque<float> temp_points_;
    std::deque<float> fan_points_;
    std::array<ui::Chip, 4> chips_;

    int target_ = 75;
    int pending_target_ = 0;
    int applied_target_ = 0;
    float apply_in_ = 0.0f;
    ui::Pulse target_bump_;
    ui::Pulse target_refusal_;

    ui::ListView games_;
    ui::DetailList game_info_;
    ui::DetailList details_;
    std::string details_key_; // what details_ shows, to skip refilling it unchanged
    ui::Sheet sheet_;
    std::size_t shown_games_ = static_cast<std::size_t>(-1);
    std::string shown_running_ = "\x01";

    std::array<ui::ProgressBar, static_cast<std::size_t>(JobKind::count)> bars_;
    ui::ListView history_;
    int job_focus_ = 0;
    ui::SpringRect job_focus_amount_;

    std::array<std::unique_ptr<ps5cc::Page>, kPageCount> pages_;
    ui::Menu menu_;
    std::string switch_target_; // game to start once running_id_ has closed
    float switch_wait_ = 0.0f;  // seconds left to wait for that
    std::string running_id_;
    float leave_in_ = 0.0f; // seconds until the app ends itself for a booked launch
    ui::Dialog link_dialog_;
    ui::ToastStack toasts_;
    ui::LoadingScreen loader_;
    ps5cc::GameTools tools_;
};

} // namespace

std::unique_ptr<app::Concept> make_ps5cc(app::Context &context)
{
    return std::make_unique<Ps5cc>(context);
}

} // namespace hui::concepts
