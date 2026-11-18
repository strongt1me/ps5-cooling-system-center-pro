// ps5cc - The Lüfterkurve page (16.11.2026): what the automatic mode asks of the fan at each
// temperature, as a picture, with the temperature of now; and the recording of the fan regulation,
// saved from here. Copyright (C) 2026 strongt1me SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/look.hpp"
#include "ps5cc/pages.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace hui::ps5cc
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr float kChartTop = kTop + 118.0f;

// A German decimal comma: "68,9".
std::string comma(float v)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(v));
    for (char &c : text)
        c = c == '.' ? ',' : c;
    return text;
}

const char *profile_name(const std::string &p)
{
    if (p == "cool")
        return "Kühl";
    if (p == "balanced")
        return "Ausgewogen";
    return "Leise";
}

class FanCurvePage final : public Page
{
  public:
    void init(const ui::Theme &theme) override
    {
        theme_ = theme;
        chart_.style.theme = theme;
        chart_.style.legend = ui::LegendPlacement::none;
        chart_.style.readout = true;
        chart_.style.axis.ticks = 4;
        chart_.style.axis.from_zero = true;
        chart_.style.axis.include_low = 0.0f;
        chart_.style.axis.include_high = 100.0f;
        chart_.style.axis.unit = " %";
        chart_.style.point_size = 5.0f;
        chart_.title = "Was die Automatik vom Lüfter verlangt";
        chart_.set_active(true);
        chart_.set_bounds({kLeftX, kChartTop, 1824.0f - kLeftX, kBottom - kChartTop - 70.0f});
    }

    void absorb(const Snapshot &data) override
    {
        curve_ = data.fan_curve;
        // Where nothing is asked for there is no curve (the console runs by itself): the picture
        // begins at the first temperature that has a figure, as in the browser, and does not start
        // with a flat stretch and a jump.
        std::size_t lead = 0;
        while (lead + 1 < curve_.wish.size() && curve_.wish[lead] <= 0)
            ++lead;
        if (lead > 0 && lead < curve_.wish.size())
        {
            curve_.temps.erase(curve_.temps.begin(),
                               curve_.temps.begin() + static_cast<std::ptrdiff_t>(lead));
            curve_.wish.erase(curve_.wish.begin(),
                              curve_.wish.begin() + static_cast<std::ptrdiff_t>(lead));
        }
        std::string key = std::to_string(curve_.temps.size()) + ":" + curve_.profile + ":" +
                          std::to_string(curve_.target_c) + ":" + std::to_string(curve_.cap_pct);
        for (int w : curve_.wish)
            key += "," + std::to_string(w);
        if (key == shown_key_)
            return; // the curve is the same: the chart must not start its drawing again
        shown_key_ = key;
        std::vector<std::string> labels;
        ui::ChartSeries line{"Wunsch %", {}, kBlue};
        for (std::size_t i = 0; i < curve_.temps.size(); ++i)
        {
            labels.push_back(std::to_string(curve_.temps[i]) + " °C");
            line.values.push_back(static_cast<float>(curve_.wish[i]));
        }
        chart_.set_labels(labels);
        chart_.set_series({line}, !started_);
        if (!started_ && !curve_.temps.empty())
            chart_.set_focus(index_of_now());
        started_ = !curve_.temps.empty();
    }

    void enter() override
    {
        chart_.enter();
        if (!curve_.temps.empty())
            chart_.set_focus(index_of_now());
    }

    void handle(const InputFrame &input, ui::Feedback &feedback) override
    {
        // X: the recording of the regulation into a file on the console
        if (input.is_pressed(Action::confirm))
        {
            feedback.play(audio::Cue::select);
            source().fan_trace_save();
            return;
        }
        chart_.handle(input, feedback);
    }

    void update(float dt) override
    {
        chart_.update(dt);
    }

