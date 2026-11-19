// ps5cc - Tests for what the name of a ZIP of a save says: the title and the slot.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/save_zip.hpp"

#include <gtest/gtest.h>

namespace
{

using hui::ps5cc::zip_slot;
using hui::ps5cc::zip_stamp;
using hui::ps5cc::zip_title_id;

TEST(Ps5ccSaveZip, TheNameOfTheAppSaysTitleAndSlot)
{
    const std::string name = "PPSA19015_sce_sdmemory_20261119-100716.zip";
    EXPECT_EQ(zip_title_id(name), "PPSA19015");
    EXPECT_EQ(zip_slot(name), "sce_sdmemory"); // a slot can hold "_"
    EXPECT_EQ(zip_title_id("CUSA03474_save_20261119-081130.zip"), "CUSA03474");
    EXPECT_EQ(zip_slot("CUSA03474_save_20261119-081130.zip"), "save");
}

TEST(Ps5ccSaveZip, OtherNamesHaveNoTitle)
{
    EXPECT_EQ(zip_title_id("meine-sicherung.zip"), "");
    EXPECT_EQ(zip_slot("meine-sicherung.zip"), "");
    EXPECT_EQ(zip_title_id("PPSA1901_x_2026.zip"), "");   // too short
    EXPECT_EQ(zip_title_id("PPSA19O15_x_2026.zip"), "");  // a letter in the number
    EXPECT_EQ(zip_title_id("XXXX19015_x_2026.zip"), "");  // not a title kind
    EXPECT_EQ(zip_title_id("PPSA190150_x_2026.zip"), ""); // no "_" after nine characters
    EXPECT_EQ(zip_title_id(""), "");
}

TEST(Ps5ccSaveZip, ATitleWithoutSlotOrDateGivesNoSlot)
{
    EXPECT_EQ(zip_title_id("PPSA19015_SaveData.zip"), "PPSA19015");
    EXPECT_EQ(zip_slot("PPSA19015_SaveData.zip"),
              ""); // only one "_": no slot between title and date
    EXPECT_EQ(zip_slot("PPSA19015_.zip"), "");
    EXPECT_EQ(zip_slot("PPSA19015.zip"), "");
}

TEST(Ps5ccSaveZip, TheTimeBindsTheSlotsOfOneRun)
{
    EXPECT_EQ(zip_stamp("PPSA19015_sce_sdmemory_20261119-100716.zip"), "20261119-100716");
    EXPECT_EQ(zip_stamp("PPSA19015_SaveData_20261119-100716.zip"), "20261119-100716");
    EXPECT_EQ(zip_stamp("CUSA03474_save_20261119-081130.zip"), "20261119-081130");
    EXPECT_EQ(zip_stamp("PPSA19015_SaveData.zip"), "");
    EXPECT_EQ(zip_stamp("PPSA19015_SaveData_2026111-100716.zip"), "");  // a digit short
    EXPECT_EQ(zip_stamp("PPSA19015_SaveData_20261119_100716.zip"), ""); // no "-"
    EXPECT_EQ(zip_stamp("PPSA19015_SaveData_20261119-10071x.zip"), ""); // a letter
    EXPECT_EQ(zip_stamp("meine-sicherung_20261119-100716.zip"), "");    // no title
    EXPECT_EQ(zip_stamp(""), "");
}

} // namespace
