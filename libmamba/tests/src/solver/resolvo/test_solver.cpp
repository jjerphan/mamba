// Copyright (c) 2024, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <iostream>

#include <doctest/doctest.h>
#include <resolvo/resolvo.h>
#include <resolvo/resolvo_dependency_provider.h>
#include <resolvo/resolvo_pool.h>

#include "mamba/specs/channel.hpp"
#include "mamba/specs/package_info.hpp"

#include "mambatests.hpp"


using namespace mamba;
using namespace mamba::specs;
using namespace mamba::solver;

using namespace resolvo;

template <>
struct std::hash<VersionSetId> {
    std::size_t operator()(const VersionSetId& id) const {
        return std::hash<uint32_t>{}(id.id);
    }
};

template <>
struct std::hash<SolvableId> {
    std::size_t operator()(const SolvableId& id) const {
        return std::hash<uint32_t>{}(id.id);
    }
};

template <>
struct std::hash<NameId> {
    std::size_t operator()(const NameId& id) const {
        return std::hash<uint32_t>{}(id.id);
    }
};

template <>
struct std::hash<StringId> {
    std::size_t operator()(const StringId& id) const {
        return std::hash<uint32_t>{}(id.id);
    }
};


// Create a template Pool class that maps a key to a set of values
template <typename ID, typename T>
struct Mapping {
    Mapping() = default;
    ~Mapping() = default;

    /**
     * Adds the value to the Mapping and returns its associated id. If the
     * value is already in the Mapping, returns the id associated with it.
     */
    ID alloc(T value) {
        if (auto element = value_to_id.find(value); element != value_to_id.end()) {
            return element->second;
        }
        auto id = ID{static_cast<uint32_t>(id_to_value.size())};
        id_to_value[id] = value;
        value_to_id[value] = id;
        return id;
    }

    /**
     * Returns the value associated with the given id.
     */
    T operator[](ID id) { return id_to_value[id]; }

    /**
     * Returns the id associated with the given value.
     */
    ID operator[](T value) { return value_to_id[value]; }

    // Iterator for the Mapping
    auto begin() { return id_to_value.begin(); }
    auto end() { return id_to_value.end(); }
    auto begin() const { return id_to_value.begin(); }
    auto end() const { return id_to_value.end(); }
    auto cbegin() { return id_to_value.cbegin(); }
    auto cend() { return id_to_value.cend(); }
    auto cbegin() const { return id_to_value.cbegin(); }
    auto cend() const { return id_to_value.cend(); }
    auto find(T value) { return value_to_id.find(value); }

private:
    std::unordered_map<T, ID> value_to_id;
    std::unordered_map<ID, T> id_to_value;
};


struct PackageDatabase : public DependencyProvider {

    ::Mapping<NameId, String> name_pool;
    ::Mapping<StringId, String> string_pool;

    // MatchSpec are VersionSet in resolvo's semantics
    ::Mapping<VersionSetId, MatchSpec> version_set_pool;

    // PackageInfo are Solvable in resolvo's semantics
    ::Mapping<SolvableId, PackageInfo> solvable_pool;

    /**
     * Allocates a new requirement and return the id of the requirement.
     */
    VersionSetId alloc_version_set(
        std::string_view raw_match_spec
    ) {
        const MatchSpec match_spec = MatchSpec::parse(raw_match_spec).value();
        const std::string name = match_spec.name().str();

        // Add the version set to the version set pool
        auto id = version_set_pool.alloc(match_spec);

        // Add name to the name pool
        name_pool.alloc(String{name});

        // Add name to the string pool
        string_pool.alloc(String{name});

        return id;
    }

    SolvableId alloc_solvable(
        PackageInfo package_info
    ) {
        const std::string name = package_info.name;

        // Add the solvable to the solvable pool
        auto id = solvable_pool.alloc(package_info);

        // Add name to the name pool
        name_pool.alloc(String{name});

        // Add name to the string pool
        string_pool.alloc(String{name});

        for (auto& dep : package_info.dependencies) {
            alloc_version_set(dep);
        }
        for (auto& constr : package_info.constrains) {
            alloc_version_set(constr);
        }

        return id;
    }

