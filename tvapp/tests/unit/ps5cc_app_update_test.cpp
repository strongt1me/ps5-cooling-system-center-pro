// ps5cc - Tests for the tile's decision to replace an older running app.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/app_update.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

using hui::ps5cc::compare_versions;
using hui::ps5cc::embedded_version;
using hui::ps5cc::job_active;
using hui::ps5cc::should_replace;

TEST(Ps5ccAppUpdate, VersionsCompareNumerically)
{
    EXPECT_EQ(compare_versions("1.77.0", "1.77.0"), 0);
    EXPECT_EQ(compare_versions("1.76.9", "1.77.0"), -1);
    EXPECT_EQ(compare_versions("1.77.1", "1.77.0"), 1);
    EXPECT_EQ(compare_versions("1.9.0", "1.10.0"), -1); // not as text
    EXPECT_EQ(compare_versions("2.0.0", "1.99.99"), 1);
    EXPECT_EQ(compare_versions("1.77", "1.77.0"), 0); // a missing part is 0
}

TEST(Ps5ccAppUpdate, NonVersionsAreNeverCompared)
{
    EXPECT_EQ(compare_versions("", "1.77.0"), 2);
    EXPECT_EQ(compare_versions("1.77.0", ""), 2);
    EXPECT_EQ(compare_versions("1.77.0-beta", "1.77.0"), 2);
    EXPECT_EQ(compare_versions("1..0", "1.77.0"), 2);
    EXPECT_EQ(compare_versions("abc", "1.77.0"), 2);
    EXPECT_EQ(compare_versions("1.2.3.4.5", "1.2.3"), 2);
}

TEST(Ps5ccAppUpdate, OnlyAnOlderAppIsReplaced)
{
    EXPECT_TRUE(should_replace("1.76.9", "1.77.0"));
    EXPECT_FALSE(should_replace("1.77.0", "1.77.0"));
    EXPECT_FALSE(should_replace("1.78.0", "1.77.0")); // a newer app stays (someone sent it by hand)
    EXPECT_FALSE(should_replace("", "1.77.0"));       // the app did not name its version: leave it
    EXPECT_FALSE(should_replace("1.76.9", ""));       // the ELF carries no mark: leave it
}

TEST(Ps5ccAppUpdate, TheVersionIsReadFromTheMark)
{
    std::vector<unsigned char> elf(5000, 0x90);
    const char mark[] = "PS5CC-VERSION:1.77.1:";
    std::memcpy(elf.data() + 1234, mark, sizeof(mark) - 1);
    EXPECT_EQ(embedded_version(elf.data(), elf.size()), "1.77.1");
    EXPECT_EQ(embedded_version(elf.data(), 1234 + 10), ""); // cut inside the mark
    std::vector<unsigned char> none(5000, 0x41);
    EXPECT_EQ(embedded_version(none.data(), none.size()), "");
    EXPECT_EQ(embedded_version(nullptr, 0), "");
    // the first mark with a version in it counts (a lookalike text before it does not)
    std::vector<unsigned char> two(200, 0);
    const char bad[] = "PS5CC-VERSION:x:";
    const char good[] = "PS5CC-VERSION:2.0.1:";
    std::memcpy(two.data() + 10, bad, sizeof(bad) - 1);
    std::memcpy(two.data() + 80, good, sizeof(good) - 1);
    EXPECT_EQ(embedded_version(two.data(), two.size()), "2.0.1");
}

TEST(Ps5ccAppUpdate, AJobIsSeenInTheAnswer)
{
    EXPECT_TRUE(job_active(R"({"ok":true,"state":"copying","active":true,"kind":"copy"})"));
    EXPECT_FALSE(job_active(R"({"ok":true,"state":"idle","active":false,"kind":""})"));
    EXPECT_FALSE(job_active(""));
}

} // namespace
