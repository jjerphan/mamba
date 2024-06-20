// Copyright (c) 2023, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <simdjson.h>


#include "mamba/core/output.hpp"
#include "mamba/core/util.hpp"
#include "mamba/specs/archive.hpp"
#include "mamba/specs/conda_url.hpp"
#include "mamba/util/cfile.hpp"
#include "mamba/util/random.hpp"
#include "mamba/util/string.hpp"
#include "mamba/util/type_traits.hpp"

#include "solver/helpers.hpp"
#include "solver/resolvo/helpers.hpp"

#define MAMBA_TOOL_VERSION "2.0"

namespace mamba::solver::resolvo_cpp
{

    void mamba_read_json(
        const fs::u8path& filename,
        const std::string& repo_url,
        const std::string& channel_id,
        PackageTypes package_types,
        bool verify_artifacts
    )
    {
        LOG_INFO << "Reading repodata.json file " << filename << " using mamba" << std::endl;

        auto parser = simdjson::dom::parser();
        const auto lock = LockFile(filename);
        const auto repodata = parser.load(filename);

        // An override for missing package subdir is found at the top level
        auto default_subdir = std::string();
        if (auto subdir = repodata.at_pointer("/info/subdir").get_string(); !subdir.error())
        {
            default_subdir = std::string(subdir.value_unsafe());
        }

        // Get `base_url` in case 'repodata_version': 2
        // cf. https://github.com/conda-incubator/ceps/blob/main/cep-15.md
        auto base_url = repo_url;
        if (auto repodata_version = repodata["repodata_version"].get_int64();
            !repodata_version.error())
        {
            if (repodata_version.value_unsafe() == 2)
            {
                if (auto url = repodata.at_pointer("/info/base_url").get_string(); !url.error())
                {
                    base_url = std::string(url.value_unsafe());
                }
            }
        }

        const auto parsed_url = specs::CondaURL::parse(base_url)
                                    .or_else([](specs::ParseError&& err) { throw std::move(err); })
                                    .value();

        auto signatures = std::optional<simdjson::dom::object>(std::nullopt);
        if (auto maybe_sigs = repodata["signatures"].get_object();
            !maybe_sigs.error() && verify_artifacts)
        {
            signatures = std::move(maybe_sigs).value();
        }

        if (package_types == PackageTypes::CondaOrElseTarBz2)
        {
            auto added = util::flat_set<std::string_view>();
            if (auto pkgs = repodata["packages.conda"].get_object(); !pkgs.error())
            {
                std::cout << "CondaOrElseTarBz2 (packages.conda)" << std::endl;
            }
            if (auto pkgs = repodata["packages"].get_object(); !pkgs.error())
            {
                std::cout << "CondaOrElseTarBz2 (packages)" << std::endl;
            }
        }
        else
        {
            if (auto pkgs = repodata["packages"].get_object();
                !pkgs.error() && (package_types != PackageTypes::CondaOnly))
            {
                std::cout << "CondaOrElseTarBz2 (packages)" << std::endl;
            }

            if (auto pkgs = repodata["packages.conda"].get_object();
                !pkgs.error() && (package_types != PackageTypes::TarBz2Only))
            {
                std::cout << "CondaOrElseTarBz2 (packages.conda)" << std::endl;
            }
        }
    }

}
