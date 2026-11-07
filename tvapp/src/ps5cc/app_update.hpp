// ps5cc - When the tile replaces an app that is already running.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tile carries a copy of the app (an ELF). It used to send it only when nothing answered on port 8086, so whoever copied a new tile
// over an old one kept the old app until the console restarted. Now the tile compares versions: the app names its own in
// /api/v1/system ("app_version") and the ELF carries the mark "PS5CC-VERSION:<version>:". An OLDER running app is replaced (the new
// one retires it), a newer or equal one is left alone, and so is one that is busy with a job (the replacement would end it).

#pragma once

#include <cstddef>
#include <string>

namespace hui::ps5cc
{

// The version of the ELF at elf (size bytes): the text between "PS5CC-VERSION:" and the next ':', "" when the mark is missing.
std::string embedded_version(const unsigned char *elf, std::size_t size);

// "1.77.1" against "1.77.0": -1 when a is older than b, 0 when equal, 1 when newer, 2 when one of them is not a version.
int compare_versions(const std::string &a, const std::string &b);

// True when the running app is older than the embedded one (both must be versions).
bool should_replace(const std::string &running, const std::string &embedded);

// Whether the answer of a job route says that a job is under way (`"active":true`).
bool job_active(const std::string &body);

} // namespace hui::ps5cc
