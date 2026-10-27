// ps5cc - The Settings page: how the fan is regulated, the night mode, the fan self-test.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

enum Row
{
    kMode = 1,
    kProfile,
    kSafety,
    kNight,
    kNightStart,
    kNightEnd,
    kNightTarget,
    kSelfTest,
};

constexpr float kSaveDelay = 0.6f;
constexpr const char *kProfiles[] = {"comfort", "balanced", "cool"};

int profile_index(const std::string &profile)
{
    for (int i = 0; i < 3; ++i)
    {
        if (profile == kProfiles[i])
            return i;
    }
    return 0;
}

bool testing(const SelfTest &t)
{
    return t.state == "running" || t.state == "requested";
}

std::string verdict_text(const SelfTest &t)
{
    if (t.state == "requested")
        return "Wird gestartet …";
    if (t.state == "running")
        return "Läuft …";
    if (t.verdict == "responds")
        return "Der Lüfter reagiert.";
    if (t.verdict == "no_response")
        return "Der Lüfter reagiert nicht.";
    if (t.verdict == "aborted")
        return "Abgebrochen";
    if (t.verdict == "not_possible")
        return "Gerade nicht möglich";
    return "Noch nicht ausgeführt";
}

Color verdict_color(const SelfTest &t)
{
    if (testing(t))
        return kBlue;
    if (t.verdict == "responds")
        return kGood;
    if (t.verdict == "no_response")
        return kCritical;
    if (t.verdict == "aborted" || t.verdict == "not_possible")
        return kWarning;
    return kMuted;
}

