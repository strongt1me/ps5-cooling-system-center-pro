// ps5cc - Game covers: fetched and decoded off the frame, uploaded on the GL thread.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The worker asks next_wanted() for a cover the screen has none of, fetches the
// PNG, hands the bytes to decode_cover() and the result to deliver() (or calls
// fail()). The main loop calls upload_pending() once a frame: it turns at most a
// couple of decoded pictures into textures, so a burst of arrivals never costs
// one long frame. The screen only reads texture_of(). Nothing here touches GL
// itself; the texture calls come in as functions, so the tests need no context.

#pragma once

#include "ps5cc/data.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace hui::ps5cc
{

// The edge of a stored cover. The list draws it at 60 px and the details panel
// at 150 px, so 160 keeps both sharp at 1080p for 100 KiB a cover.
constexpr int kCoverSize = 160;
// At most this many textures (about 40 MiB): later games keep their initials.
constexpr std::size_t kCoverLimit = 400;

struct CoverWant
{
    std::string id;
    std::string path; // what to GET, "" when nothing is wanted
};

struct CoverShown
{
    std::uint32_t texture = 0; // 0: not loaded, draw the initials
    float alpha = 0.0f;        // 0..1 while it fades in
};

class Covers
{
  public:
    // Worker side.
    CoverWant next_wanted(const std::vector<Game> &games);
    void deliver(const std::string &id, std::vector<std::uint8_t> rgba);
    void fail(const std::string &id);
    // A new library: failed covers may be tried again.
    void forget_failures();
    // The screen's focused game is fetched before the others.
    void prefer(const std::string &id);

    // GL side, once a frame. create(w, h, rgba) returns a texture name.
    int upload_pending(const std::function<std::uint32_t(int, int, const std::uint8_t *)> &create,
                       int max_per_frame, float dt);

    // Screen side.
    CoverShown texture_of(const std::string &id) const;

    std::size_t loaded() const;
    std::size_t pending() const;
    bool failed(const std::string &id) const;

  private:
    struct Ready
    {
        std::string id;
        std::vector<std::uint8_t> rgba;
    };
    mutable std::mutex mutex_;
    std::map<std::string, CoverShown> textures_;
    std::vector<Ready> ready_;
    std::set<std::string> busy_;   // fetched or decoded, not uploaded yet
    std::set<std::string> failed_; // not tried again until forget_failures()
    std::string preferred_;
};

Covers &covers();

// PNG bytes to a kCoverSize square of RGBA (centre crop, box filter); empty
// when the bytes are not a picture.
std::vector<std::uint8_t> decode_cover(const std::uint8_t *png, std::size_t size);

// Box-filters the centred square of an RGBA picture down (or up) to size x size.
std::vector<std::uint8_t> square_rgba(const std::uint8_t *rgba, int width, int height, int size);

} // namespace hui::ps5cc
