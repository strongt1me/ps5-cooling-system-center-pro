// ps5cc - How much a copy of the data snapshot costs (audit F-85: the frame copies it whenever the app has new numbers).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/data.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace
{

TEST(SnapshotCopy, ABigLibraryIsCopiedInUnderAMillisecond)
{
    hui::ps5cc::Snapshot snap;
    std::uint64_t seen = 0;
    hui::ps5cc::source().fetch(snap, seen);
    // a library of 700 games with the strings a real one carries
    const std::size_t have = snap.games.size();
    ASSERT_GT(have, 0u);
    while (snap.games.size() < 700)
    {
        hui::ps5cc::Game g = snap.games[snap.games.size() % have];
        g.id += std::to_string(snap.games.size());
        snap.games.push_back(std::move(g));
    }
    constexpr int kRuns = 300;
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t sink = 0;
    for (int i = 0; i < kRuns; ++i)
    {
        hui::ps5cc::Snapshot copy = snap;
        sink += copy.games.size();
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / kRuns;
    std::printf("[SNAPSHOT] %zu games: one copy %.3f ms (sanitizers on)\n", snap.games.size(), ms);
    EXPECT_EQ(sink, snap.games.size() * kRuns);
    // under AddressSanitizer a copy is several times slower than on the console; a copy in the milliseconds would be a finding
    EXPECT_LT(ms, 5.0);
}

} // namespace
