// ps5cc - What the name of a ZIP of a save says: the title and the slot.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app names a ZIP <title>_<slot>_<date>-<time>.zip (src/savebackup.c):
// PPSA19015_sce_sdmemory_20261119-100716.zip. A ZIP somebody else made may have any name that the
// app accepts (letters, digits, "_", "-", "."): then the file name is all there is. No OpenGL here:
// the unit tests use it.

#pragma once

#include <cstddef>
#include <string>

namespace hui::ps5cc
{

// "PPSA19015" or "CUSA03474" at the start of the name, followed by "_"; else "".
inline std::string zip_title_id(const std::string &name)
{
    if (name.size() < 10 || name[9] != '_')
        return {};
    if (name.compare(0, 4, "PPSA") != 0 && name.compare(0, 4, "CUSA") != 0)
        return {};
    for (std::size_t i = 4; i < 9; ++i)
    {
        if (name[i] < '0' || name[i] > '9')
            return {};
    }
    return name.substr(0, 9);
}

// The time at the end of the name ("20261119-100716"): all ZIP files of one run (one title, every
// slot) carry the same; "" when the name does not end in one.
inline std::string zip_stamp(const std::string &name)
{
    if (zip_title_id(name).empty() || name.size() < 4 + 15 + 1 ||
        name.compare(name.size() - 4, 4, ".zip") != 0)
        return {};
    const std::string stamp = name.substr(name.size() - 4 - 15, 15);
    if (name[name.size() - 4 - 16] != '_' || stamp[8] != '-')
        return {};
    for (std::size_t i = 0; i < stamp.size(); ++i)
    {
        if (i != 8 && (stamp[i] < '0' || stamp[i] > '9'))
            return {};
    }
    return stamp;
}

// The slot between the title and the date ("sce_sdmemory" in the example); "" when the name does
// not have the form (a slot can hold "_", so it is what lies between the first and the last "_").
inline std::string zip_slot(const std::string &name)
{
    if (zip_title_id(name).empty())
        return {};
    const std::size_t last = name.rfind('_');
    if (last == std::string::npos || last <= 10)
        return {};
    return name.substr(10, last - 10);
}

} // namespace hui::ps5cc
