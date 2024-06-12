#include "mamba/solver/resolvo/solver.hpp"

namespace mamba::solver::resolvo_cpp
{

    resolvo::VersionSetId
    PackageDatabase::alloc_requirement(std::string_view package, uint32_t version_start,
                                            uint32_t version_end) {
        auto name_id = names.alloc(std::move(package));
        auto id = resolvo::VersionSetId{static_cast<uint32_t>(requirements.size())};
        requirements.push_back(Requirement{name_id, version_start, version_end});
        return id;
    }


    resolvo::SolvableId
    PackageDatabase::alloc_candidate(std::string_view name, uint32_t version, resolvo::Dependencies dependencies)
    {
        auto name_id = names.alloc(std::move(name));
        auto id = resolvo::SolvableId{ static_cast<uint32_t>(candidates.size()) };
        candidates.push_back(Candidate{ name_id, version, dependencies });
        return id;
    }

    resolvo::String
    PackageDatabase::display_name(resolvo::NameId name)
    {
        return resolvo::String(names[name]);
    }

    resolvo::String
    PackageDatabase::display_solvable(resolvo::SolvableId solvable)
    {
        const auto& candidate = candidates[solvable.id];
        std::stringstream ss;
        ss << names[candidate.name] << "=" << candidate.version;
        return resolvo::String(ss.str());
    }

    resolvo::String
    PackageDatabase::display_merged_solvables(resolvo::Slice<resolvo::SolvableId> solvables)
    {
        if (solvables.empty())
        {
            return resolvo::String();
        }

        std::stringstream ss;
        ss << names[candidates[solvables[0].id].name];

        bool first = true;
        for (const auto& solvable : solvables)
        {
            if (!first)
            {
                ss << " | ";
                first = false;
            }

            ss << candidates[solvable.id].version;
        }

        return resolvo::String(ss.str());
    }

    resolvo::String
    PackageDatabase::display_version_set(resolvo::VersionSetId version_set)
    {
        const auto& req = requirements[version_set.id];
        std::stringstream ss;
        ss << req.version_start << ".." << req.version_end;
        return resolvo::String(ss.str());
    }

    resolvo::String
    PackageDatabase::display_string(resolvo::StringId string_id)
    {
        return strings[string_id];
    }

    resolvo::NameId
    PackageDatabase::version_set_name(resolvo::VersionSetId version_set_id)
    {
        return requirements[version_set_id.id].name;
    }

    resolvo::NameId
    PackageDatabase::solvable_name(resolvo::SolvableId solvable_id)
    {
        return candidates[solvable_id.id].name;
    }

    resolvo::Candidates
    PackageDatabase::get_candidates(resolvo::NameId package)
    {
        resolvo::Candidates result;

        for (uint32_t i = 0; i < static_cast<uint32_t>(candidates.size()); ++i)
        {
            const auto& candidate = candidates[i];
            if (candidate.name != package)
            {
                continue;
            }
            result.candidates.push_back(resolvo::SolvableId{ i });
            result.hint_dependencies_available.push_back(resolvo::SolvableId{ i });
        }

        result.favored = nullptr;
        result.locked = nullptr;

        return result;
    }

    void
    PackageDatabase::sort_candidates(resolvo::Slice<resolvo::SolvableId> solvables)
    {
        std::sort(
            solvables.begin(),
            solvables.end(),
            [&](resolvo::SolvableId a, resolvo::SolvableId b)
            { return candidates[a.id].version > candidates[b.id].version; }
        );
    }

    resolvo::Vector<resolvo::SolvableId>
    PackageDatabase::filter_candidates(
        resolvo::Slice<resolvo::SolvableId> solvables,
        resolvo::VersionSetId version_set_id,
        bool inverse
    )
    {
        resolvo::Vector<resolvo::SolvableId> result;
        const auto& requirement = requirements[version_set_id.id];
        for (auto solvable : solvables)
        {
            const auto& candidate = candidates[solvable.id];
            // TODO adapt the comparison of versions, here.
            bool matches = candidate.version >= requirement.version_start
                           && candidate.version < requirement.version_end;
            if (matches != inverse)
            {
                result.push_back(solvable);
            }
        }
        return result;
    }

    resolvo::Dependencies
    PackageDatabase::get_dependencies(resolvo::SolvableId solvable)
    {
        const auto& candidate = candidates[solvable.id];
        return candidate.dependencies;
    }

}  // namespace mamba::solver::resolvo_cpp