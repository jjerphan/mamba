// Copyright (c) 2024, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <iostream>

#include <doctest/doctest.h>

#include "mamba/fs/filesystem.hpp"
#include "mamba/specs/channel.hpp"
#include "mamba/specs/package_info.hpp"

#include "solver/resolvo/helpers.hpp"

#include "mambatests.hpp"

using namespace mamba;
using namespace mamba::solver;

TEST_SUITE("solver::solution")
{
    using PackageInfo = specs::PackageInfo;

    TEST_CASE("Resolvo_load") {
        std::cout << "Loading repodata.json" << std::endl;
        solver::resolvo_cpp::mamba_read_json(
            fs::u8path("/tmp/repodata.json"),
            "https://repo.anaconda.com/pkgs/main",
            "main",
            solver::resolvo_cpp::PackageTypes::CondaOrElseTarBz2,
            true
        );
    }
}