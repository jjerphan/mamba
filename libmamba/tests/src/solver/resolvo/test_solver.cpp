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

#include <simdjson.h>

#include "mamba/core/util.hpp"  // for LockFile
#include "mamba/specs/channel.hpp"
#include "mamba/specs/package_info.hpp"

// TODO: move PackageTypes and MAX_CONDA_TIMESTAMP to a common place
#include "mamba/solver/libsolv/parameters.hpp" // for PackageTypes

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

    /**
     * Allocates a new requirement and return the id of the requirement.
     */
    VersionSetId alloc_version_set(
        std::string_view raw_match_spec
    ) {
        std::string raw_match_spec_str = std::string(raw_match_spec);
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
            for (const std::string& match_spec : match_specs)
            {
                alloc_version_set(match_spec);
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
            std::cout << "Displaying solvable " << solvable_id.id << " " << solvable_pool[solvable_id].long_str() << std::endl;
            result += std::to_string(solvable_id.id) + " " + solvable_pool[solvable_id].long_str();
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
        std::cout << "Getting name id for solvable " << package_info.long_str() << std::endl;
        return name_pool[String{package_info.name}];
    }

    /**
     * Obtains a list of solvables that should be considered when a package
     * with the given name is requested.
     */
    Candidates get_candidates(NameId package) override {
        std::cout << "Getting candidates for " << name_pool[package] << std::endl;
        Candidates candidates;
        candidates.favored = nullptr;
        candidates.locked = nullptr;
        // TODO: inefficient for now, O(n) which can be turned into O(1)
        for (auto& [solvable_id, package_info] : solvable_pool) {
            std::cout << "  Checking " << package_info.long_str() << std::endl;
            if (package == solvable_name(solvable_id)) {
                std::cout << "  Adding candidate " << package_info.long_str() << std::endl;
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
            const auto a_version = Version::parse(package_info_a.version).value();
            const auto b_version = Version::parse(package_info_b.version).value();

            if (a_version != b_version) {
                return a_version < b_version;
            }
            // TODO: add sorting on track features and other things

            return package_info_a.build_number < package_info_b.build_number;
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
        std::cout << "Getting dependencies for " << package_info.long_str() << std::endl;
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

// TODO: reuse it from `mamba/solver/libsolv/helpers.cpp`
auto lsplit_track_features(std::string_view features)
{
    constexpr auto is_sep = [](char c) -> bool { return (c == ',') || util::is_space(c); };
    auto [_, tail] = util::lstrip_if_parts(features, is_sep);
    return util::lstrip_if_parts(tail, [&](char c) { return !is_sep(c); });
}

bool parse_packageinfo_json(
    const std::string_view& filename,
    const simdjson::dom::element& pkg,
    PackageDatabase& database
    ) {
    PackageInfo package_info;

    package_info.filename = filename;
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

    std::cout << "Parsing repodata.json" << std::endl;
    auto added = util::flat_set<std::string_view>();
    if (auto pkgs = repodata["packages.conda"].get_object(); !pkgs.error())
    {
        std::cout << "CondaOrElseTarBz2 packages.conda" << std::endl;

        for (auto [key, value] : pkgs.value())
        {
            parse_packageinfo_json(key, value, database);
        }
    }
    if (auto pkgs = repodata["packages"].get_object(); !pkgs.error())
    {
        std::cout << "CondaOrElseTarBz2 packages" << std::endl;

        for (auto [key, value] : pkgs.value())
        {
            parse_packageinfo_json(key, value, database);
        }
    }

}

TEST_SUITE("solver::resolvo")
{
    using PackageInfo = PackageInfo;

    TEST_CASE("Addition of PackageInfo to PackageDatabase") {

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

        CHECK(database.version_set_pool[deps.requirements[0]].str() == "numpy[version=\">=1.20.0,<2.0a0\"]");
        CHECK(database.version_set_pool[deps.requirements[1]].str() == "scipy[version=\">=1.6.0,<2.0a0\"]");
        CHECK(database.version_set_pool[deps.requirements[2]].str() == "joblib[version=\">=1.0.1,<2.0a0\"]");
        CHECK(database.version_set_pool[deps.requirements[3]].str() == "threadpoolctl[version=\">=2.1.0,<3.0a0\"]");

        CHECK(database.name_pool.find(String{"scikit-learn"}) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{"numpy"}) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{"scipy"}) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{"joblib"}) != database.name_pool.end_ids());
        CHECK(database.name_pool.find(String{"threadpoolctl"}) != database.name_pool.end_ids());

        CHECK(database.string_pool.find(String{"scikit-learn"}) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{"numpy"}) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{"scipy"}) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{"joblib"}) != database.string_pool.end_ids());
        CHECK(database.string_pool.find(String{"threadpoolctl"}) != database.string_pool.end_ids());
    }

    TEST_CASE("Filter solvables") {
        PackageDatabase database;

        PackageInfo skl0("scikit-learn", "1.4.0", "py310h981052a_0", 0);
        auto sol0 = database.alloc_solvable(skl0);

        PackageInfo skl1("scikit-learn", "1.5.0", "py310h981052a_1", 1);
        auto sol1 = database.alloc_solvable(skl1);

        PackageInfo skl2("scikit-learn", "1.5.1", "py310h981052a_0", 0);
        auto sol2 = database.alloc_solvable(skl2);

        PackageInfo skl3("scikit-learn", "1.5.1", "py310h981052a_2", 2);
        auto sol3 = database.alloc_solvable(skl3);

        auto solvables = Vector<SolvableId>{sol0, sol1, sol2, sol3};

        // Filter on scikit-learn
        auto all = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn"), false);
        CHECK(all.size() == 4);
        CHECK(all[0] == sol0);
        CHECK(all[1] == sol1);
        CHECK(all[2] == sol2);
        CHECK(all[3] == sol3);

        // Inverse filter on scikit-learn
        auto none = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn"), true);
        CHECK(none.size() == 0);

        // Filter on scikit-learn==1.5.1
        auto one = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn==1.5.1"), false);
        CHECK(one.size() == 2);
        CHECK(one[0] == sol2);
        CHECK(one[1] == sol3);

        // Inverse filter on scikit-learn==1.5.1
        auto three = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn==1.5.1"), true);
        CHECK(three.size() == 2);
        CHECK(three[0] == sol0);
        CHECK(three[1] == sol1);

        // Filter on scikit-learn<1.5.1
        auto two = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn<1.5.1"), false);
        CHECK(two.size() == 2);
        CHECK(two[0] == sol0);
        CHECK(two[1] == sol1);

        // Filter on build number 0
        auto build = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn[build_number==0]"), false);
        CHECK(build.size() == 2);
        CHECK(build[0] == sol0);
        CHECK(build[1] == sol2);

        // Filter on build number 2
        auto build_bis = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn[build_number==2]"), false);
        CHECK(build_bis.size() == 1);
        CHECK(build_bis[0] == sol3);

        // Filter on build number 3
        auto build_ter = database.filter_candidates(solvables, database.alloc_version_set("scikit-learn[build_number==3]"), false);
        CHECK(build_ter.size() == 0);

    }

    TEST_CASE("Sort solvables increasing order") {
        PackageDatabase database;

        PackageInfo skl0("scikit-learn", "1.5.2", "py310h981052a_0", 0);
        auto sol0 = database.alloc_solvable(skl0);

        PackageInfo skl1("scikit-learn", "1.5.0", "py310h981052a_1", 1);
        auto sol1 = database.alloc_solvable(skl1);

        PackageInfo skl2("scikit-learn", "1.5.1", "py310h981052a_2", 2);
        auto sol2 = database.alloc_solvable(skl2);

        PackageInfo skl3("scikit-learn", "1.5.0", "py310h981052a_2", 2);
        auto sol3 = database.alloc_solvable(skl3);

        PackageInfo scikit_learn_ter("scikit-learn", "1.5.1", "py310h981052a_1", 1);
        auto sol4 = database.alloc_solvable(skl3);

        Vector<SolvableId> solvables = {sol0, sol1, sol2, sol3, sol4};

        database.sort_candidates(solvables);

        CHECK(solvables[0] == sol1);
        CHECK(solvables[1] == sol3);
        CHECK(solvables[2] == sol4);
        CHECK(solvables[3] == sol2);
        CHECK(solvables[4] == sol0);

    }

    TEST_CASE("Trivial problem") {

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

    TEST_CASE("Parse repodata.json")
    {
        PackageDatabase database;

        parse_repodata_json(
            database,
            "/tmp/repodata.json",
            "https://conda.anaconda.org/conda-forge/linux-64/repodata.json",
            "conda-forge",
            false
        );

        std::cout << "Number of solvables: " << database.solvable_pool.size() << std::endl;

    }

}