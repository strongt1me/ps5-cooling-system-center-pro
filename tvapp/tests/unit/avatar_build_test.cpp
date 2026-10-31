// ps5cc - Tests for the profile picture files: square, DDS header and size, PNG framing.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/avatar_build.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

using hui::ps5cc::avatar_dds;
using hui::ps5cc::avatar_online_json;
using hui::ps5cc::avatar_png;
using hui::ps5cc::avatar_square;

std::vector<std::uint8_t> solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t i = 0; i < px.size(); i += 4)
    {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

std::uint32_t le32(const std::vector<std::uint8_t> &v, std::size_t at)
{
    return v[at] | v[at + 1] << 8 | v[at + 2] << 16 | static_cast<std::uint32_t>(v[at + 3]) << 24;
}

TEST(AvatarBuild, SquareKeepsTheCentreAndTheColour)
{
    // a wide picture: red left and right, blue in the middle square
    std::vector<std::uint8_t> px = solid(300, 100, 255, 0, 0);
    for (int y = 0; y < 100; ++y)
        for (int x = 100; x < 200; ++x)
        {
            std::uint8_t *p = &px[(static_cast<std::size_t>(y) * 300 + x) * 4];
            p[0] = 0;
            p[2] = 255;
        }
    const std::vector<std::uint8_t> sq = avatar_square(px.data(), 300, 100, 64);
    ASSERT_EQ(sq.size(), 64u * 64u * 4u);
    EXPECT_EQ(sq[0], 0);
    EXPECT_EQ(sq[2], 255);
    EXPECT_EQ(sq[(63 * 64 + 63) * 4 + 2], 255);
    EXPECT_TRUE(avatar_square(nullptr, 10, 10, 64).empty());
}

TEST(AvatarBuild, DdsHasTheHeaderTheConsoleChecks)
{
    const std::vector<std::uint8_t> sq = solid(64, 64, 10, 200, 30);
    const std::vector<std::uint8_t> dds = avatar_dds(sq, 64);
    ASSERT_EQ(dds.size(), 128u + 16u * 16u * 16u);
    EXPECT_EQ(dds[0], 'D');
    EXPECT_EQ(dds[3], ' ');
    EXPECT_EQ(le32(dds, 4), 124u);
    EXPECT_EQ(le32(dds, 12), 64u);
    EXPECT_EQ(le32(dds, 16), 64u);
    EXPECT_EQ(le32(dds, 20), 16u * 16u * 16u);
    EXPECT_EQ(dds[84], 'D');
    EXPECT_EQ(dds[87], '5');
    EXPECT_EQ(dds[109], 0x10);
    EXPECT_TRUE(avatar_dds(sq, 63).empty());
}

TEST(AvatarBuild, PngIsFramedAndStoresEveryByte)
{
    const std::vector<std::uint8_t> sq = solid(440, 440, 1, 2, 3);
    const std::vector<std::uint8_t> png = avatar_png(sq, 440);
    ASSERT_GT(png.size(), 440u * 440u * 4u);
    EXPECT_EQ(png[0], 0x89);
    EXPECT_EQ(png[1], 'P');
    EXPECT_EQ(png[12], 'I');
    EXPECT_EQ(png[15], 'R');
    EXPECT_EQ(png[png.size() - 8], 'I');
    EXPECT_EQ(png[png.size() - 5], 'D');
}

TEST(AvatarBuild, OnlineJsonCarriesTheName)
{
    const std::string json = avatar_online_json("Spieler \"1\"");
    EXPECT_NE(json.find("\"firstName\":\"Spieler \\\"1\\\"\""), std::string::npos);
    EXPECT_NE(json.find("isOfficiallyVerified"), std::string::npos);
}

} // namespace
