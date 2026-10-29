// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <spd_logger/logging.h>

import std;

import nom.assembly;
import nom.spd_logger;

namespace example
{
    struct app_config
    {
        std::string_view app_name{"asio_comm_example"};
        std::string_view message{"Hello from asio_comm"};
        nom::log_config log{.level = nom::log_level::info};
    };

    // Relay calls are synchronous; these borrowed values outlive the whole workflow.
    struct start_app
    {
        const app_config& config;
    };

    struct app_finished
    {
        std::string_view app_name;
    };

    template <typename Traits> struct startup_node
    {
        void on_request(const start_app& event)
        {
            LINFO("{} app started with config: message='{}', log_level={}",
                  event.config.app_name,
                  event.config.message,
                  nom::to_string(event.config.log.level));
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
            LINFO("{}", event.config.message);
            LWARN("Example warning: this is a logging demonstration");
            LERROR("Example error: no operation actually failed");
            nom::relay<Traits>::pass_response(this, app_finished{event.config.app_name});
        }
    };

    struct app_assembly : nom::assembly<app_assembly, startup_node, log_messages_node>
    {
    };
} // namespace example

int main()
{
    try
    {
        const example::app_config config;
        nom::init_logger(nom::main_logger, config.log);
        nom::relay<example::app_assembly> workflow;
        workflow.request(example::start_app{config});
        nom::flush_logger(nom::main_logger);
        return 0;
    }
    catch (const std::exception& error)
    {
        // Logger initialization can fail before a logger is available.
        std::cerr << error.what() << '\n';
        return 1;
    }
}
