// ps5cc - The line editor of the Dateien area: what may be edited, and a text taken apart into
// lines and put together again.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// As the web app's editor (web/app.js, fmEdit; the app's POST /api/v1/files/write): text files by
// their ending, up to 64 KiB, and only where the app allows changing. A controller cannot type into
// a text field, so the screen edits one line at a time with the on-screen keyboard. The line
// endings (LF or CRLF) and a missing last newline stay as the file had them. No OpenGL here: the
// unit tests use it.

#pragma once

#include "ps5cc/data.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace hui::ps5cc
{

constexpr std::size_t kEditMaxBytes = 65536; // the same limit as the web app's editor
constexpr std::size_t kEditMaxLines = 4000;
constexpr std::size_t kEditMaxLineBytes = 400; // what the keyboard takes for one line

// A name that ends like a text file (the list of the web app's editor).
bool is_text_name(const std::string &name);

// Why this entry cannot be edited, "" when it can.
std::string edit_refusal(const FsEntry &entry);

struct EditText
{
    std::vector<std::string> lines; // at least one; without the line ending
    bool crlf = false;              // every line ended with CR LF
    bool final_newline = true;      // the text ended with a line ending
    std::string error;              // not editable: no text, too many lines
};

// Takes a text apart. A text with a NUL byte is no text; more than kEditMaxLines lines are refused.
EditText split_edit_text(const std::string &text);

// Puts it together again: split_edit_text then join_edit_text gives back the same bytes.
std::string join_edit_text(const EditText &text);

// A line as the list shows it: tabs as spaces, a stray CR gone.
std::string edit_display(const std::string &line);

// The lines the editor shows have been changed against the original: by their index.
std::vector<bool> edit_marks(const std::vector<std::string> &now,
                             const std::vector<std::string> &before);

} // namespace hui::ps5cc
