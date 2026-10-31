// ps5cc - Tests for the area pictures: scaling, the modes, and swapping in the user's picture.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/backdrops.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{

using hui::ps5cc::BackdropMode;
using hui::ps5cc::Backdrops;
using hui::ps5cc::BackdropSetting;
using hui::ps5cc::kBackdropHeight;
using hui::ps5cc::kBackdropWidth;

std::vector<std::uint8_t> picture(std::uint8_t value)
{
    return std::vector<std::uint8_t>(
        static_cast<std::size_t>(kBackdropWidth) * kBackdropHeight * 4u, value);
}

std::vector<BackdropSetting> games_area(BackdropMode mode, bool custom, const std::string &stamp)
{
    BackdropSetting b;
    b.id = "spiele";
    b.label = "Spiele";
    b.mode = mode;
    b.custom = custom;
    b.stamp = stamp;
    return {b};
}

// Hands out texture names 1, 2, ... and remembers what was reused.
struct FakeGl
{
    std::uint32_t next = 1;
    std::vector<std::uint32_t> reused;
    Backdrops::Create create()
    {
        return [this](std::uint32_t reuse, int, int, const std::uint8_t *)
        {
            if (reuse != 0)
            {
                reused.push_back(reuse);
                return reuse;
            }
            return next++;
        };
    }
};

TEST(Backdrops, NamesAndAreasMatch)
{
    for (int i = 0; i < hui::ps5cc::kBackdropAreas; ++i)
        EXPECT_EQ(hui::ps5cc::backdrop_area(hui::ps5cc::backdrop_name(i)), i);
    EXPECT_EQ(hui::ps5cc::backdrop_area("kuehlung"), 0);
    EXPECT_EQ(hui::ps5cc::backdrop_area("../spiele"), -1);
    EXPECT_STREQ(hui::ps5cc::backdrop_name(7), "");
}

TEST(Backdrops, CoverKeepsTheCentreOfAWidePicture)
{
    // 4 x 1 to 2 x 1: the middle two columns (green, blue) survive.
    const std::vector<std::uint8_t> rgba = {255, 0, 0,   255, 0,   255, 0,   255,
                                            0,   0, 255, 255, 255, 255, 255, 255};
    const std::vector<std::uint8_t> out = hui::ps5cc::cover_rgba(rgba.data(), 4, 1, 2, 1);
    ASSERT_EQ(out.size(), 8u);
    EXPECT_EQ(out[1], 255); // green
    EXPECT_EQ(out[6], 255); // blue
    EXPECT_EQ(out[0], 0);
}

TEST(Backdrops, CoverMakesTheTargetSizeFromAnyShape)
{
    const std::vector<std::uint8_t> tall(static_cast<std::size_t>(300) * 900 * 4u, 90);
    const std::vector<std::uint8_t> out = hui::ps5cc::cover_rgba(tall.data(), 300, 900, 160, 90);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(160) * 90 * 4u);
    EXPECT_EQ(out[0], 90);
    EXPECT_TRUE(hui::ps5cc::cover_rgba(nullptr, 4, 4, 2, 2).empty());
    EXPECT_TRUE(hui::ps5cc::decode_backdrop(nullptr, 0).empty());
}

TEST(Backdrops, FetchesOnlyACustomPictureThatIsShown)
{
    Backdrops b;
    b.apply(games_area(BackdropMode::standard, true, "1-100"));
    EXPECT_EQ(b.wanted_custom().area, -1); // set aside: not fetched

    b.apply(games_area(BackdropMode::custom, true, "1-100"));
    const hui::ps5cc::BackdropWant want = b.wanted_custom();
    EXPECT_EQ(want.area, 1);
    EXPECT_EQ(want.stamp, "1-100");
    EXPECT_EQ(b.wanted_custom().area, -1); // already on its way
}

TEST(Backdrops, TheUsersPictureReplacesTheSameTexture)
{
    Backdrops b;
    FakeGl gl;
    b.apply(games_area(BackdropMode::custom, true, "1-100"));
    EXPECT_EQ(b.texture(1), 0u);
    b.deliver_custom(b.wanted_custom().area, "1-100", picture(10));
    b.upload_pending(gl.create());
    const std::uint32_t first = b.texture(1);
    EXPECT_NE(first, 0u);

    // A new upload: a new stamp, fetched again, drawn into the texture it already has.
    b.apply(games_area(BackdropMode::custom, true, "2-120"));
    const hui::ps5cc::BackdropWant again = b.wanted_custom();
    ASSERT_EQ(again.stamp, "2-120");
    b.deliver_custom(again.area, again.stamp, picture(20));
    b.upload_pending(gl.create());
    EXPECT_EQ(b.texture(1), first);
    ASSERT_EQ(gl.reused.size(), 1u);
    EXPECT_EQ(gl.reused[0], first);
}

TEST(Backdrops, OffShowsNothingAndAFailureIsNotRetried)
{
    Backdrops b;
    FakeGl gl;
    b.apply(games_area(BackdropMode::custom, true, "1-100"));
    const hui::ps5cc::BackdropWant want = b.wanted_custom();
    b.deliver_custom(want.area, want.stamp, {1, 2, 3}); // not a whole picture
    EXPECT_EQ(b.wanted_custom().area, -1);
    b.upload_pending(gl.create());
    EXPECT_EQ(b.texture(1), 0u);

    b.apply(games_area(BackdropMode::off, true, "1-100"));
    EXPECT_EQ(b.mode(1), BackdropMode::off);
    EXPECT_EQ(b.texture(1), 0u);
    EXPECT_EQ(b.texture(-1), 0u);
}

} // namespace
