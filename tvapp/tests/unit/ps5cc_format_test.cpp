// ps5cc - Tests for the clock, date and kernel-log helpers of the console app's data.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/data.hpp"

#include <gtest/gtest.h>

namespace
{

using hui::ps5cc::KlogLine;

TEST(Ps5ccFormat, ClockWrapsAtADay)
{
    EXPECT_EQ(hui::ps5cc::format_clock(0), "00:00");
    EXPECT_EQ(hui::ps5cc::format_clock(1380), "23:00");
    EXPECT_EQ(hui::ps5cc::format_clock(435), "07:15");
    EXPECT_EQ(hui::ps5cc::format_clock(1440 + 60), "01:00");
    EXPECT_EQ(hui::ps5cc::format_clock(-15), "23:45");
}

TEST(Ps5ccFormat, TimeOfDayFollowsTheOffset)
{
    // 27.10.2026 10:29:26 UTC
    const std::int64_t ms = 1793096966000;
    EXPECT_EQ(hui::ps5cc::format_time_of_day(ms, 0), "10:29:26");
    EXPECT_EQ(hui::ps5cc::format_time_of_day(ms, 60), "11:29:26");
    EXPECT_EQ(hui::ps5cc::format_time_of_day(ms, -660), "23:29:26");
}

TEST(Ps5ccFormat, DateIsCivil)
{
    EXPECT_EQ(hui::ps5cc::format_date(0), "01.01.1970");
    EXPECT_EQ(hui::ps5cc::format_date(1793096966), "27.10.2026");
    EXPECT_EQ(hui::ps5cc::format_date(951782400), "29.02.2000");
}

TEST(Ps5ccKlog, AppendsOnlyNewerLinesAndKeepsTheNewest)
{
    std::vector<KlogLine> ring;
    EXPECT_TRUE(hui::ps5cc::klog_append(ring, {{1, 10, "a"}, {2, 11, "b"}}, 3));
    // An overlapping answer adds only what is new.
    EXPECT_TRUE(hui::ps5cc::klog_append(ring, {{2, 11, "b"}, {3, 12, "c"}, {4, 13, "d"}}, 3));
    ASSERT_EQ(ring.size(), 3u);
    EXPECT_EQ(ring.front().n, 2);
    EXPECT_EQ(ring.back().text, "d");
    EXPECT_FALSE(hui::ps5cc::klog_append(ring, {{3, 12, "c"}}, 3));
    EXPECT_FALSE(hui::ps5cc::klog_append(ring, {}, 3));
}

TEST(Ps5ccLibrary, OwnTilesAreNoGames)
{
    EXPECT_TRUE(hui::ps5cc::is_own_tile("FAKE86921"));
    EXPECT_TRUE(hui::ps5cc::is_own_tile("FAKE86922"));
    EXPECT_TRUE(hui::ps5cc::is_own_tile("PPSA86921"));
    EXPECT_TRUE(hui::ps5cc::is_own_tile("PPSA86922"));
    EXPECT_FALSE(hui::ps5cc::is_own_tile("PPSA01325"));
    EXPECT_FALSE(hui::ps5cc::is_own_tile(""));
}

} // namespace
