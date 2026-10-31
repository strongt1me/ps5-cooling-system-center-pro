// ps5cc - A profile picture in the console's formats (see avatar_build.hpp).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/avatar_build.hpp"

#include "third_party/cjson/cJSON.h"
#include "third_party/stb/image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace hui::ps5cc
{

namespace
{

void put32le(std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t v)
{
    out[at] = static_cast<std::uint8_t>(v);
    out[at + 1] = static_cast<std::uint8_t>(v >> 8);
    out[at + 2] = static_cast<std::uint8_t>(v >> 16);
    out[at + 3] = static_cast<std::uint8_t>(v >> 24);
}

void push32be(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

// One 4x4 block (64 bytes RGBA) into 16 bytes, as compress_dxt5_block / dxt5Block.
std::array<std::uint8_t, 16> dxt5_block(const std::uint8_t *px)
{
    std::array<std::uint8_t, 16> out{};
    int a_min = 255, a_max = 0;
    for (int i = 0; i < 16; ++i)
    {
        a_min = std::min<int>(a_min, px[i * 4 + 3]);
        a_max = std::max<int>(a_max, px[i * 4 + 3]);
    }
    const int a0 = a_max, a1 = a_min;
    std::array<int, 8> apal{a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1)
    {
        for (int i = 0; i < 6; ++i)
            apal[static_cast<std::size_t>(2 + i)] = ((6 - i) * a0 + (1 + i) * a1) / 7;
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            apal[static_cast<std::size_t>(2 + i)] = ((4 - i) * a0 + (1 + i) * a1) / 5;
        apal[6] = 0;
        apal[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i)
    {
        const int a = px[i * 4 + 3];
        int best = 0, dist = 256;
        for (int j = 0; j < 8; ++j)
        {
            const int d = std::abs(a - apal[static_cast<std::size_t>(j)]);
            if (d < dist)
            {
                dist = d;
                best = j;
            }
        }
        bits |= static_cast<std::uint64_t>(best) << (i * 3);
    }
    out[0] = static_cast<std::uint8_t>(a0);
    out[1] = static_cast<std::uint8_t>(a1);
    for (int b = 0; b < 6; ++b)
        out[static_cast<std::size_t>(2 + b)] = static_cast<std::uint8_t>(bits >> (b * 8));

    std::array<int, 3> mn{255, 255, 255}, mx{0, 0, 0};
    for (int i = 0; i < 16; ++i)
    {
        for (std::size_t c = 0; c < 3; ++c)
        {
            mn[c] = std::min<int>(mn[c], px[i * 4 + static_cast<int>(c)]);
            mx[c] = std::max<int>(mx[c], px[i * 4 + static_cast<int>(c)]);
        }
    }
    const auto to565 = [](int r, int g, int b)
    { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); };
    const auto to888 = [](int c)
    {
        return std::array<int, 3>{((c >> 11) & 0x1f) << 3, ((c >> 5) & 0x3f) << 2, (c & 0x1f) << 3};
    };
    int c0 = to565(mx[0], mx[1], mx[2]);
    int c1 = to565(mn[0], mn[1], mn[2]);
    if (c0 < c1)
        std::swap(c0, c1);
    else if (c0 == c1 && c0 < 0xffff)
        c0 += 1;
    const std::array<int, 3> p0 = to888(c0), p1 = to888(c1);
    std::array<std::array<int, 3>, 4> pal{p0, p1, std::array<int, 3>{}, std::array<int, 3>{}};
    for (std::size_t c = 0; c < 3; ++c)
    {
        pal[2][c] = (2 * p0[c] + p1[c]) / 3;
        pal[3][c] = (p0[c] + 2 * p1[c]) / 3;
    }
    std::uint32_t ci = 0;
    for (int i = 0; i < 16; ++i)
    {
        const int r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
        int best = 0;
        long dist = -1;
        for (std::size_t j = 0; j < 4; ++j)
        {
            const long dr = r - pal[j][0], dg = g - pal[j][1], db = b - pal[j][2];
            const long d = dr * dr + dg * dg + db * db;
            if (dist < 0 || d < dist)
            {
                dist = d;
                best = static_cast<int>(j);
            }
        }
        ci |= static_cast<std::uint32_t>(best) << (i * 2);
    }
    out[8] = static_cast<std::uint8_t>(c0);
    out[9] = static_cast<std::uint8_t>(c0 >> 8);
    out[10] = static_cast<std::uint8_t>(c1);
    out[11] = static_cast<std::uint8_t>(c1 >> 8);
    out[12] = static_cast<std::uint8_t>(ci);
    out[13] = static_cast<std::uint8_t>(ci >> 8);
    out[14] = static_cast<std::uint8_t>(ci >> 16);
    out[15] = static_cast<std::uint8_t>(ci >> 24);
    return out;
}

std::uint32_t crc32_of(const std::uint8_t *data, std::size_t size, std::uint32_t crc = 0)
{
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void png_chunk(std::vector<std::uint8_t> &out, const char type[4],
               const std::vector<std::uint8_t> &data)
{
    push32be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    push32be(out, crc32_of(out.data() + start, out.size() - start));
}

} // namespace

std::vector<std::uint8_t> avatar_square(const std::uint8_t *rgba, int width, int height, int size)
{
    if (rgba == nullptr || width <= 0 || height <= 0 || size <= 0)
        return {};
    const int side = std::min(width, height);
    const double ox = (width - side) / 2.0, oy = (height - side) / 2.0;
    const double scale = static_cast<double>(side) / size; // source pixels per target pixel
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size) * size * 4u);
    const auto at = [&](int x, int y, int c)
    {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return static_cast<double>(rgba[(static_cast<std::size_t>(y) * width + x) * 4 + c]);
    };
    for (int ty = 0; ty < size; ++ty)
    {
        for (int tx = 0; tx < size; ++tx)
        {
            std::array<double, 4> sum{};
            if (scale > 1.0)
            {
                // box filter over the source pixels this one covers
                const int x0 = static_cast<int>(ox + tx * scale),
                          x1 = static_cast<int>(ox + (tx + 1) * scale);
                const int y0 = static_cast<int>(oy + ty * scale),
                          y1 = static_cast<int>(oy + (ty + 1) * scale);
                int n = 0;
                for (int y = y0; y < std::max(y1, y0 + 1); ++y)
                {
                    for (int x = x0; x < std::max(x1, x0 + 1); ++x)
                    {
                        for (int c = 0; c < 4; ++c)
                            sum[static_cast<std::size_t>(c)] += at(x, y, c);
                        ++n;
                    }
                }
                for (double &v : sum)
                    v /= n;
            }
            else
            {
                const double sx = ox + (tx + 0.5) * scale - 0.5, sy = oy + (ty + 0.5) * scale - 0.5;
                const int x = static_cast<int>(std::floor(sx)),
                          y = static_cast<int>(std::floor(sy));
                const double fx = sx - x, fy = sy - y;
                for (int c = 0; c < 4; ++c)
                    sum[static_cast<std::size_t>(c)] =
                        (at(x, y, c) * (1 - fx) + at(x + 1, y, c) * fx) * (1 - fy) +
                        (at(x, y + 1, c) * (1 - fx) + at(x + 1, y + 1, c) * fx) * fy;
            }
            std::uint8_t *d = &out[(static_cast<std::size_t>(ty) * size + tx) * 4];
            for (int c = 0; c < 4; ++c)
                d[c] = static_cast<std::uint8_t>(
                    std::clamp(sum[static_cast<std::size_t>(c)] + 0.5, 0.0, 255.0));
        }
    }
    return out;
}

std::vector<std::uint8_t> avatar_dds(const std::vector<std::uint8_t> &rgba, int size)
{
    if (size <= 0 || rgba.size() != static_cast<std::size_t>(size) * size * 4u)
        return {};
    const int bw = (size + 3) / 4, bh = (size + 3) / 4;
    const std::size_t comp = static_cast<std::size_t>(bw) * bh * 16u;
    std::vector<std::uint8_t> out(128 + comp, 0);
    out[0] = 'D';
    out[1] = 'D';
    out[2] = 'S';
    out[3] = ' ';
    out[4] = 124;
    put32le(out, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000);
    put32le(out, 12, static_cast<std::uint32_t>(size)); // height
    put32le(out, 16, static_cast<std::uint32_t>(size)); // width
    put32le(out, 20, static_cast<std::uint32_t>(comp));
    out[76] = 32;
    out[80] = 4; // FOURCC
    out[84] = 'D';
    out[85] = 'X';
    out[86] = 'T';
    out[87] = '5';
    out[109] = 0x10; // TEXTURE
    std::array<std::uint8_t, 64> block{};
    std::size_t p = 128;
    for (int by = 0; by < bh; ++by)
    {
        for (int bx = 0; bx < bw; ++bx)
        {
            for (int y = 0; y < 4; ++y)
            {
                const int py = std::min(by * 4 + y, size - 1);
                for (int x = 0; x < 4; ++x)
                {
                    const int px = std::min(bx * 4 + x, size - 1);
                    std::memcpy(&block[static_cast<std::size_t>((y * 4 + x) * 4)],
                                &rgba[(static_cast<std::size_t>(py) * size + px) * 4], 4);
                }
            }
            const std::array<std::uint8_t, 16> enc = dxt5_block(block.data());
            std::memcpy(&out[p], enc.data(), enc.size());
            p += 16;
        }
    }
    return out;
}

std::vector<std::uint8_t> avatar_png(const std::vector<std::uint8_t> &rgba, int size)
{
    if (size <= 0 || rgba.size() != static_cast<std::size_t>(size) * size * 4u)
        return {};
    // the scanlines, each with filter type 0
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(size) * (size * 4u + 1u));
    for (int y = 0; y < size; ++y)
    {
        raw.push_back(0);
        const std::uint8_t *row = &rgba[static_cast<std::size_t>(y) * size * 4u];
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(size) * 4u);
    }
    // zlib with stored blocks of at most 65535 bytes, Adler-32 at the end
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t v : raw)
    {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    for (std::size_t at = 0; at < raw.size() || raw.empty();)
    {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                 raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last)
            break;
    }
    push32be(z, (b << 16) | a);

    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> ihdr;
    push32be(ihdr, static_cast<std::uint32_t>(size));
    push32be(ihdr, static_cast<std::uint32_t>(size));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8 bit, RGBA, deflate, adaptive, no interlace
    png_chunk(out, "IHDR", ihdr);
    png_chunk(out, "IDAT", z);
    png_chunk(out, "IEND", {});
    return out;
}

