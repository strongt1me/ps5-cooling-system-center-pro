// ps5cc - The fullscreen app's translation: German texts looked up as they are drawn.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/i18n.hpp"

#include "third_party/cjson/cJSON.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>

namespace hui::ps5cc::i18n
{

namespace
{

constexpr std::string_view kSeparator = " \xC2\xB7 "; // " · "
constexpr std::size_t kLookupCap = 8000;              // as the browser's cache
constexpr std::size_t kShownCap = 6000;

bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// A letter as web/i18n.js counts one: A–Z, a–z, À–ÿ (with Ä Ö Ü ß), А–я.
bool is_letter(std::uint32_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c <= 0xFF) ||
           (c >= 0x410 && c <= 0x44F);
}

// Two letters in a row somewhere: a word, not a number or a symbol.
bool has_letters(std::string_view s)
{
    int run = 0;
    for (std::size_t i = 0; i < s.size();)
    {
        if (is_letter(gfx::next_codepoint(s, &i)))
        {
            if (++run >= 2)
                return true;
        }
        else
        {
            run = 0;
        }
    }
    return false;
}

// A German letter (ä ö ü ß, either case) left in a translation: part of the text was not found.
bool has_umlaut(std::string_view s)
{
    for (std::size_t i = 0; i < s.size();)
    {
        const std::uint32_t c = gfx::next_codepoint(s, &i);
        if (c == 0xE4 || c == 0xF6 || c == 0xFC || c == 0xDF || c == 0xC4 || c == 0xD6 || c == 0xDC)
            return true;
    }
    return false;
}

// "{n}" at s[i]: its number and where it ends; -1 when there is none.
int placeholder_at(std::string_view s, std::size_t i, std::size_t *end)
{
    if (i >= s.size() || s[i] != '{')
        return -1;
    std::size_t j = i + 1;
    int n = 0;
    while (j < s.size() && s[j] >= '0' && s[j] <= '9')
    {
        n = n * 10 + (s[j] - '0');
        ++j;
    }
    if (j == i + 1 || j >= s.size() || s[j] != '}')
        return -1;
    *end = j + 1;
    return n;
}

bool has_placeholder(std::string_view s)
{
    std::size_t end = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (placeholder_at(s, i, &end) >= 0)
            return true;
    }
    return false;
}

std::string lead_key(std::string_view s)
{
    std::string k(s.substr(0, 2));
    for (char &c : k)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return k;
}

