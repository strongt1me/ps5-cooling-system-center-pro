// ps5-homebrew-ui - The font set every screen draws with.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

// A loaded font and the GL texture holding its atlas.
struct FontRef
{
    const gfx::Font *font = nullptr;
    std::uint32_t texture = 0;

    float measure(std::string_view text, float size, float tracking = 0.0f) const
    {
        return font->measure(text, size, tracking);
    }
};

struct Fonts
{
    FontRef regular;  // Inter Regular: body text
    FontRef semibold; // Inter SemiBold: titles, labels, buttons
    FontRef display;  // Montserrat Medium: wide geometric headlines
    FontRef mono;     // DejaVu Sans Mono: numbers that must not jump, terminals
    FontRef pixel;    // Press Start 2P: an 8x8 bitmap face (the Pixel theme)
    FontRef hand;     // Patrick Hand: handwriting (the Sketch theme)
};

// Draws one line with its baseline at y; x is the left edge, centre or right
// edge depending on align. Returns the width.
inline float text(gfx::DrawList &list, const FontRef &font, std::string_view value, float x,
                  float baseline, float size, gfx::Color color, gfx::Align align = gfx::Align::left,
                  float tracking = 0.0f)
{
    return list.text(*font.font, font.texture, value, x, baseline, size, color, align, tracking);
}

// Upper case for small tracked labels ("CONTINUE PLAYING"): ASCII, the Latin-1 letters of the
// European languages (ä ö ü é è à ç ñ å ..., in UTF-8; ß and ÿ stay as they are) and Cyrillic.
// (app) It was ASCII only, so a German heading read "4. PRüFUNG DANACH" and a Russian one stayed
// in small letters (review of 21.11.2026).
inline std::string upper(std::string_view value)
{
    std::string result(value);
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(result[i]);
        if (c >= 'a' && c <= 'z')
        {
            result[i] = static_cast<char>(c - 'a' + 'A');
            continue;
        }
        if (i + 1 >= result.size())
            break;
        const unsigned char d = static_cast<unsigned char>(result[i + 1]);
        if (c == 0xC3 && d >= 0xA0 && d <= 0xBE && d != 0xB7) // à..þ (not ÷) -> À..Þ
            result[i + 1] = static_cast<char>(d - 0x20);
        else if (c == 0xD0 && d >= 0xB0 && d <= 0xBF) // а..п -> А..П
            result[i + 1] = static_cast<char>(d - 0x20);
        else if (c == 0xD1 && d >= 0x80 && d <= 0x8F) // р..я -> Р..Я
        {
            result[i] = static_cast<char>(0xD0);
            result[i + 1] = static_cast<char>(d + 0x20);
        }
        else if (c == 0xD1 && d >= 0x90 && d <= 0x9F) // ѐ..џ (ё among them) -> Ѐ..Џ
        {
            result[i] = static_cast<char>(0xD0);
            result[i + 1] = static_cast<char>(d - 0x10);
        }
        else
            continue;
        ++i; // the second byte is done
    }
    return result;
}

// Draws word-wrapped text from its first baseline at y, at most max_lines
// lines (the last one ends in an ellipsis if text remains). Returns the
// baseline after the last line drawn.
inline float paragraph(gfx::DrawList &list, const FontRef &font, std::string_view value, float x,
                       float y, float size, float width, float line_height, gfx::Color color,
                       int max_lines = 99, gfx::Align align = gfx::Align::left)
{
    const std::vector<std::string> lines = font.font->wrap(value, size, width);
    int drawn = 0;
    for (const std::string &line : lines)
    {
        const bool last =
            drawn + 1 == max_lines && lines.size() > static_cast<std::size_t>(max_lines);
        if (last)
            text(list, font, font.font->fit(line + " \xE2\x80\xA6", size, width), x, y, size, color,
                 align);
        else
            text(list, font, line, x, y, size, color, align);
        y += line_height;
        if (++drawn >= max_lines)
            break;
    }
    return y;
}

} // namespace hui::ui