    /**
     * Returns a user-friendly string representation of the specified solvable.
     *
     * When formatting the solvable, it should it include both the name of
     * the package and any other identifying properties.
     */
    String display_solvable(SolvableId solvable) override {
        const PackageInfo& package_info = solvable_pool[solvable];
        return String{package_info.long_str()};
    }

    /**
     * Returns a user-friendly string representation of the name of the
     * specified solvable.
     */
    String display_solvable_name(SolvableId solvable) override {
        const PackageInfo& package_info = solvable_pool[solvable];
        return String{package_info.name};
    }

    /**
     * Returns a string representation of multiple solvables merged together.
     *
     * When formatting the solvables, both the name of the packages and any
     * other identifying properties should be included.
     */
    String display_merged_solvables(Slice<SolvableId> solvable) override {
        std::string result;
        for (auto& solvable_id : solvable) {
            // Append "solvable_id" and its name to the result
            result += std::to_string(solvable_id.id) + " " + solvable_pool[solvable_id].build_string + "\n";
        }
        return String{result};
    }

    /**
     * Returns an object that can be used to display the given name in a
     * user-friendly way.
     */
    String display_name(NameId name) override {
        return name_pool[name];
    }

    /**
     * Returns a user-friendly string representation of the specified version
     * set.
     *
     * The name of the package should *not* be included in the display. Where
     * appropriate, this information is added.
     */
    String display_version_set(VersionSetId version_set) override {
        const MatchSpec match_spec = version_set_pool[version_set];
        return String{match_spec.str()};
    }

    /**
     * Returns the string representation of the specified string.
     */
    String display_string(StringId string) override {
        return string_pool[string];
    }

    /**
     * Returns the name of the package that the specified version set is
     * associated with.
     */
    NameId version_set_name(VersionSetId version_set_id) override {
        const MatchSpec match_spec = version_set_pool[version_set_id];
        std::cout << "Getting name id for version_set_id " << match_spec.name().str() << std::endl;
        return name_pool[String{match_spec.name().str()}];
    }

    /**
     * Returns the name of the package for the given solvable.
     */
    NameId solvable_name(SolvableId solvable_id) override {
        const PackageInfo& package_info = solvable_pool[solvable_id];
        std::cout << "Getting name id for solvable " << package_info.name << std::endl;
        return name_pool[String{package_info.name}];
    }

    /**
     * Obtains a list of solvables that should be considered when a package
     * with the given name is requested.
     */
    Candidates get_candidates(NameId package) override {
        std::cout << "Getting candidates for " << name_pool[package] << std::endl;
        Candidates candidates;
        // TODO: inefficient for now, O(n) which can be turned into O(1)
        for (auto& [solvable_id, package_info] : solvable_pool) {
            std::cout << "  Checking " << package_info.name << " " << package_info.version << std::endl;
            if (package == solvable_name(solvable_id)) {
                std::cout << "  Adding candidate " << package_info.name << std::endl;
                candidates.candidates.push_back(solvable_id);
            }
        }
        return candidates;
    }

    /**
     * Sort the specified solvables based on which solvable to try first. The
     * solver will iteratively try to select the highest version. If a
     * conflict is found with the highest version the next version is
     * tried. This continues until a solution is found.
     */
    void sort_candidates(Slice<SolvableId> solvables) override {
        std::cout << "Sorting candidates" << std::endl;
        std::sort(solvables.begin(), solvables.end(), [&](const SolvableId& a, const SolvableId& b) {
            const PackageInfo& package_info_a = solvable_pool[a];
            const PackageInfo& package_info_b = solvable_pool[b];
            // TODO: Add some caching on the version parsing
            return Version::parse(package_info_a.version).value() < Version::parse(package_info_b.version).value();
        });
    }

