// ps5cc_files_search_test - The search rule of Dateien: the same names as on the console.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/files_search.hpp"

#include <gtest/gtest.h>

namespace hui::ps5cc
{
namespace
{

TEST(FilesSearch, PartOfTheNameWithoutCase)
{
    EXPECT_TRUE(fs_name_matches("notiz", "Meine NOTIZEN.txt"));
    EXPECT_TRUE(fs_name_matches("TXT", "notizen.txt"));
    EXPECT_TRUE(fs_name_matches("(1)", "datei (1).txt"));
    EXPECT_FALSE(fs_name_matches("notizx", "notiz.txt"));
    EXPECT_TRUE(fs_name_matches("", "alles"));
}

TEST(FilesSearch, WildcardsMatchTheWholeName)
{
    EXPECT_TRUE(fs_name_matches("*.pkg", "Spiel.PKG"));
    EXPECT_FALSE(fs_name_matches("*.pkg", "spiel.pkg.bak"));
    EXPECT_TRUE(fs_name_matches("PPSA?????", "PPSA04933"));
    EXPECT_FALSE(fs_name_matches("PPSA?????", "PPSA04933.exfat"));
    EXPECT_TRUE(fs_name_matches("a*b*c", "aXXbYYc"));
    EXPECT_FALSE(fs_name_matches("a.b", "axb")); // a dot is a dot
}

TEST(FilesSearch, UmlautsWithoutCase)
{
    EXPECT_TRUE(fs_name_matches("\xC3\xA4pfel", "\xC3\x84pfel-\xC3\x9C"
                                                "bersicht.txt")); // äpfel / Äpfel-Übersicht
    EXPECT_TRUE(fs_name_matches("\xC3\xBC"
                                "ber",
                                "\xC3\x9C"
                                "ber.txt")); // über / Über
}

TEST(FilesSearch, ParentOfAPath)
{
    EXPECT_EQ(fs_parent_of("/data/a/b.txt"), "/data/a");
    EXPECT_EQ(fs_parent_of("/data"), "/");
    EXPECT_EQ(fs_parent_of("/ntfs0/x"), "/ntfs0");
}

} // namespace
} // namespace hui::ps5cc
