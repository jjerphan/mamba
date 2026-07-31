// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include "mamba/api/configuration.hpp"
#include "mamba/core/channel_context.hpp"
#include "mamba/core/context.hpp"
#include "mamba/util/string.hpp"
#include "mamba/version.hpp"

#include "common_options.hpp"
#include "umamba.hpp"
#include "version.hpp"

using namespace mamba;  // NOLINT(build/namespaces)

namespace
{
    // Collect names and aliases of all subcommands registered in the CLI app.
    // Used as candidates for suggesting correction for a mistyped subcommand.
    auto subcommand_names(const CLI::App* app) -> std::vector<std::string>
    {
        std::vector<std::string> names;
        for (const auto& subcom : app->get_subcommands(nullptr))
        {
            names.push_back(subcom->get_name());
            for (const auto& alias : subcom->get_aliases())
            {
                names.push_back(alias);
            }
        }
        return names;
    }

    // Descend from CLI app through the chain of parsed subcommands to the deepest one.
    auto deepest_parsed_app(const CLI::App* app) -> const CLI::App*
    {
        const auto parsed = app->get_subcommands();
        if (parsed.empty())
        {
            return app;
        }
        return deepest_parsed_app(parsed.back());
    }

    // Mirror Conda's behavior of single best suggestion for a subcommand when the user mistypes it.
    auto command_suggestion(const CLI::App* app, const CLI::Error& e) -> std::string
    {
        if (dynamic_cast<const CLI::ExtrasError*>(&e) == nullptr)
        {
            return {};
        }

        const CLI::App* parsed_app = deepest_parsed_app(app);

        // The first leftover argument that is not an option is the token we treat as a mistyped
        // command.
        std::string offending;
        for (const auto& arg : parsed_app->remaining(false))
        {
            if (!util::starts_with(arg, "-"))
            {
                offending = arg;
                break;
            }
        }
        if (offending.empty())
        {
            return {};
        }

        // Rank the candidate commands like Conda does and return the best match if it is above the
        // cutoff.
        const auto matches = util::closest_matches(offending, subcommand_names(parsed_app), 0.6, 1);
        if (matches.empty())
        {
            return {};
        }

        return "Did you mean '" + matches.front() + "'?";
    }

    // CLI11 failure message that appends hint for mistyped subcommand. Falls back to default CLI11
    // message otherwise.
    auto failure_message_with_suggestion(const CLI::App* app, const CLI::Error& e) -> std::string
    {
        std::string base = CLI::FailureMessage::simple(app, e);
        const std::string suggestion = command_suggestion(app, e);
        if (suggestion.empty())
        {
            return base;
        }

        const std::string what = e.what();
        if (base.rfind(what, 0) == 0)
        {
            return base.substr(0, what.size()) + "\n" + suggestion + base.substr(what.size());
        }

        return base + "\n" + suggestion;
    }
}

void
init_umamba_options(CLI::App* subcom, Configuration& config)
{
    init_general_options(subcom, config);
    init_prefix_options(subcom, config);
}

void
set_umamba_command(CLI::App* com, mamba::Configuration& config)
{
    init_umamba_options(com, config);

    auto& context = config.context();

    context.command_params.caller_version = umamba::version();

    auto print_version = [&](int /*count*/)
    {
        if (config.context().output_params.json)
        {
            Console::instance().set_json_output("/version"_json_pointer, umamba::version());
        }
        else
        {
            std::cout << umamba::version() << std::endl;
            exit(0);
        }
    };

    com->add_flag_function("--version", print_version);

    CLI::App* shell_subcom = com->add_subcommand("shell", "Generate shell init scripts");
    set_shell_command(shell_subcom, config);

    CLI::App* create_subcom = com->add_subcommand("create", "Create new environment");
    set_create_command(create_subcom, config);

    CLI::App* install_subcom = com->add_subcommand("install", "Install packages in active environment");
    set_install_command(install_subcom, config);

    CLI::App* update_subcom = com->add_subcommand("update", "Update packages in active environment");
    set_update_command(update_subcom, config);

#ifdef BUILDING_MICROMAMBA
    CLI::App* self_update_subcom = com->add_subcommand("self-update", "Update micromamba");
    set_self_update_command(self_update_subcom, config);
#endif

    CLI::App* repoquery_subcom = com->add_subcommand(
        "repoquery",
        "Find and analyze packages in active environment or channels"
    );
    set_repoquery_command(repoquery_subcom, config);

    CLI::App* remove_subcom = com->add_subcommand("remove", "Remove packages from active environment");
    set_remove_command(remove_subcom, config);
    remove_subcom->alias("uninstall");

    CLI::App* list_subcom = com->add_subcommand("list", "List packages in active environment");
    set_list_command(list_subcom, config);

    CLI::App* package_subcom = com->add_subcommand(
        "package",
        "Extract a package or bundle files into an archive"
    );
    set_package_command(package_subcom, config);

    CLI::App* clean_subcom = com->add_subcommand("clean", "Clean package cache");
    set_clean_command(clean_subcom, config);

    CLI::App* config_subcom = com->add_subcommand("config", "Configuration of micromamba");
    set_config_command(config_subcom, config);

    CLI::App* info_subcom = com->add_subcommand("info", "Information about micromamba");
    set_info_command(info_subcom, config);

    CLI::App* constructor_subcom = com->add_subcommand(
        "constructor",
        "Commands to support using micromamba in constructor"
    );
    set_constructor_command(constructor_subcom, config);

    CLI::App* env_subcom = com->add_subcommand("env", "See `mamba/micromamba env --help`");
    set_env_command(env_subcom, config);

    CLI::App* activate_subcom = com->add_subcommand("activate", "Activate an environment");
    set_activate_command(activate_subcom);

    CLI::App* deactivate_subcom = com->add_subcommand("deactivate", "Deactivate the active environment");
    set_deactivate_command(deactivate_subcom);

    CLI::App* run_subcom = com->add_subcommand("run", "Run an executable in an environment");
    set_run_command(run_subcom, config);

    CLI::App* ps_subcom = com->add_subcommand("ps", "Show, inspect or kill running processes");
    set_ps_command(ps_subcom, context);

    CLI::App* auth_subcom = com->add_subcommand("auth", "Login or logout of a given host");
    set_auth_command(auth_subcom);

    CLI::App* search_subcom = com->add_subcommand(
        "search",
        "Find packages in active environment or channels\n"
        "This is equivalent to `repoquery search` command"
    );
    set_repoquery_search_command(search_subcom, config);

    com->require_subcommand(/* min */ 0, /* max */ 1);

    com->failure_message(&failure_message_with_suggestion);
}
