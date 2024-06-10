#pragma once
#include <resolvo.h>
#include <resolvo_pool.h>

#include "mamba/solver/libsolv/parameters.hpp"

#include <sstream>
#include <vector>

namespace mamba::solver::libsolv
{
    class RepoInfo;

    enum class PipAsPythonDependency : bool
    {
        No = false,
        Yes = true,
    };

}

namespace mamba::solver::resolvo_cpp
{
    /**
     * A single candidate for a package.
     */
    struct Candidate {
        resolvo::NameId name;
        uint32_t version;
        resolvo::Dependencies dependencies;
    };

    /**
     * A requirement for a package.
     */
    struct Requirement {
        resolvo::NameId name;
        uint32_t version_start;
        uint32_t version_end;
    };

    /**
     * A simple database of packages that also implements resolvos DependencyProvider interface.
     */
    struct PackageDatabase : public resolvo::DependencyProvider {
        resolvo::Pool<resolvo::NameId, resolvo::String> names;
        resolvo::Pool<resolvo::StringId, resolvo::String> strings;
        std::vector<Candidate> candidates;
        std::vector<Requirement> requirements;

        /**
         * Allocates a new requirement and return the id of the requirement.
         */
        resolvo::VersionSetId alloc_requirement(std::string_view package, uint32_t version_start,
                                                uint32_t version_end);

        /**
         * Allocates a new candidate and return the id of the candidate.
         */
        resolvo::SolvableId alloc_candidate(std::string_view name, uint32_t version,
                                            resolvo::Dependencies dependencies);

        resolvo::String display_name(resolvo::NameId name) override;

        resolvo::String display_solvable(resolvo::SolvableId solvable);

        resolvo::String display_merged_solvables(
                resolvo::Slice<resolvo::SolvableId> solvables) override;

        resolvo::String display_version_set(resolvo::VersionSetId version_set) override;

        resolvo::String display_string(resolvo::StringId string_id) override;

        resolvo::NameId version_set_name(resolvo::VersionSetId version_set_id) override;

        resolvo::NameId solvable_name(resolvo::SolvableId solvable_id) override;

        resolvo::Candidates get_candidates(resolvo::NameId package) override;

        void sort_candidates(resolvo::Slice<resolvo::SolvableId> solvables) override;

        resolvo::Vector<resolvo::SolvableId> filter_candidates(
                resolvo::Slice<resolvo::SolvableId> solvables,
                resolvo::VersionSetId version_set_id,
                bool inverse) override;

        resolvo::Dependencies get_dependencies(resolvo::SolvableId solvable) override;


        template <typename Iter>
        auto add_repo_from_packages(
                Iter first_package,
                Iter last_package,
                std::string_view name,
                mamba::solver::libsolv::PipAsPythonDependency add
        ) -> mamba::solver::libsolv::RepoInfo
        {
            auto repo = add_repo_from_packages_impl_pre(name);
            for (; first_package != last_package; ++first_package)
            {
                add_repo_from_packages_impl_loop(repo, *first_package);
            }
            add_repo_from_packages_impl_post(repo, add);
            return repo;
        }

    };

}  // namespace mamba::solver::resolvo_cpp