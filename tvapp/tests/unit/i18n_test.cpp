// ps5cc - Tests for the translation: exact texts, patterns, inner values, segments, edges, plurals.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/i18n.hpp"
#include "ui/fonts.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

namespace gfx = hui::gfx;
using hui::ps5cc::i18n::console_language;
using hui::ps5cc::i18n::Entry;
using hui::ps5cc::i18n::plural_word;
using hui::ps5cc::i18n::Translator;

Translator english()
{
    Translator t;
    t.load("en", std::vector<Entry>{
                     {"Kühlung", "Cooling"},
                     {"Dateien", "files"},
                     {"Spiele", "Games"},
                     {"Nicht lesbar: {0}", "Not readable: {0}"},
                     {"{0} {1} gelöscht", "{0} {1} deleted"},
                     {"Kopiere {0} nach {1}", "Copying {0} to {1}"},
                     {"{0} Dateien", "{0} {0|file|files}"},
                     {"Fertig: {0} GB in {1} s", "Done: {0} GB in {1} s"},
                     {"Satz mit Punkt {0}.", "Sentence with stop {0}."},
                 });
    return t;
}

TEST(I18n, ExactAndMiss)
{
    Translator t = english();
    EXPECT_EQ(t.translate("Kühlung"), "Cooling");
    EXPECT_EQ(t.translate("Nicht im Wörterbuch"), "Nicht im Wörterbuch");
    EXPECT_EQ(t.translate("68 °C"), "68 °C");
    EXPECT_EQ(t.translate(""), "");
}

TEST(I18n, PatternsAndInnerValues)
{
    Translator t = english();
    EXPECT_EQ(t.translate("Nicht lesbar: /data/x"), "Not readable: /data/x");
    EXPECT_EQ(t.translate("3 Dateien gelöscht"), "3 files deleted");
    EXPECT_EQ(t.translate("Kopiere Spiel nach usb0"), "Copying Spiel to usb0");
    EXPECT_EQ(t.translate("Fertig: 12,5 GB in 30 s"), "Done: 12,5 GB in 30 s");
}

TEST(I18n, ValueNeverSpansASeparator)
{
    Translator t;
    t.load("en", std::vector<Entry>{
                     {"{0} Min.", "{0} min"},
                     {"Stand vor {0} Min.", "As of {0} min ago"},
                     {"{0} Zusatzinhalte", "{0} add-ons"},
                     {"Spiel und {0} Zusatzinhalte", "Game and {0} add-ons"},
                     {"{0} Payloads", "{0} payloads"},
                 });
    // "{0} Min." and "{0} Zusatzinhalte" must not take "6 Payloads · Stand vor 40" as their value:
    // each piece is translated for itself
    EXPECT_EQ(t.translate("6 Payloads · Stand vor 40 Min."), "6 payloads · As of 40 min ago");
    EXPECT_EQ(t.translate("CUSA10001 · Spiel und 2 Zusatzinhalte"),
              "CUSA10001 · Game and 2 add-ons");
    // a value without a separator still matches as before
    EXPECT_EQ(t.translate("12 Min."), "12 min");
}

TEST(I18n, Plurals)
{
    Translator t = english();
    EXPECT_EQ(t.translate("1 Dateien"), "1 file");
    EXPECT_EQ(t.translate("7 Dateien"), "7 files");
    EXPECT_EQ(plural_word("ru", "1", {"a", "b", "c"}), "a");
    EXPECT_EQ(plural_word("ru", "22", {"a", "b", "c"}), "b");
    EXPECT_EQ(plural_word("ru", "12", {"a", "b", "c"}), "c");
    EXPECT_EQ(plural_word("ru", "1,5", {"a", "b", "c"}), "b");
    EXPECT_EQ(plural_word("fr", "0", {"x", "y"}), "x");
    EXPECT_EQ(plural_word("en", "0", {"x", "y"}), "y");
}

TEST(I18n, SegmentsEdgesAndMarks)
{
    Translator t = english();
    EXPECT_EQ(t.translate("Kühlung · Spiele"), "Cooling · Games");
    EXPECT_EQ(t.translate("  Kühlung · "), "  Cooling · ");
    EXPECT_EQ(t.translate("Kühlung:"), "Cooling:");
    EXPECT_EQ(t.translate("Kühlung\nSpiele"), "Cooling\nGames");
    // a value that ends in a full stop does not double the sentence's own
    EXPECT_EQ(t.translate("Satz mit Punkt Ende."), "Sentence with stop Ende.");
}

