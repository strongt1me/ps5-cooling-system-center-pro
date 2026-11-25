// ps5cc - The search of Dateien: which names a term finds (see files_search.hpp).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/files_search.hpp"

#include <cstddef>
#include <vector>

namespace hui::ps5cc
{
namespace
{

// One character, folded as fm_search.inc folds it: ASCII and U+00C0..U+00DE (not ×) to lower case.
// A stray byte stands for itself (above the code points).
unsigned fold(std::string_view s, std::size_t &i)
{
    const auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned c0 = b(i);
    if (c0 < 0x80)
    {
        ++i;
        return c0 >= 'A' && c0 <= 'Z' ? c0 + 32 : c0;
    }
    const auto cont = [&](std::size_t k) { return k < s.size() && (b(k) & 0xC0) == 0x80; };
    if ((c0 & 0xE0) == 0xC0 && cont(i + 1))
    {
        const unsigned c = ((c0 & 0x1F) << 6) | (b(i + 1) & 0x3F);
        i += 2;
        return c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c;
    }
    if ((c0 & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2))
    {
        const unsigned c = ((c0 & 0x0F) << 12) | ((b(i + 1) & 0x3F) << 6) | (b(i + 2) & 0x3F);
        i += 3;
        return c;
    }
    if ((c0 & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3))
    {
        const unsigned c = ((c0 & 0x07) << 18) | ((b(i + 1) & 0x3F) << 12) |
                           ((b(i + 2) & 0x3F) << 6) | (b(i + 3) & 0x3F);
        i += 4;
        return c;
    }
    ++i;
    return 0x110000u + c0;
}

std::vector<unsigned> folded(std::string_view s)
{
    std::vector<unsigned> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();)
        out.push_back(fold(s, i));
    return out;
}

// '*' any run, '?' one character
bool glob(const std::vector<unsigned> &p, const std::vector<unsigned> &s)
{
    std::size_t pi = 0;
    std::size_t si = 0;
    std::size_t star = static_cast<std::size_t>(-1);
    std::size_t mark = 0;
    while (si < s.size())
    {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == s[si]))
        {
            ++pi;
            ++si;
        }
        else if (pi < p.size() && p[pi] == '*')
        {
            star = pi++;
            mark = si;
        }
        else if (star != static_cast<std::size_t>(-1))
        {
            pi = star + 1;
            si = ++mark;
        }
        else
            return false;
    }
    while (pi < p.size() && p[pi] == '*')
        ++pi;
    return pi == p.size();
}

} // namespace

bool fs_name_matches(std::string_view term, std::string_view name)
{
    if (term.empty())
        return true;
    const std::vector<unsigned> t = folded(term);
    const std::vector<unsigned> n = folded(name);
    bool wild = false;
    for (const unsigned c : t)
        wild = wild || c == '*' || c == '?';
    if (wild)
        return glob(t, n);
    if (t.size() > n.size())
        return false;
    for (std::size_t i = 0; i + t.size() <= n.size(); ++i)
    {
        std::size_t k = 0;
        while (k < t.size() && n[i + k] == t[k])
            ++k;
        if (k == t.size())
            return true;
    }
    return false;
}

std::string fs_parent_of(std::string_view path)
{
    const std::size_t s = path.find_last_of('/');
    if (s == std::string_view::npos || s == 0)
        return "/";
    return std::string(path.substr(0, s));
}

} // namespace hui::ps5cc
