// Copyright (c) 2023, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "mamba/core/error_handling.hpp"
#include "mamba/solver/resolvo/parameters.hpp"
#include "mamba/specs/channel.hpp"
#include "mamba/specs/package_info.hpp"
#include "mamba/util/loop_control.hpp"

namespace mamba
{
    namespace fs
    {
        class u8path;
    }

    namespace specs
    {
        class MatchSpec;
    }
}

enum class LogLevel
{
    Debug,
    Warning,
    Error,
    Fatal,
};


// TODO: probably introduce other information from parameters which are present in parameters.hpp.

namespace mamba::solver::resolvo_cpp
{
    class Solver;
    class UnSolvable;

    class Database
    {
    public:

        using logger_type = std::function<void(LogLevel, std::string_view)>;

        explicit Database(specs::ChannelResolveParams channel_params);
        Database(const Database&) = delete;
        Database(Database&&);

        ~Database();

        auto operator=(const Database&) -> Database& = delete;
        auto operator=(Database&&) -> Database&;

        [[nodiscard]] auto channel_params() const -> const specs::ChannelResolveParams&;

        void add_repo_from_repodata_json(
            const fs::u8path& path,
            std::string_view url,
            const std::string& channel_id
        );

        template <typename Iter>
        void add_repo_from_packages(
            Iter first_package,
            Iter last_package,
            std::string_view name = ""
        );

        template <typename Range>
        void add_repo_from_packages(
            const Range& packages,
            std::string_view name = ""
        );

        struct DatabaseImpl;

        std::unique_ptr<DatabaseImpl> m_data;

    };

}