TEST(I18n, ApplyKeepsItsViewAndOffWithoutDictionary)
{
    Translator t = english();
    const std::string german = "Spiele";
    EXPECT_EQ(t.apply(german), "Games");
    EXPECT_EQ(t.apply(german), "Games");
    t.frame();
    Translator off;
    off.load("de", {});
    EXPECT_FALSE(off.active());
    EXPECT_EQ(off.apply(german).data(), german.data());
}

TEST(I18n, FilterReachesFontsAndCountsChanges)
{
    Translator t = english();
    const unsigned before = gfx::text_filter_generation();
    gfx::set_text_filter(&t);
    EXPECT_NE(gfx::text_filter_generation(), before); // kept layouts see the new dictionary
    EXPECT_EQ(gfx::filter_text("Spiele"), "Games");
    gfx::set_text_filter(nullptr);
    EXPECT_EQ(gfx::filter_text("Spiele"), "Spiele");
}

TEST(I18n, ATranslationIsNotTranslatedAgain)
{
    Translator t;
    t.load("en", std::vector<Entry>{{"Profil", "Profile"}, {"Profile", "Profiles"}});
    EXPECT_EQ(t.translate("Profil"), "Profile");
    EXPECT_EQ(t.translate("Profile"), "Profile"); // drawn again after fitting: stays
    EXPECT_EQ(t.translate("Profile..."), "Profile...");

    gfx::set_text_filter(&t);
    {
        const gfx::RawText raw; // pieces of a text already rewritten go through as given
        EXPECT_EQ(gfx::filter_text("Profil"), "Profil");
    }
    EXPECT_EQ(gfx::filter_text("Profil"), "Profile");
    gfx::set_text_filter(nullptr);
}

// Headings in capitals (form steps, labels of caps themes): ui::upper translates first, so the
// dictionary needs the text as written in the code, not its capitals (22.11.2026: "3. WOHIN?"
// stayed German in the English fullscreen app).
TEST(I18n, CapitalsAreTranslatedBeforeTheyBecomeCapitals)
{
    Translator t;
    t.load("en", std::vector<Entry>{{"3. Wohin?", "3. Where to?"},
                                    {"4. Prüfung danach", "4. Check afterwards"},
                                    {"{0} Titel", "{0} titles"}});
    gfx::set_text_filter(&t);
    EXPECT_EQ(hui::ui::upper("3. Wohin?"), "3. WHERE TO?");
    EXPECT_EQ(hui::ui::upper("4. Prüfung danach"), "4. CHECK AFTERWARDS");
    EXPECT_EQ(hui::ui::upper("12 Titel"), "12 TITLES");
    EXPECT_EQ(hui::ui::upper("Nicht im Wörterbuch"), "NICHT IM WÖRTERBUCH");
    {
        const gfx::RawText raw; // a text already rewritten stays as it is
        EXPECT_EQ(hui::ui::upper("3. Wohin?"), "3. WOHIN?");
    }
    gfx::set_text_filter(nullptr);
    EXPECT_EQ(hui::ui::upper("3. Wohin?"), "3. WOHIN?");
}

// A header in normal case finds its own pattern before the general "{0} von {1}" (review of
// 22.11.2026: the running payload profile read "LÄUFT: AUTOSTART (SCHRITT 1 OF 3)").
TEST(I18n, AHeaderFindsItsOwnPatternBeforeAGeneralOne)
{
    Translator t;
    t.load("en", std::vector<Entry>{
                     {"{0} von {1}", "{0} of {1}"},
                     {"Läuft: {0} (Schritt {1} von {2})", "Running: {0} (step {1} of {2})"}});
    gfx::set_text_filter(&t);
    EXPECT_EQ(hui::ui::upper("Läuft: Autostart (Schritt 1 von 3)"),
              "RUNNING: AUTOSTART (STEP 1 OF 3)");
    gfx::set_text_filter(nullptr);
}

TEST(I18n, ConsoleLanguage)
{
    EXPECT_EQ(console_language(4), "de");
    EXPECT_EQ(console_language(1), "en");
    EXPECT_EQ(console_language(18), "en");
    EXPECT_EQ(console_language(20), "es");
    EXPECT_EQ(console_language(22), "fr");
    EXPECT_EQ(console_language(5), "it");
    EXPECT_EQ(console_language(8), "ru");
    EXPECT_EQ(console_language(13), "sv");
    EXPECT_EQ(console_language(0), "en");
}

} // namespace
