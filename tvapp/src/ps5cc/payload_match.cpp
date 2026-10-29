// ps5cc - Whether a payload is already running (see payload_match.hpp).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/payload_match.hpp"

#include <cctype>

namespace hui::ps5cc
{

std::string payload_key(const std::string &name)
{
    std::string lower;
    for (const char c : name)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char *ext : {".elf", ".bin", ".self"})
    {
        const std::string e(ext);
        if (lower.size() > e.size() && lower.compare(lower.size() - e.size(), e.size(), e) == 0)
            lower.resize(lower.size() - e.size());
    }
    // the version starts at a separator followed by a digit, or by "v" and a digit
    for (std::size_t i = 1; i < lower.size(); ++i)
    {
        const char c = lower[i];
        if (c != '_' && c != '-' && c != ' ')
            continue;
        const std::size_t d = i + 1 < lower.size() && lower[i + 1] == 'v' ? i + 2 : i + 1;
        if (d < lower.size() && std::isdigit(static_cast<unsigned char>(lower[d])))
        {
            lower.resize(i);
            break;
        }
    }
    std::string key;
    for (const char c : lower)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            key += c;
    }
    return key;
}

bool payload_same(const std::string &a, const std::string &b)
{
    const std::string x = payload_key(a), y = payload_key(b);
    if (x.empty() || y.empty())
        return false;
    if (x == y)
        return true;
    const std::string &shorter = x.size() < y.size() ? x : y;
    const std::string &longer = x.size() < y.size() ? y : x;
    return shorter.size() >= 6 && longer.compare(0, shorter.size(), shorter) == 0;
}

bool payload_loaded(const std::string &name, const std::vector<std::string> &running)
{
    for (const std::string &r : running)
    {
        if (payload_same(name, r))
            return true;
    }
    return false;
}

bool payload_file_loaded(const std::string &file, const std::vector<std::string> &folder,
                         const std::vector<RunningRef> &running)
{
    if (file.empty())
        return false;
    for (const RunningRef &r : running)
    {
        if (!r.file.empty() && r.file == file)
            return true;
    }
    int kin = 0;
    for (const std::string &f : folder)
    {
        if (payload_same(f, file))
            ++kin;
    }
    for (const RunningRef &r : running)
    {
        if (r.file.empty() && payload_same(r.name, file) && kin <= 1)
            return true;
    }
    return false;
}

} // namespace hui::ps5cc
