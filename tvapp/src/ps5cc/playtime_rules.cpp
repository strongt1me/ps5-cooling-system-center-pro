// ps5cc - Spielzeit's numbers (see playtime_rules.hpp).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/playtime_rules.hpp"

#include <algorithm>
#include <cstdio>

namespace hui::ps5cc
{

namespace
{

constexpr std::int64_t kDay = 86400;

std::int64_t floor_div(std::int64_t a, std::int64_t b)
{
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0);
}

// Days since 1970-01-01 to year, month, day (H. Hinnant's civil_from_days).
void civil(std::int64_t z, int &y, int &m, int &d)
{
    z += 719468;
    const std::int64_t era = floor_div(z, 146097);
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

const char *weekday(std::int64_t day)
{
    static const char *const kNames[] = {"Do.", "Fr.", "Sa.", "So.", "Mo.", "Di.", "Mi."}; // 1970-01-01 was a Thursday
    return kNames[static_cast<int>(((day % 7) + 7) % 7)];
}

// The share of a session's time between from and to (wall clock), in proportion to its overlap.
int share(const PlaySession &s, std::int64_t from, std::int64_t to)
{
    const std::int64_t span = s.end - s.start;
    if (span <= 0)
        return s.start >= from && s.start < to ? s.play : 0;
    const std::int64_t a = std::max(s.start, from), b = std::min(s.end, to);
    if (b <= a)
        return 0;
    return static_cast<int>(static_cast<double>(s.play) * static_cast<double>(b - a) / static_cast<double>(span) + 0.5);
}

bool touches(const PlaySession &s, std::int64_t from, std::int64_t to)
{
    return s.start < to && std::max(s.end, s.start + 1) > from;
}

} // namespace

std::vector<PlaySession> play_all(const Playtime &p)
{
    std::vector<PlaySession> all = p.sessions;
    if (p.live)
    {
        PlaySession s = p.current;
        s.end = std::max(p.now, s.start);
        all.push_back(s);
    }
    return all;
}

std::int64_t play_day(std::int64_t t, int tz_min)
{
    return floor_div(t + static_cast<std::int64_t>(tz_min) * 60, kDay);
}

PlayTotals play_totals(const Playtime &p, int tz_min)
{
    PlayTotals out;
    const std::int64_t off = static_cast<std::int64_t>(tz_min) * 60;
    const std::int64_t today = play_day(p.now, tz_min);
    const std::int64_t end = (today + 1) * kDay - off;
    const std::int64_t from[3] = {today * kDay - off, (today - 6) * kDay - off, (today - 29) * kDay - off};
    for (const PlaySession &s : play_all(p))
    {
        out.all_s += s.play;
        ++out.all_n;
        int *secs[3] = {&out.today_s, &out.week_s, &out.month_s};
        int *count[3] = {&out.today_n, &out.week_n, &out.month_n};
        for (int i = 0; i < 3; ++i)
        {
            if (!touches(s, from[i], end))
                continue;
            *secs[i] += share(s, from[i], end);
            ++*count[i];
        }
    }
    return out;
}

std::array<int, 14> play_days(const Playtime &p, int tz_min)
{
    std::array<int, 14> out{};
    const std::int64_t off = static_cast<std::int64_t>(tz_min) * 60;
    const std::int64_t today = play_day(p.now, tz_min);
    for (const PlaySession &s : play_all(p))
    {
        for (int i = 0; i < 14; ++i)
        {
            const std::int64_t day = today - 13 + i;
            const std::int64_t from = day * kDay - off;
            if (touches(s, from, from + kDay))
                out[static_cast<std::size_t>(i)] += share(s, from, from + kDay);
        }
    }
    return out;
}

std::vector<PlayRank> play_ranking(const Playtime &p)
{
    std::vector<PlayRank> out;
    for (const PlaySession &s : play_all(p))
    {
        auto it = std::find_if(out.begin(), out.end(), [&](const PlayRank &r) { return r.id == s.id; });
        if (it == out.end())
        {
            PlayRank r;
            r.id = s.id;
            r.name = s.name.empty() ? s.id : s.name;
            out.push_back(r);
            it = out.end() - 1;
        }
        it->seconds += s.play;
        ++it->sessions;
        it->last = std::max(it->last, s.end);
        it->temp_max = std::max({it->temp_max, s.cpu_max, s.soc_max});
    }
    std::stable_sort(out.begin(), out.end(), [](const PlayRank &a, const PlayRank &b) { return a.seconds > b.seconds; });
    return out;
}

std::string play_duration(int seconds)
{
    char text[32];
    if (seconds <= 0)
        return "—";
    if (seconds < 60)
        std::snprintf(text, sizeof(text), "%d Sek.", seconds);
    else if (seconds < 3600)
        std::snprintf(text, sizeof(text), "%d Min.", seconds / 60);
    else
        std::snprintf(text, sizeof(text), "%d Std. %02d Min.", seconds / 3600, seconds / 60 % 60);
    return text;
}

std::string play_hours(int seconds)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%d h %02d min", seconds / 3600, seconds / 60 % 60);
    return text;
}

std::string play_date(std::int64_t t, int tz_min)
{
    int y = 0, m = 0, d = 0;
    civil(play_day(t, tz_min), y, m, d);
    char text[16];
    std::snprintf(text, sizeof(text), "%02d.%02d.%04d", d, m, y);
    return text;
}

std::string play_day_label(std::int64_t day)
{
    int y = 0, m = 0, d = 0;
    civil(day, y, m, d);
    char text[16];
    std::snprintf(text, sizeof(text), "%s %02d.%02d.", weekday(day), d, m);
    return text;
}

std::string play_clock(std::int64_t t, int tz_min)
{
    const std::int64_t s = t + static_cast<std::int64_t>(tz_min) * 60;
    const std::int64_t in_day = s - floor_div(s, kDay) * kDay;
    char text[8];
    std::snprintf(text, sizeof(text), "%02d:%02d", static_cast<int>(in_day / 3600), static_cast<int>(in_day / 60 % 60));
    return text;
}

std::string play_range(std::int64_t start, std::int64_t end, int tz_min)
{
    const std::int64_t a = play_day(start, tz_min), b = play_day(end, tz_min);
    int y = 0, m = 0, d = 0;
    civil(a, y, m, d);
    char text[64];
    if (a == b)
        std::snprintf(text, sizeof(text), "%s, %02d.%02d., %s–%s Uhr", weekday(a), d, m, play_clock(start, tz_min).c_str(),
                      play_clock(end, tz_min).c_str());
    else
    {
        int y2 = 0, m2 = 0, d2 = 0;
        civil(b, y2, m2, d2);
        std::snprintf(text, sizeof(text), "%s, %02d.%02d., %s – %02d.%02d., %s Uhr", weekday(a), d, m,
                      play_clock(start, tz_min).c_str(), d2, m2, play_clock(end, tz_min).c_str());
    }
    return text;
}

} // namespace hui::ps5cc
