// ps5cc - The picture behind each area: the tile's own from assets/backdrops, or the user's from the console app.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/backdrops.hpp"

#include "core/save_file.hpp"
#include "third_party/stb/image.h"

#include <algorithm>
#include <climits>

namespace hui::ps5cc
{

namespace
{
// The tile's files are 1280 x 720 (about 70 KiB each); the user's come scaled by the browser app. Anything much
// larger would cost a long decode for nothing.
constexpr int kMaxEdge = 4096;
constexpr std::size_t kMaxFile = 4u << 20;
constexpr std::size_t kPixels = static_cast<std::size_t>(kBackdropWidth) * kBackdropHeight * 4u;

std::vector<std::uint8_t> decode(const std::uint8_t *bytes, std::size_t size)
{
    if (bytes == nullptr || size == 0 || size > static_cast<std::size_t>(INT_MAX))
        return {};
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &w, &h, &channels, 4);
    if (pixels == nullptr)
        return {};
    std::vector<std::uint8_t> out;
    if (w > 0 && h > 0 && w <= kMaxEdge && h <= kMaxEdge)
        out = cover_rgba(pixels, w, h, kBackdropWidth, kBackdropHeight);
    stbi_image_free(pixels);
    return out;
}
} // namespace

const char *backdrop_name(int area)
{
    static const char *const kNames[kBackdropAreas] = {"kuehlung", "spiele",  "dateien",  "payloads",
                                                       "system",   "profil", "protokoll"};
    return area >= 0 && area < kBackdropAreas ? kNames[area] : "";
}

int backdrop_area(const std::string &id)
{
    for (int i = 0; i < kBackdropAreas; ++i)
    {
        if (id == backdrop_name(i))
            return i;
    }
    return -1;
}

Backdrops &backdrops()
{
    static Backdrops instance;
    return instance;
}

int Backdrops::load(const std::string &assets_dir)
{
    int read = 0;
    for (int i = 0; i < kBackdropAreas; ++i)
    {
        std::string data;
        if (!save::read_file(assets_dir + "/backdrops/" + backdrop_name(i) + ".jpg", &data, kMaxFile))
            continue;
        std::vector<std::uint8_t> rgba = decode(reinterpret_cast<const std::uint8_t *>(data.data()), data.size());
        if (rgba.empty())
            continue;
        std::lock_guard<std::mutex> lock(mutex_);
        slots_[static_cast<std::size_t>(i)].own = std::move(rgba);
        ++read;
    }
    return read;
}

void Backdrops::apply(const std::vector<BackdropSetting> &settings)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const BackdropSetting &s : settings)
    {
        const int area = backdrop_area(s.id);
        if (area < 0)
            continue;
        Slot &slot = slots_[static_cast<std::size_t>(area)];
        slot.mode = s.mode;
        slot.custom = s.custom;
        slot.want_stamp = s.custom ? s.stamp : std::string();
    }
}

BackdropWant Backdrops::wanted_custom()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < kBackdropAreas; ++i)
    {
        Slot &s = slots_[static_cast<std::size_t>(i)];
        // Only what is to be seen: a picture set aside (standard or off) is fetched when it is chosen.
        if (s.mode != BackdropMode::custom || !s.custom || s.want_stamp.empty())
            continue;
        const std::string &w = s.want_stamp;
        if (w == s.shown_stamp || w == s.ready_stamp || w == s.busy_stamp || w == s.failed_stamp)
            continue;
        s.busy_stamp = w;
        return {i, w};
    }
    return {};
}

void Backdrops::deliver_custom(int area, const std::string &stamp, std::vector<std::uint8_t> rgba)
{
    if (area < 0 || area >= kBackdropAreas)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    Slot &s = slots_[static_cast<std::size_t>(area)];
    if (s.busy_stamp == stamp)
        s.busy_stamp.clear();
    if (rgba.size() != kPixels)
    {
        s.failed_stamp = stamp;
        return;
    }
    s.ready = std::move(rgba);
    s.ready_stamp = stamp;
}

void Backdrops::fail_custom(int area, const std::string &stamp)
{
    if (area < 0 || area >= kBackdropAreas)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    Slot &s = slots_[static_cast<std::size_t>(area)];
    if (s.busy_stamp == stamp)
        s.busy_stamp.clear();
    s.failed_stamp = stamp;
}