// Runs of white space become one space; none at the ends.
std::string collapse(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for (char c : s)
    {
        if (is_space(c))
        {
            space = !out.empty();
            continue;
        }
        if (space)
            out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

// "x.." -> "x." where a value ended in a full stop the sentence repeats; "..." stays.
std::string fix_double_stop(std::string s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '.' && i + 1 < s.size() && s[i + 1] == '.' && i > 0 && s[i - 1] != '.' &&
            (i + 2 >= s.size() || s[i + 2] != '.'))
        {
            out.push_back('.');
            ++i;
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

// The edge of a text that is not part of its key: white space, then at most one "·", "•", "," or
// ";" (at the end "·", "," or ";"), then white space.
std::size_t lead_edge(std::string_view s)
{
    std::size_t i = 0;
    while (i < s.size() && is_space(s[i]))
        ++i;
    if (s.compare(i, 2, "\xC2\xB7") == 0)
        i += 2;
    else if (s.compare(i, 3, "\xE2\x80\xA2") == 0)
        i += 3;
    else if (i < s.size() && (s[i] == ',' || s[i] == ';'))
        ++i;
    else
        return i;
    while (i < s.size() && is_space(s[i]))
        ++i;
    return i;
}

std::size_t trail_edge(std::string_view s, std::size_t from)
{
    std::size_t j = s.size();
    while (j > from && is_space(s[j - 1]))
        --j;
    if (j >= from + 2 && s.compare(j - 2, 2, "\xC2\xB7") == 0)
        j -= 2;
    else if (j > from && (s[j - 1] == ',' || s[j - 1] == ';'))
        --j;
    else
        return j;
    while (j > from && is_space(s[j - 1]))
        --j;
    return j;
}

} // namespace

std::string plural_word(const std::string &lang, std::string_view value,
                        const std::vector<std::string> &forms)
{
    if (forms.empty())
        return {};
    std::string v;
    for (std::size_t i = 0; i < value.size();)
    {
        const std::size_t at = i;
        const std::uint32_t c = gfx::next_codepoint(value, &i);
        if (c == ' ' || c == 0xA0 || c == 0x202F || c == '\t')
            continue;
        v.append(value.substr(at, i - at));
    }
    // a fraction of one or two digits: 1,5 or 2.25
    bool frac = false;
    std::size_t cut = v.size();
    for (std::size_t digits = 1; digits <= 2; ++digits)
    {
        if (v.size() < digits + 2)
            break;
        const std::size_t mark = v.size() - digits - 1;
        bool all = true;
        for (std::size_t k = mark + 1; k < v.size(); ++k)
            all = all && v[k] >= '0' && v[k] <= '9';
        if (all && (v[mark] == '.' || v[mark] == ',') && v[mark - 1] >= '0' && v[mark - 1] <= '9')
        {
            frac = true;
            cut = mark;
            break;
        }
    }
    long n = 0;
    for (std::size_t k = 0; k < cut; ++k)
    {
        if (v[k] >= '0' && v[k] <= '9')
            n = n * 10 + (v[k] - '0');
        if (n > 1000000000L)
            break;
    }
    std::size_t i = 0;
    if (lang == "ru")
    {
        const long m10 = n % 10, m100 = n % 100;
        if (frac)
            i = 1;
        else if (m10 == 1 && m100 != 11)
            i = 0;
        else if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
            i = 1;
        else
            i = 2;
    }
    else if (lang == "fr")
    {
        i = !frac && n < 2 ? 0 : 1;
    }
    else
    {
        i = !frac && n == 1 ? 0 : 1;
    }
    return forms[std::min(i, forms.size() - 1)];
}

std::string console_language(int system_language)
{
    switch (system_language)
    {
    case 2:
    case 22:
        return "fr";
    case 3:
    case 20:
        return "es";
    case 4:
        return "de";
    case 5:
        return "it";
    case 8:
        return "ru";
    case 13:
        return "sv";
    default:
        return "en";
    }
}

bool available(const std::string &lang)
{
    return lang == "de" || lang == "en" || lang == "es" || lang == "fr" || lang == "it" ||
           lang == "ru" || lang == "sv";
}

std::string wanted_language(const std::string &setting, int console_system_language)
{
    std::string lang = setting;
    if (lang.empty())
        lang = console_system_language < 0 ? "de" : console_language(console_system_language);
    return available(lang) ? lang : "en";
}

bool parse_dictionary(std::string_view json, std::vector<Entry> *out)
{
    cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
    if (root == nullptr)
        return false;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    const bool ok = cJSON_IsObject(t);
    if (ok)
    {
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, t)
        {
            if (item->string != nullptr && cJSON_IsString(item) && item->valuestring != nullptr)
                out->emplace_back(item->string, item->valuestring);
        }
    }
    cJSON_Delete(root);
    return ok;
}

void Translator::load(const std::string &lang, const std::vector<Entry> &entries)
{
    lang_ = lang;
    exact_.clear();
    outputs_.clear();
    patterns_.clear();
    by_lead_.clear();
    floating_.clear();
    lookups_.clear();
    shown_.clear();
    // the last entry for a German text wins (the fullscreen app's own dictionary comes after the
    // browser's)
    std::unordered_map<std::string, std::string> merged;
    std::vector<std::string> order;
    for (const Entry &e : entries)
    {
        if (e.first.empty() || e.second.empty())
            continue;
        auto [it, fresh] = merged.insert_or_assign(e.first, e.second);
        if (fresh)
            order.push_back(it->first);
    }
    for (const std::string &src : order)
    {
        const std::string &tr = merged[src];
        if (!has_placeholder(src))
        {
            exact_[src] = tr;
            outputs_.insert(tr);
            continue;
        }
        Pattern p;
        p.out = tr;
        std::string piece;
        for (std::size_t i = 0; i < src.size();)
        {
            std::size_t end = 0;
            const int n = placeholder_at(src, i, &end);
            if (n >= 0)
            {
                p.pieces.push_back(piece);
                piece.clear();
                p.order.push_back(n);
                i = end;
                continue;
            }
            piece.push_back(src[i]);
            ++i;
        }
        p.pieces.push_back(piece);
        for (const std::string &s : p.pieces)
        {
            if (s.size() > p.need.size())
                p.need = s;
        }
        if (p.need.size() < 3)
            p.need.clear();
        patterns_.push_back(std::move(p));
    }
    // the most literal text first: "Kopiere {0} nach {1}" before "{0} {1}"
    const auto weight = [](const Pattern &p)
    {
        std::size_t w = 0;
        for (const std::string &s : p.pieces)
            w += s.size();
        return w;
    };
    std::stable_sort(patterns_.begin(), patterns_.end(),
                     [&](const Pattern &a, const Pattern &b) { return weight(a) > weight(b); });
    for (std::size_t i = 0; i < patterns_.size(); ++i)
    {
        const std::string &lead = patterns_[i].pieces.front();
        if (lead.size() >= 2)
            by_lead_[lead_key(lead)].push_back(i);
        else
            floating_.push_back(i);
    }
}

bool Translator::try_list(const std::vector<std::size_t> &list, std::string_view core, int depth,
                          std::string *out)
{
    std::vector<std::string_view> values;
    for (std::size_t index : list)
    {
        const Pattern &p = patterns_[index];
        if (!p.need.empty() && core.find(p.need) == std::string_view::npos)
            continue;
        const std::string &first = p.pieces.front();
        if (core.compare(0, first.size(), first) != 0)
            continue;
        values.clear();
        std::size_t pos = first.size();
        bool ok = true;
        const std::size_t n = p.order.size();
        for (std::size_t k = 1; k <= n && ok; ++k)
        {
            const std::string &piece = p.pieces[k];
            if (k == n)
            {
                ok = core.size() >= pos + piece.size() &&
                     core.compare(core.size() - piece.size(), piece.size(), piece) == 0;
                if (ok)
                    values.push_back(core.substr(pos, core.size() - piece.size() - pos));
            }
            else
            {
                const std::size_t at = core.find(piece, pos);
                ok = at != std::string_view::npos;
                if (ok)
                {
                    values.push_back(core.substr(pos, at - pos));
                    pos = at + piece.size();
                }
            }
        }
        if (!ok)
            continue;
        const auto value_of = [&](int number) -> int
        {
            for (std::size_t k = 0; k < p.order.size(); ++k)
            {
                if (p.order[k] == number)
                    return static_cast<int>(k);
            }
            return -1;
        };
        std::string result;
        const std::string &tr = p.out;
        for (std::size_t i = 0; i < tr.size();)
        {
            std::size_t end = 0;
            const int number = placeholder_at(tr, i, &end);
            if (number >= 0)
            {
                const int k = value_of(number);
                if (k >= 0)
                    result += inner(values[static_cast<std::size_t>(k)], depth);
                i = end;
                continue;
            }
            // a plural group {n|one|few|many}
            if (tr[i] == '{')
            {
                std::size_t j = i + 1;
                int number2 = 0;
                while (j < tr.size() && tr[j] >= '0' && tr[j] <= '9')
                    number2 = number2 * 10 + (tr[j++] - '0');
                const std::size_t close = tr.find('}', j);
                if (j > i + 1 && j < tr.size() && tr[j] == '|' && close != std::string::npos &&
                    tr.find('{', j) > close)
                {
                    std::vector<std::string> forms;
                    std::size_t from = j + 1;
                    for (;;)
                    {
                        const std::size_t bar = tr.find('|', from);
                        if (bar == std::string::npos || bar > close)
                        {
                            forms.emplace_back(tr.substr(from, close - from));
                            break;
                        }
                        forms.emplace_back(tr.substr(from, bar - from));
                        from = bar + 1;
                    }
                    const int k = value_of(number2);
                    if (k >= 0)
                        result += plural_word(lang_, values[static_cast<std::size_t>(k)], forms);
                    i = close + 1;
                    continue;
                }
            }
            result.push_back(tr[i]);
            ++i;
        }
        *out = fix_double_stop(std::move(result));
        return true;
    }
    return false;
}

std::string Translator::inner(std::string_view value, int depth)
{
    if (depth >= 3 || !has_letters(value))
        return std::string(value);
    std::size_t a = 0, b = value.size();
    while (a < b && is_space(value[a]))
        ++a;
    while (b > a && is_space(value[b - 1]))
        --b;
    std::string t;
    if (!lookup(collapse(value.substr(a, b - a)), depth + 1, &t))
        return std::string(value);
    return std::string(value.substr(0, a)) + t + std::string(value.substr(b));
}

bool Translator::via_segments(std::string_view core, int depth, std::string *out)
{
    std::string result;
    bool changed = false;
    std::size_t from = 0;
    for (;;)
    {
        const std::size_t at = core.find(kSeparator, from);
        const std::string_view part =
            core.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from);
        std::string t;
        if (lookup(collapse(part), depth + 1, &t))
        {
            result += t;
            changed = true;
        }
        else
        {
            result.append(part);
        }
        if (at == std::string_view::npos)
            break;
        result.append(kSeparator);
        from = at + kSeparator.size();
    }
    if (changed)
        *out = std::move(result);
    return changed;
}