std::string avatar_online_json(const std::string &username)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "avatarUrl",
                            "http://static-resource.np.community.playstation.net/avatar_xl/WWS_E/"
                            "E0012_XL.png");
    cJSON_AddStringToObject(o, "firstName", username.c_str());
    cJSON_AddStringToObject(o, "lastName", "");
    cJSON_AddStringToObject(o, "pictureUrl",
                            "https://image.api.np.km.playstation.net/images/"
                            "?format=png&w=440&h=440&image=https%3A%2F%2Fkfscdn.api.np.km."
                            "playstation.net%2F00000000000008%"
                            "2F000000000000003.png&sign=blablabla019501");
    cJSON_AddStringToObject(o, "trophySummary",
                            "{\"level\":1,\"progress\":0,\"earnedTrophies\":{\"platinum\":0,"
                            "\"gold\":0,\"silver\":0,\"bronze\":0}}");
    cJSON_AddStringToObject(o, "isOfficiallyVerified", "true");
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    std::string out = text != nullptr ? text : "{}";
    cJSON_free(text);
    return out;
}

std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
avatar_files(const std::uint8_t *encoded, std::size_t size, const std::string &username)
{
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
    if (encoded == nullptr || size == 0 || size > (16u << 20))
        return files;
    int w = 0, h = 0, channels = 0;
    unsigned char *pixels =
        stbi_load_from_memory(encoded, static_cast<int>(size), &w, &h, &channels, 4);
    if (pixels == nullptr)
        return files;
    for (const int edge : kAvatarSizes)
    {
        const std::vector<std::uint8_t> square = avatar_square(pixels, w, h, edge);
        std::vector<std::uint8_t> dds = avatar_dds(square, edge);
        if (dds.empty())
        {
            stbi_image_free(pixels);
            return {};
        }
        files.emplace_back("avatar" + std::to_string(edge) + ".dds", dds);
        files.emplace_back("picture" + std::to_string(edge) + ".dds", std::move(dds));
    }
    std::vector<std::uint8_t> png = avatar_png(avatar_square(pixels, w, h, 440), 440);
    stbi_image_free(pixels);
    files.emplace_back("avatar.png", png);
    files.emplace_back("picture.png", std::move(png));
    const std::string json = avatar_online_json(username);
    files.emplace_back("online.json", std::vector<std::uint8_t>(json.begin(), json.end()));
    return files;
}

} // namespace hui::ps5cc
