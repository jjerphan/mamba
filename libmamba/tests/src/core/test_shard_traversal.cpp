// Copyright (c) 2024, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include <algorithm>
#include <cstdint>

#include <catch2/catch_all.hpp>

#include "mamba/core/channel_context.hpp"
#include "mamba/core/shard_traversal.hpp"
#include "mamba/core/shard_types.hpp"
#include "mamba/core/shards.hpp"
#include "mamba/download/parameters.hpp"
#include "mamba/specs/channel.hpp"
#include "mamba/specs/conda_url.hpp"
#include "mamba/specs/unresolved_channel.hpp"

#include "mambatests.hpp"

using namespace mamba;

namespace
{
    auto make_simple_channel(std::string_view chan) -> specs::Channel
    {
        const auto resolve_params = ChannelContext::ChannelResolveParams{
            { "linux-64", "noarch" },
            specs::CondaURL::parse("https://conda.anaconda.org").value()
        };

        return specs::Channel::resolve(specs::UnresolvedChannel::parse(chan).value(), resolve_params)
            .value()
            .front();
    }

    /** Create Shards with pre-loaded shard data for RepodataSubset testing. */
    auto create_shards_with_preloaded_deps(
        const std::string& channel_url,
        std::map<std::string, ShardDict> shards_by_package
    ) -> std::shared_ptr<Shards>
    {
        ShardsIndexDict index;
        index.info.base_url = "https://example.com/packages";
        index.info.shards_base_url = "shards";
        index.info.subdir = "linux-64";
        index.version = 1;

        for (const auto& [pkg, _] : shards_by_package)
        {
            index.shards[pkg] = std::vector<std::uint8_t>(32, 0xAB);
        }

        specs::Channel channel = make_simple_channel(channel_url);
        specs::AuthenticationDataBase auth_info;
        download::RemoteFetchParams remote_fetch_params;

        auto shards = std::make_shared<Shards>(
            index,
            channel_url + "/linux-64/repodata.json",
            channel,
            auth_info,
            remote_fetch_params
        );

        for (auto& [pkg, shard] : shards_by_package)
        {
            shards->process_fetched_shard(pkg, shard);
        }

        return shards;
    }
}

