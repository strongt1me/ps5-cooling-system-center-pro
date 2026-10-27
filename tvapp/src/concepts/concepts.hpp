// ps5-homebrew-ui - Factories of every UI design in the app.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/concept.hpp"

#include <memory>

namespace hui::concepts
{

// The one screen of PS5 Cooling & System Center (src/concepts/ps5cc.cpp). The
// kit's sample designs were removed (see ORIGIN.md).
std::unique_ptr<app::Concept> make_ps5cc(app::Context &context);

} // namespace hui::concepts
