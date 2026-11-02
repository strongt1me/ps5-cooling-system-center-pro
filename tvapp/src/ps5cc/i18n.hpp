// ps5cc - The fullscreen app's translation: German texts looked up as they are drawn.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/font.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hui::ps5cc::i18n
{

// One entry of a dictionary: the German text (with {0}, {1} … where values go) and its translation.
using Entry = std::pair<std::string, std::string>;

// The rules are the browser's (web/i18n.js), so both surfaces read the same dictionaries the same
// way: a text is looked up whole; else against the patterns (values put back, and translated
// themselves when they are in the dictionary: "Dateien" in "{0} {1} gelöscht"); else split at " · "
// and each part looked up; else with a sentence end or an opening bracket stuck to it taken off. A
// leading or trailing "·", ",", ";" and spaces stay as they are. What is not found stays German.
// Plural groups {0|one|few|many} pick the word for the value of {0}.
//
// Installed as the fonts' text filter, it sees every text the app measures and draws. Lines of
// several paragraphs are looked up line by line. Results are cached; frame() empties the cache when
// it grows (values that change every second would fill it), never in the middle of a frame, so a
// view apply() returned stays valid for that frame.
class Translator final : public gfx::TextFilter
{
  public:
    // A new dictionary (entries replace earlier ones with the same German text); lang decides the
    // plural rules. An empty list with "de" switches translating off.
    void load(const std::string &lang, const std::vector<Entry> &entries);
    const std::string &language() const
    {
        return lang_;
    }
    bool active() const
    {
        return !exact_.empty() || !patterns_.empty();
    }

    std::string_view apply(std::string_view text) override;

    // Call once per frame, before anything is drawn.
    void frame();

    // The lookup alone (for tests): the translation, or the text itself.
    std::string translate(std::string_view text);

    // For finding the texts a dictionary still lacks (the PC preview): every text with letters that
    // stayed as it was.
    void record_misses(bool on)
    {
        record_ = on;
    }
    const std::vector<std::string> &misses() const
    {
        return misses_;
    }

  private:
    struct Pattern
    {
        std::vector<std::string> pieces; // the literal text around the values: one more than order
        std::vector<int> order;          // which {n} each value is
        std::string out;                 // the translation with its {n}
        std::string need;                // the longest piece, when 3 bytes or more: a quick reject
    };
    struct Cached
    {
        bool hit = false;
        std::string out;
    };

    bool lookup(std::string_view core, int depth, std::string *out);
    bool try_list(const std::vector<std::size_t> &list, std::string_view core, int depth,
                  std::string *out);
    std::string inner(std::string_view value, int depth);
    bool via_segments(std::string_view core, int depth, std::string *out);
    bool via_marks(std::string_view core, int depth, std::string *out);
    std::string translate_text(std::string_view text, bool *changed);

    std::string lang_ = "de";
    std::unordered_map<std::string, std::string> exact_;
    std::vector<Pattern> patterns_;
    std::unordered_map<std::string, std::vector<std::size_t>>
        by_lead_;                                     // first two bytes, ASCII lower case
    std::vector<std::size_t> floating_;               // patterns whose text starts with a value
    std::unordered_map<std::string, Cached> lookups_; // core -> result, for every depth
    std::unordered_map<std::string, Cached> shown_;   // whole texts as apply() saw them
    bool record_ = false;
    std::vector<std::string> misses_;
    std::unordered_map<std::string, bool> missed_;
};

// The plural word for a value: Russian has three forms (1, 21 …; 2–4, 22–24 …; the rest), French
// counts 0 and 1 as one, the others only 1. A value with a fraction takes the second form.
std::string plural_word(const std::string &lang, std::string_view value,
                        const std::vector<std::string> &forms);

// The language of the console's system setting (locale.language of /api/v1/system) as one of de,
// en, es, fr, it, ru, sv; any other language reads English.
std::string console_language(int system_language);

// Can the screen be shown in this language? (The fonts' "european" set has the accents and basic
// Cyrillic.)
bool available(const std::string &lang);

// The language to show: the setting when it names one, else the console's (unknown yet: -1 -> "de",
// so nothing switches back and forth at the start); one that is not available reads English.
std::string wanted_language(const std::string &setting, int console_system_language);

// The entries of a dictionary file {"v":1,"t":{"German":"translation",…}} appended to out; false if
// it is not one.
bool parse_dictionary(std::string_view json, std::vector<Entry> *out);

} // namespace hui::ps5cc::i18n
