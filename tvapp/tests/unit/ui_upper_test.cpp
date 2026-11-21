// ps5cc - upper() for headings: the letters of every language the app has (review of 21.11.2026).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/fonts.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(UiUpper, AsciiStaysAsItWas)
{
    EXPECT_EQ(hui::ui::upper("Continue playing 2"), "CONTINUE PLAYING 2");
}

TEST(UiUpper, GermanUmlauts)
{
    EXPECT_EQ(hui::ui::upper("4. Prüfung danach"), "4. PRÜFUNG DANACH");
    EXPECT_EQ(hui::ui::upper("Größe äöü"), "GRÖßE ÄÖÜ"); // ß has no single capital here
}

TEST(UiUpper, OtherLatinLetters)
{
    EXPECT_EQ(hui::ui::upper("éèàçñåø ÷"), "ÉÈÀÇÑÅØ ÷");
}

TEST(UiUpper, Cyrillic)
{
    EXPECT_EQ(hui::ui::upper("консоль ёж"), "КОНСОЛЬ ЁЖ");
    EXPECT_EQ(hui::ui::upper("Система"), "СИСТЕМА");
}

TEST(UiUpper, CutSequenceIsLeftAlone)
{
    const std::string cut = std::string("ab") + static_cast<char>(0xC3);
    EXPECT_EQ(hui::ui::upper(cut), std::string("AB") + static_cast<char>(0xC3));
}
