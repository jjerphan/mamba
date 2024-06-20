// Copyright (c) 2023, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <exception>
#include <iostream>
#include <limits>
#include <string_view>

#include <fmt/format.h>
#include <solv/evr.h>
#include <solv/selection.h>
#include <solv/solver.h>
#include <spdlog/spdlog.h>

#include "mamba/fs/filesystem.hpp"
#include "mamba/solver/resolvo/database.hpp"
#include "mamba/solver/resolvo/repo_info.hpp"
#include "mamba/solver/resolvo/parameters.hpp"
#include "mamba/specs/match_spec.hpp"
#include "mamba/util/random.hpp"

#include "solver/resolvo/helpers.hpp"

namespace mamba::solver::resolvo_cpp
{

    void add_repo_from_repodata_json(
        const fs::u8path& path,
        std::string_view url,
        const std::string& channel_id,
        PipAsPythonDependency add = PipAsPythonDependency::No,
        PackageTypes package_types = PackageTypes::CondaOrElseTarBz2,
        VerifyPackages verify_packages = VerifyPackages::No,
        RepodataParser parser = RepodataParser::Mamba
    ) {
        const auto verify_artifacts = static_cast<bool>(verify_packages);

        if (!fs::exists(path))
        {
            throw std::runtime_error(fmt::format(R"(File "{}" does not exist)", path));
        }

        return mamba_read_json(
            path,
            std::string(url),
            channel_id,
            package_types,
            verify_artifacts
        );
    }

    template <typename Iter>
    void add_repo_from_packages(
        Iter first_package,
        Iter last_package,
        std::string_view name = ""
    ) {

    }

    template <typename Range>
    void add_repo_from_packages(
        const Range& packages,
        std::string_view name = ""
    ) {

    }
}
