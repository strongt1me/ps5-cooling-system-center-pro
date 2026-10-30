// ps5cc - Tests for telling whether a payload file or store entry is the one already running.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/payload_match.hpp"

#include <gtest/gtest.h>

namespace
{

using hui::ps5cc::payload_key;
using hui::ps5cc::payload_loaded;
using hui::ps5cc::payload_same;

TEST(Ps5ccPayloadMatch, KeysLoseTheVersionAndTheExtension)
{
    EXPECT_EQ(payload_key("ShadowMountPlus_1.7beta10.elf"), "shadowmountplus");
    EXPECT_EQ(payload_key("pldmgr_v0.6.3.elf"), "pldmgr");
    EXPECT_EQ(payload_key("ps5-web-file-manager_v1.12.elf"), "ps5webfilemanager");
    EXPECT_EQ(payload_key("YT-Blocker_v1.4r2_(13.60).elf"), "ytblocker");
    EXPECT_EQ(payload_key("ftpsrv.elf"), "ftpsrv");
}

TEST(Ps5ccPayloadMatch, RunningNamesFindTheirFiles)
{
    EXPECT_TRUE(payload_same("ShadowMountPlus_1.7beta10.elf", "shadowmountplus.elf"));
    EXPECT_TRUE(payload_same("nanodns_v0.4.elf", "nanodns.elf"));
    // the kernel keeps 19 characters of a name
    EXPECT_TRUE(payload_same("ps5-web-file-manager_v1.12.elf", "ps5-web-file-manage"));
    EXPECT_FALSE(payload_same("ftpsrv.elf", "zftpd.elf"));
    EXPECT_FALSE(payload_same("elfldr.elf", "elf"));
    EXPECT_TRUE(payload_loaded("pldmgr_v0.6.3.elf", {"kstuff.elf", "pldmgr.elf"}));
    EXPECT_FALSE(payload_loaded("CheatRunner_v0.18.elf", {"kstuff.elf", "pldmgr.elf"}));
}

TEST(Ps5ccPayloadMatch, OnlyTheVersionStartedGlows)
{
    using hui::ps5cc::payload_file_loaded;
    const std::vector<std::string> folder = {"ShadowMountPlus_1.7beta7.elf", "ShadowMountPlus_1.7beta10.elf",
                                             "nanodns_v0.4.elf"};
    // started by the app: the file is known
    const std::vector<hui::ps5cc::RunningRef> known = {{"shadowmountplus.elf", "ShadowMountPlus_1.7beta10.elf"}};
    EXPECT_TRUE(payload_file_loaded("ShadowMountPlus_1.7beta10.elf", folder, known));
    EXPECT_FALSE(payload_file_loaded("ShadowMountPlus_1.7beta7.elf", folder, known));
    // started by something else: two versions in the folder, which one is unknown, so none glows
    const std::vector<hui::ps5cc::RunningRef> unknown = {{"shadowmountplus.elf", ""}, {"nanodns.elf", ""}};
    EXPECT_FALSE(payload_file_loaded("ShadowMountPlus_1.7beta10.elf", folder, unknown));
    EXPECT_FALSE(payload_file_loaded("ShadowMountPlus_1.7beta7.elf", folder, unknown));
    // a single file of its kind is the one
    EXPECT_TRUE(payload_file_loaded("nanodns_v0.4.elf", folder, unknown));
    // the same version internal and on USB, written differently, is still one version
    const std::vector<std::string> twice = {"ShadowMountPlus_1.7beta10.elf", "shadowmountplus_v1.7_Beta10.elf"};
    const std::vector<hui::ps5cc::RunningRef> smp = {{"shadowmountplus.elf", ""}};
    EXPECT_EQ(hui::ps5cc::payload_version(twice[0]), hui::ps5cc::payload_version(twice[1]));
    EXPECT_TRUE(payload_file_loaded(twice[0], twice, smp));
    EXPECT_TRUE(payload_file_loaded(twice[1], twice, smp));
}

} // namespace
