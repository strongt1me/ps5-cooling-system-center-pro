// ps5cc - A tile for a payload: payloads are bare ELF files with no picture of their own.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components.hpp"

#include <cctype>
#include <string>

namespace hui::ps5cc
{

struct PayloadTile
{
    std::string label; // one or two characters
    gfx::Color color;
};

// "ShadowMountPlus_1.7beta7.elf" -> "shadowmountplus": lower case, no ".elf", no version at the
// end.
inline std::string payload_stem(const std::string &name)
{
    std::string s;
    for (char c : name)
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".elf") == 0)
        s.resize(s.size() - 4);
    for (std::size_t i = 1; i < s.size(); ++i)
    {
        const bool sep = s[i] == '_' || s[i] == '-' || s[i] == ' ';
        const std::size_t d = i + 1 < s.size() && s[i + 1] == 'v' ? i + 2 : i + 1;
        if (sep && d < s.size() && std::isdigit(static_cast<unsigned char>(s[d])))
        {
            s.resize(i);
            break;
        }
    }
    return s;
}

inline gfx::Color payload_category_color(const std::string &category)
{
    if (category.find("Network") != std::string::npos)
        return gfx::Color::rgb(0x2f7de1);
    if (category.find("Jailbreak") != std::string::npos ||
        category.find("System") != std::string::npos)
        return gfx::Color::rgb(0xc2453b);
    if (category.find("Loader") != std::string::npos)
        return gfx::Color::rgb(0x7f56d9);
    if (category.find("Cheat") != std::string::npos)
        return gfx::Color::rgb(0xd9822b);
    if (category.find("Util") != std::string::npos || category.find("Tool") != std::string::npos)
        return gfx::Color::rgb(0x168f68);
    return gfx::Color{0.0f, 0.0f, 0.0f, 0.0f};
}

// The known payloads keep one mark and one colour wherever they turn up (a file, the store, a
// running process); anything else gets its initials and the colour of its store category, or one
// derived from its name.
inline PayloadTile payload_tile(const std::string &name, const std::string &category = {})
{
    struct Known
    {
        const char *stem;
        const char *label;
        const char *category;
    };
    static const Known kKnown[] = {
        {"shadowmount", "SM", "Utilities"}, {"ps4mount", "P4", "Utilities"},
        {"pkg-manager", "PK", "Utilities"}, {"backpork", "BP", "Utilities"},
        {"garlic", "GS", "Utilities"},      {"ps5save", "SV", "Utilities"},
        {"etahen", "eH", "Jailbreak"},      {"kstuff", "KS", "Jailbreak"},
        {"onionhen", "oH", "Jailbreak"},    {"ps5debug", "DB", "Jailbreak"},
        {"elfldr", "EL", "Loader"},         {"pldmgr", "PL", "Loader"},
        {"ps5-linux", "LX", "Loader"},      {"ftpsrv", "FT", "Network"},
        {"zftpd", "FT", "Network"},         {"websrv", "WS", "Network"},
        {"ps5upload", "UP", "Network"},     {"nanodns", "DN", "Network"},
        {"shsrv", "SH", "Network"},         {"klogsrv", "KL", "Network"},
        {"cheatrunner", "CR", "Cheat"},     {"ps5tm", "CC", "Network"},
        {"ps5_cooling", "CC", "Network"},   {"ps5 cooling", "CC", "Network"},
    };
    const std::string stem = payload_stem(name);
    for (const Known &k : kKnown)
        if (stem.rfind(k.stem, 0) == 0)
            return {k.label, payload_category_color(k.category)};

    PayloadTile tile;
    std::string first, second;
    std::size_t i = 0;
    while (i < stem.size() && !std::isalnum(static_cast<unsigned char>(stem[i])))
        ++i;
    if (i < stem.size())
        first = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[i])));
    for (std::size_t j = i + 1; j < stem.size(); ++j)
        if ((stem[j - 1] == '-' || stem[j - 1] == '_' || stem[j - 1] == ' ') &&
            std::isalnum(static_cast<unsigned char>(stem[j])))
        {
            second = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[j])));
            break;
        }
    if (second.empty() && i + 1 < stem.size())
        second = stem[i + 1];
    tile.label = first.empty() ? "?" : first + second;
    tile.color = payload_category_color(category);
    if (tile.color.a <= 0.0f)
        tile.color = ui::Avatar::color_of(stem.empty() ? name : stem, 0.55f, 0.55f);
    return tile;
}

inline void draw_payload_tile(ui::Canvas &canvas, const gfx::Rect &box, const PayloadTile &tile,
                              float alpha = 1.0f)
{
    canvas.list.rounded_rect(box, box.w * 0.22f, tile.color.with_alpha(tile.color.a * alpha));
    const float size = box.w * 0.4f;
    ui::text(canvas.list, canvas.fonts.semibold, tile.label, box.x + box.w * 0.5f,
             box.y + box.h * 0.5f + size * 0.36f, size, gfx::Color::rgb(0xffffff).with_alpha(alpha),
             gfx::Align::center);
}

} // namespace hui::ps5cc
