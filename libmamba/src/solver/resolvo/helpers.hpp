// Copyright (c) 2023, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "mamba/core/error_handling.hpp"

#include "mamba/specs/channel.hpp"
#include "mamba/specs/match_spec.hpp"
#include "mamba/specs/package_info.hpp"

#include "mamba/solver/resolvo/parameters.hpp"
#include "mamba/solver/request.hpp"
#include "mamba/solver/solution.hpp"

/**
 * Helpers which are resolvo dependent.
 */

namespace mamba::fs
{
    class u8path;
}

namespace mamba::solver::resolvo_cpp
{

    void mamba_read_json(
        const fs::u8path& filename,
        const std::string& repo_url,
        const std::string& channel_id,
        PackageTypes types,
        bool verify_artifacts
    );

}
