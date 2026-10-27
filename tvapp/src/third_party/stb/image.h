// ps5cc - The part of stb_image the app uses (implementation in image.c).
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y,
                                     int *channels_in_file, int desired_channels);
void stbi_image_free(void *retval_from_stbi_load);

#ifdef __cplusplus
}
#endif