bool Translator::via_marks(std::string_view core, int depth, std::string *out)
{
    std::size_t a = 0;
    while (a < core.size() && (core[a] == '.' || is_space(core[a])))
        ++a;
    std::size_t b = core.size();
    while (b > a && (core[b - 1] == '.' || core[b - 1] == '(' || core[b - 1] == ':' ||
                     is_space(core[b - 1])))
        --b;
    if (a == 0 && b == core.size())
        return false;
    const std::string_view mid = core.substr(a, b - a);
    if (mid.empty() || !has_letters(mid))
        return false;
    std::string t;
    if (!lookup(mid, depth + 1, &t))
        return false;
    *out = std::string(core.substr(0, a)) + t + std::string(core.substr(b));
    return true;
}

bool Translator::lookup(std::string_view core, int depth, std::string *out)
{
    // At depth 3 neither segments nor marks are tried: that answer must not stand for a shallower
    // ask.
    const std::string key = depth >= 3 ? "\x01" + std::string(core) : std::string(core);
    if (const auto hit = lookups_.find(key); hit != lookups_.end())
    {
        if (hit->second.hit)
            *out = hit->second.out;
        return hit->second.hit;
    }
    Cached result;
    if (outputs_.count(std::string(core)) != 0)
    {
        lookups_.emplace(key, result); // already a translation: it stays as it is
        return false;
    }
    if (const auto e = exact_.find(std::string(core)); e != exact_.end())
    {
        result.hit = true;
        result.out = e->second;
    }
    else
    {
        if (core.size() >= 2)
        {
            if (const auto list = by_lead_.find(lead_key(core)); list != by_lead_.end())
                result.hit = try_list(list->second, core, depth, &result.out);
        }
        if (!result.hit)
            result.hit = try_list(floating_, core, depth, &result.out);
    }
    if (!result.hit && depth < 3)
    {
        const std::size_t sep = core.find(kSeparator);
        if (sep != std::string_view::npos && sep > 0)
            result.hit = via_segments(core, depth, &result.out);
    }
    if (!result.hit && depth < 3)
        result.hit = via_marks(core, depth, &result.out);
    if (lookups_.size() > kLookupCap)
        lookups_.clear();
    if (result.hit)
        *out = result.out;
    lookups_.emplace(key, std::move(result));
    return lookups_[key].hit;
}

