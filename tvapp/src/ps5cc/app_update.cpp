// ps5cc - When the tile replaces an app that is already running.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/app_update.hpp"

#include <array>
#include <cstring>

namespace hui::ps5cc
{

namespace
{

constexpr char kMark[] = "PS5CC-VERSION:";
constexpr std::size_t kMarkLen = sizeof(kMark) - 1;

// "1.77.1" -> {1, 77, 1}; false for anything else (letters, an empty part, more than four parts).
bool parse(const std::string &text, std::array<long, 4> &out)
{
    out = {0, 0, 0, 0};
    if (text.empty())
        return false;
    std::size_t part = 0;
    bool digits = false;
    for (const char c : text)
    {
        if (c >= '0' && c <= '9')
        {
            out[part] = out[part] * 10 + (c - '0');
            digits = true;
            if (out[part] > 100000)
                return false;
        }
        else if (c == '.' && digits && part + 1 < out.size())
        {
            ++part;
            digits = false;
        }
        else
        {
            return false;
        }
    }
    return digits;
}

} // namespace

std::string embedded_version(const unsigned char *elf, std::size_t size)
{
    if (elf == nullptr || size < kMarkLen + 2)
        return {};
    for (std::size_t i = 0; i + kMarkLen + 2 <= size; ++i)
    {
        if (elf[i] != static_cast<unsigned char>(kMark[0]) || std::memcmp(elf + i, kMark, kMarkLen) != 0)
            continue;
        std::string version;
        for (std::size_t j = i + kMarkLen; j < size && elf[j] != ':' && version.size() < 24; ++j)
            version += static_cast<char>(elf[j]);
        std::array<long, 4> v{};
        if (parse(version, v))
            return version;
    }
    return {};
}

int compare_versions(const std::string &a, const std::string &b)
{
    std::array<long, 4> x{}, y{};
    if (!parse(a, x) || !parse(b, y))
        return 2;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

bool should_replace(const std::string &running, const std::string &embedded)
{
    return compare_versions(running, embedded) == -1;
}

bool job_active(const std::string &body)
{
    return body.find("\"active\":true") != std::string::npos;
}

} // namespace hui::ps5cc
