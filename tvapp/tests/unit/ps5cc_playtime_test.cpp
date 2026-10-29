// ps5cc - Tests for Spielzeit's numbers: days, totals, sessions across midnight, the ranking and the date texts.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/playtime_rules.hpp"

#include <gtest/gtest.h>

namespace
{

using hui::ps5cc::Playtime;
using hui::ps5cc::PlaySession;

// 29.10.2026 12:00:00 UTC
constexpr std::int64_t kNoon = 1793275200;

PlaySession session(const char *id, std::int64_t start, std::int64_t end, int play)
{
    PlaySession s;
    s.id = id;
    s.name = id;
    s.start = start;
    s.end = end;
    s.play = play;
    return s;
}

TEST(Ps5ccPlaytime, DatesAndClockFollowTheOffset)
{
    EXPECT_EQ(hui::ps5cc::play_date(kNoon, 0), "29.10.2026");
    EXPECT_EQ(hui::ps5cc::play_clock(kNoon, 60), "13:00");
    // 23:30 UTC is already the next day an hour east
    EXPECT_EQ(hui::ps5cc::play_date(kNoon + 11 * 3600 + 1800, 60), "30.10.2026");
    EXPECT_EQ(hui::ps5cc::play_day_label(hui::ps5cc::play_day(kNoon, 0)), "Do. 29.10.");
    EXPECT_EQ(hui::ps5cc::play_range(kNoon, kNoon + 3600, 0), "Do., 29.10., 12:00–13:00 Uhr");
    EXPECT_EQ(hui::ps5cc::play_range(kNoon + 11 * 3600, kNoon + 13 * 3600, 0), "Do., 29.10., 23:00 – 30.10., 01:00 Uhr");
}

TEST(Ps5ccPlaytime, DurationsInWords)
{
    EXPECT_EQ(hui::ps5cc::play_duration(0), "—");
    EXPECT_EQ(hui::ps5cc::play_duration(45), "45 Sek.");
    EXPECT_EQ(hui::ps5cc::play_duration(35 * 60), "35 Min.");
    EXPECT_EQ(hui::ps5cc::play_duration(2 * 3600 + 5 * 60), "2 Std. 05 Min.");
    EXPECT_EQ(hui::ps5cc::play_hours(18 * 3600 + 6 * 60), "18 h 06 min");
}

TEST(Ps5ccPlaytime, ASessionAcrossMidnightCountsOnBothDays)
{
    Playtime p;
    p.now = kNoon;
    // 28.10. 23:00 to 29.10. 01:00 UTC, two hours played
    p.sessions.push_back(session("CUSA00001", kNoon - 13 * 3600, kNoon - 11 * 3600, 7200));
    const auto days = hui::ps5cc::play_days(p, 0);
    EXPECT_EQ(days[12], 3600);
    EXPECT_EQ(days[13], 3600);
    const auto totals = hui::ps5cc::play_totals(p, 0);
    EXPECT_EQ(totals.today_s, 3600);
    EXPECT_EQ(totals.today_n, 1);
    EXPECT_EQ(totals.week_s, 7200);
    EXPECT_EQ(totals.all_s, 7200);
}

TEST(Ps5ccPlaytime, TheRunningGameCountsAndRanks)
{
    Playtime p;
    p.now = kNoon;
    p.sessions.push_back(session("A", kNoon - 40 * 86400, kNoon - 40 * 86400 + 600, 600));
    p.sessions.push_back(session("B", kNoon - 3 * 86400, kNoon - 3 * 86400 + 1800, 1800));
    p.live = true;
    p.current = session("A", kNoon - 3600, 0, 3000);
    p.current.cpu_max = 71;
    const auto totals = hui::ps5cc::play_totals(p, 0);
    EXPECT_EQ(totals.all_n, 3);
    EXPECT_EQ(totals.month_s, 4800);
    EXPECT_EQ(totals.today_s, 3000);
    const auto rank = hui::ps5cc::play_ranking(p);
    ASSERT_EQ(rank.size(), 2u);
    EXPECT_EQ(rank[0].id, "A");
    EXPECT_EQ(rank[0].seconds, 3600);
    EXPECT_EQ(rank[0].sessions, 2);
    EXPECT_EQ(rank[0].temp_max, 71);
    EXPECT_EQ(rank[1].id, "B");
}

} // namespace
