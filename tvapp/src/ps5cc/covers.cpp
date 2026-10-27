// ps5cc - Game covers: fetched and decoded off the frame, uploaded on the GL thread.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps5cc/covers.hpp"

#include "third_party/stb/image.h"

#include <algorithm>
#include <climits>

namespace hui::ps5cc
{

namespace
{
constexpr float kFadeSeconds = 0.25f;
} // namespace

Covers &covers()
{
    static Covers instance;
    return instance;
}

CoverWant Covers::next_wanted(const std::vector<Game> &games)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (textures_.size() + busy_.size() >= kCoverLimit)
        return {};
    const auto open = [&](const Game &g)
    { return !g.cover.empty() && !textures_.contains(g.id) && !busy_.contains(g.id) && !failed_.contains(g.id); };
    const Game *pick = nullptr;
    if (!preferred_.empty())
    {
        for (const Game &g : games)
        {
            if (g.id == preferred_ && open(g))
                pick = &g;
        }
    }
    if (pick == nullptr)
    {
        for (const Game &g : games)
        {
            if (open(g))
            {
                pick = &g;
                break;
            }
        }
    }
    if (pick == nullptr)
        return {};
    busy_.insert(pick->id);
    return {pick->id, pick->cover};
}

void Covers::deliver(const std::string &id, std::vector<std::uint8_t> rgba)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (rgba.size() != static_cast<std::size_t>(kCoverSize) * kCoverSize * 4u)
    {
        busy_.erase(id);
        failed_.insert(id);
        return;
    }
    ready_.push_back({id, std::move(rgba)});
}

void Covers::fail(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    busy_.erase(id);
    failed_.insert(id);
}

void Covers::forget_failures()
{
    std::lock_guard<std::mutex> lock(mutex_);
    failed_.clear();
}

void Covers::prefer(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    preferred_ = id;
}

int Covers::upload_pending(const std::function<std::uint32_t(int, int, const std::uint8_t *)> &create,
                           int max_per_frame, float dt)
{
    std::vector<Ready> take;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &[id, shown] : textures_)
            shown.alpha = std::min(1.0f, shown.alpha + dt / kFadeSeconds);
        const std::size_t n = std::min(ready_.size(), static_cast<std::size_t>(std::max(0, max_per_frame)));
        take.assign(std::make_move_iterator(ready_.begin()),
                    std::make_move_iterator(ready_.begin() + static_cast<std::ptrdiff_t>(n)));
        ready_.erase(ready_.begin(), ready_.begin() + static_cast<std::ptrdiff_t>(n));
    }
    // The GL calls happen outside the lock: the worker must never wait for a frame.
    int uploaded = 0;
    for (Ready &r : take)
    {
        const std::uint32_t texture = create(kCoverSize, kCoverSize, r.rgba.data());
        std::lock_guard<std::mutex> lock(mutex_);
        busy_.erase(r.id);
        if (texture == 0)
        {
            failed_.insert(r.id);
            continue;
        }
        textures_[r.id] = {texture, 0.0f};
        ++uploaded;
    }
    return uploaded;
}

CoverShown Covers::texture_of(const std::string &id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textures_.find(id);
    return it == textures_.end() ? CoverShown{} : it->second;
}

std::size_t Covers::loaded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return textures_.size();
}

std::size_t Covers::pending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_.size();
}

bool Covers::failed(const std::string &id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_.contains(id);
}

std::vector<std::uint8_t> square_rgba(const std::uint8_t *rgba, int width, int height, int size)
{
    std::vector<std::uint8_t> out;
    if (rgba == nullptr || width <= 0 || height <= 0 || size <= 0)
        return out;
    const int side = std::min(width, height);
    const int left = (width - side) / 2;
    const int top = (height - side) / 2;
    out.resize(static_cast<std::size_t>(size) * size * 4u);
    for (int dy = 0; dy < size; ++dy)
    {
        const int y0 = top + dy * side / size;
        const int y1 = std::max(y0 + 1, top + (dy + 1) * side / size);
        for (int dx = 0; dx < size; ++dx)
        {
            const int x0 = left + dx * side / size;
            const int x1 = std::max(x0 + 1, left + (dx + 1) * side / size);
            unsigned sum[4] = {0, 0, 0, 0};
            unsigned count = 0;
            for (int y = y0; y < y1; ++y)
            {
                const std::uint8_t *p = rgba + (static_cast<std::size_t>(y) * width + x0) * 4u;
                for (int x = x0; x < x1; ++x, p += 4)
                {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    sum[3] += p[3];
                    ++count;
                }
            }
            std::uint8_t *d = out.data() + (static_cast<std::size_t>(dy) * size + dx) * 4u;
            for (int c = 0; c < 4; ++c)
                d[c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);
        }
    }
    return out;
}

std::vector<std::uint8_t> decode_cover(const std::uint8_t *png, std::size_t size)
{
    if (png == nullptr || size == 0 || size > static_cast<std::size_t>(INT_MAX))
        return {};
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(png, static_cast<int>(size), &w, &h, &channels, 4);
    if (pixels == nullptr)
        return {};
    // Anything wildly out of shape is not a cover; it would also cost a long decode.
    std::vector<std::uint8_t> out;
    if (w > 0 && h > 0 && w <= 4096 && h <= 4096)
        out = square_rgba(pixels, w, h, kCoverSize);
    stbi_image_free(pixels);
    return out;
}

} // namespace hui::ps5cc