std::string Translator::translate_text(std::string_view text, bool *changed)
{
    const std::size_t a = lead_edge(text);
    const std::size_t b = trail_edge(text, a);
    const std::string core = collapse(text.substr(a, b - a));
    std::string t;
    if (core.empty() || !has_letters(core))
        return std::string(text);
    if (!lookup(core, 0, &t))
    {
        if (record_ && missed_.emplace(core, true).second)
            misses_.push_back(core);
        return std::string(text);
    }
    // Found only in part (a pattern or the segments took some words, the rest stayed German): worth
    // an entry too.
    if (record_ && has_umlaut(t) && missed_.emplace(core, true).second)
        misses_.push_back(core);
    *changed = true;
    return std::string(text.substr(0, a)) + t + std::string(text.substr(b));
}

std::string Translator::translate(std::string_view text)
{
    bool changed = false;
    std::string out;
    std::size_t from = 0;
    for (;;)
    {
        const std::size_t nl = text.find('\n', from);
        const std::string_view line =
            text.substr(from, nl == std::string_view::npos ? std::string_view::npos : nl - from);
        out += translate_text(line, &changed);
        if (nl == std::string_view::npos)
            break;
        out.push_back('\n');
        from = nl + 1;
    }
    return changed ? out : std::string(text);
}

std::string_view Translator::apply(std::string_view text)
{
    if (!active())
        return text;
    const std::string key(text);
    auto it = shown_.find(key);
    if (it == shown_.end())
    {
        Cached c;
        c.out = translate(text);
        c.hit = c.out != key;
        it = shown_.emplace(key, std::move(c)).first;
    }
    return it->second.hit ? std::string_view(it->second.out) : text;
}

void Translator::frame()
{
    if (shown_.size() > kShownCap)
        shown_.clear();
}

} // namespace hui::ps5cc::i18n