    void draw(ui::Canvas &canvas) const override
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        ui::Painter paint(list, fonts, theme_);
        const float left = kLeftX + 8.0f;
        const float width = 1824.0f - kLeftX - 16.0f;
        if (!curve_.loaded)
        {
            ui::text(list, fonts.semibold, "Wird geladen …", left, kTop + 50.0f, 26.0f, kMuted);
            return;
        }
        // the figures of now
        std::string now = curve_.now_c >= 0.0f ? comma(curve_.now_c) + " °C" : "-- °C";
        ui::text(list, fonts.display, now, left, kTop + 44.0f, 40.0f, kInk);
        std::string right;
        if (curve_.fan_pct >= 0)
            right = "Lüfter " + std::to_string(curve_.fan_pct) + " %";
        const int at = wish_at_now();
        if (curve_.automatic && at >= 0)
            right += std::string(right.empty() ? "" : " · ") + "Kurve " + std::to_string(at) + " %";
        ui::text(list, fonts.semibold, right, left + width, kTop + 44.0f, 28.0f, kInk,
                 gfx::Align::right);
        std::string second = std::string("Betriebsart ") + profile_name(curve_.profile) +
                             " · Ziel " + std::to_string(curve_.target_c) + " °C · Notfallgrenze " +
                             std::to_string(curve_.safety_c) + " °C";
        if (curve_.cap_pct < 100)
            second += " · Obergrenze " + std::to_string(curve_.cap_pct) + " %";
        ui::text(list, fonts.regular, ui::fit_label(paint, second, 21.0f, width), left,
                 kTop + 84.0f, 21.0f, kMuted);
        chart_.draw(canvas);
        std::string note =
            curve_.automatic
                ? "Sanfter Anstieg schon 10 °C unter dem Ziel, darüber bis zur vollen Drehzahl. "
                  "Links und rechts wählt die Temperatur."
                : "Automatik aus: Die Konsole regelt selbst, die Kurve gilt nicht.";
        ui::text(list, fonts.regular, ui::fit_label(paint, note, 19.0f, width), left,
                 kBottom - 24.0f, 19.0f, curve_.automatic ? kMuted : kWarning);
    }

    int hints(ui::Hint *out) const override
    {
        out[0] = {ui::Button::cross, "Regelaufzeichnung speichern"};
        return 1;
    }

  private:
    // the step of the temperature nearest to the one of now (the middle of the picture without one)
    int index_of_now() const
    {
        if (curve_.temps.empty())
            return 0;
        if (curve_.now_c < 0.0f)
            return static_cast<int>(curve_.temps.size()) / 2;
        int best = 0;
        float gap = 1e9f;
        for (std::size_t i = 0; i < curve_.temps.size(); ++i)
        {
            const float d = std::fabs(static_cast<float>(curve_.temps[i]) - curve_.now_c);
            if (d < gap)
            {
                gap = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    // what the curve asks for at the temperature of now, in percent (-1 off the curve)
    int wish_at_now() const
    {
        if (curve_.now_c < 0.0f || curve_.temps.size() < 2 ||
            curve_.now_c < static_cast<float>(curve_.temps.front()) ||
            curve_.now_c > static_cast<float>(curve_.temps.back()))
            return -1;
        for (std::size_t i = 1; i < curve_.temps.size(); ++i)
        {
            if (curve_.now_c <= static_cast<float>(curve_.temps[i]))
            {
                const float t0 = static_cast<float>(curve_.temps[i - 1]);
                const float t1 = static_cast<float>(curve_.temps[i]);
                const float f = (curve_.now_c - t0) / std::max(t1 - t0, 1.0f);
                return static_cast<int>(
                    std::lround(static_cast<float>(curve_.wish[i - 1]) +
                                f * static_cast<float>(curve_.wish[i] - curve_.wish[i - 1])));
            }
        }
        return -1;
    }

    ui::Theme theme_;
    ui::LineChart chart_;
    FanCurve curve_;
    std::string shown_key_;
    bool started_ = false;
};

} // namespace

std::unique_ptr<Page> make_fancurve_page()
{
    return std::make_unique<FanCurvePage>();
}

} // namespace hui::ps5cc
