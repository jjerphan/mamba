// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <string_view>
#include <type_traits>

#include "mamba/solver/resolvo/repo_info.hpp"
#include "mamba/solver/resolvo/parameters.hpp"

namespace mamba::solver::resolvo_cpp
{
    RepoInfo::RepoInfo(::Repo* repo)
        : m_ptr(repo)
    {
    }

    auto RepoInfo::name() const -> std::string_view
    {

    }

    auto RepoInfo::priority() const -> Priorities
    {

    }

    auto RepoInfo::package_count() const -> std::size_t
    {
    }

    auto RepoInfo::id() const -> RepoId
    {

    }

    auto operator==(RepoInfo lhs, RepoInfo rhs) -> bool
    {
        return lhs.m_ptr == rhs.m_ptr;
    }

    auto operator!=(RepoInfo lhs, RepoInfo rhs) -> bool
    {
        return !(rhs == lhs);
    }
}  // namespace mamba
