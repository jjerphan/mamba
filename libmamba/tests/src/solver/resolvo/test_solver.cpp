// Copyright (c) 2024, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <iostream>
#include <unordered_set>
#include <doctest/doctest.h>
#include <resolvo/resolvo.h>
#include <resolvo/resolvo_dependency_provider.h>
#include <resolvo/resolvo_pool.h>

#include <simdjson.h>

#include "mamba/api/install.hpp" // for parsing YAML specs

#include "mamba/core/util.hpp"  // for LockFile
#include "mamba/specs/channel.hpp"
#include "mamba/specs/package_info.hpp"

// TODO: move PackageTypes and MAX_CONDA_TIMESTAMP to a common place
#include "mamba/core/virtual_packages.hpp"
#include "mamba/solver/libsolv/database.hpp"
#include "mamba/solver/libsolv/parameters.hpp"  // for PackageTypes
#include "mamba/solver/libsolv/solver.hpp"

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

    auto begin_ids() { return value_to_id.begin(); }
    auto end_ids() { return value_to_id.end(); }
    auto begin_ids() const { return value_to_id.begin(); }
    auto end_ids() const { return value_to_id.end(); }
    auto cbegin_ids() { return value_to_id.cbegin(); }
    auto cend_ids() { return value_to_id.cend(); }
    auto cbegin_ids() const { return value_to_id.cbegin(); }
    auto cend_ids() const { return value_to_id.cend(); }

    auto size() const { return id_to_value.size(); }


private:
    std::unordered_map<T, ID> value_to_id;
    std::unordered_map<ID, T> id_to_value;
};


struct PackageDatabase : public DependencyProvider {

    virtual ~PackageDatabase() = default;

    ::Mapping<NameId, String> name_pool;
    ::Mapping<StringId, String> string_pool;

    // MatchSpec are VersionSet in resolvo's semantics
    ::Mapping<VersionSetId, MatchSpec> version_set_pool;

    // PackageInfo are Solvable in resolvo's semantics
    ::Mapping<SolvableId, PackageInfo> solvable_pool;

    // PackageName to Vector<SolvableId>
    std::unordered_map<NameId, Vector<SolvableId>> name_to_solvable;

    // VersionSetId to max version
    // TODO use `SolvableId` instead of `std::pair<Version, size_t>`?
    std::unordered_map<VersionSetId, std::pair<Version, size_t>> version_set_to_max_version_and_track_features_numbers;

    /**
     * Allocates a new requirement and return the id of the requirement.
     */
    VersionSetId alloc_version_set(
        std::string_view raw_match_spec
    ) {
        std::string raw_match_spec_str = std::string(raw_match_spec);
        // Replace all " v" with simply " " to work around the `v` prefix in some version strings
        // e.g. `mingw-w64-ucrt-x86_64-crt-git v12.0.0.r2.ggc561118da h707e725_0` in `infom2w64-sysroot_win-64-v12.0.0.r2.ggc561118da-h707e725_0.conda`
        while (raw_match_spec_str.find(" v") != std::string::npos)
        {
            raw_match_spec_str = raw_match_spec_str.replace(raw_match_spec_str.find(" v"), 2, " ");
        }

        // Remove any presence of selector on python version in the match spec
        // e.g. `pillow-heif >=0.10.0,<1.0.0<py312` -> `pillow-heif >=0.10.0,<1.0.0` in `infowillow-1.6.3-pyhd8ed1ab_0.conda`
        for(const auto specifier: {"=py", "<py", ">py", ">=py", "<=py", "!=py"})
        {
            while (raw_match_spec_str.find(specifier) != std::string::npos)
            {
                raw_match_spec_str = raw_match_spec_str.substr(0, raw_match_spec_str.find(specifier));
            }
        }
        // Remove any white space between version
        // e.g. `kytea >=0.1.4, 0.2.0` -> `kytea >=0.1.4,0.2.0` in `infokonoha-4.6.3-pyhd8ed1ab_0.tar.bz2`
        while (raw_match_spec_str.find(", ") != std::string::npos)
        {
            raw_match_spec_str = raw_match_spec_str.replace(raw_match_spec_str.find(", "), 2, ",");
        }

        // TODO: skip allocation for now if "*.*" is in the match spec
        if (raw_match_spec_str.find("*.*") != std::string::npos)
        {
            return VersionSetId{0};
        }


        // NOTE: works around `openblas 0.2.18|0.2.18.*.` from `dlib==19.0=np110py27_blas_openblas_200`
        // If contains "|", split on it and recurse
        if (raw_match_spec_str.find("|") != std::string::npos)
        {
            std::vector<std::string> match_specs;
            std::string match_spec;
            for (char c : raw_match_spec_str)
            {
                if (c == '|')
                {
                    match_specs.push_back(match_spec);
                    match_spec.clear();
                }
                else
                {
                    match_spec += c;
                }
            }
            match_specs.push_back(match_spec);
            std::vector<VersionSetId> version_sets;
            for (const std::string& ms : match_specs)
            {
                alloc_version_set(ms);
            }
            // Placeholder return value
            return VersionSetId{0};
        }

        // NOTE: This works around some improperly encoded `constrains` in the test data, e.g.:
        //      `openmpi-4.1.4-ha1ae619_102`'s improperly encoded `constrains`: "cudatoolkit  >= 10.2"
        //      `pytorch-1.13.0-cpu_py310h02c325b_0.conda`'s improperly encoded `constrains`: "pytorch-cpu = 1.13.0", "pytorch-gpu = 99999999"
        //      `fipy-3.4.2.1-py310hff52083_3.tar.bz2`'s improperly encoded `constrains` or `dep`: ">=4.5.2"
        // Remove any with space after the binary operators
        for(const std::string& op : {">=", "<=", "==", ">", "<", "!=", "=", "=="}) {
            const std::string& bad_op = op + " ";
            while (raw_match_spec_str.find(bad_op) != std::string::npos) {
                raw_match_spec_str = raw_match_spec_str.substr(0, raw_match_spec_str.find(bad_op)) + op + raw_match_spec_str.substr(raw_match_spec_str.find(bad_op) + bad_op.size());
            }
            // If start with binary operator, prepend NONE
            if (raw_match_spec_str.find(op) == 0) {
                raw_match_spec_str = "NONE " + raw_match_spec_str;
            }
        }

        const MatchSpec match_spec = MatchSpec::parse(raw_match_spec_str).value();
        // Add the version set to the version set pool
        auto id = version_set_pool.alloc(match_spec);

        // Add name to the Name and String pools
        const std::string name = match_spec.name().str();
        name_pool.alloc(String{name});
        string_pool.alloc(String{name});

        // Add the MatchSpec's string representation to the Name and String pools
        const std::string match_spec_str = match_spec.str();
        name_pool.alloc(String{match_spec_str});
        string_pool.alloc(String{match_spec_str});
        return id;
    }

