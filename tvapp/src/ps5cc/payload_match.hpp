// ps5cc - Whether a payload file or a store entry is the one already running.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console names a running payload after itself ("shadowmountplus.elf", at most 19 characters),
// the file on the drive carries a version ("ShadowMountPlus_1.7beta10.elf"). Both are cut down to
// their letters and digits before the version, and a name cut short by the kernel still matches its
// longer file (the web app does the same).

#pragma once

#include <string>
#include <vector>

namespace hui::ps5cc
{

// "ShadowMountPlus_1.7beta10.elf" -> "shadowmountplus", "pldmgr_v0.6.3.elf" -> "pldmgr"
std::string payload_key(const std::string &name);
// "ShadowMountPlus_1.7beta10.elf" and "shadowmountplus_v1.7_Beta10.elf" -> "17beta10"
std::string payload_version(const std::string &name);
// The same payload: equal keys, or one of at least six characters the start of the other.
bool payload_same(const std::string &a, const std::string &b);
// Whether any of the running names is this payload.
bool payload_loaded(const std::string &name, const std::vector<std::string> &running);

// A running payload as the console lists it: its name and, when the console app started it, its
// file.
struct RunningRef
{
    std::string name;
    std::string file;
};

// Whether this file of the payload folder is the one running. Known files decide exactly (two
// versions of one payload: only the one started glows); a running payload of unknown file only
// counts when the folder holds a single file of its kind, since otherwise nobody can say which
// version it is.
bool payload_file_loaded(const std::string &file, const std::vector<std::string> &folder,
                         const std::vector<RunningRef> &running);

} // namespace hui::ps5cc