class SettingsPage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        form_.style.theme = theme;
        form_.style.panel = true;
        form_.style.on_page = false;
        form_.style.on_text = "An";
        form_.style.off_text = "Aus";
        form_.style.label_ratio = 0.42f;
        form_.style.control_width = 360.0f;
        form_.set_bounds({kMargin, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});
        build(Config{});

        info_.style.theme = theme;
        info_.style.panel = false;
        info_.style.label_size = 20.0f;
        info_.style.value_size = 22.0f;
        info_.style.row_height = 42.0f;
        info_.style.max_lines = 1;
        info_.set_bounds({kRightX + 30.0f, kTop + 200.0f, kRightW - 60.0f, 220.0f});

        chart_.style.theme = theme;
        chart_.style.readout = false;
        chart_.style.legend = ui::LegendPlacement::none;
        chart_.style.axis.ticks = 4;
        chart_.style.axis.from_zero = true;
        chart_.style.axis.include_high = 100.0f;
        chart_.style.axis.unit = " %";
        chart_.title = "Lüfterdrehzahl";
        chart_.set_active(false);
        chart_.set_bounds({kRightX + 16.0f, kTop + 440.0f, kRightW - 32.0f, kBottom - kTop - 456.0f});
        show_test(SelfTest{});
    }

    void absorb(const Snapshot &data) override
    {
        if (data.config.valid && pending_.empty())
        {
            const std::string key = config_key(data.config);
            if (key != applied_)
            {
                applied_ = key;
                apply(data.config);
            }
        }
        const std::string test_key = data.selftest.state + data.selftest.verdict +
                                     std::to_string(data.selftest.samples.size()) +
                                     std::to_string(data.selftest.elapsed_s);
        if (test_key != test_key_)
        {
            test_key_ = test_key;
            show_test(data.selftest);
        }
    }

    void enter() override
    {
        form_.enter();
        chart_.enter();
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        const ui::Event event = form_.handle(input, feedback);
        if (event == ui::Event::changed)
            changed(form_.changed_id());
        else if (event == ui::Event::activated && form_.changed_id() == kSelfTest)
            source().selftest(!testing(test_));
    }

    void update(float dt) override
    {
        form_.update(dt);
        info_.update(dt);
        chart_.update(dt);
        if (save_in_ > 0.0f)
        {
            save_in_ -= dt;
            if (save_in_ <= 0.0f)
                save();
        }
    }

    void draw(ui::Canvas &canvas) const override
    {
        form_.draw(canvas);

        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const Rect card{kRightX, kTop + 10.0f, kRightW, kBottom - kTop - 10.0f};
        ui::Painter paint(list, fonts, theme_);
        paint.panel(card);
        ui::text(list, fonts.semibold, "Lüfter-Selbsttest", card.x + 30.0f, card.y + 50.0f, 26.0f, kInk);
        ui::text(list, fonts.semibold, verdict_text(test_), card.x + 30.0f, card.y + 100.0f, 30.0f,
                 verdict_color(test_));
        std::string why = test_.why;
        if (why.empty())
            why = testing(test_) ? "Die Schwelle steht kurz tief; gemessen wird, ob der Lüfter schneller wird."
                                 : "Prüft, ob die Lüftersteuerung wirkt – unabhängig von der Temperatur.";
        float y = card.y + 140.0f;
        int lines = 0;
        for (const std::string &line : fonts.regular.font->wrap(why, 21.0f, card.w - 60.0f))
        {
            if (++lines > 2)
                break;
            ui::text(list, fonts.regular, line, card.x + 30.0f, y, 21.0f, kMuted);
            y += 28.0f;
        }
        info_.draw(canvas);
        if (!test_.samples.empty())
            chart_.draw(canvas);
    }

    int hints(ui::Hint *out) const override
    {
        out[0] = {ui::Button::dpad, "Auswählen und ändern"};
        if (form_.focus_id() == kSelfTest)
        {
            out[1] = {ui::Button::cross, testing(test_) ? "Test abbrechen" : "Test starten"};
            return 2;
        }
        return 1;
    }

  private:
    static std::string config_key(const Config &c)
    {
        char text[160];
        std::snprintf(text, sizeof(text), "%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d", c.mode.c_str(), c.profile.c_str(),
                      c.safety_temp_c, c.safety_min_c, c.safety_max_c, c.night_enabled ? 1 : 0, c.night_start_min,
                      c.night_end_min, c.night_target_c, c.target_min_c, c.target_max_c);
        return text;
    }

    void build(const Config &c)
    {
        form_.clear();
        form_.add_header("LÜFTERREGELUNG");
        form_.add_choice(kMode, "Modus", {"Automatik", "Beobachten"}, c.mode == "observe" ? 1 : 0).description =
            "Beobachten: Die App misst nur, die Konsole regelt den Lüfter selbst.";
        form_.add_choice(kProfile, "Betriebsart", {"Leise", "Ausgewogen", "Kühl"}, profile_index(c.profile))
            .description = "Wie kräftig der Lüfter auf Wärme antwortet. Das Ziel bleibt.";
        ui::FormRow &safety = form_.add_slider(kSafety, "Sicherheitsgrenze", static_cast<float>(c.safety_temp_c),
                                             static_cast<float>(c.safety_min_c),
                                             static_cast<float>(c.safety_max_c), 1.0f);
        safety.unit = " °C";
        safety.description = "Darüber regelt der Lüfter ohne Rücksicht auf die Lautstärke hoch.";
        form_.add_header("NACHTMODUS");
        form_.add_toggle(kNight, "Nachtmodus", c.night_enabled).description =
            "In dieser Zeit gilt das Nachtziel statt der Zieltemperatur.";
        const auto clock = [](float minutes) { return format_clock(static_cast<int>(minutes)); };
        form_.add_slider(kNightStart, "Beginn", static_cast<float>(c.night_start_min), 0.0f, 1425.0f, 15.0f).format =
            clock;
        form_.add_slider(kNightEnd, "Ende", static_cast<float>(c.night_end_min), 0.0f, 1425.0f, 15.0f).format = clock;
        ui::FormRow &night = form_.add_slider(kNightTarget, "Nachtziel", static_cast<float>(c.night_target_c),
                                            static_cast<float>(c.target_min_c),
                                            static_cast<float>(c.target_max_c), 1.0f);
        night.unit = " °C";
        form_.add_header("LÜFTER-SELBSTTEST");
        form_.add_action(kSelfTest, "Selbsttest starten").description =
            "Senkt die Schwelle für höchstens 30 s und misst, ob der Lüfter schneller wird.";
        night_rows(c.night_enabled);
    }

    void apply(const Config &c)
    {
        form_.set_choice(kMode, c.mode == "observe" ? 1 : 0);
        form_.set_choice(kProfile, profile_index(c.profile));
        if (ui::FormRow *row = form_.row(kSafety))
        {
            row->minimum = static_cast<float>(c.safety_min_c);
            row->maximum = static_cast<float>(c.safety_max_c);
        }
        form_.set_slider(kSafety, static_cast<float>(c.safety_temp_c));
        form_.set_toggle(kNight, c.night_enabled);
        form_.set_slider(kNightStart, static_cast<float>(c.night_start_min));
        form_.set_slider(kNightEnd, static_cast<float>(c.night_end_min));
        if (ui::FormRow *row = form_.row(kNightTarget))
        {
            row->minimum = static_cast<float>(c.target_min_c);
            row->maximum = static_cast<float>(c.target_max_c);
        }
        form_.set_slider(kNightTarget, static_cast<float>(c.night_target_c));
        night_rows(c.night_enabled);
    }

    void night_rows(bool on)
    {
        form_.set_disabled(kNightStart, !on);
        form_.set_disabled(kNightEnd, !on);
        form_.set_disabled(kNightTarget, !on);
    }

    void changed(int id)
    {
        const auto number = [&](int row) { return std::to_string(static_cast<int>(form_.slider_value(row) + 0.5f)); };
        switch (id)
        {
        case kMode:
            pending_["mode"] = form_.choice_index(kMode) == 1 ? "\"observe\"" : "\"automatic\"";
            break;
        case kProfile:
            pending_["profile"] = std::string("\"") + kProfiles[std::clamp(form_.choice_index(kProfile), 0, 2)] + "\"";
            break;
        case kSafety:
            pending_["safety_temp_c"] = number(kSafety);
            break;
        case kNight:
            pending_["night_enabled"] = form_.toggle_value(kNight) ? "1" : "0";
            night_rows(form_.toggle_value(kNight));
            break;
        case kNightStart:
            pending_["night_start_min"] = number(kNightStart);
            break;
        case kNightEnd:
            pending_["night_end_min"] = number(kNightEnd);
            break;
        case kNightTarget:
            pending_["night_target_c"] = number(kNightTarget);
            break;
        default:
            return;
        }
        save_in_ = kSaveDelay;
    }

    // One PUT with every field changed since the last one (the app takes a partial object, as from the web page).
    void save()
    {
        if (pending_.empty())
            return;
        std::string json = "{";
        for (const auto &[key, value] : pending_)
        {
            if (json.size() > 1)
                json += ',';
            json += "\"" + key + "\":" + value;
        }
        json += "}";
        pending_.clear();
        source().save_config(json);
    }

    void show_test(const SelfTest &t)
    {
        test_ = t;
        if (ui::FormRow *row = form_.row(kSelfTest))
            row->label = testing(t) ? "Selbsttest abbrechen" : "Selbsttest starten";
        std::vector<ui::DetailItem> items;
        char text[48];
        std::snprintf(text, sizeof(text), "%d %%", t.baseline_pct);
        items.push_back({"Vorher", t.state == "idle" ? "–" : text});
        std::snprintf(text, sizeof(text), "%d %%", t.peak_pct);
        items.push_back({"Höchstwert", t.state == "idle" ? "–" : text});
        std::snprintf(text, sizeof(text), "+%d %%", t.rise_pct);
        items.push_back({"Anstieg", t.state == "idle" ? "–" : text});
        std::snprintf(text, sizeof(text), "%d von %d s", t.elapsed_s, t.limit_s > 0 ? t.limit_s : 30);
        items.push_back({"Dauer", t.state == "idle" ? "–" : text});
        info_.set_items(items);

        if (t.samples.empty())
            return;
        std::vector<float> values(t.samples.begin(), t.samples.end());
        if (values.size() < 2)
            values.push_back(values.front());
        std::vector<std::string> labels(values.size());
        labels.front() = "0 s";
        labels.back() = std::to_string(values.size() - 1) + " s";
        chart_.set_labels(labels);
        chart_.set_series({ui::ChartSeries{"Lüfter %", values, kBlue}}, false);
    }

    ui::Theme theme_;
    ui::Form form_;
    ui::DetailList info_;
    ui::LineChart chart_;
    std::map<std::string, std::string> pending_; // field -> JSON value, waiting for the save
    float save_in_ = 0.0f;
    std::string applied_; // the config the form shows
    std::string test_key_;
    SelfTest test_;
};

} // namespace

std::unique_ptr<Page> make_settings_page()
{
    return std::make_unique<SettingsPage>();
}

} // namespace hui::ps5cc
