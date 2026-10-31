// ps5cc - The System area (Phase 7): Konsole, Speicher, Kühlleistung, Zusatzabfragen, Experte.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's System page, in five sub-tabs (L2/R2). Speicher is the drives page of its own
// (storage_page.cpp), held here; the others read the console app's answers as they come
// (SystemData) with cJSON. What only a browser can do stays there: uploading a picture, exporting
// and importing, and the views the browser keeps for itself (monitoring alarms,
// the power history). The expert views are always shown here: there is no expert switch. Nothing is
// asked twice: a probe switched here is saved at once (user, 27.10.2026).

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"
#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

enum View
{
    kConsole = 0,
    kStorage = 1,
    kHealth = 2,
    kProbes = 3,
    kExpert = 4,
};

// List tags: what a row does, and in Experte which part it belongs to (the right card explains that
// part).
constexpr int kInfo = 1;
constexpr int kHealthReset = 10;
constexpr int kProbe = 20;  // + bit index
constexpr int kPreset = 40; // + 0 Sicher, 1 Monitoring, 2 Voll
constexpr int kTimed = 50;  // + 0 2 min, 1 5 min, 2 10 min
constexpr int kTimedStop = 59;
constexpr int kRiskyMeasure = 60;
constexpr int kPart = 100; // + part index * 100 + row, Experte

struct Probe
{
    int bit;
    const char *name;
    const char *about;
};

// The bits of probe_mask, in the web page's order and words.
constexpr Probe kProbeList[] = {
    {1, "Spielerkennung", "Erkennt, welches Spiel gerade läuft."},
    {4, "Netzwerk und Bildschirm", "Verbindung, Adresse und angeschlossenes Anzeigegerät."},
    {2, "Controller-Akku", "Ladestand des DualSense."},
    {8, "Risikotelemetrie",
     "Last aller 16 CPUs, Takt live und Stromschienen (Grafik, Prozessor, Speicher) über "
     "riskantere Wege."},
    {16, "Laufwerkstelemetrie", "Zusätzliche Angaben zu den eingehängten Laufwerken."},
    {32, "Bilder pro Sekunde",
     "Zählt die Bildwechsel der Bildausgabe; mit einem Spiel im Vordergrund dessen Bildrate."},
};

enum Part
{
    kCores = 0,
    kGraphics,
    kMemory,
    kChannels,
    kDrives,
    kRisky,
    kPad,
    kFirmware,
    kParts,
};

constexpr const char *kPartTitle[kParts] = {
    "Auslastung je Kern",  "Grafik",           "Arbeitsspeicher",     "Rohsensoren",
    "Laufwerkstelemetrie", "Risikotelemetrie", "Controller-Diagnose", "Region und Firmware"};
constexpr const char *kPartAbout[kParts] = {
    "Die acht Kerne der Konsole, wie stark sie gerade arbeiten. Die Konsole teilt sie zwischen "
    "Spiel und System auf.",
    "Temperatur, Takt und Leistung des Grafikteils. Die Grafiktemperatur meldet nur die PS5 Pro.",
    "Was die App, der Kernel und die Prozesse belegen; die Seitentabellen begrenzen, wie viel ein "
    "Titel bekommt.",
    "Die acht Temperaturkanäle des SoC, so wie die Konsole sie roh meldet (zuletzt gemessene "
    "Werte).",
    "Laufwerke mit Temperatur, wenn sie eine melden. Braucht die Zusatzabfrage "
    "„Laufwerkstelemetrie“.",
    "Takt, Stromschienen und die rechenhungrigsten Threads, nur auf Abruf. Braucht die "
    "Zusatzabfrage „Risikotelemetrie“.",
    "Welche Wege zum Controller die App gefunden hat. Nur zum Nachsehen bei Problemen mit Akku "
    "oder Lichtleiste.",
    "Sprache, Zeitzone, Region und die Firmware, wie das System sie intern führt.",
};

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

double num(const cJSON *o, const char *name, double fallback = 0.0)
{
    const cJSON *v = get(o, name);
    return v != nullptr && cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

bool has(const cJSON *o, const char *name)
{
    return cJSON_IsNumber(get(o, name));
}

std::string str(const cJSON *o, const char *name)
{
    const cJSON *v = get(o, name);
    return cJSON_IsString(v) ? v->valuestring : std::string();
}

bool yes(const cJSON *o, const char *name)
{
    return cJSON_IsTrue(get(o, name));
}

std::string fixed(double v, int digits)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", digits, v);
    std::string s = text;
    for (char &c : s)
    {
        if (c == '.')
            c = ',';
    }
    return s;
}

