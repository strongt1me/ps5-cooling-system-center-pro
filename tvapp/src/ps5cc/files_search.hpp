// ps5cc - The search of Dateien: which names a term finds, as the console and the web page decide it.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 25.11.2026 (user): the folder is filtered while the term is typed; with "Auch Unterordner" the
// console searches below it (POST /api/v1/files/search, src/fm_search.inc). The rule is the same in
// all three places: a term without * and ? matches a part of the name, with them the whole name
// (*.pkg, PPSA?????); upper and lower case are the same (ASCII and the Latin letters with accents).
#pragma once

#include <string>
#include <string_view>

namespace hui::ps5cc
{

// Does the name match the term? An empty term matches everything.
bool fs_name_matches(std::string_view term, std::string_view name);

// The folder a path lies in ("/data/a/b.txt" -> "/data/a", "/x" -> "/").
std::string fs_parent_of(std::string_view path);

} // namespace hui::ps5cc