void Backdrops::upload_pending(const Create &create)
{
    // Taken out under the lock, uploaded outside it: the worker must never wait for a frame.
    int area = -1;
    bool own = false;
    std::vector<std::uint8_t> pixels;
    std::string stamp;
    std::uint32_t reuse = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < kBackdropAreas && area < 0; ++i)
        {
            Slot &s = slots_[static_cast<std::size_t>(i)];
            if (s.own_texture == 0 && !s.own.empty())
            {
                area = i;
                own = true;
                pixels.swap(s.own);
            }
            else if (!s.ready.empty())
            {
                area = i;
                pixels.swap(s.ready);
                stamp = s.ready_stamp;
                s.ready_stamp.clear();
                reuse = s.custom_texture;
            }
        }
    }
    if (area < 0)
        return;
    const std::uint32_t texture = create(reuse, kBackdropWidth, kBackdropHeight, pixels.data());
    std::lock_guard<std::mutex> lock(mutex_);
    Slot &s = slots_[static_cast<std::size_t>(area)];
    if (own)
    {
        s.own_texture = texture;
        return;
    }
    if (texture == 0)
    {
        s.failed_stamp = stamp;
        return;
    }
    s.custom_texture = texture;
    s.shown_stamp = stamp;
}

std::uint32_t Backdrops::texture(int area) const
{
    if (area < 0 || area >= kBackdropAreas)
        return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot &s = slots_[static_cast<std::size_t>(area)];
    switch (s.mode)
    {
    case BackdropMode::off:
        return 0;
    case BackdropMode::custom:
        // The tile's own picture while the user's is on its way (or could not be read).
        if (s.custom && s.custom_texture != 0)
            return s.custom_texture;
        return s.own_texture;
    case BackdropMode::standard:
        break;
    }
    return s.own_texture;
}

BackdropMode Backdrops::mode(int area) const
{
    if (area < 0 || area >= kBackdropAreas)
        return BackdropMode::standard;
    std::lock_guard<std::mutex> lock(mutex_);
    return slots_[static_cast<std::size_t>(area)].mode;
}

std::vector<std::uint8_t> decode_backdrop(const std::uint8_t *bytes, std::size_t size)
{
    return decode(bytes, size);
}

std::vector<std::uint8_t> cover_rgba(const std::uint8_t *rgba, int src_w, int src_h, int width, int height)
{
    std::vector<std::uint8_t> out;
    if (rgba == nullptr || src_w <= 0 || src_h <= 0 || width <= 0 || height <= 0)
        return out;
    // The largest centred part with the target's aspect (64-bit: 4096 x 4096 times 1280 overflows an int).
    int crop_w = src_w;
    int crop_h = static_cast<int>(static_cast<std::int64_t>(src_w) * height / width);
    if (crop_h > src_h)
    {
        crop_h = src_h;
        crop_w = static_cast<int>(static_cast<std::int64_t>(src_h) * width / height);
    }
    crop_w = std::max(1, crop_w);
    crop_h = std::max(1, crop_h);
    const int left = (src_w - crop_w) / 2;
    const int top = (src_h - crop_h) / 2;
    out.resize(static_cast<std::size_t>(width) * height * 4u);
    for (int dy = 0; dy < height; ++dy)
    {
        const int y0 = top + static_cast<int>(static_cast<std::int64_t>(dy) * crop_h / height);
        const int y1 = std::max(y0 + 1, top + static_cast<int>(static_cast<std::int64_t>(dy + 1) * crop_h / height));
        for (int dx = 0; dx < width; ++dx)
        {
            const int x0 = left + static_cast<int>(static_cast<std::int64_t>(dx) * crop_w / width);
            const int x1 =
                std::max(x0 + 1, left + static_cast<int>(static_cast<std::int64_t>(dx + 1) * crop_w / width));
            unsigned sum[4] = {0, 0, 0, 0};
            unsigned count = 0;
            for (int y = y0; y < y1; ++y)
            {
                const std::uint8_t *p = rgba + (static_cast<std::size_t>(y) * src_w + x0) * 4u;
                for (int x = x0; x < x1; ++x, p += 4)
                {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    sum[3] += p[3];
                    ++count;
                }
            }
            std::uint8_t *d = out.data() + (static_cast<std::size_t>(dy) * width + dx) * 4u;
            for (int c = 0; c < 4; ++c)
                d[c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);
        }
    }
    return out;
}

} // namespace hui::ps5cc
