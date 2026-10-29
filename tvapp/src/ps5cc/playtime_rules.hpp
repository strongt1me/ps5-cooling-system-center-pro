// ps5cc - Spielzeit's numbers: totals, the last fourteen days, the most played games, dates in words.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's tab (web/app.js, computed in the client): a session's time is shared out over the days it touches,
// in proportion to its wall-clock time on each, so a game played across midnight counts on both. Days are local days,
// tz_min minutes east of UTC. No OpenGL here: the unit tests use it.

#pragma once

#include "ps5cc/data.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::ps5cc
{

struct PlayTotals
{
    int today_s = 0, week_s = 0, month_s = 0, all_s = 0;
    int today_n = 0, week_n = 0, month_n = 0, all_n = 0; // sessions that touch the span
};

struct PlayRank
{
    std::string id;
    std::string name;
    int seconds = 0;
    int sessions = 0;
    std::int64_t last = 0; // end of the latest session
    int temp_max = -1;     // the highest CPU or SoC temperature seen
};

// The sessions, with the running one, as one list (the running one last, ending now).
std::vector<PlaySession> play_all(const Playtime &p);
// Today, the last 7 and 30 days (today included) and everything.
PlayTotals play_totals(const Playtime &p, int tz_min);
// Seconds per local day: [0] is thirteen days ago, [13] today.
std::array<int, 14> play_days(const Playtime &p, int tz_min);
// Games by time played, most first.
std::vector<PlayRank> play_ranking(const Playtime &p);

// "—", "45 Sek.", "35 Min.", "2 Std. 05 Min."
std::string play_duration(int seconds);
// "18 h 06 min" for the big numbers
std::string play_hours(int seconds);
// The local day of a time (days since 1970).
std::int64_t play_day(std::int64_t t, int tz_min);
// "03.10.2026"
std::string play_date(std::int64_t t, int tz_min);
// "Di. 03.10." for a day number
std::string play_day_label(std::int64_t day);
// "Di., 03.10., 14:05–15:20 Uhr", the date again when it ends on another day
std::string play_range(std::int64_t start, std::int64_t end, int tz_min);
// "14:05"
std::string play_clock(std::int64_t t, int tz_min);

} // namespace hui::ps5cc
