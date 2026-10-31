// ps5cc - A profile picture in the console's formats: DXT5 DDS at four sizes and a PNG.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The web app builds these files in the browser (buildAvatarFiles in web/app.js) and sends them to
// POST /api/v1/profile/avatar/file; the fullscreen app does the same here, without OpenGL, from a
// picture the console app serves (the built-in ones in /avatars). Ported from that code: the same
// centre crop, the same DXT5 block encoder and the same 128-byte DDS header, which the console
// checks.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hui::ps5cc
{

// The edges the console wants as DDS (avatar<N>.dds and picture<N>.dds); the PNG is 440.
inline constexpr int kAvatarSizes[] = {64, 128, 260, 440};

// The largest centred square of an RGBA picture, scaled to size x size (box filter down, bilinear
// up). Empty on bad input.
std::vector<std::uint8_t> avatar_square(const std::uint8_t *rgba, int width, int height, int size);

// A DXT5 DDS of a square RGBA picture.
std::vector<std::uint8_t> avatar_dds(const std::vector<std::uint8_t> &rgba, int size);

// A PNG of a square RGBA picture (zlib "stored" blocks: no compressor needed, about 760 KB at 440).
std::vector<std::uint8_t> avatar_png(const std::vector<std::uint8_t> &rgba, int size);

// online.json as the web app writes it, with the user's name.
std::string avatar_online_json(const std::string &username);

// All files for one picture: name and bytes, in the order the web app sends them. Empty when the
// picture could not be decoded.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
avatar_files(const std::uint8_t *encoded, std::size_t size, const std::string &username);

} // namespace hui::ps5cc
