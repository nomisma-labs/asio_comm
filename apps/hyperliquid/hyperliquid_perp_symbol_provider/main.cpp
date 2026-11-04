// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/error_code.hpp>
#include <csignal>
#include <spd_logger/logging.h>

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

#include "config.h"

namespace nom::event
{
    enum class service_command
    {
        start,
        stop
    };

    // Borrowed views are consumed synchronously before the WebSocket callback returns.
    struct symbol_information
    {
        network_timestamp ts;
        std::string_view source;
        std::string_view data;
    };
} // namespace nom::event

// Inspect only known envelope/heartbeat fields; this does not validate JSON or nesting.
// Market payloads remain opaque and no DOM or normalized data model is built.
std::string_view response_field(std::string_view data, std::string_view name)
{
    const auto token = '"' + std::string(name) + '"';
    std::size_t start = 0;
    for (;;)
    {
        const auto key = data.find(token, start);
        if (key == data.npos)
            return {};
        start = key + token.size();
        const auto colon = data.find_first_not_of(" \t\r\n", start);
        if (colon != data.npos && data[colon] == ':')
        {
            start = colon;
            break;
        }
    }
    start = data.find_first_not_of(" \t\r\n", start + 1);
    if (start == data.npos)
        return {};
    if (data[start] == '"')
    {
        auto end = start + 1;
        while (end < data.size())
        {
            if (data[end] == '\\')
                end += 2;
            else if (data[end++] == '"')
                return data.substr(start, end - start);
        }
        return {};
    }
    const auto end = data.find_first_of(",}] \t\r\n", start);
    return data.substr(start, end == data.npos ? data.size() - start : end - start);
}

bool has_api_error(std::string_view data)
{
    const auto error = response_field(data, "error");
    if ((!error.empty() && error != "null") || response_field(data, "type") == "\"error\"" ||
        response_field(data, "channel") == "\"error\"")
        return true;
    return (response_field(data, "type") == "\"error\"");
}

bool is_catalog(std::string_view data)
{
    return (response_field(data, "channel") == "\"post\"") && (response_field(data, "id") == "1") &&
           !response_field(data, "universe").empty() && !response_field(data, "marginTables").empty();
}

template <typename Traits> struct asio_service
{
    struct context_mixin
    {
        // Context services outlive the assembly's nodes and their asynchronous operations.
        boost::asio::io_context io;
        boost::asio::signal_set signals{io, SIGINT, SIGTERM};
        bool success{false};
    };
};

template <typename Traits> struct symbol_output
{
    void on_response(const nom::event::symbol_information& event)
    {
        std::cout << event.data << '\n';
        std::cout.flush();
        if (!std::cout)
        {
            std::cerr << "Failed to write symbol information\n";
            return;
        }
        nom::ctx(this).success = true;
        std::cerr << "Received " << event.data.size() << " bytes from " << event.source << '\n';
    }
};

template <typename Traits> class hyperliquid_perp_client
{
public:
    explicit hyperliquid_perp_client(const app_config& config)
        : client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
        auto params = client_.params();
        params.max_message_size = config.max_message_size;
        client_.update_params(params);
    }

    void on_request(nom::event::service_command command)
    {
        switch (command)
        {
        case nom::event::service_command::start:
            client_.connect(nom::web_socket::ws_delegate{std::ref(*this)});
            break;
        case nom::event::service_command::stop:
            nom::ctx(this).signals.cancel();
            client_.close();
            break;
        }
    }

    void operator()(nom::web_socket&, const nom::event::on_connected&)
    {
        // Connect -> request -> ignore acknowledgements -> catalog/error -> close.
        if (!client_.send(request_))
        {
            std::cerr << "Request send failed\n";
            on_request(nom::event::service_command::stop);
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        const auto data = event.sv_data();
        if (has_api_error(data))
        {
            std::cerr << "API error: " << data << '\n';
            on_request(nom::event::service_command::stop);
            return;
        }
        if (!is_catalog(data))
            return; // Acknowledgements, status messages and heartbeats are not a catalog.
        nom::relay<Traits>::pass_response(
            this, nom::event::symbol_information{.ts = event.ts, .source = connection.session_id(), .data = data});
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        std::cerr << "Disconnected before receiving a catalog: " << event.reason << '\n';
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        std::cerr << event.description << ": " << event.message << '\n';
        on_request(nom::event::service_command::stop);
    }

private:
    // meta returns the default perpetual DEX's universe and margin tables. Other perp DEX catalogs
    // require their own dex parameter; this example deliberately requests the default one only.
    // https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint/perpetuals
    // https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket/post-requests
    // Example universe entry: {"name":"BTC","szDecimals":5,"maxLeverage":40}; values can change.
    const std::string request_{R"({"method":"post","id":1,"request":{"type":"info","payload":{"type":"meta"}}})"};
    nom::web_socket client_;
};

struct hyperliquid_perp_assembly
    : nom::assembly<hyperliquid_perp_assembly, asio_service, symbol_output, hyperliquid_perp_client>
{
};

struct app : nom::relay<hyperliquid_perp_assembly>
{
    explicit app(const app_config& config) : nom::relay<hyperliquid_perp_assembly>(config)
    {
    }

    int start()
    {
        ctx().signals.async_wait(
            [this](const boost::system::error_code& error, int)
            {
                if (!error)
                    request(nom::event::service_command::stop);
            });
        request(nom::event::service_command::start);
        // Relay delivery and callbacks share one thread. Drain canceled handlers before node destruction.
        ctx().io.run();
        return ctx().success ? 0 : 1;
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: hyperliquid_perp_symbol_provider [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
                     "\nDefault endpoint: "
                  << app_config{}.endpoint << '\n';
        return 0;
    }
    try
    {
        const auto config = parse_arguments(argc, argv);
        if (const auto result = config.validate(); !result)
        {
            std::cerr << result.error() << '\n';
            return 1;
        }
        nom::init_logger(nom::main_logger, config.log);
        const auto result = app(config).start();
        nom::flush_logger(nom::main_logger);
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