std::string uptime(double s)
{
    const long t = static_cast<long>(s);
    char text[48];
    if (t >= 86400)
        std::snprintf(text, sizeof(text), "%ld d %ld h", t / 86400, (t % 86400) / 3600);
    else if (t >= 3600)
        std::snprintf(text, sizeof(text), "%ld h %ld min", t / 3600, (t % 3600) / 60);
    else
        std::snprintf(text, sizeof(text), "%ld min", t / 60);
    return text;
}

ui::ListItem row(const std::string &title, const std::string &value, int tag = kInfo)
{
    ui::ListItem i;
    i.title = title;
    i.value = value;
    i.tag = tag;
    return i;
}

ui::ListItem header(const std::string &title)
{
    ui::ListItem h;
    h.title = title;
    h.header = true;
    h.tag = -1;
    return h;
}

ui::ListItem action(const std::string &title, const std::string &subtitle, int tag,
                    bool disabled = false)
{
    ui::ListItem i;
    i.title = title;
    i.subtitle = subtitle;
    i.tag = tag;
    i.disabled = disabled;
    i.chevron = !disabled;
    return i;
}

// The QR code as filled squares: the console app's SVG is one path of "Mx yhWv1h-Wz" runs in a
// square viewBox.
struct QrCode
{
    int size = 0;
    struct Run
    {
        int x, y, w;
    };
    std::vector<Run> runs;
};

QrCode parse_qr(const std::string &svg)
{
    QrCode q;
    const std::size_t vb = svg.find("viewBox=");
    if (vb != std::string::npos)
    {
        // viewBox="0 0 N N" (or with escaped quotes): the third number is the side
        char *end = nullptr;
        const char *p = svg.c_str() + vb + 8;
        while (*p != '\0' && (*p < '0' || *p > '9'))
            ++p;
        std::strtol(p, &end, 10);
        std::strtol(end, &end, 10);
        q.size = static_cast<int>(std::strtol(end, &end, 10));
    }
    std::size_t at = svg.find(" d=");
    while (at != std::string::npos && (at = svg.find('M', at)) != std::string::npos)
    {
        const char *p = svg.c_str() + at + 1;
        char *end = nullptr;
        const long x = std::strtol(p, &end, 10);
        const long y = std::strtol(end, &end, 10);
        if (end == nullptr || *end != 'h')
            break;
        const long w = std::strtol(end + 1, &end, 10);
        q.runs.push_back({static_cast<int>(x), static_cast<int>(y), static_cast<int>(w)});
        at = static_cast<std::size_t>(end - svg.c_str());
    }
    return q;
}

class SystemPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        style_subtabs(views_, theme, 5);
        views_.set_tabs(
            {{"Konsole"}, {"Speicher"}, {"Kühlleistung"}, {"Zusatzabfragen"}, {"Experte"}});
        views_.set_active(kConsole, true);

        list_.style.theme = theme;
        list_.style.panel = true;
        list_.style.row_height = 58.0f;
        list_.style.title_size = 22.0f;
        list_.style.subtitle_size = 18.0f;
        list_.style.value_size = 21.0f;
        list_.style.highlight.kind = ui::HighlightKind::bar;
        list_.style.header_rule = true;
        list_.style.header_height = 50.0f;
        list_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});

        storage_ = make_storage_page();
        storage_->init(theme);
    }

    void absorb(const Snapshot &data) override
    {
        storage_->absorb(data);
        if (data.system_data.seq != seq_)
        {
            seq_ = data.system_data.seq;
            d_ = data.system_data;
            Json c(d_.config);
            if (c.root != nullptr)
            {
                const int mask = static_cast<int>(num(c.root, "probe_mask"));
                if (wanted_mask_ < 0 || wanted_mask_ == mask)
                {
                    wanted_mask_ = -1;
                    mask_ = mask;
                }
                console_mask_ = mask;
                revert_in_s_ = static_cast<int>(num(c.root, "probe_revert_in_s"));
                revert_left_ = static_cast<float>(revert_in_s_);
                revert_mask_ = static_cast<int>(num(c.root, "probe_revert_mask"));
                config_loaded_ = true;
            }
            Json q(d_.qr);
            qr_url_ = str(q.root, "url");
            qr_ = parse_qr(str(q.root, "svg"));
            rebuild();
        }
    }

    void enter() override
    {
        if (view_ == kStorage)
            storage_->enter();
        list_.enter();
    }

    bool modal() const override
    {
        return view_ == kStorage && storage_->modal();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        if (!modal() &&
            (input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next)))
        {
            if (views_.step(input.is_pressed(Action::jump_next) ? 1 : -1, input, feedback) ==
                ui::Event::changed)
            {
                view_ = views_.active();
                key_.clear();
                rebuild();
                list_.set_focus(0);
                if (view_ == kStorage)
                    storage_->enter();
                else
                    list_.enter();
            }
            return;
        }
        if (view_ == kStorage)
        {
            storage_->handle(input, feedback);
            return;
        }
        if (list_.handle(input, feedback) == ui::Event::activated)
            activate(feedback);
    }

    void update(float dt) override
    {
        views_.update(dt);
        list_.update(dt);
        storage_->update(dt);
        // a switch the console never confirmed (save timed out or was refused) gives way to what it
        // reports
        if (wanted_mask_ >= 0 && (wanted_age_ += dt) > 12.0f)
        {
            wanted_mask_ = -1;
            mask_ = console_mask_;
            rebuild();
        }
        // the diagnosis countdown runs here between the polls (every 4 s), so it does not jump
        if (revert_in_s_ > 0)
        {
            revert_left_ = std::max(revert_left_ - dt, 0.0f);
            const int shown = static_cast<int>(std::ceil(revert_left_));
            if (shown != revert_in_s_ && shown > 0)
            {
                revert_in_s_ = shown;
                rebuild();
            }
        }
    }

    void draw(ui::Canvas &canvas) const override
    {
        views_.draw(canvas);
        draw_subtab_glyphs(canvas, views_);
        if (view_ == kStorage)
        {
            storage_->draw(canvas);
            return;
        }
        list_.draw(canvas);
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        ui::Painter paint(list, fonts, theme_);
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        paint.panel(card);
        const float x = card.x + 30.0f;
        const float w = card.w - 60.0f;
        switch (view_)
        {
        case kConsole:
            draw_qr(canvas, paint, card);
            break;
        case kHealth:
        {
            heading(canvas, "Kühlleistung über die Zeit", card);
            float y =
                paragraph(canvas,
                          "Vergleicht die Temperatur der letzten Woche mit früheren Wochen bei "
                          "ähnlicher Last. "
                          "Steigt sie über Wochen, lohnt sich ein Blick auf Staub und Luftwege.",
                          x, card.y + 130.0f, w, kInk, 6);
            paragraph(canvas,
                      "„Vergleich neu beginnen“ nach dem Reinigen oder einer neuen Wärmeleitpaste: "
                      "dann zählt nur, "
                      "was ab jetzt gemessen wird.",
                      x, y + 20.0f, w, kMuted, 6);
            break;
        }
        case kProbes:
        {
            const int tag = focused_tag();
            if (tag >= kProbe && tag < kProbe + 6)
            {
                const Probe &p = kProbeList[tag - kProbe];
                heading(canvas, p.name, card);
                paragraph(canvas, p.about, x, card.y + 130.0f, w, kInk, 5);
            }
            else
                heading(canvas, "Zusatzabfragen", card);
            paragraph(canvas,
                      "Diese Abfragen holen ihre Angaben direkt beim Betriebssystem der PS5. Eine "
                      "davon hat die App "
                      "schon einmal zum Stillstand gebracht, deshalb sind sie zunächst aus. Die "
                      "X-Taste schaltet sofort "
                      "um. Reagiert danach nichts mehr, die App einfach neu an die Konsole "
                      "schicken: dann sind wieder alle "
                      "aus. Eine Diagnose auf Zeit schaltet sich von selbst wieder zurück.",
                      x, card.y + 330.0f, w, kMuted, 10);
            break;
        }
        case kExpert:
        {
            const int part = focused_part();
            heading(canvas, part >= 0 ? kPartTitle[part] : "Experte", card);
            paragraph(canvas,
                      part >= 0 ? kPartAbout[part]
                                : "Werte für die Fehlersuche, so wie die Konsole sie meldet.",
                      x, card.y + 130.0f, w, kInk, 6);
            break;
        }
        default:
            break;
        }
    }

    void draw_overlay(ui::Canvas &canvas) const override
    {
        if (view_ == kStorage)
            storage_->draw_overlay(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        out[0] = {ui::Button::l2, "Ansicht", ui::Button::r2};
        if (view_ == kStorage)
        {
            ui::Hint more[3];
            const int n = storage_->hints(more);
            for (int i = 0; i < n && i < 2; ++i)
                out[1 + i] = more[i];
            return 1 + std::min(n, 2);
        }
        const int tag = focused_tag();
        if (tag >= kProbe && tag < kProbe + 6)
        {
            out[1] = {ui::Button::cross,
                      (mask_ & kProbeList[tag - kProbe].bit) != 0 ? "Ausschalten" : "Einschalten"};
            return 2;
        }
        if (tag == kHealthReset || tag == kRiskyMeasure || tag == kTimedStop ||
            (tag >= kPreset && tag < kPreset + 3) || (tag >= kTimed && tag < kTimed + 3))
        {
            out[1] = {ui::Button::cross, "Ausführen"};
            return 2;
        }
        return 1;
    }

  private:
    int focused_tag() const
    {
        const int i = list_.focus();
        if (i < 0 || i >= static_cast<int>(list_.items().size()))
            return -1;
        return list_.items()[static_cast<std::size_t>(i)].tag;
    }

    int focused_part() const
    {
        const int tag = focused_tag();
        if (tag == kRiskyMeasure)
            return kRisky;
        if (tag < kPart)
            return -1;
        return std::clamp((tag - kPart) / 100, 0, kParts - 1);
    }

    void activate(ui::Feedback &feedback)
    {
        const int tag = focused_tag();
        // a switch flips one bit of the console's mask: before that mask is known it would flip it
        // in 0 and switch every other probe off
        const bool switches = (tag >= kProbe && tag < kProbe + 6) ||
                              (tag >= kPreset && tag < kPreset + 3) ||
                              (tag >= kTimed && tag < kTimed + 3) || tag == kTimedStop;
        if (switches && !config_loaded_)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        if (tag >= kProbe && tag < kProbe + 6)
        {
            set_mask(mask_ ^ kProbeList[tag - kProbe].bit, 0, feedback);
            return;
        }
        if (tag >= kPreset && tag < kPreset + 3)
        {
            static const int kMasks[3] = {0, 13, 63};
            set_mask(kMasks[tag - kPreset], 0, feedback);
            return;
        }
        if (tag >= kTimed && tag < kTimed + 3)
        {
            static const int kMasks[3] = {13, 13, 63};
            static const int kSeconds[3] = {120, 300, 600};
            set_mask(kMasks[tag - kTimed], kSeconds[tag - kTimed], feedback);
            return;
        }
        if (tag == kTimedStop)
        {
            set_mask(revert_mask_, 0, feedback);
            return;
        }
        if (tag == kHealthReset)
        {
            feedback.play(audio::Cue::select);
            source().system_post("cooling-health", "{}");
            return;
        }
        if (tag == kRiskyMeasure)
        {
            feedback.play(audio::Cue::select);
            source().risky_measure();
            return;
        }
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    }

    void set_mask(int mask, int revert_s, ui::Feedback &feedback)
    {
        feedback.play(audio::Cue::select);
        mask_ = mask;
        wanted_mask_ = mask;
        wanted_age_ = 0.0f;
        std::string json = "{\"probe_mask\":" + std::to_string(mask);
        if (revert_s > 0)
            json += ",\"probe_revert_after_s\":" + std::to_string(revert_s);
        source().save_config(json + "}");
        rebuild();
    }

    // ------------------------------------------------------------ the lists

    std::vector<ui::ListItem> console_items() const
    {
        std::vector<ui::ListItem> out;
        Json s(d_.system);
        const cJSON *r = s.root;
        if (r == nullptr)
        {
            out.push_back(row("Wird gelesen …", ""));
            return out;
        }
        out.push_back(header("KONSOLE"));
        std::string model = str(r, "model");
        if (model.rfind("CFI-7", 0) == 0)
            model += " (PS5 Pro)";
        out.push_back(row("Modell", model));
        if (!str(r, "console_name").empty())
            out.push_back(row("Name", str(r, "console_name")));
        out.push_back(row("Seriennummer", str(r, "serial_masked")));
        out.push_back(row("Firmware", str(r, "firmware_version")));
        out.push_back(row("Läuft seit", uptime(num(r, "uptime_sec"))));
        out.push_back(row("App-Version", str(r, "app_version")));

        out.push_back(header("NETZWERK"));
        const cJSON *n = get(r, "network");
        if (n == nullptr || !yes(n, "probed"))
            out.push_back(row("Netzwerk", "Abfrage aus (Zusatzabfragen)"));
        else if (!yes(n, "up"))
            out.push_back(row("Verbindung", "keine"));
        else
        {
            const bool wlan = str(n, "kind") == "wlan";
            std::string link = wlan ? "WLAN" : "Kabel";
            if (wlan && !str(n, "ssid").empty())
                link += " · " + str(n, "ssid");
            if (wlan && has(n, "signal_pct"))
                link += " · " + std::to_string(static_cast<int>(num(n, "signal_pct"))) + " %";
            out.push_back(row("Verbindung", link));
            if (wlan && has(n, "band_ghz"))
                out.push_back(row(
                    "Band", fixed(num(n, "band_ghz"), num(n, "band_ghz") < 3 ? 1 : 0) + " GHz"));
            if (has(n, "link_mbit"))
                out.push_back(
                    row("Geschwindigkeit",
                        std::to_string(static_cast<int>(num(n, "link_mbit"))) + " MBit/s"));
            out.push_back(row("IP-Adresse", str(n, "ip")));
        }

        const cJSON *dp = get(r, "display");
        if (dp != nullptr && has(dp, "width"))
        {
            out.push_back(header("BILDSCHIRM"));
            if (!str(dp, "name").empty())
                out.push_back(row("Gerät", str(dp, "name")));
            std::string mode = std::to_string(static_cast<int>(num(dp, "width"))) + " × " +
                               std::to_string(static_cast<int>(num(dp, "height")));
            if (has(dp, "refresh_hz"))
                mode += " · " + std::to_string(static_cast<int>(num(dp, "refresh_hz"))) + " Hz";
            if (yes(dp, "hdr"))
                mode += " · HDR";
            out.push_back(row("Auflösung", mode));
        }

        const cJSON *pad = get(r, "controller");
        if (pad != nullptr)
        {
            out.push_back(header("CONTROLLER"));
            out.push_back(row("In Benutzung", yes(pad, "in_use") ? "ja" : "nein"));
            if (has(pad, "battery_pct") && num(pad, "battery_pct") >= 0)
                out.push_back(
                    row("Akku", std::to_string(static_cast<int>(num(pad, "battery_pct"))) + " %" +
                                    (yes(pad, "charging") ? " · lädt" : "")));
        }
        return out;
    }

    std::vector<ui::ListItem> health_items() const
    {
        std::vector<ui::ListItem> out;
        Json h(d_.health);
        out.push_back(header("KÜHLLEISTUNG ÜBER DIE ZEIT"));
        if (h.root == nullptr)
            out.push_back(row("Wird gelesen …", ""));
        else if (!yes(h.root, "verdict_ready"))
        {
            out.push_back(row("Stand", "noch zu wenig Daten"));
            out.push_back(
                row("Wochen gemessen",
                    std::to_string(static_cast<int>(num(h.root, "weeks_usable"))) + " von " +
                        std::to_string(static_cast<int>(num(h.root, "weeks_needed", 4)))));
        }
        else
        {
            const double delta = num(h.root, "delta_c");
            const char *verdict = delta >= 5.0    ? "deutlich wärmer als früher"
                                  : delta >= 2.0  ? "etwas wärmer als früher"
                                  : delta <= -2.0 ? "kühler als früher"
                                                  : "unverändert";
            ui::ListItem v = row("Stand", verdict);
            v.badge = delta >= 5.0 ? "PRÜFEN" : delta >= 2.0 ? "BEOBACHTEN" : "";
            out.push_back(v);
            out.push_back(row("Letzte Woche", fixed(num(h.root, "current_c"), 1) + " °C"));
            out.push_back(row("Früher", fixed(num(h.root, "baseline_c"), 1) + " °C"));
            out.push_back(row("Unterschied", (delta > 0 ? "+" : "") + fixed(delta, 1) + " °C"));
            out.push_back(row("Wochen gemessen",
                              std::to_string(static_cast<int>(num(h.root, "weeks_usable")))));
        }
        out.push_back(action("Vergleich neu beginnen", "Nach dem Reinigen: zählt ab jetzt neu.",
                             kHealthReset));
        return out;
    }

    std::vector<ui::ListItem> probe_items() const
    {
        std::vector<ui::ListItem> out;
        out.push_back(header("ZUSATZABFRAGEN"));
        for (int i = 0; i < 6; ++i)
        {
            ui::ListItem it = row(kProbeList[i].name,
                                  !config_loaded_                    ? "…"
                                  : (mask_ & kProbeList[i].bit) != 0 ? "An"
                                                                     : "Aus",
                                  kProbe + i);
            out.push_back(it);
        }
        out.push_back(header("VORGABEN"));
        out.push_back(action("Sicher", "Alle aus.", kPreset + 0));
        out.push_back(action("Monitoring",
                             "Spielerkennung, Netzwerk und Bildschirm, Risikotelemetrie.",
                             kPreset + 1));
        out.push_back(action("Voll", "Alle sechs an.", kPreset + 2));
        out.push_back(header("DIAGNOSE AUF ZEIT"));
        if (revert_in_s_ > 0)
        {
            char left[32];
            std::snprintf(left, sizeof(left), "noch %d:%02d", revert_in_s_ / 60, revert_in_s_ % 60);
            out.push_back(row("Diagnose läuft", left));
            out.push_back(action("Diagnose stoppen", "Stellt die Abfragen von vorher wieder her.",
                                 kTimedStop));
        }
        out.push_back(
            action("Diagnose 2 Min.", "Monitoring für 2 Minuten, danach wie vorher.", kTimed + 0));
        out.push_back(
            action("Diagnose 5 Min.", "Monitoring für 5 Minuten, danach wie vorher.", kTimed + 1));
        out.push_back(action("Diagnose 10 Min.", "Alle sechs für 10 Minuten, danach wie vorher.",
                             kTimed + 2));
        return out;
    }

    std::vector<ui::ListItem> expert_items() const
    {
        std::vector<ui::ListItem> out;
        Json s(d_.system);
        Json st(d_.status);
        const cJSON *load = get(st.root, "load");
        const auto add = [&](int part, int &n, const std::string &title, const std::string &value)
        { out.push_back(row(title, value, kPart + part * 100 + n++)); };

        // cores
        int n = 0;
        out.push_back(header("AUSLASTUNG JE KERN"));
        const cJSON *cores = get(load, "cores");
        int i = 0;
        const cJSON *c = nullptr;
        cJSON_ArrayForEach(c, cores)
        {
            const double v = cJSON_IsNumber(c) ? c->valuedouble : -1;
            add(kCores, n, "Kern " + std::to_string(i++),
                v < 0 ? "–" : std::to_string(static_cast<int>(v)) + " %");
        }
        if (n == 0)
            add(kCores, n, "Auslastung", "keine Angabe");

        // graphics
        n = 0;
        out.push_back(header("GRAFIK"));
        const cJSON *t = get(st.root, "temperatures");
        if (yes(t, "gpu_valid"))
            add(kGraphics, n, "Temperatur",
                std::to_string(static_cast<int>(num(t, "gpu_c"))) + " °C");
        const cJSON *lc = get(load, "live_clocks");
        if (has(lc, "gfx_mhz"))
            add(kGraphics, n, "Grafiktakt",
                std::to_string(static_cast<int>(num(lc, "gfx_mhz"))) + " MHz" +
                    (has(lc, "gfx_limit_mhz")
                         ? " · Grenze " +
                               std::to_string(static_cast<int>(num(lc, "gfx_limit_mhz"))) + " MHz"
                         : ""));
        if (has(lc, "mem_mhz"))
            add(kGraphics, n, "Speichertakt",
                std::to_string(static_cast<int>(num(lc, "mem_mhz"))) + " MHz");
        const cJSON *pw = get(st.root, "power");
        if (yes(pw, "valid"))
        {
            if (has(pw, "gpu_w"))
                add(kGraphics, n, "Leistung Grafik", fixed(num(pw, "gpu_w"), 1) + " W");
            if (has(pw, "mem_w"))
                add(kGraphics, n, "Leistung Speicher", fixed(num(pw, "mem_w"), 1) + " W");
        }
        const cJSON *mem = get(s.root, "memory");
        if (has(mem, "pt_gpu_total"))
            add(kGraphics, n, "Seitentabellen Grafik",
                std::to_string(static_cast<int>(num(mem, "pt_gpu_used"))) + " von " +
                    std::to_string(static_cast<int>(num(mem, "pt_gpu_total"))));
        if (n == 0)
            add(kGraphics, n, "Grafik", "keine Angabe");

        // memory
        n = 0;
        out.push_back(header("ARBEITSSPEICHER"));
        if (mem != nullptr)
        {
            add(kMemory, n, "Diese App", fixed(num(mem, "rss_mb"), 1) + " MB");
            add(kMemory, n, "Kernel", fixed(num(mem, "kernel_mb"), 0) + " MB");
            add(kMemory, n, "Fest belegt", fixed(num(mem, "wire_mb"), 1) + " MB");
            add(kMemory, n, "Seitentabellen Prozessor",
                std::to_string(static_cast<int>(num(mem, "pt_cpu_used"))) + " von " +
                    std::to_string(static_cast<int>(num(mem, "pt_cpu_total"))));
            int shown = 0;
            const cJSON *p = nullptr;
            cJSON_ArrayForEach(p, get(mem, "processes"))
            {
                if (num(p, "used_mb") <= 0 || shown >= 8)
                    continue;
                ++shown;
                std::string name = str(p, "name");
                if (name.find("ps5tm") != std::string::npos)
                    name += " (diese App)";
                add(kMemory, n, name, fixed(num(p, "used_mb"), 0) + " MB");
            }
        }
        const cJSON *ps = get(s.root, "power_state");
        if (!str(ps, "mode").empty())
            add(kMemory, n, "Betriebszustand",
                str(ps, "mode") + " · " + std::to_string(static_cast<int>(num(ps, "idle_s"))) +
                    " s ohne Eingabe");
        if (n == 0)
            add(kMemory, n, "Arbeitsspeicher", "keine Angabe");

        // SoC channels: the last reading of each
        n = 0;
        out.push_back(header("ROHSENSOREN (SOC-KANÄLE)"));
        Json ch(d_.channels);
        int k = 0;
        const cJSON *series = nullptr;
        cJSON_ArrayForEach(series, get(ch.root, "channels"))
        {
            double last = -1;
            const cJSON *v = nullptr;
            cJSON_ArrayForEach(v, series)
            {
                if (cJSON_IsNumber(v) && v->valuedouble >= 0)
                    last = v->valuedouble;
            }
            add(kChannels, n, "Kanal " + std::to_string(k++),
                last < 0 ? "–" : fixed(last, 0) + " °C");
        }
        if (n == 0)
            add(kChannels, n, "Kanäle", "keine Angabe");

        // drives
        n = 0;
        out.push_back(header("LAUFWERKSTELEMETRIE"));
        Json dr(d_.drives);
        const cJSON *dv = nullptr;
        cJSON_ArrayForEach(dv, get(dr.root, "drives"))
        {
            std::string label = str(dv, "label").empty() ? str(dv, "device") : str(dv, "label");
            // the console's note is a sentence: it goes under the name, the value stays short
            const bool temp = yes(dv, "temp_valid");
            add(kDrives, n, label,
                temp ? std::to_string(static_cast<int>(num(dv, "temp_c"))) + " °C"
                     : "keine Temperatur");
            if (!temp)
                out.back().subtitle = str(dv, "temp_note");
        }
        if (n == 0)
            add(kDrives, n, "Laufwerke",
                (mask_ & 16) == 0 ? "Abfrage aus (Zusatzabfragen)" : "keine gemeldet");

        // risky, on request
        n = 0;
        out.push_back(header("RISIKOTELEMETRIE"));
        out.push_back(action("Jetzt messen",
                             (mask_ & 8) == 0
                                 ? "Erst die Zusatzabfrage „Risikotelemetrie“ einschalten."
                                 : "Liest einmal Takt, Stromschienen und Threads.",
                             kRiskyMeasure, (mask_ & 8) == 0));
        Json rk(d_.risky);
        if (rk.root != nullptr)
        {
            const cJSON *rl = get(rk.root, "load");
            if (yes(rl, "cpu_valid"))
            {
                // game and system shares come only when they were measured: absent is "–", not 0 %
                const auto pct = [&](const char *key)
                {
                    return has(rl, key) ? std::to_string(static_cast<int>(num(rl, key))) + " %"
                                        : std::string("–");
                };
                add(kRisky, n, "Prozessor",
                    pct("cpu_pct") + " · Spiel " + pct("game_pct") + " · System " +
                        pct("system_pct"));
            }
            if (yes(rl, "cpu_mhz_valid"))
                add(kRisky, n, "Prozessortakt",
                    std::to_string(static_cast<int>(num(rl, "cpu_mhz"))) + " MHz");
            const cJSON *rp = get(rk.root, "power");
            if (yes(rp, "valid"))
            {
                add(kRisky, n, "Leistung jetzt", fixed(num(rp, "live_w"), 1) + " W");
                const cJSON *rail = nullptr;
                cJSON_ArrayForEach(rail, get(rp, "rails"))
                    add(kRisky, n, "Schiene " + str(rail, "name"),
                        fixed(num(rail, "w"), 1) + " W · " + fixed(num(rail, "v"), 2) + " V · " +
                            fixed(num(rail, "a"), 1) + " A");
            }
            const cJSON *th = nullptr;
            cJSON_ArrayForEach(th, get(rk.root, "top_threads"))
                add(kRisky, n, "Thread " + str(th, "name"),
                    std::to_string(static_cast<int>(num(th, "pct"))) + " %");
        }

        // controller
        n = 0;
        out.push_back(header("CONTROLLER-DIAGNOSE"));
        Json dg(d_.diag);
        const auto ok = [](bool b) { return std::string(b ? "gefunden" : "fehlt"); };
        const cJSON *sp = get(dg.root, "scepad");
        const cJSON *hid = get(dg.root, "hid");
        if (sp != nullptr)
        {
            add(kPad, n, "Controller-Abfrage", ok(yes(sp, "init") && yes(sp, "read_state")));
            add(kPad, n, "Lichtleiste", ok(yes(sp, "set_lightbar")));
        }
        if (hid != nullptr)
        {
            add(kPad, n, "Akku-Abfrage", ok(yes(hid, "hidcontrol_get_battery_state")));
            add(kPad, n, "Bluetooth", ok(yes(hid, "dev_bluetooth_hid_present")));
        }
        if (n == 0)
            add(kPad, n, "Controller", "keine Angabe");

        // locale and firmware
        n = 0;
        out.push_back(header("REGION UND FIRMWARE"));
        const cJSON *loc = get(s.root, "locale");
        if (loc != nullptr)
        {
            add(kFirmware, n, "Sprache (Code)",
                std::to_string(static_cast<int>(num(loc, "language"))));
            const int off = static_cast<int>(num(loc, "timezone_offset_min"));
            char tz[32];
            std::snprintf(tz, sizeof(tz), "UTC%c%d:%02d", off < 0 ? '-' : '+', std::abs(off) / 60,
                          std::abs(off) % 60);
            add(kFirmware, n, "Zeitzone", tz);
            if (!str(loc, "region_code").empty())
                add(kFirmware, n, "Region", str(loc, "region_code"));
        }
        if (s.root != nullptr)
        {
            add(kFirmware, n, "Firmware intern", str(s.root, "firmware_raw"));
            add(kFirmware, n, "Firmware-Gruppe", str(s.root, "firmware_group"));
            add(kFirmware, n, "Firmware laut System", str(s.root, "firmware_reported"));
        }
        return out;
    }

    // Rebuilt only when what the list shows changed: a refill restarts its animation.
    void rebuild()
    {
        if (view_ == kStorage)
            return;
        std::vector<ui::ListItem> next = view_ == kConsole  ? console_items()
                                         : view_ == kHealth ? health_items()
                                         : view_ == kProbes ? probe_items()
                                                            : expert_items();
        std::string key = std::to_string(view_);
        for (const ui::ListItem &i : next)
            key += '\n' + i.title + '\t' + i.subtitle + '\t' + i.value + '\t' + i.badge +
                   (i.disabled ? "-" : "");
        if (key == key_)
            return;
        key_ = std::move(key);
        const int focus = refocus(list_, next);
        list_.set_items(next);
        list_.set_focus(focus);
    }

    // ------------------------------------------------------------ the right card

    void heading(ui::Canvas &canvas, const std::string &title, const Rect &card) const
    {
        ui::Painter paint(canvas.list, canvas.fonts, theme_);
        ui::text(canvas.list, canvas.fonts.display,
                 ui::fit_label(paint, title, 32.0f, card.w - 60.0f), card.x + 30.0f, card.y + 66.0f,
                 32.0f, kInk);
    }

    void draw_qr(ui::Canvas &canvas, ui::Painter &paint, const Rect &card) const
    {
        gfx::DrawList &list = canvas.list;
        heading(canvas, "Am Handy öffnen", card);
        paragraph(canvas,
                  "Mit der Kamera des Handys scannen: Die Browser-App öffnet sich, mit allem, was "
                  "hier nicht geht "
                  "(Hochladen, Exportieren).",
                  card.x + 30.0f, card.y + 122.0f, card.w - 60.0f, kMuted, 3);
        const float side = 360.0f;
        const Rect box{card.x + (card.w - side) * 0.5f, card.y + 230.0f, side, side};
        if (qr_.size <= 0 || qr_.runs.empty())
        {
            list.rounded_rect(box, 18.0f, Color::rgb(0x3880ff, 0.10f));
            ui::text(list, canvas.fonts.regular, "Wird geladen …", box.cx(), box.cy() + 8.0f, 22.0f,
                     kMuted, gfx::Align::center);
            return;
        }
        // the code dark on white, as a camera expects it; its own quiet zone is part of the viewBox
        list.rounded_rect(box, 18.0f, Color::rgb(0xffffff));
        const float cell = side / static_cast<float>(qr_.size);
        for (const QrCode::Run &r : qr_.runs)
            list.rounded_rect({box.x + static_cast<float>(r.x) * cell,
                               box.y + static_cast<float>(r.y) * cell,
                               static_cast<float>(r.w) * cell + 0.5f, cell + 0.5f},
                              0.0f, Color::rgb(0x0b1430));
        ui::text(list, canvas.fonts.semibold, ui::fit_label(paint, qr_url_, 22.0f, card.w - 60.0f),
                 card.cx(), box.y + box.h + 50.0f, 22.0f, kInk, gfx::Align::center);
    }

    float paragraph(ui::Canvas &canvas, const std::string &text, float x, float y, float width,
                    Color ink, int max_lines) const
    {
        int lines = 0;
        for (const std::string &line : canvas.fonts.regular.font->wrap(text, 21.0f, width))
        {
            if (++lines > max_lines)
                break;
            ui::text(canvas.list, canvas.fonts.regular, line, x, y, 21.0f, ink);
            y += 29.0f;
        }
        return y;
    }

    ui::Theme theme_;
    ui::TabBar views_;
    ui::ListView list_;
    std::unique_ptr<Page> storage_;
    int view_ = kConsole;
    std::string key_;
    std::uint64_t seq_ = 0;
    SystemData d_;
    QrCode qr_;
    std::string qr_url_;
    bool config_loaded_ = false;
    int mask_ = 0;
    int wanted_mask_ = -1; // a switch made here holds until the console reports it, at most 12 s
    float wanted_age_ = 0.0f;
    int console_mask_ = 0; // the mask the console reported last
    int revert_in_s_ = 0;
    float revert_left_ = 0.0f;
    int revert_mask_ = 0;
};

} // namespace

std::unique_ptr<Page> make_system_page()
{
    return std::make_unique<SystemPage>();
}

} // namespace hui::ps5cc
