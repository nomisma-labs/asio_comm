// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include "../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

#include "config.h"

struct response_received
{
    nom::https::request_id id;
    const nom::http_response& response;
};

template <typename Traits> struct response_output
{
    void on_response(const response_received& event)
    {
        LINFO("Response {}: HTTP {} ({} bytes): {}",
              event.id,
              event.response.status_code,
              event.response.body.size(),
              event.response.body);
    }
};

template <typename Traits> class https_client
{
public:
    struct context_mixin
    {
        bool success{};
    };

    explicit https_client(const app_config& config)
        : client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)), remaining_(config.requests)
    {
    }

    void on_request(nom::event::service_command action)
    {
        switch (action)
        {
        case nom::event::service_command::start:
            client_.connect(nom::https::https_delegate{std::ref(*this)});
            break;
        case nom::event::service_command::stop:
            client_.close();
            break;
        }
    }

    void operator()(nom::https& client, const nom::event::on_connected&)
    {
        // The URL's encoded path/query is the request target; Host and TLS identity come from its origin.
        LINFO("{} connected", client.session_id());
        client.send(nom::http_request{.target = client.url().target()});
    }

    void operator()(nom::https& client, nom::https::request_id id, const nom::http_response& response)
    {
        nom::relay<Traits>::pass_response(this, response_received{id, response});
        if (response.status_code < 200 || response.status_code >= 300 || !std::cout)
        {
            client.close();
            return;
        }
        if (--remaining_ == 0)
        {
            nom::ctx(this).success = true;
            client.close();
        }
        else if (client.is_connected())
        {
            // Re-entry is intentional: https queues this until response delivery has returned.
            client.send(nom::http_request{.target = client.url().target()});
        }
    }

    void operator()(nom::https&, const nom::event::on_disconnected& event)
    {
        if (!nom::ctx(this).success)
            LERROR("Disconnected before completing requests: {}", event.reason);
    }

    void operator()(nom::https&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
    }

private:
    nom::https client_;
    unsigned remaining_;
};

struct app_assembly : nom::assembly<app_assembly, asio_service, response_output, https_client>
{
};
struct app : nom::relay<app_assembly>
{
    explicit app(const app_config& config) : nom::relay<app_assembly>(config)
    {
    }
    int run()
    {
        request(nom::event::service_command::start);
        ctx().io.run();
        return ctx().success ? 0 : 1;
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: https_example [https://HOST[:PORT]/PATH [REQUESTS]]\n"
                     "GET and print the response; repeat on the same connection when REQUESTS > 1.\n";
        return 0;
    }
    nom::init_logger(nom::main_logger);
    try
    {
        const auto config = parse_arguments(argc, argv);
        if (const auto valid = config.validate(); !valid)
            throw std::invalid_argument(valid.error());
        app application(config);
        const auto result = application.run();
        nom::flush_logger(nom::main_logger);
        return result;
    }
    catch (const std::exception& error)
    {
        LERROR("{}", error.what());
        nom::flush_logger(nom::main_logger);
        return 1;
    }
}
