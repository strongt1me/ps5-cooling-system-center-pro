// ps5cc - The picture behind each area (Kühlung, Spiele, ...): the tile's own or the user's.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// load() reads and decodes the tile's own JPEGs (assets/backdrops) once at start-up. The user can upload a picture
// per area in the browser app; the console app keeps it, and its list says per area which picture to show
// (GET /api/v1/backdrops): the worker hands that to apply(), asks wanted_custom() which picture to fetch, decodes it
// with decode_backdrop() and passes the pixels to deliver_custom(). upload_pending() turns one decoded picture a frame
// into a texture (a changed picture reuses its texture). Nothing here touches GL itself: the texture calls come in as
// functions, so the tests need no context. A missing file just leaves that area on the plain gradient.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace hui::ps5cc
{

constexpr int kBackdropAreas = 7; // the sidebar's areas, in its order
// What a picture is scaled to (the tile's own are this size): about 3.5 MiB a texture.
constexpr int kBackdropWidth = 1280;
constexpr int kBackdropHeight = 720;

enum class BackdropMode : std::uint8_t
{
    standard, // the tile's own picture
    custom,   // the user's, uploaded in the browser app
    off,      // none: the plain gradient
};

// One area as the console app reports it.
struct BackdropSetting
{
    std::string id;    // "kuehlung" ... "protokoll"
    std::string label; // "Kühlung"
    BackdropMode mode = BackdropMode::standard;
    bool custom = false; // the user's picture is there
    std::string stamp;   // changes when it is replaced
};

struct BackdropWant
{
    int area = -1; // -1: nothing to fetch
    std::string stamp;
};

class Backdrops
{
  public:
    // assets_dir/backdrops/<name>.jpg for every area; returns how many were read.
    int load(const std::string &assets_dir);

    // Worker side.
    void apply(const std::vector<BackdropSetting> &settings);
    BackdropWant wanted_custom();
    void deliver_custom(int area, const std::string &stamp, std::vector<std::uint8_t> rgba);
    void fail_custom(int area, const std::string &stamp);

    // GL side, once a frame: at most one picture becomes a texture. create(reuse, w, h, rgba) fills `reuse` when it
    // is not 0 and returns it, or makes a new texture.
    using Create = std::function<std::uint32_t(std::uint32_t reuse, int, int, const std::uint8_t *)>;
    void upload_pending(const Create &create);

    // Screen side: the texture to draw for an area, 0 for none (yet).
    std::uint32_t texture(int area) const;
    BackdropMode mode(int area) const;

  private:
    struct Slot
    {
        std::vector<std::uint8_t> own; // the tile's picture, until it is a texture
        std::uint32_t own_texture = 0;
        BackdropMode mode = BackdropMode::standard;
        bool custom = false;
        std::string want_stamp;  // the user's picture the app has
        std::string busy_stamp;  // being fetched or decoded
        std::string ready_stamp; // decoded, waiting for the upload
        std::string shown_stamp; // in custom_texture
        std::string failed_stamp;
        std::vector<std::uint8_t> ready;
        std::uint32_t custom_texture = 0;
    };
    mutable std::mutex mutex_;
    std::array<Slot, kBackdropAreas> slots_{};
};

Backdrops &backdrops();

// The file name of an area's picture (without .jpg), also its id in the console app; "" out of range.
const char *backdrop_name(int area);
// The area of an id, -1 for none.
int backdrop_area(const std::string &id);

// JPEG or PNG bytes to kBackdropWidth x kBackdropHeight RGBA (scaled to cover, centred); empty when they are no
// picture.
std::vector<std::uint8_t> decode_backdrop(const std::uint8_t *bytes, std::size_t size);
// Box-filters the centred part of an RGBA picture with the aspect of width x height down (or up) to that size.
std::vector<std::uint8_t> cover_rgba(const std::uint8_t *rgba, int src_w, int src_h, int width, int height);

} // namespace hui::ps5cc
