// ps5-homebrew-ui - Baked SDF font: loading, measuring and glyph layout.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/font_format.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::gfx
{

// One positioned glyph quad in output pixels, with its atlas UV rectangle.
struct GlyphQuad
{
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    std::uint32_t texture = 0; // the fallback face's atlas; 0: the font's own
    float range = 0.0f;        // distance-field spread of the face it came from
};

enum class Align : std::uint8_t
{
    left,
    center,
    right,
};

// Rewrites every text a Font measures, fits, lays out or wraps, before it does
// so: the hook a translation hangs on (one place, so measuring and drawing
// always see the same words). apply() returns the text to use; a view it
// returns must stay valid until the filter's owner says otherwise (the frame).
// Used from the render thread only.
class TextFilter
{
  public:
    virtual ~TextFilter() = default;
    virtual std::string_view apply(std::string_view text) = 0;
};

// The filter every Font uses from now on; nullptr draws text as given. Call it again when the
// filter starts rewriting differently (a new dictionary): it counts up text_filter_generation().
void set_text_filter(TextFilter *filter);
// Changes with every set_text_filter(). Code that keeps wrapped or shortened text compares it, so
// text laid out before a dictionary arrived is laid out again.
unsigned text_filter_generation();
// The text as the filter rewrites it. For code that wraps or shortens a text itself, word by word
// or character by character, before any of it reaches a Font: it must start from the rewritten
// text, or it cuts the original.
std::string_view filter_text(std::string_view text);

// While one lives, Fonts take text as given. For the loops that cut or wrap a text filter_text()
// already rewrote: every piece they measure would otherwise go through the filter again, cost a
// lookup each and could be rewritten a second time (a translation that is itself a German word).
class RawText
{
  public:
    RawText();
    ~RawText();
    RawText(const RawText &) = delete;
    RawText &operator=(const RawText &) = delete;
};

class Font
{
  public:
    // Parses a .huifont blob; returns false (and keeps an error) if invalid.
    bool load(std::string_view data);
    const std::string &error() const
    {
        return error_;
    }

    // Width in pixels of one line of UTF-8 text at the given pixel size.
    // tracking is extra space between glyphs, in pixels.
    float measure(std::string_view text, float size, float tracking = 0.0f) const;
    // text, or its longest prefix plus an ellipsis that fits max_width.
    std::string fit(std::string_view text, float size, float max_width,
                    float tracking = 0.0f) const;
    float ascent(float size) const
    {
        return header_.ascent * size / header_.pixel_size;
    }
    float descent(float size) const
    {
        return -header_.descent * size / header_.pixel_size;
    }
    float line_height(float size) const
    {
        return (header_.ascent - header_.descent + header_.line_gap) * size / header_.pixel_size;
    }
    // Distance-field spread in output pixels at a size (for shader anti-aliasing).
    float sdf_range(float size) const
    {
        return header_.sdf_range * size / header_.pixel_size;
    }

    // Lays out one line with its baseline at y. x is the left edge, centre or
    // right edge depending on align. Appends to quads; returns the advance.
    float layout(std::string_view text, float x, float y, float size, Align align,
                 std::vector<GlyphQuad> &quads, float tracking = 0.0f) const;

    // Breaks text into lines no wider than max_width, at word boundaries; a
    // word wider than a line (a long address, text in a script that writes no
    // spaces) breaks between code points.
    std::vector<std::string> wrap(std::string_view text, float size, float max_width) const;

    // Another face for the code points this one lacks: a font for other
    // scripts shared by every weight. Faces are asked in the order they were
    // added; `texture` is what create_font_texture returned for that face.
    // The caller keeps the face alive for as long as this font draws.
    void add_fallback(const Font *face, std::uint32_t texture)
    {
        if (face != nullptr && face != this)
            fallbacks_.push_back({face, texture});
    }
    void clear_fallbacks()
    {
        fallbacks_.clear();
    }

    std::uint16_t atlas_width() const
    {
        return header_.atlas_width;
    }
    std::uint16_t atlas_height() const
    {
        return header_.atlas_height;
    }
    const std::vector<std::uint8_t> &atlas() const
    {
        return atlas_;
    }
    // True when this face or one of its fallbacks draws the code point.
    bool has_glyph(std::uint32_t codepoint) const;

  private:
    struct Fallback
    {
        const Font *face;
        std::uint32_t texture;
    };
    // A glyph and the face it is in.
    struct Resolved
    {
        const Font *face = nullptr;
        const font_format::Glyph *glyph = nullptr;
        std::uint32_t texture = 0;
    };

    // The work behind the public calls, on text the filter has already seen
    // (they call each other, and a second pass through the filter could
    // rewrite a piece of an already rewritten text).
    float measure_raw(std::string_view text, float size, float tracking) const;
    std::string fit_raw(std::string_view text, float size, float max_width, float tracking) const;
    float layout_raw(std::string_view text, float x, float y, float size, Align align,
                     std::vector<GlyphQuad> &quads, float tracking) const;
    std::vector<std::string> wrap_raw(std::string_view text, float size, float max_width) const;

    const font_format::Glyph *find(std::uint32_t codepoint) const;
    float kern(std::uint32_t first, std::uint32_t second) const;
    // The glyph for *codepoint here or in a fallback; a face that has none
    // draws '?' (and *codepoint becomes '?'). glyph is null when even that is
    // missing.
    Resolved resolve(std::uint32_t *codepoint) const;

    font_format::Header header_{};
    std::vector<font_format::Glyph> glyphs_;
    std::vector<font_format::Kern> kerns_;
    std::vector<std::uint8_t> atlas_;
    std::vector<Fallback> fallbacks_;
    std::string error_;
};

// Decodes the next UTF-8 codepoint from text at *index (advancing it);
// invalid bytes decode as U+FFFD.
std::uint32_t next_codepoint(std::string_view text, std::size_t *index);

} // namespace hui::gfx
