// ps5cc - The single compilation unit for the vendored stb_image (PNG and JPEG).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game covers arrive over HTTP as PNG, developer pictures from GitHub mostly as
// JPEG; both are decoded from memory, so stdio and every other format are left out.

#define STB_IMAGE_IMPLEMENTATION 1
#define STBI_ONLY_PNG 1
#define STBI_ONLY_JPEG 1
#define STBI_NO_STDIO 1
#define STBI_NO_LINEAR 1
#define STBI_NO_HDR 1
#define STBI_NO_THREAD_LOCALS 1
#include "stb_image.inc"
