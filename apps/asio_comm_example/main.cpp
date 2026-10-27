// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <json_config/config.h>
#include <spd_logger/logging.h>

#include <exception>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>

import nom.assembly;
import nom.config_types;
import nom.json_config;
import nom.spd_logger;

namespace example
{
    struct app_config
    {
        nom::cfg_defaulted<nom::cfg_string> app_name{"asio_comm_example"};
        nom::cfg_defaulted<nom::cfg_string> message{"Hello from asio_comm"};
        nom::cfg_defaulted<nom::cfg_bool> debug_logging{false};

        std::expected<void, std::string> validate() const
        {
            if (app_name->empty() || message->empty())
            {
                return std::unexpected("app_name and message must be nonempty");
            }
            return {};
        }
    };

    NOM_JCONFIG(app_config, app_name, message, debug_logging)

    // Relay calls are synchronous; these borrowed values outlive the whole workflow.
    struct start_app
    {
        const app_config& config;
        nom::jcfg::config_source source;
    };

    struct app_finished
    {
        std::string_view app_name;
    };

    template <typename Traits> struct startup_node
    {
        void on_request(const start_app& event)
        {
            LINFO("{} app started with config: message='{}', debug_logging={}",
                  *event.config.app_name,
                  *event.config.message,
                  *event.config.debug_logging);
            LDEBUG("Configuration source: {}", nom::jcfg::config_source_name(event.source));
            nom::relay<Traits>::pass_request(this, event);
        }

        void on_response(const app_finished& event)
        {
            LALWAYS("{} finished", event.app_name);
        }
    };

    template <typename Traits> struct log_messages_node
    {
        void on_request(const start_app& event)
        {
            LINFO("{}", *event.config.message);
            LWARN("Example warning: this is a logging demonstration");
            LERROR("Example error: no operation actually failed");
            nom::relay<Traits>::pass_response(this, app_finished{*event.config.app_name});
        }
    };

    struct app_assembly : nom::assembly<app_assembly, startup_node, log_messages_node>
    {
    };
} // namespace example

int main(int argc, char** argv)
{
    try
    {
        const auto arguments = nom::jcfg::parse_config_arguments(argc, argv);
        if (!arguments.remaining_arguments.empty())
        {
            std::cerr << "Usage: asio_comm_example [--config FILE] [--set POINTER=value] [--check-config]\n";
            return 1;
        }

        const auto loaded = nom::jcfg::load_config<example::app_config>(arguments.options);
        const auto& config = loaded.value;
        if (arguments.options.check_config)
        {
            std::cout << "Config OK for " << *config.app_name << '\n';
            return 0;
        }

        nom::init_logger(nom::main_logger,
                         {.level = *config.debug_logging ? nom::log_level::debug : nom::log_level::info});
        nom::relay<example::app_assembly> workflow;
        workflow.request(example::start_app{config, loaded.location.source});
        nom::flush_logger(nom::main_logger);
        return 0;
    }
    catch (const std::exception& error)
    {
        // Configuration and logger initialization can fail before a logger is available.
        std::cerr << error.what() << '\n';
        return 1;
    }
}