    SolvableId alloc_solvable(
        PackageInfo package_info
    ) {
        // Add the solvable to the solvable pool
        auto id = solvable_pool.alloc(package_info);

        // Add name to the Name and String pools
        const std::string name = package_info.name;
        name_pool.alloc(String{name});
        string_pool.alloc(String{name});

        // Add the long string representation of the package to the Name and String pools
        const std::string long_str = package_info.long_str();
        name_pool.alloc(String{long_str});
        string_pool.alloc(String{long_str});

        for (auto& dep : package_info.dependencies) {
            alloc_version_set(dep);
        }
        for (auto& constr : package_info.constrains) {
            alloc_version_set(constr);
        }

        // Add the solvable to the name_to_solvable map
        const NameId name_id = name_pool.alloc(String{package_info.name});
        name_to_solvable[name_id].push_back(id);

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
            result += solvable_pool[solvable_id].long_str();
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
        // std::cout << "Getting name id for version_set_id " << match_spec.name().str() << std::endl;
        return name_pool[String{match_spec.name().str()}];
    }

    /**
     * Returns the name of the package for the given solvable.
     */
    NameId solvable_name(SolvableId solvable_id) override {
        const PackageInfo& package_info = solvable_pool[solvable_id];
        // std::cout << "Getting name id for solvable " << package_info.long_str() << std::endl;
        return name_pool[String{package_info.name}];
    }

    /**
     * Obtains a list of solvables that should be considered when a package
     * with the given name is requested.
     */
    Candidates get_candidates(NameId package) override {
        Candidates candidates{};
        candidates.favored = nullptr;
        candidates.locked = nullptr;
        candidates.candidates = name_to_solvable[package];
        return candidates;
    }

    std::pair<Version, size_t> find_highest_version(
        VersionSetId version_set_id
    ) {
        // If the version set has already been computed, return it.
        if(version_set_to_max_version_and_track_features_numbers.find(version_set_id) != version_set_to_max_version_and_track_features_numbers.end()) {
            return version_set_to_max_version_and_track_features_numbers[version_set_id];
        }

        const MatchSpec match_spec = version_set_pool[version_set_id];

        const std::string& name = match_spec.name().str();

        auto name_id = name_pool.alloc(String{name});

        auto solvables = name_to_solvable[name_id];

        auto filtered = filter_candidates(solvables, version_set_id, false);

        Version max_version = Version();
        size_t max_version_n_track_features = 0;

        for(auto& solvable_id : filtered) {
            const PackageInfo& package_info = solvable_pool[solvable_id];
            const auto version = Version::parse(package_info.version).value();
            if(version == max_version) {
                max_version_n_track_features = std::min(max_version_n_track_features, package_info.track_features.size());
            }
            if(version > max_version) {
                max_version = version;
                max_version_n_track_features = package_info.track_features.size();
            }
        }

        auto val = std::make_pair(max_version, max_version_n_track_features);
        version_set_to_max_version_and_track_features_numbers[version_set_id] = val;
        return val;
    }

