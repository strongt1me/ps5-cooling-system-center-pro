// ps5-homebrew-ui - The list of UI designs, in switcher order.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/concepts.hpp"

namespace hui::app
{

std::span<const ConceptFactory> concept_registry()
{
    static constexpr ConceptFactory kFactories[] = {
        concepts::make_ps5cc,
    };
    return kFactories;
}

} // namespace hui::app