TEST_CASE("NodeId equality and ordering", "[mamba::core][mamba::core::shard_traversal]")
{
    SECTION("NodeId equality")
    {
        NodeId a{ "python", "https://conda-forge/linux-64", "https://shards/abc.msgpack.zst" };
        NodeId b{ "python", "https://conda-forge/linux-64", "https://shards/abc.msgpack.zst" };
        NodeId c{ "numpy", "https://conda-forge/linux-64", "https://shards/abc.msgpack.zst" };

        REQUIRE(a == b);
        REQUIRE_FALSE(a == c);
    }

    SECTION("NodeId ordering")
    {
        NodeId a{ "a", "ch1", "url1" };
        NodeId b{ "b", "ch1", "url1" };
        NodeId c{ "a", "ch2", "url1" };

        REQUIRE(a < b);
        REQUIRE(a < c);
        REQUIRE_FALSE(b < a);
    }

    SECTION("NodeId ordering by shard_url")
    {
        NodeId a{ "pkg", "ch", "url1" };
        NodeId b{ "pkg", "ch", "url2" };

        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("NodeId reflexive equality")
    {
        NodeId a{ "x", "y", "z" };
        REQUIRE(a == a);
    }

    SECTION("NodeId distinct packages same channel")
    {
        NodeId a{ "a", "ch", "url" };
        NodeId b{ "b", "ch", "url" };
        REQUIRE_FALSE(a == b);
    }
}

TEST_CASE("Node to_id", "[mamba::core][mamba::core::shard_traversal]")
{
    Node node{ 1, "python", "ch", "url", true };
    NodeId id = node.to_id();
    REQUIRE(id.package == "python");
    REQUIRE(id.channel == "ch");
    REQUIRE(id.shard_url == "url");
}

TEST_CASE("RepodataSubset constructor and accessors", "[mamba::core][mamba::core::shard_traversal]")
{
    SECTION("Empty shards")
    {
        RepodataSubset subset({});
        REQUIRE(subset.shards().empty());
        REQUIRE(subset.nodes().empty());
    }

    SECTION("With single shard collection")
    {
        auto shards = create_shards_with_preloaded_deps(
            "https://example.com/conda-forge",
            { { "python", {} } }
        );
        RepodataSubset subset({ *shards });
        REQUIRE(subset.shards().size() == 1);
        REQUIRE(subset.shards()[0].url() == shards->url());
    }
}

TEST_CASE("RepodataSubset reachable empty", "[mamba::core][mamba::core::shard_traversal]")
{
    SECTION("Empty root_packages returns early")
    {
        auto shards = create_shards_with_preloaded_deps(
            "https://example.com/conda-forge",
            { { "python", {} } }
        );
        RepodataSubset subset({ *shards });
        subset.reachable({}, "pipelined", std::nullopt);
        REQUIRE(subset.nodes().empty());
    }

    SECTION("Empty root_packages with bfs")
    {
        auto shards = create_shards_with_preloaded_deps(
            "https://example.com/conda-forge",
            { { "python", {} } }
        );
        RepodataSubset subset({ *shards });
        subset.reachable({}, "bfs", std::nullopt);
        REQUIRE(subset.nodes().empty());
    }
}

TEST_CASE("RepodataSubset reachable pipelined", "[mamba::core][mamba::core::shard_traversal]")
{
    ShardDict python_shard;
    ShardPackageRecord python_rec;
    python_rec.name = "python";
    python_rec.depends = { "numpy" };
    python_shard.packages["python-3.11-0.conda"] = python_rec;

    ShardDict numpy_shard;
    ShardPackageRecord numpy_rec;
    numpy_rec.name = "numpy";
    numpy_rec.depends = { "libffi" };
    numpy_shard.packages["numpy-1.24-0.conda"] = numpy_rec;

    ShardDict libffi_shard;
    ShardPackageRecord libffi_rec;
    libffi_rec.name = "libffi";
    libffi_rec.depends = {};
    libffi_shard.packages["libffi-1.0-0.conda"] = libffi_rec;

    auto shards = create_shards_with_preloaded_deps(
        "https://example.com/conda-forge",
        {
            { "python", python_shard },
            { "numpy", numpy_shard },
            { "libffi", libffi_shard },
        }
    );

    RepodataSubset subset({ *shards });
    subset.reachable({ "python" }, "pipelined", std::nullopt);

    const auto& nodes = subset.nodes();
    REQUIRE(nodes.size() >= 3);

    auto has_package = [&nodes](const std::string& pkg)
    {
        return std::find_if(
                   nodes.begin(),
                   nodes.end(),
                   [&pkg](const auto& p) { return p.first.package == pkg; }
               )
               != nodes.end();
    };
    REQUIRE(has_package("python"));
    REQUIRE(has_package("numpy"));
    REQUIRE(has_package("libffi"));
}

TEST_CASE("RepodataSubset reachable bfs", "[mamba::core][mamba::core::shard_traversal]")
{
    ShardDict python_shard;
    ShardPackageRecord python_rec;
    python_rec.name = "python";
    python_rec.depends = { "numpy" };
    python_shard.packages["python-3.11-0.conda"] = python_rec;

    ShardDict numpy_shard;
    ShardPackageRecord numpy_rec;
    numpy_rec.name = "numpy";
    numpy_rec.depends = { "libffi" };
    numpy_shard.packages["numpy-1.24-0.conda"] = numpy_rec;

    ShardDict libffi_shard;
    ShardPackageRecord libffi_rec;
    libffi_rec.name = "libffi";
    libffi_rec.depends = {};
    libffi_shard.packages["libffi-1.0-0.conda"] = libffi_rec;

    auto shards = create_shards_with_preloaded_deps(
        "https://example.com/conda-forge",
        {
            { "python", python_shard },
            { "numpy", numpy_shard },
            { "libffi", libffi_shard },
        }
    );

    RepodataSubset subset({ *shards });
    subset.reachable({ "python" }, "bfs", std::nullopt);

    const auto& nodes = subset.nodes();
    REQUIRE(nodes.size() >= 3);

    auto has_package = [&nodes](const std::string& pkg)
    {
        return std::find_if(
                   nodes.begin(),
                   nodes.end(),
                   [&pkg](const auto& p) { return p.first.package == pkg; }
               )
               != nodes.end();
    };
    REQUIRE(has_package("python"));
    REQUIRE(has_package("numpy"));
    REQUIRE(has_package("libffi"));
}

TEST_CASE("RepodataSubset reachable with root_shards filter", "[mamba::core][mamba::core::shard_traversal]")
{
    ShardDict python_shard;
    ShardPackageRecord python_rec;
    python_rec.name = "python";
    python_rec.depends = {};
    python_shard.packages["python-3.11-0.conda"] = python_rec;

    auto shards = create_shards_with_preloaded_deps(
        "https://example.com/conda-forge",
        { { "python", python_shard } }
    );

    RepodataSubset subset({ *shards });
    std::set<std::string> root_shards = { shards->shard_url("python") };
    subset.reachable({ "python" }, "pipelined", std::optional{ std::cref(root_shards) });

    REQUIRE(subset.nodes().size() >= 1);
}

TEST_CASE(
    "RepodataSubset reachable root_shards excludes non-matching",
    "[mamba::core][mamba::core::shard_traversal]"
)
{
    ShardDict python_shard;
    ShardPackageRecord python_rec;
    python_rec.name = "python";
    python_rec.depends = {};
    python_shard.packages["python-3.11-0.conda"] = python_rec;

    auto shards = create_shards_with_preloaded_deps(
        "https://example.com/conda-forge",
        { { "python", python_shard } }
    );

    RepodataSubset subset({ *shards });
    std::set<std::string> root_shards = { "https://nonexistent/shard.msgpack.zst" };
    subset.reachable({ "python" }, "pipelined", std::optional{ std::cref(root_shards) });

    REQUIRE(subset.nodes().empty());
}

TEST_CASE("RepodataSubset multiple channels", "[mamba::core][mamba::core::shard_traversal]")
{
    ShardDict python_shard;
    ShardPackageRecord python_rec;
    python_rec.name = "python";
    python_rec.depends = {};
    python_shard.packages["python-3.11-0.conda"] = python_rec;

    auto cf_shards = create_shards_with_preloaded_deps(
        "https://conda-forge.org/channels/conda-forge",
        { { "python", python_shard } }
    );

    auto defaults_shards = create_shards_with_preloaded_deps(
        "https://repo.anaconda.com/pkgs/main",
        { { "python", python_shard } }
    );

    RepodataSubset subset({ *cf_shards, *defaults_shards });
    subset.reachable({ "python" }, "pipelined", std::nullopt);

    REQUIRE(subset.nodes().size() >= 2);
}

TEST_CASE("RepodataSubset package not in any shard", "[mamba::core][mamba::core::shard_traversal]")
{
    ShardDict python_shard;
    ShardPackageRecord python_rec;
    python_rec.name = "python";
    python_rec.depends = {};
    python_shard.packages["python-3.11-0.conda"] = python_rec;

    auto shards = create_shards_with_preloaded_deps(
        "https://example.com/conda-forge",
        { { "python", python_shard } }
    );

    RepodataSubset subset({ *shards });
    subset.reachable({ "nonexistent_package" }, "pipelined", std::nullopt);

    REQUIRE(subset.nodes().empty());
}

TEST_CASE("RepodataSubset default strategy is bfs", "[mamba::core][mamba::core::shard_traversal]")
{
    ShardDict python_shard;
    ShardPackageRecord python_rec;
    python_rec.name = "python";
    python_rec.depends = {};
    python_shard.packages["python-3.11-0.conda"] = python_rec;

    auto shards = create_shards_with_preloaded_deps(
        "https://example.com/conda-forge",
        { { "python", python_shard } }
    );

    RepodataSubset subset({ *shards });
    subset.reachable({ "python" });

    REQUIRE(subset.nodes().size() >= 1);
}
