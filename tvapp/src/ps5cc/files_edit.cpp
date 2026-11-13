// ps5cc - The line editor of the Dateien area (see files_edit.hpp).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/files_edit.hpp"

#include <algorithm>
#include <cctype>

namespace hui::ps5cc
{

bool is_text_name(const std::string &name)
{
    static const char *const kEndings[] = {
        "txt", "cfg", "ini", "conf", "config",     "json",     "yaml", "yml", "toml",
        "sh",  "py",  "js",  "ts",   "log",        "csv",      "md",   "xml", "html",
        "htm", "css", "bat", "cmd",  "properties", "env",      "lst",  "m3u", "pls",
        "nfo", "diz", "reg", "inf",  "htaccess",   "gitignore"};
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size())
        return false;
    std::string ending = name.substr(dot + 1);
    std::transform(ending.begin(), ending.end(), ending.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::any_of(std::begin(kEndings), std::end(kEndings),
                       [&](const char *e) { return ending == e; });
}

std::string edit_refusal(const FsEntry &entry)
{
    if (entry.type != "file")
        return "Das ist keine Datei.";
    if (!is_text_name(entry.name))
        return "Nur Textdateien lassen sich bearbeiten.";
    if (!entry.writable)
        return "Hier darf nichts geändert werden.";
    if (entry.size > kEditMaxBytes)
        return "Diese Datei ist zu groß für den Editor (über 64 KiB).";
    return {};
}

EditText split_edit_text(const std::string &text)
{
    EditText out;
    if (text.find('\0') != std::string::npos)
    {
        out.error = "Das sieht nicht nach Text aus.";
        out.lines = {""};
        return out;
    }
    out.final_newline = !text.empty() && text.back() == '\n';
    std::size_t start = 0;
    int terminated = 0;
    int terminated_cr = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        const bool ended = end != std::string::npos;
        if (!ended)
            end = text.size();
        std::string line = text.substr(start, end - start);
        if (ended)
        {
            ++terminated;
            if (!line.empty() && line.back() == '\r')
                ++terminated_cr;
        }
        out.lines.push_back(std::move(line));
        if (out.lines.size() > kEditMaxLines)
        {
            out.error = "Zu viele Zeilen für den Editor (über 4000).";
            out.lines.resize(1);
            return out;
        }
        start = end + 1;
    }
    out.crlf = terminated > 0 && terminated_cr == terminated;
    if (out.crlf)
    {
        // the CR belongs to the line ending; a last line without a line ending keeps what it has
        for (std::size_t i = 0; i < out.lines.size(); ++i)
        {
            if (i + 1 < out.lines.size() || out.final_newline)
                out.lines[i].pop_back();
        }
    }
    if (out.lines.empty())
        out.lines.push_back({});
    return out;
}

std::string join_edit_text(const EditText &text)
{
    const std::string eol = text.crlf ? "\r\n" : "\n";
    std::string out;
    for (std::size_t i = 0; i < text.lines.size(); ++i)
    {
        out += text.lines[i];
        if (i + 1 < text.lines.size() || text.final_newline)
            out += eol;
    }
    return out;
}

std::string edit_display(const std::string &line)
{
    std::string out;
    out.reserve(line.size());
    for (const char c : line)
    {
        if (c == '\t')
            out += "    ";
        else if (c != '\r')
            out += c;
    }
    return out;
}

std::vector<bool> edit_marks(const std::vector<std::string> &now,
                             const std::vector<std::string> &before)
{
    // lines are matched from both ends, so that a line put in or taken out marks only itself
    std::vector<bool> marks(now.size(), true);
    std::size_t head = 0;
    while (head < now.size() && head < before.size() && now[head] == before[head])
    {
        marks[head] = false;
        ++head;
    }
    std::size_t tail = 0;
    while (tail < now.size() - head && tail < before.size() - head &&
           now[now.size() - 1 - tail] == before[before.size() - 1 - tail])
    {
        marks[now.size() - 1 - tail] = false;
        ++tail;
    }
    return marks;
}

} // namespace hui::ps5cc