    /**
     * Given a set of solvables, return the solvables that match the given
     * version set or if `inverse` is true, the solvables that do *not* match
     * the version set.
     */
    Vector<SolvableId> filter_candidates(
        Slice<SolvableId> candidates,
        VersionSetId version_set_id,
        bool inverse
    ) override {
        Vector<SolvableId> filtered;

        if(inverse) {
            for (auto& solvable_id : candidates)
            {
                const PackageInfo& package_info = solvable_pool[solvable_id];
                const MatchSpec match_spec = version_set_pool[version_set_id];

                // Is it an appropriate check? Or must another one be crafted?
                if (!match_spec.contains_except_channel(package_info))
                {
                    filtered.push_back(solvable_id);
                }
            }
        } else {
            for (auto& solvable_id : candidates)
            {
                const PackageInfo& package_info = solvable_pool[solvable_id];
                const MatchSpec match_spec = version_set_pool[version_set_id];

                // Is it an appropriate check? Or must another one be crafted?
                if (match_spec.contains_except_channel(package_info))
                {
                    filtered.push_back(solvable_id);
                }
            }
        }

        return filtered;
    }

    /**
     * Returns the dependencies for the specified solvable.
     */
    Dependencies get_dependencies(SolvableId solvable_id) override {
        const PackageInfo& package_info = solvable_pool[solvable_id];
        std::cout << "Getting dependencies for " << package_info.name << std::endl;
        Dependencies dependencies;

        for (auto& dep : package_info.dependencies) {
            const MatchSpec match_spec = MatchSpec::parse(dep).value();
            dependencies.requirements.push_back(version_set_pool[match_spec]);
        }
        for (auto& constr : package_info.constrains) {
            const MatchSpec match_spec = MatchSpec::parse(constr).value();
            dependencies.constrains.push_back(version_set_pool[match_spec]);
        }

        return dependencies;
    }

};

TEST_SUITE("solver::resolvo")
{
    using PackageInfo = PackageInfo;

    TEST_CASE("Simple resolution problem") {

        PackageDatabase database;

        // Create a PackageInfo for scikit-learn
        PackageInfo scikit_learn0("scikit-learn", "1.5.0", "py310h981052a_0", 0);
        PackageInfo scikit_learn("scikit-learn", "1.5.0", "py310h981052a_1", 1);

        // Add the above dependencies to the PackageInfo object dependencies
        scikit_learn.dependencies.emplace_back("joblib==1.2.0");
        // scikit_learn.dependencies.push_back("numpy >=1.19,<3");
        // scikit_learn.dependencies.push_back("scipy");
        // scikit_learn.dependencies.push_back("threadpoolctl >=3.1.0");

        // Create a PackageInfo for numpy
        PackageInfo numpy("numpy", "1.21.0", "py310h4a8c4bd_0", 0);

        // Create a PackageInfo for scipy
        PackageInfo scipy("scipy", "1.7.0", "py310h4a8c4bd_0", 0);
        // scipy.dependencies.push_back("numpy >=1.19,<3");

        // Create a PackageInfo for joblib
        PackageInfo joblib("joblib", "1.2.0", "py310h4a8c4bd_0", 0);

        // Create a PackageInfo for threadpoolctl
        PackageInfo threadpoolctl("threadpoolctl", "3.1.0", "py310h4a8c4bd_0", 0);

        // Allocate all the PackageInfo
        database.alloc_solvable(scikit_learn0);
        database.alloc_solvable(scikit_learn);
        // database.alloc_solvable(numpy);
        // database.alloc_solvable(scipy);
        database.alloc_solvable(joblib);
        // database.alloc_solvable(threadpoolctl);

        // Construct a problem to be solved by the solver
        resolvo::Vector<resolvo::VersionSetId> requirements = {
            database.alloc_version_set("scikit-learn==1.5.0"),
        };
        resolvo::Vector<resolvo::VersionSetId> constraints = {};

        // Solve the problem
        std::cout << "Solving the problem" << std::endl;
        resolvo::Vector<resolvo::SolvableId> result;
        String reason = resolvo::solve(database, requirements, constraints, result);

        std::cout << "Reason: " << reason << std::endl;

        // Display the result
        std::cout << "Result contains " << result.size() << " solvables" << std::endl;
        for (auto& solvable_id : result) {
            std::cout << database.display_solvable(solvable_id) << std::endl;
        }

    }
}