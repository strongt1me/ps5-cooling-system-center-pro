// ps5cc - Tests for the cover service: shrinking, the fetch order and failures.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/covers.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{

using hui::ps5cc::Covers;
using hui::ps5cc::Game;
using hui::ps5cc::kCoverSize;

std::vector<Game> two_games()
{
    std::vector<Game> games(2);
    games[0].id = "PPSA00001";
    games[0].cover = "/api/v1/library/cover?id=PPSA00001";
    games[1].id = "PPSA00002";
    games[1].cover = "/api/v1/library/cover?id=PPSA00002";
    return games;
}

std::vector<std::uint8_t> blank_cover()
{
    return std::vector<std::uint8_t>(static_cast<std::size_t>(kCoverSize) * kCoverSize * 4u, 200);
}

TEST(Covers, SquareCropsTheCentreAndAverages)
{
    // 4 x 2: columns red, green, blue, white; the centre square is columns 1 and 2.
    const std::vector<std::uint8_t> rgba = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };
    const std::vector<std::uint8_t> out = hui::ps5cc::square_rgba(rgba.data(), 4, 2, 1);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[1], 128);
    EXPECT_EQ(out[2], 128);
    EXPECT_EQ(out[3], 255);
}

TEST(Covers, ShrinkingKeepsAFlatColour)
{
    const int side = 512;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(side) * side * 4u);
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        rgba[i] = 10;
        rgba[i + 1] = 120;
        rgba[i + 2] = 230;
        rgba[i + 3] = 255;
    }
    const std::vector<std::uint8_t> out = hui::ps5cc::square_rgba(rgba.data(), side, side, kCoverSize);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kCoverSize) * kCoverSize * 4u);
    EXPECT_EQ(out[0], 10);
    EXPECT_EQ(out[out.size() - 3], 120);
    EXPECT_EQ(out[out.size() - 2], 230);
}

TEST(Covers, GarbageIsNotACover)
{
    const std::uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_TRUE(hui::ps5cc::decode_cover(junk, sizeof(junk)).empty());
    EXPECT_TRUE(hui::ps5cc::decode_cover(nullptr, 0).empty());
}

TEST(Covers, EachCoverIsAskedForOnce)
{
    Covers covers;
    const std::vector<Game> games = two_games();
    EXPECT_EQ(covers.next_wanted(games).id, "PPSA00001");
    EXPECT_EQ(covers.next_wanted(games).id, "PPSA00002");
    EXPECT_TRUE(covers.next_wanted(games).path.empty());
}

TEST(Covers, TheFocusedGameComesFirst)
{
    Covers covers;
    covers.prefer("PPSA00002");
    EXPECT_EQ(covers.next_wanted(two_games()).id, "PPSA00002");
}

TEST(Covers, AFailureIsNotRetriedUntilTheLibraryReloads)
{
    Covers covers;
    std::vector<Game> games = two_games();
    games.resize(1);
    ASSERT_EQ(covers.next_wanted(games).id, "PPSA00001");
    covers.fail("PPSA00001");
    EXPECT_TRUE(covers.failed("PPSA00001"));
    EXPECT_TRUE(covers.next_wanted(games).path.empty());
    covers.forget_failures();
    EXPECT_EQ(covers.next_wanted(games).id, "PPSA00001");
}

TEST(Covers, AWrongSizeCountsAsAFailure)
{
    Covers covers;
    covers.deliver("PPSA00001", std::vector<std::uint8_t>(16, 0));
    EXPECT_TRUE(covers.failed("PPSA00001"));
}

TEST(Covers, UploadIsLimitedPerFrameAndFadesIn)
{
    Covers covers;
    covers.deliver("PPSA00001", blank_cover());
    covers.deliver("PPSA00002", blank_cover());
    std::uint32_t next = 10;
    const auto create = [&](int w, int h, const std::uint8_t *rgba)
    {
        EXPECT_EQ(w, kCoverSize);
        EXPECT_EQ(h, kCoverSize);
        EXPECT_NE(rgba, nullptr);
        return next++;
    };
    EXPECT_EQ(covers.upload_pending(create, 1, 0.0f), 1);
    EXPECT_EQ(covers.texture_of("PPSA00001").texture, 10u);
    EXPECT_EQ(covers.texture_of("PPSA00002").texture, 0u);
    EXPECT_FLOAT_EQ(covers.texture_of("PPSA00001").alpha, 0.0f);
    EXPECT_EQ(covers.upload_pending(create, 1, 1.0f), 1);
    EXPECT_FLOAT_EQ(covers.texture_of("PPSA00001").alpha, 1.0f);
    EXPECT_EQ(covers.texture_of("PPSA00002").texture, 11u);
    EXPECT_EQ(covers.loaded(), 2u);
}

TEST(Covers, ALoadedCoverIsNotFetchedAgain)
{
    Covers covers;
    std::vector<Game> games = two_games();
    games.resize(1);
    ASSERT_EQ(covers.next_wanted(games).id, "PPSA00001");
    covers.deliver("PPSA00001", blank_cover());
    covers.upload_pending([](int, int, const std::uint8_t *) { return 5u; }, 2, 0.0f);
    EXPECT_TRUE(covers.next_wanted(games).path.empty());
    EXPECT_EQ(covers.pending(), 0u);
}

} // namespace
