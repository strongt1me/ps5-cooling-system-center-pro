// ps5cc - The Settings page: how the fan is regulated, warnings, the light bar, game profiles,
// the night mode and the fan self-test.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <array>
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
    kWarnCpu,
    kWarnCountdown,
    kLightbar,
    kLightWarn,
    kLightHot,
    kRuleSave,
    kRuleNone,
    kRuleBase = 100, // + index of the kept game profile
};

const char *profile_word(const std::string &profile)
{
    if (profile == "cool")
        return "Kühl";
    if (profile == "balanced")
        return "Ausgewogen";
    return "Leise";
}

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
        form_.set_bounds({kLeftX, kTop + 10.0f, kLeftW, kBottom - kTop - 10.0f});
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
        chart_.set_bounds(
            {kRightX + 16.0f, kTop + 440.0f, kRightW - 32.0f, kBottom - kTop - 456.0f});
        show_test(SelfTest{});
    }

    void absorb(const Snapshot &data) override
    {
        if (data.config.valid)
            config_ = data.config;
        // The game profiles change the form's rows: rebuilt only when they or the running game
        // change, and never over an edit not yet saved.
        if (data.config.valid && pending_.empty())
        {
            std::string rules = data.status.game_id + "|" + data.status.game_name + "|" +
                                data.active_rule + "|" + (data.rules_loaded ? "1" : "0");
            for (const GameRule &r : data.game_rules)
                rules += "|" + r.title_id + ":" + r.title_name + ":" + std::to_string(r.target_c) +
                         ":" + r.profile;
            if (rules != rules_key_)
            {
                rules_key_ = rules;
                game_id_ = data.status.game_id;
                game_name_ = data.status.game_name;
                rules_ = data.game_rules;
                active_rule_ = data.active_rule;
                rules_loaded_ = data.rules_loaded;
                const int keep = form_.focus_id();
                build(data.config);
                applied_.clear();
                if (keep >= 0 && form_.row(keep) != nullptr)
                    form_.focus_row(keep);
                else if (keep >= kRuleBase)
                    form_.focus_row(rules_.empty()
                                        ? kRuleSave
                                        : kRuleBase + static_cast<int>(rules_.size()) - 1);
            }
        }
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
        else if (event == ui::Event::activated && form_.changed_id() == kRuleSave &&
                 !game_id_.empty())
        {
            // the setting in force now, as the web page's "Aktuelle Einstellung merken"
            GameRule rule;
            rule.title_id = game_id_;
            rule.title_name = game_name_.empty() ? game_id_ : game_name_;
            rule.target_c = config_.target_temp_c;
            rule.profile = config_.profile;
            source().save_game_rule(rule);
        }
        else if (event == ui::Event::activated && form_.changed_id() >= kRuleBase)
        {
            // at once, nothing asked twice
            const std::size_t index = static_cast<std::size_t>(form_.changed_id() - kRuleBase);
            if (index < rules_.size())
                source().remove_game_rule(rules_[index].title_id);
        }
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
        ui::text(list, fonts.semibold, "Lüfter-Selbsttest", card.x + 30.0f, card.y + 50.0f, 26.0f,
                 kInk);
        ui::text(list, fonts.semibold, verdict_text(test_), card.x + 30.0f, card.y + 100.0f, 30.0f,
                 verdict_color(test_));
        std::string why = test_.why;
        if (why.empty())
            why = testing(test_)
                      ? "Die Schwelle steht kurz tief; gemessen wird, ob der Lüfter schneller wird."
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
        if (form_.focus_id() == kRuleSave && !game_id_.empty())
        {
            out[1] = {ui::Button::cross, "Für dieses Spiel merken"};
            return 2;
        }
        if (form_.focus_id() >= kRuleBase)
        {
            out[1] = {ui::Button::cross, "Spielprofil entfernen"};
            return 2;
        }
        return 1;
    }

  private:
    static std::string config_key(const Config &c)
    {
        char text[200];
        std::snprintf(text, sizeof(text), "%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
                      c.mode.c_str(), c.profile.c_str(), c.safety_temp_c, c.safety_min_c,
                      c.safety_max_c, c.night_enabled ? 1 : 0, c.night_start_min, c.night_end_min,
                      c.night_target_c, c.target_min_c, c.target_max_c, c.warning_cpu_c,
                      c.warning_countdown_s, c.lightbar_enabled ? 1 : 0, c.lightbar_warn_c,
                      c.lightbar_hot_c, c.target_temp_c);
        return text;
    }

    void build(const Config &c)
    {
        form_.clear();
        form_.add_header("LÜFTERREGELUNG");
        form_.add_choice(kMode, "Modus", {"Automatik", "Beobachten"}, c.mode == "observe" ? 1 : 0)
            .description = "Beobachten: Die App misst nur, die Konsole regelt den Lüfter selbst.";
        form_
            .add_choice(kProfile, "Betriebsart", {"Leise", "Ausgewogen", "Kühl"},
                        profile_index(c.profile))
            .description = "Wie kräftig der Lüfter auf Wärme antwortet. Das Ziel bleibt.";
        ui::FormRow &safety = form_.add_slider(
            kSafety, "Sicherheitsgrenze", static_cast<float>(c.safety_temp_c),
            static_cast<float>(c.safety_min_c), static_cast<float>(c.safety_max_c), 1.0f);
        safety.unit = " °C";
        safety.description = "Darüber regelt der Lüfter ohne Rücksicht auf die Lautstärke hoch.";
        form_.add_header("WARNUNGEN");
        ui::FormRow &warn = form_.add_slider(
            kWarnCpu, "Warnung ab", static_cast<float>(c.warning_cpu_c), 40.0f, 110.0f, 1.0f);
        warn.unit = " °C";
        warn.description = "Meldung auf dem Fernseher, wenn der Prozessor so warm wird. Der Chip "
                           "warnt 5 °C später, im Automatikbetrieb nie unter Ziel + 2 °C.";
        ui::FormRow &countdown =
            form_.add_slider(kWarnCountdown, "Warn-Countdown",
                             static_cast<float>(c.warning_countdown_s), 30.0f, 900.0f, 30.0f);
        countdown.format = [](float seconds)
        {
            const int v = static_cast<int>(seconds + 0.5f);
            return v % 60 == 0 ? std::to_string(v / 60) + " min" : std::to_string(v) + " s";
        };
        countdown.description = "Läuft ab dem Beginn einer Warnung; System zeigt die Restzeit.";
        form_.add_header("LICHTLEISTE");
        form_.add_toggle(kLightbar, "Lichtleiste als Temperaturampel", c.lightbar_enabled)
            .description = "Färbt das Licht des Controllers grün, gelb oder rot. Braucht die "
                           "Zusatzabfrage „Controller“ unter System.";
        ui::FormRow &yellow = form_.add_slider(
            kLightWarn, "Gelb ab", static_cast<float>(c.lightbar_warn_c), 55.0f, 90.0f, 1.0f);
        yellow.unit = " °C";
        ui::FormRow &red = form_.add_slider(
            kLightHot, "Rot ab", static_cast<float>(c.lightbar_hot_c), 56.0f, 95.0f, 1.0f);
        red.unit = " °C";
        red.description = "Liegt immer über Gelb.";
        form_.add_header("SPIELPROFILE");
        {
            const std::string game = game_name_.empty() ? game_id_ : game_name_;
            ui::FormRow &save = form_.add_action(
                kRuleSave, game_id_.empty() ? "Kein Spiel läuft" : "„" + game + "“ merken");
            if (!game_id_.empty())
                save.text = std::to_string(c.target_temp_c) + " °C · " + profile_word(c.profile);
            save.disabled = game_id_.empty();
            save.description = game_id_.empty()
                                   ? "Ein Spielprofil merkt sich Ziel und Betriebsart für das "
                                     "laufende Spiel; dafür muss die Spielerkennung an sein."
                                   : "Ziel und Betriebsart von jetzt gelten künftig, sobald dieses "
                                     "Spiel läuft.";
        }
        for (std::size_t i = 0; i < rules_.size(); ++i)
        {
            const GameRule &r = rules_[i];
            ui::FormRow &row = form_.add_action(kRuleBase + static_cast<int>(i),
                                                r.title_name.empty() ? r.title_id : r.title_name);
            row.text = std::to_string(r.target_c) + " °C · " + profile_word(r.profile) +
                       (r.title_id == active_rule_ ? " · aktiv" : "");
            row.description = r.title_id + " · Die X-Taste entfernt das Profil sofort.";
        }
        if (rules_.empty() && rules_loaded_)
            form_.add_value(kRuleNone, "Noch keine Spielprofile gespeichert", "");
        form_.add_header("NACHTMODUS");
        form_.add_toggle(kNight, "Nachtmodus", c.night_enabled).description =
            "In dieser Zeit gilt das Nachtziel statt der Zieltemperatur.";
        const auto clock = [](float minutes) { return format_clock(static_cast<int>(minutes)); };
        form_
            .add_slider(kNightStart, "Beginn", static_cast<float>(c.night_start_min), 0.0f, 1425.0f,
                        15.0f)
            .format = clock;
        form_
            .add_slider(kNightEnd, "Ende", static_cast<float>(c.night_end_min), 0.0f, 1425.0f,
                        15.0f)
            .format = clock;
        ui::FormRow &night = form_.add_slider(
            kNightTarget, "Nachtziel", static_cast<float>(c.night_target_c),
            static_cast<float>(c.target_min_c), static_cast<float>(c.target_max_c), 1.0f);
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
        form_.set_slider(kWarnCpu, static_cast<float>(c.warning_cpu_c));
        form_.set_slider(kWarnCountdown, static_cast<float>(c.warning_countdown_s));
        form_.set_toggle(kLightbar, c.lightbar_enabled);
        form_.set_slider(kLightWarn, static_cast<float>(c.lightbar_warn_c));
        form_.set_slider(kLightHot, static_cast<float>(c.lightbar_hot_c));
        if (ui::FormRow *row = form_.row(kRuleSave); row != nullptr && !game_id_.empty())
            row->text = std::to_string(c.target_temp_c) + " °C · " + profile_word(c.profile);
        night_rows(c.night_enabled);
        light_rows(c.lightbar_enabled);
    }

    void light_rows(bool on)
    {
        form_.set_disabled(kLightWarn, !on);
        form_.set_disabled(kLightHot, !on);
    }

    void night_rows(bool on)
    {
        form_.set_disabled(kNightStart, !on);
        form_.set_disabled(kNightEnd, !on);
        form_.set_disabled(kNightTarget, !on);
    }

    void changed(int id)
    {
        const auto number = [&](int row)
        { return std::to_string(static_cast<int>(form_.slider_value(row) + 0.5f)); };
        switch (id)
        {
        case kMode:
            pending_["mode"] = form_.choice_index(kMode) == 1 ? "\"observe\"" : "\"automatic\"";
            break;
        case kProfile:
            pending_["profile"] = std::string("\"") +
                                  kProfiles[std::clamp(form_.choice_index(kProfile), 0, 2)] + "\"";
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
        case kWarnCpu:
            pending_["warning_cpu_c"] = number(kWarnCpu);
            break;
        case kWarnCountdown:
            pending_["warning_countdown_s"] = number(kWarnCountdown);
            break;
        case kLightbar:
            pending_["lightbar_enabled"] = form_.toggle_value(kLightbar) ? "1" : "0";
            light_rows(form_.toggle_value(kLightbar));
            break;
        case kLightWarn:
            // red stays above yellow, as the app keeps it (config.c): it follows up
            pending_["lightbar_warn_c"] = number(kLightWarn);
            if (form_.slider_value(kLightHot) <= form_.slider_value(kLightWarn))
            {
                form_.set_slider(kLightHot, form_.slider_value(kLightWarn) + 1.0f);
                pending_["lightbar_hot_c"] = number(kLightHot);
            }
            break;
        case kLightHot:
            pending_["lightbar_hot_c"] = number(kLightHot);
            if (form_.slider_value(kLightWarn) >= form_.slider_value(kLightHot))
            {
                form_.set_slider(kLightWarn, form_.slider_value(kLightHot) - 1.0f);
                pending_["lightbar_warn_c"] = number(kLightWarn);
            }
            break;
        default:
            return;
        }
        save_in_ = kSaveDelay;
    }

    // One PUT with every field changed since the last one (the app takes a partial object, as from
    // the web page).
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
        std::snprintf(text, sizeof(text), "%d von %d s", t.elapsed_s,
                      t.limit_s > 0 ? t.limit_s : 30);
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
    Config config_;       // the last one from the app: what a game profile keeps
    std::string rules_key_;
    std::string game_id_, game_name_;
    std::vector<GameRule> rules_;
    std::string active_rule_;
    bool rules_loaded_ = false;
    std::string test_key_;
    SelfTest test_;
};

} // namespace

std::unique_ptr<Page> make_settings_page()
{
    return std::make_unique<SettingsPage>();
}

} // namespace hui::ps5cc
