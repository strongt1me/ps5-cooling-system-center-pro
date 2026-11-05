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

TEST(Ps5ccFormat, RateIsInMegabytesPerSecond)
{
    EXPECT_EQ(hui::ps5cc::format_rate(82400000), "82 MB/s");
    EXPECT_EQ(hui::ps5cc::format_rate(6400000), "6,4 MB/s");
    EXPECT_EQ(hui::ps5cc::format_rate(340000), "340 KB/s");
    EXPECT_EQ(hui::ps5cc::format_rate(10), "1 KB/s");
}

TEST(Ps5ccFormat, EtaIsSecondsMinutesHours)
{
    EXPECT_EQ(hui::ps5cc::format_eta(0), "");
    EXPECT_EQ(hui::ps5cc::format_eta(-5), "");
    EXPECT_EQ(hui::ps5cc::format_eta(40), "40 Sek.");
    EXPECT_EQ(hui::ps5cc::format_eta(60), "1 Min.");
    EXPECT_EQ(hui::ps5cc::format_eta(12 * 60 + 30), "12 Min.");
    EXPECT_EQ(hui::ps5cc::format_eta(3600 + 5 * 60), "1 Std. 05 Min.");
    EXPECT_EQ(hui::ps5cc::format_eta(3 * 3600 + 42 * 60), "3 Std. 42 Min.");
}

TEST(Ps5ccFormat, JobTitleCountsTheEntries)
{
    EXPECT_EQ(hui::ps5cc::format_job_title("copy", 0, 2), "Kopiere 1 von 2 Dateien/Ordnern");
    EXPECT_EQ(hui::ps5cc::format_job_title("move", 1, 3), "Verschiebe 2 von 3 Dateien/Ordnern");
    EXPECT_EQ(hui::ps5cc::format_job_title("delete", 5, 5), "Lösche 5 von 5 Dateien/Ordnern"); // never past the last
    EXPECT_EQ(hui::ps5cc::format_job_title("copy", 0, 1), "Kopiere 1 Datei/Ordner");
    EXPECT_EQ(hui::ps5cc::format_job_title("copy", 0, 0), "Kopieren");
}

TEST(Ps5ccFormat, JobRouteNamesSourceAndTarget)
{
    EXPECT_EQ(hui::ps5cc::format_job_route("copy", "/mnt/usb0/neu", "/mnt/ext1"),
              "von /mnt/usb0/neu nach /mnt/ext1");
    EXPECT_EQ(hui::ps5cc::format_job_route("move", "", "/mnt/ext1"), "Ziel: /mnt/ext1");
    EXPECT_EQ(hui::ps5cc::format_job_route("delete", "/data/neu", ""), "Ordner: /data/neu");
    EXPECT_EQ(hui::ps5cc::format_job_route("copy", "/a", ""), "");
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