    /**
     * Sort the specified solvables based on which solvable to try first. The
     * solver will iteratively try to select the highest version. If a
     * conflict is found with the highest version the next version is
     * tried. This continues until a solution is found.
     */
    void sort_candidates(Slice<SolvableId> solvables) override {
        std::sort(solvables.begin(), solvables.end(), [&](const SolvableId& a, const SolvableId& b) {
            const PackageInfo& package_info_a = solvable_pool[a];
            const PackageInfo& package_info_b = solvable_pool[b];

            // If track features are present, prefer the solvable having the least of them.
            if (package_info_a.track_features.size() != package_info_b.track_features.size()) {
                return package_info_a.track_features.size() < package_info_b.track_features.size() ;
            }

            const auto a_version = Version::parse(package_info_a.version).value();
            const auto b_version = Version::parse(package_info_b.version).value();

            if (a_version != b_version) {
                return a_version > b_version;
            }

            if (package_info_a.build_number != package_info_b.build_number) {
                return package_info_a.build_number > package_info_b.build_number;
            }

            // Compare the dependencies of the variants.
            std::unordered_map<NameId, VersionSetId> a_deps;
            std::unordered_map<NameId, VersionSetId> b_deps;
            for(auto dep_a: package_info_a.dependencies) {
                // TODO: have a VersionID to NameID mapping instead
                MatchSpec ms = MatchSpec::parse(dep_a).value();
                const std::string& name = ms.name().str();
                auto name_id = name_pool.alloc(String{name});

                a_deps[name_id] = version_set_pool[ms];
            }
            for(auto dep_b: package_info_b.dependencies) {
                // TODO: have a VersionID to NameID mapping instead
                MatchSpec ms = MatchSpec::parse(dep_b).value();
                const std::string& name = ms.name().str();
                auto name_id = name_pool.alloc(String{name});

                b_deps[name_id] = version_set_pool[ms];
            }

            auto ordering_score = 0;
            for (auto [name_id, version_set_id] : a_deps) {
                if (b_deps.find(name_id) != b_deps.end()) {
                    auto [a_version, a_n_track_features] = find_highest_version(version_set_id);
                    auto [b_version, b_n_track_features] = find_highest_version(b_deps[name_id]);

                    // Favor the solvable with higher versions of their dependencies
                    if (a_version != b_version) {
                        ordering_score += a_version > b_version ? 1 : -1;
                    }

                    // Highly penalize the solvable if a dependencies has more track features
                    if (a_n_track_features != b_n_track_features) {
                        ordering_score += a_n_track_features > b_n_track_features ? -100 : 100;
                    }
                }
            }

            if(ordering_score != 0) {
                return ordering_score > 0;
            }

            return package_info_a.timestamp > package_info_b.timestamp;
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
        MatchSpec match_spec = version_set_pool[version_set_id];
        Vector<SolvableId> filtered;

        if(inverse) {
            for (auto& solvable_id : candidates)
            {
                const PackageInfo& package_info = solvable_pool[solvable_id];

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

                // Is it an appropriate check? Or must another one be crafted?
                if (match_spec.contains_except_channel(package_info))
                {
                    filtered.push_back(solvable_id);
                }
            }
        }
        // std::cout << "Keeping " << filtered.size() << " candidates for " << match_spec.str() << ":" << std::endl;
        for (auto& solvable_id : filtered)
        {
            const PackageInfo& package_info = solvable_pool[solvable_id];
            // std::cout << "  - " << package_info.long_str() << std::endl;
        }

        return filtered;
    }

    /**
     * Returns the dependencies for the specified solvable.
     */
    Dependencies get_dependencies(SolvableId solvable_id) override {
        const PackageInfo& package_info = solvable_pool[solvable_id];
        // std::cout << "Getting dependencies for " << package_info.long_str() << std::endl;

        Dependencies dependencies;

        // TODO: do this in O(1)
        for (auto& dep : package_info.dependencies) {
            // std::cout << "Parsing dep " << dep << std::endl;
            const MatchSpec match_spec = MatchSpec::parse(dep).value();
            dependencies.requirements.push_back(version_set_pool[match_spec]);
        }
        for (auto& constr : package_info.constrains) {
            // std::cout << "Parsing constr " << constr << std::endl;
            // if constr contain " == " replace it with "=="
            std::string constr2 = constr;
            while (constr2.find(" == ") != std::string::npos)
            {
                constr2 = constr2.replace(constr2.find(" == "), 4, "==");
            }
            while (constr2.find(" >= ") != std::string::npos)
            {
                constr2 = constr2.replace(constr2.find(" >= "), 4, ">=");
            }
            const MatchSpec match_spec = MatchSpec::parse(constr2).value();
            dependencies.constrains.push_back(version_set_pool[match_spec]);
        }

        return dependencies;
    }

};

// TODO: reuse it from `mamba/solver/libsolv/helpers.cpp`
auto lsplit_track_features(std::string_view features)
{
    constexpr auto is_sep = [](char c) -> bool { return (c == ',') || util::is_space(c); };
    auto [_, tail] = util::lstrip_if_parts(features, is_sep);
    return util::lstrip_if_parts(tail, [&](char c) { return !is_sep(c); });
}

// TODO: factorise with the implementation from `set_solvable` in `mamba/solver/libsolv/helpers.cpp`
bool parse_packageinfo_json(
    const std::string_view& filename,
    const simdjson::dom::element& pkg,
    const CondaURL& repo_url,
    const std::string& channel_id,
    PackageDatabase& database
    ) {
    PackageInfo package_info;

    package_info.channel = channel_id;
    package_info.filename = filename;
    package_info.package_url = (repo_url / filename).str(specs::CondaURL::Credentials::Show);

    if (auto fn = pkg["fn"].get_string(); !fn.error())
    {
        package_info.name = fn.value_unsafe();
    }
    else
    {
        // Fallback from key entry
        package_info.name = filename;
    }

    if (auto name = pkg["name"].get_string(); !name.error())
    {
        package_info.name = name.value_unsafe();
    }
    else
    {
        LOG_WARNING << R"(Found invalid name in ")" << filename << R"(")";
        return false;
    }

    if (auto version = pkg["version"].get_string(); !version.error())
    {
        package_info.version = version.value_unsafe();
    }
    else
    {
        LOG_WARNING << R"(Found invalid version in ")" << filename << R"(")";
        return false;
    }

    if (auto build_string = pkg["build"].get_string(); !build_string.error())
    {
        package_info.build_string = build_string.value_unsafe();
    }
    else
    {
        LOG_WARNING << R"(Found invalid build in ")" << filename << R"(")";
        return false;
    }

    if (auto build_number = pkg["build_number"].get_uint64(); !build_number.error())
    {
        package_info.build_number = build_number.value_unsafe();
    }
    else
    {
        LOG_WARNING << R"(Found invalid build_number in ")" << filename << R"(")";
        return false;
    }

    if (auto subdir = pkg["subdir"].get_c_str(); !subdir.error())
    {
        package_info.platform = subdir.value_unsafe();
    }
    else
    {
        LOG_WARNING << R"(Found invalid subdir in ")" << filename << R"(")";
    }

    if (auto size = pkg["size"].get_uint64(); !size.error())
    {
        package_info.size = size.value_unsafe();
    }

    if (auto md5 = pkg["md5"].get_c_str(); !md5.error())
    {
        package_info.md5 = md5.value_unsafe();
    }

    if (auto sha256 = pkg["sha256"].get_c_str(); !sha256.error())
    {
        package_info.sha256 = sha256.value_unsafe();
    }

    if (auto elem = pkg["noarch"]; !elem.error())
    {
        // TODO: is the following right?
        if (auto val = elem.get_bool(); !val.error() && val.value_unsafe())
        {
            package_info.noarch = NoArchType::No;
        }
        else if (auto noarch = elem.get_c_str(); !noarch.error())
        {
            package_info.noarch = NoArchType::No;
        }
    }

    if (auto license = pkg["license"].get_c_str(); !license.error())
    {
        package_info.license = license.value_unsafe();
    }

    // TODO conda timestamp are not Unix timestamp.
    // Libsolv normalize them this way, we need to do the same here otherwise the current
    // package may get arbitrary priority.
    if (auto timestamp = pkg["timestamp"].get_uint64(); !timestamp.error())
    {
        const auto time = timestamp.value_unsafe();
        // TODO: reuse it from `mamba/solver/libsolv/helpers.cpp`
        constexpr auto MAX_CONDA_TIMESTAMP = 253402300799ULL;
        package_info.timestamp = (time > MAX_CONDA_TIMESTAMP) ? (time / 1000) : time;
    }

    if (auto depends = pkg["depends"].get_array(); !depends.error())
    {
        for (auto elem : depends)
        {
            if (auto dep = elem.get_c_str(); !dep.error())
            {
                package_info.dependencies.emplace_back(dep.value_unsafe());
            }
        }
    }

    if (auto constrains = pkg["constrains"].get_array(); !constrains.error())
    {
        for (auto elem : constrains)
        {
            if (auto cons = elem.get_c_str(); !cons.error())
            {
                package_info.constrains.emplace_back(cons.value_unsafe());
            }
        }
    }

    if (auto obj = pkg["track_features"]; !obj.error())
    {
        if (auto track_features_arr = obj.get_array(); !track_features_arr.error())
        {
            for (auto elem : track_features_arr)
            {
                if (auto feat = elem.get_string(); !feat.error())
                {
                    package_info.track_features.emplace_back(feat.value_unsafe());
                }
            }
        }
        else if (auto track_features_str = obj.get_string(); !track_features_str.error())
        {
            auto splits = lsplit_track_features(track_features_str.value_unsafe());
            while (!splits[0].empty())
            {
                package_info.track_features.emplace_back(splits[0]);
                splits = lsplit_track_features(splits[1]);
            }
        }
    }

    database.alloc_solvable(package_info);
    return true;
}

void parse_repodata_json(
    PackageDatabase& database,
    const fs::u8path& filename,
    const std::string& repo_url,
    const std::string& channel_id,
    bool verify_artifacts
)
{
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

    auto added = util::flat_set<std::string_view>();
    if (auto pkgs = repodata["packages.conda"].get_object(); !pkgs.error())
    {
        std::cout << "CondaOrElseTarBz2 packages.conda" << std::endl;

        for (auto [key, value] : pkgs.value())
        {
            parse_packageinfo_json(key, value, parsed_url, channel_id, database);
        }
    }
    if (auto pkgs = repodata["packages"].get_object(); !pkgs.error())
    {
        std::cout << "CondaOrElseTarBz2 packages" << std::endl;

        for (auto [key, value] : pkgs.value())
        {
            parse_packageinfo_json(key, value, parsed_url, channel_id, database);
        }
    }
}

// from `src/test_solver.cpp`
auto find_actions_with_name(const Solution& solution, std::string_view name) -> std::vector<Solution::Action>;
auto find_actions(const Solution& solution) -> std::vector<Solution::Action>;
auto extract_package_to_install(const Solution& solution) -> std::vector<specs::PackageInfo>;


// wget https://conda.anaconda.org/conda-forge/linux-64/repodata.json
// wget https://conda.anaconda.org/conda-forge/noarch/repodata.json

mamba::solver::libsolv::Database create_libsolv_db() {
    auto libsolv_db = mamba::solver::libsolv::Database({
        /* .platforms= */ { "linux-64", "noarch" },
        /* .channel_alias= */ specs::CondaURL::parse("https://conda.anaconda.org/").value(),
    });


    const auto repo_linux = libsolv_db.add_repo_from_repodata_json(
        "/tmp/linux-64/repodata.json",
        "https://conda.anaconda.org/conda-forge/linux-64",
        "conda-forge",
        libsolv::PipAsPythonDependency::No
    );

    const auto repo_noarch = libsolv_db.add_repo_from_repodata_json(
        "/tmp/noarch/repodata.json",
        "https://conda.anaconda.org/conda-forge/noarch",
        "conda-forge",
        libsolv::PipAsPythonDependency::Yes
    );

    // Not adding Pip dependency since it might needlessly make the installed/active environment
    // broken if pip is not already installed (debatable).
    // TODO: change the signature of `get_virtual_packages` to take a `Context` object instead
    Context context;
    context.platform = "linux-64";
    auto repo = libsolv_db.add_repo_from_packages(
        get_virtual_packages(context),
        "virtual",
        solver::libsolv::PipAsPythonDependency::No
    );
    libsolv_db.set_installed_repo(repo);

    return libsolv_db;
};

PackageDatabase create_resolvo_db() {
    PackageDatabase resolvo_db;

    parse_repodata_json(
        resolvo_db,
        "/tmp/linux-64/repodata.json",
        "https://conda.anaconda.org/conda-forge/linux-64/repodata.json",
        "conda-forge",
        false
    );

    parse_repodata_json(
        resolvo_db,
        "/tmp/noarch/repodata.json",
        "https://conda.anaconda.org/conda-forge/noarch/repodata.json",
        "conda-forge",
        false
    );

    // TODO: change the signature of `get_virtual_packages` to take a `Context` object instead
    Context context;
    context.platform = "linux-64";

    for (const auto& package : get_virtual_packages(context))
    {
        resolvo_db.alloc_solvable(package);
    }

    return resolvo_db;
}

mamba::solver::libsolv::Database libsolv_db = create_libsolv_db();
PackageDatabase resolvo_db = create_resolvo_db();


std::vector<PackageInfo> libsolv_resolve(
    mamba::solver::libsolv::Database& db,
    const std::vector<std::string>& specs
) {
    // libsolv's specification and resolution

    Request::job_list jobs;

    std::transform(
        specs.begin(),
        specs.end(),
        std::back_inserter(jobs),
        [](const std::string& spec)
        { return Request::Install{ MatchSpec::parse(spec).value() }; }
    );

    const auto request = Request{
        /* .flags= */ {},
        /* .jobs= */ jobs,
    };

    std::cout << "Start with libsolv" << std::endl;
    auto tick_libsolv = std::chrono::steady_clock::now();
    const auto outcome = libsolv::Solver().solve(libsolv_db, request);
    auto tack_libsolv = std::chrono::steady_clock::now();
    std::cout << "End with libsolv" << std::endl;
    std::cout << "Elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>(tack_libsolv - tick_libsolv).count() << "ms" << std::endl;

    REQUIRE(outcome.has_value());
    if (std::holds_alternative<Solution>(outcome.value()))
    {
        const auto& solution = std::get<Solution>(outcome.value());

        std::vector<PackageInfo> libsolv_resolution = extract_package_to_install(solution);
        std::sort(
            libsolv_resolution.begin(),
            libsolv_resolution.end(),
            [&](const PackageInfo& a, const PackageInfo& b) { return a.name < b.name; }
        );
        return libsolv_resolution;
    }
    return {};
}


std::vector<PackageInfo> resolvo_resolve(
    PackageDatabase& database,
    const std::vector<std::string>& specs
) {
    // resolvo's specification and resolution
    resolvo::Vector<resolvo::VersionSetId> requirements;
    for (const auto& spec : specs)
    {
        requirements.push_back(resolvo_db.alloc_version_set(spec));
    }

    resolvo::Vector<resolvo::VersionSetId> constraints = {};
    resolvo::Vector<resolvo::SolvableId> result;

    std::cout << "Start with resolvo" << std::endl;
    auto tick_resolvo = std::chrono::steady_clock::now();
    String reason = resolvo::solve(resolvo_db, requirements, constraints, result);
    auto tack_resolvo = std::chrono::steady_clock::now();
    std::cout << "End with resolvo" << std::endl;
    std::cout << "Elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>(tack_resolvo - tick_resolvo).count() << "ms" << std::endl;

    if (reason == "")
    {
        std::vector<PackageInfo> resolvo_resolution;
        for(auto solvable_id : result)
        {
            PackageInfo package_info = resolvo_db.solvable_pool[solvable_id];
            // Skip virtual package (i.e. whose `package_info.name` starts with "__")
            if (package_info.name.find("__") != 0) {
                resolvo_resolution.push_back(package_info);
            }
        }

        std::sort(
            resolvo_resolution.begin(),
            resolvo_resolution.end(),
            [&](const PackageInfo& a, const PackageInfo& b) { return a.name < b.name; }
        );
        return resolvo_resolution;
    }
    return {};
}


TEST_SUITE("solver::resolvo")
{
    using namespace specs::match_spec_literals;

    using PackageInfo = PackageInfo;

    TEST_CASE("Addition of PackageInfo to PackageDatabase")
    {
        PackageDatabase database;

        PackageInfo scikit_learn("scikit-learn", "1.5.0", "py310h981052a_0", 0);
        scikit_learn.dependencies.emplace_back("numpy >=1.20.0,<2.0a0");
        scikit_learn.dependencies.emplace_back("scipy >=1.6.0,<2.0a0");
        scikit_learn.dependencies.emplace_back("joblib >=1.0.1,<2.0a0");
        scikit_learn.dependencies.emplace_back("threadpoolctl >=2.1.0,<3.0a0");

        auto solvable = database.alloc_solvable(scikit_learn);

        CHECK(solvable.id == 0);
        CHECK(database.solvable_pool[solvable].name == "scikit-learn");
        CHECK(database.solvable_pool[solvable].version == "1.5.0");
        CHECK(database.solvable_pool[solvable].build_string == "py310h981052a_0");
        CHECK(database.solvable_pool[solvable].build_number == 0);

        auto deps = database.get_dependencies(solvable);
        CHECK(deps.requirements.size() == 4);
        CHECK(deps.constrains.size() == 0);

        CHECK(
            database.version_set_pool[deps.requirements[0]].str() == "numpy[version=\">=1.20.0,<2.0a0\"]"
        );
        CHECK(
            database.version_set_pool[deps.requirements[1]].str() == "scipy[version=\">=1.6.0,<2.0a0\"]"
        );
        CHECK(
            database.version_set_pool[deps.requirements[2]].str() == "joblib[version=\">=1.0.1,<2.0a0\"]"
        );
        CHECK(
            database.version_set_pool[deps.requirements[3]].str()
            == "threadpoolctl[version=\">=2.1.0,<3.0a0\"]"
        );

        CHECK(database.name_pool.find(String{ "scikit-learn" }) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{ "numpy" }) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{ "scipy" }) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{ "joblib" }) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{ "threadpoolctl" }) != database.name_pool.end_ids());

        CHECK(database.string_pool.find(String{ "scikit-learn" }) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{ "numpy" }) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{ "scipy" }) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{ "joblib" }) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{ "threadpoolctl" }) != database.string_pool.end_ids());
    }

    TEST_CASE("Filter solvables")
    {
        PackageDatabase database;

        PackageInfo skl0("scikit-learn", "1.4.0", "py310h981052a_0", 0);
        auto sol0 = database.alloc_solvable(skl0);

        PackageInfo skl1("scikit-learn", "1.5.0", "py310h981052a_1", 1);
        auto sol1 = database.alloc_solvable(skl1);

        PackageInfo skl2("scikit-learn", "1.5.1", "py310h981052a_0", 0);
        auto sol2 = database.alloc_solvable(skl2);

        PackageInfo skl3("scikit-learn", "1.5.1", "py310h981052a_2", 2);
        auto sol3 = database.alloc_solvable(skl3);

        auto solvables = Vector<SolvableId>{ sol0, sol1, sol2, sol3 };

        // Filter on scikit-learn
        auto all = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn"),
            false
        );
        CHECK(all.size() == 4);
        CHECK(all[0] == sol0);
        CHECK(all[1] == sol1);
        CHECK(all[2] == sol2);
        CHECK(all[3] == sol3);

        // Inverse filter on scikit-learn
        auto none = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn"),
            true
        );
        CHECK(none.size() == 0);

        // Filter on scikit-learn==1.5.1
        auto one = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn==1.5.1"),
            false
        );
        CHECK(one.size() == 2);
        CHECK(one[0] == sol2);
        CHECK(one[1] == sol3);

        // Inverse filter on scikit-learn==1.5.1
        auto three = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn==1.5.1"),
            true
        );
        CHECK(three.size() == 2);
        CHECK(three[0] == sol0);
        CHECK(three[1] == sol1);

        // Filter on scikit-learn<1.5.1
        auto two = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn<1.5.1"),
            false
        );
        CHECK(two.size() == 2);
        CHECK(two[0] == sol0);
        CHECK(two[1] == sol1);

        // Filter on build number 0
        auto build = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn[build_number==0]"),
            false
        );
        CHECK(build.size() == 2);
        CHECK(build[0] == sol0);
        CHECK(build[1] == sol2);

        // Filter on build number 2
        auto build_bis = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn[build_number==2]"),
            false
        );
        CHECK(build_bis.size() == 1);
        CHECK(build_bis[0] == sol3);

        // Filter on build number 3
        auto build_ter = database.filter_candidates(
            solvables,
            database.alloc_version_set("scikit-learn[build_number==3]"),
            false
        );
        CHECK(build_ter.size() == 0);
    }

    TEST_CASE("Sort solvables")
    {
        PackageDatabase database;

        PackageInfo skl0("scikit-learn", "1.5.2", "py310h981052a_0", 0);
        auto sol0 = database.alloc_solvable(skl0);

        PackageInfo skl1("scikit-learn", "1.5.0", "py310h981052a_1", 1);
        auto sol1 = database.alloc_solvable(skl1);

        PackageInfo skl2("scikit-learn", "1.5.1", "py310h981052a_2", 2);
        auto sol2 = database.alloc_solvable(skl2);

        PackageInfo skl3("scikit-learn", "1.5.0", "py310h981052a_2", 2);
        auto sol3 = database.alloc_solvable(skl3);

        PackageInfo skl4("scikit-learn", "1.5.1", "py310h981052a_1", 1);
        auto sol4 = database.alloc_solvable(skl4);

        Vector<SolvableId> solvables = { sol0, sol1, sol2, sol3, sol4 };

        database.sort_candidates(solvables);

        CHECK_EQ(solvables[0], sol0);
        CHECK_EQ(solvables[1], sol2);
        CHECK_EQ(solvables[2], sol4);
        CHECK_EQ(solvables[3], sol3);
        CHECK_EQ(solvables[4], sol1);
    }

    TEST_CASE("Sort solvables (build number only)")
    {
        PackageDatabase database;

        PackageInfo skl0("scikit-learn", "1.5.0", "py310h981052a_0", 0);
        auto sol0 = database.alloc_solvable(skl0);

        PackageInfo skl1("scikit-learn", "1.5.0", "py310h981052a_3", 3);
        auto sol1 = database.alloc_solvable(skl1);

        PackageInfo skl2("scikit-learn", "1.5.0", "py310h981052a_2", 2);
        auto sol2 = database.alloc_solvable(skl2);

        PackageInfo skl3("scikit-learn", "1.5.0", "py310h981052a_1", 1);
        auto sol3 = database.alloc_solvable(skl3);

        PackageInfo skl4("scikit-learn", "1.5.0", "py310h981052a_4", 4);
        auto sol4 = database.alloc_solvable(skl4);

        PackageInfo skl5("scikit-learn", "1.5.0", "py310h981052a_5", 5);
        skl5.timestamp = 1337;
        auto sol5 = database.alloc_solvable(skl5);

        PackageInfo skl6("scikit-learn", "1.5.0", "py310h981052a_5", 5);
        skl6.timestamp = 42;
        auto sol6 = database.alloc_solvable(skl6);

        PackageInfo skl7("scikit-learn", "1.5.0", "py310h981052a_5", 5);
        skl7.timestamp = 2000;
        auto sol7 = database.alloc_solvable(skl7);

        Vector<SolvableId> solvables = { sol0, sol1, sol2, sol3, sol4, sol5, sol6, sol7 };

        database.sort_candidates(solvables);

        CHECK_EQ(solvables[0], sol7);
        CHECK_EQ(solvables[1], sol5);
        CHECK_EQ(solvables[2], sol6);
        CHECK_EQ(solvables[3], sol4);
        CHECK_EQ(solvables[4], sol1);
        CHECK_EQ(solvables[5], sol2);
        CHECK_EQ(solvables[6], sol3);
        CHECK_EQ(solvables[7], sol0);
    }

    TEST_CASE("Trivial problem")
    {
        PackageDatabase database;

        PackageInfo scikit_learn("scikit-learn", "1.5.0", "py310h981052a_0", 0);
        database.alloc_solvable(scikit_learn);

        resolvo::Vector<resolvo::VersionSetId> requirements = {
            database.alloc_version_set("scikit-learn==1.5.0"),
        };
        resolvo::Vector<resolvo::VersionSetId> constraints = {};

        resolvo::Vector<resolvo::SolvableId> result;
        String reason = resolvo::solve(database, requirements, constraints, result);

        CHECK(reason == "");
        CHECK(result.size() == 1);
        CHECK(database.solvable_pool[result[0]] == scikit_learn);
    }

    TEST_CASE("Parse linux-64/repodata.json")
    {
        PackageDatabase database;

        parse_repodata_json(
            database,
            "/tmp/linux-64/repodata.json",
            "https://conda.anaconda.org/conda-forge/linux-64/repodata.json",
            "conda-forge",
            false
        );

        std::cout << "Number of solvables: " << database.solvable_pool.size() << std::endl;
    }

    TEST_CASE("Parse noarch/repodata.json")
    {
        PackageDatabase database;

        parse_repodata_json(
            database,
            "/tmp/noarch/repodata.json",
            "https://conda.anaconda.org/conda-forge/noarch/repodata.json",
            "conda-forge",
            false
        );

        std::cout << "Number of solvables: " << database.solvable_pool.size() << std::endl;
    }


    TEST_CASE("scikit-learn explicit")
    {
        std::vector<std::string> specs_to_install = {
            "python[version=\">=3.10,<3.11.0a0\"]",      "pip",
            "scikit-learn[version=\">=1.0.0,<1.5.1\"]",  "numpy[version=\">=1.20.0,<2.0a0\"]",
            "scipy[version=\">=1.10.0,<1.15a0\"]",       "joblib[version=\">=1.0.1,<2.0a0\"]",
            "threadpoolctl[version=\">=2.1.0,<3.6a0\"]",
        };

        std::vector<PackageInfo> known_resolution = {
            PackageInfo("_libgcc_mutex", "0.1", "conda_forge", 0),
            PackageInfo("python_abi", "3.10", "4_cp310", 0),
            PackageInfo("ld_impl_linux-64", "2.40", "hf3520f5_7", 0),
            PackageInfo("ca-certificates", "2024.7.4", "hbcca054_0", 0),
            PackageInfo("libgomp", "14.1.0", "h77fa898_0", 0),
            PackageInfo("_openmp_mutex", "4.5", "2_gnu", 0),
            PackageInfo("libgcc-ng", "14.1.0", "h77fa898_0", 0),
            PackageInfo("openssl", "3.3.1", "h4ab18f5_1", 0),
            PackageInfo("libxcrypt", "4.4.36", "hd590300_1", 0),
            PackageInfo("libzlib", "1.3.1", "h4ab18f5_1", 0),
            PackageInfo("libffi", "3.4.2", "h7f98852_5", 0),
            PackageInfo("bzip2", "1.0.8", "hd590300_5", 0),
            PackageInfo("ncurses", "6.5", "h59595ed_0", 0),
            PackageInfo("libstdcxx-ng", "14.1.0", "hc0a3c3a_0", 0),
            PackageInfo("libgfortran5", "14.1.0", "hc5f4f2c_0", 0),
            PackageInfo("libuuid", "2.38.1", "h0b41bf4_0", 0),
            PackageInfo("libnsl", "2.0.1", "hd590300_0", 0),
            PackageInfo("xz", "5.2.6", "h166bdaf_0", 0),
            PackageInfo("tk", "8.6.13", "noxft_h4845f30_101", 0),
            PackageInfo("libsqlite", "3.46.0", "hde9e2c9_0", 0),
            PackageInfo("readline", "8.2", "h8228510_1", 0),
            PackageInfo("libgfortran-ng", "14.1.0", "h69a702a_0", 0),
            PackageInfo("libopenblas", "0.3.27", "pthreads_hac2b453_1", 0),
            PackageInfo("libblas", "3.9.0", "22_linux64_openblas", 0),
            PackageInfo("libcblas", "3.9.0", "22_linux64_openblas", 0),
            PackageInfo("liblapack", "3.9.0", "22_linux64_openblas", 0),
            PackageInfo("tzdata", "2024a", "h0c530f3_0", 0),
            PackageInfo("python", "3.10.14", "hd12c33a_0_cpython", 0),
            PackageInfo("wheel", "0.43.0", "pyhd8ed1ab_1", 0),
            PackageInfo("setuptools", "70.1.1", "pyhd8ed1ab_0", 0),
            PackageInfo("pip", "24.0", "pyhd8ed1ab_0", 0),
            PackageInfo("threadpoolctl", "3.5.0", "pyhc1e730c_0", 0),
            PackageInfo("joblib", "1.4.2", "pyhd8ed1ab_0", 0),
            PackageInfo("numpy", "1.26.4", "py310hb13e2d6_0", 0),
            PackageInfo("scipy", "1.14.0", "py310h93e2701_1", 0),
            PackageInfo("scikit-learn", "1.5.0", "py310h981052a_1", 1)
        };

        std::sort(
            known_resolution.begin(),
            known_resolution.end(),
            [&](const PackageInfo& a, const PackageInfo& b) { return a.name < b.name; }
        );

        std::vector<PackageInfo> resolvo_resolution = resolvo_resolve(resolvo_db, specs_to_install);
        std::vector<PackageInfo> libsolv_resolution = libsolv_resolve(libsolv_db, specs_to_install);

        // Check libsolv's PackageInfo against the know resolution
        for (size_t i = 0; i < libsolv_resolution.size(); i++)
        {
            const PackageInfo& package_info = libsolv_resolution[i];
            const PackageInfo& known_package_info = known_resolution[i];
            CHECK_EQ(package_info.name, known_package_info.name);
            CHECK_EQ(package_info.version, known_package_info.version);
            CHECK_EQ(package_info.build_string, known_package_info.build_string);
        }

        // Check resolvo's PackageInfo against the know resolution
        for (size_t i = 0; i < resolvo_resolution.size(); i++)
        {
            const PackageInfo& package_info = resolvo_resolution[i];
            const PackageInfo& known_package_info = known_resolution[i];
            CHECK_EQ(package_info.name, known_package_info.name);
            CHECK_EQ(package_info.version, known_package_info.version);
            CHECK_EQ(package_info.build_string, known_package_info.build_string);
        }
    }

    TEST_CASE("mamba-org/rattler/issues/684")
    {
        for (const std::vector<std::string>& specs_to_install : std::initializer_list<std::vector<std::string>> {
              // TODO: Currently does not probably due to the ordering of the packages on track features
              {"arrow-cpp", "abseil-cpp"},
//            {"mlflow=2.12.2"},
//            {"orange3=3.36.2"},
//            {"ray-dashboard=2.6.3"},
//            {"ray-default=2.6.3"},
//            {"spark-nlp=5.1.2"},
//            {"spyder=5.5.1"},
//            {"streamlit-faker=0.0.2"}
        })
        {
            SUBCASE("")
            {
                // See: https://github.com/mamba-org/rattler/issues/684
                std::vector<PackageInfo> libsolv_resolution = libsolv_resolve(
                    libsolv_db,
                    specs_to_install
                );

                // Print all the packages from libsolv
                std::cout << "libsolv resolution:" << std::endl;
                for (const auto& package_info : libsolv_resolution)
                {
                    std::cout << " - " << package_info.long_str() << std::endl;
                }

                std::cout << std::endl;
                std::vector<PackageInfo> resolvo_resolution = resolvo_resolve(
                    resolvo_db,
                    specs_to_install
                );

                // Print all the packages from resolvo
                std::cout << "resolvo resolution:" << std::endl;
                for (const auto& package_info : resolvo_resolution)
                {
                    std::cout << " - " << package_info.long_str() << std::endl;
                }

                CHECK_GT(resolvo_resolution.size(), 0);
                CHECK_GT(libsolv_resolution.size(), 0);
                // Check libsolv's PackageInfo against libsolv's
                CHECK_EQ(resolvo_resolution.size(), libsolv_resolution.size());
                for (size_t i = 0; i < std::min(resolvo_resolution.size(), libsolv_resolution.size()); i++)
                {
                    const PackageInfo& resolvo_package_info = resolvo_resolution[i];
                    const PackageInfo& libsolv_package_info = libsolv_resolution[i];
                    // Currently something in the parsing of the repodata.json must be different.
                    // TODO: find the difference and use `PackageInfo::operator==` instead
                    CHECK_EQ(resolvo_package_info.name, libsolv_package_info.name);
                    CHECK_EQ(resolvo_package_info.version, libsolv_package_info.version);
                    CHECK_EQ(resolvo_package_info.build_string, libsolv_package_info.build_string);
                }
            }
        }
    }

    TEST_CASE("Find the highest version of hypothesis")
    {
        // Some builds of hypothesis depends on attrs and vice-versa
        // We test that this complete correctly.
        auto vid = resolvo_db.alloc_version_set("hypothesis");
        auto [version, n_track_features] = resolvo_db.find_highest_version(vid);
        CHECK_EQ(n_track_features, 0);
        std::cout << "Version: " << version.str() << std::endl;
        CHECK_GE(version, Version::parse("6.105.1").value());
    }

    TEST_CASE("Consistency with libsolv: yaml env specifications") {

        for(const std::string& s : {
            "/tmp/unconstrained_small_spec6.yaml",
            // "/tmp/unconstrained_small_spec5.yaml",
            // "/tmp/unconstrained_small_spec4.yaml",
            // "/tmp/unconstrained_small_spec3.yaml",
            // "/tmp/unconstrained_small_spec3.yaml",
            // "/tmp/small_spec.yaml",
        }) {
            SUBCASE("") {
                auto env_specification = mamba::detail::read_yaml_file(
                    s,
                    "linux-64"
                );
                std::vector<std::string> specs_to_install = env_specification.dependencies;

                std::vector<PackageInfo> libsolv_resolution = libsolv_resolve(
                    libsolv_db,
                    specs_to_install
                );

//                std::cout << "libsolv resolution:" << std::endl;
//                for (const auto& package_info : libsolv_resolution)
//                {
//                    std::cout << " - " << package_info.long_str() << std::endl;
//                }

                std::vector<PackageInfo> resolvo_resolution = resolvo_resolve(
                    resolvo_db,
                    specs_to_install
                );

//                std::cout << "resolvo resolution:" << std::endl;
//                for (const auto& package_info : resolvo_resolution)
//                {
//                    std::cout << " - " << package_info.long_str() << std::endl;
//                }

                // Check libsolv's PackageInfo against libsolv's
//                CHECK_EQ(resolvo_resolution.size(), libsolv_resolution.size());
//                for (size_t i = 0; i < libsolv_resolution.size(); i++)
//                {
//                    const PackageInfo& resolvo_package_info = resolvo_resolution[i];
//                    const PackageInfo& libsolv_package_info = libsolv_resolution[i];
//                    // Currently something in the parsing of the repodata.json must be different.
//                    // TODO: find the difference and use `PackageInfo::operator==` instead
//                    CHECK_EQ(resolvo_package_info.name, libsolv_package_info.name);
//                    CHECK_EQ(resolvo_package_info.version, libsolv_package_info.version);
//                    CHECK_EQ(resolvo_package_info.build_string, libsolv_package_info.build_string);
//                }
            }
        }

    }

    TEST_CASE("Consistency with libsolv: robin-env specifications") {
        for (const std::string& specification: {
            // See: https://github.com/conda-forge/rubinenv-feedstock/blob/main/recipe/meta.yaml#L45-L191
            "rubin-env-nosysroot",
//            "rubin-env",
//            "rubin-env-rsp",
//            "rubin-env-developer"
        })
        {
            SUBCASE("")
            {
                std::cout << "Resolving " << specification << std::endl;

                std::vector<std::string> specs_to_install = {specification};

                std::vector<PackageInfo> libsolv_resolution = libsolv_resolve(
                    libsolv_db,
                    specs_to_install
                );
                std::vector<PackageInfo> resolvo_resolution = resolvo_resolve(
                    resolvo_db,
                    specs_to_install
                );

                // Print all the packages from libsolv
                std::cout << "libsolv resolution:" << std::endl;
                for (const auto& package_info : libsolv_resolution)
                {
                    std::cout << " - " << package_info.long_str() << std::endl;
                }

                std::cout << std::endl;

                // Print all the packages from resolvo
                std::cout << "resolvo resolution:" << std::endl;
                for (const auto& package_info : resolvo_resolution)
                {
                    std::cout << " - " << package_info.long_str() << std::endl;
                }

                // Check libsolv's PackageInfo against libsolv's
                CHECK_EQ(resolvo_resolution.size(), libsolv_resolution.size());
                for (size_t i = 0; i < libsolv_resolution.size(); i++)
                {
                    const PackageInfo& resolvo_package_info = resolvo_resolution[i];
                    const PackageInfo& libsolv_package_info = libsolv_resolution[i];
                    // Currently something in the parsing of the repodata.json must be different.
                    // TODO: find the difference and use `PackageInfo::operator==` instead
                    CHECK_EQ(resolvo_package_info.name, libsolv_package_info.name);
                    CHECK_EQ(resolvo_package_info.version, libsolv_package_info.version);
                    CHECK_EQ(resolvo_package_info.build_string, libsolv_package_info.build_string);
                }
            }
        }
    }
}