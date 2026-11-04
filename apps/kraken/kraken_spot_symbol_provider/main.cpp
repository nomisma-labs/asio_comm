// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include "../../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

#include "config.h"

namespace nom::event
{
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
    return (!error.empty() && error != "null") || response_field(data, "success") == "false";
}

bool is_catalog(std::string_view data)
{
    return (response_field(data, "channel") == "\"instrument\"") && (response_field(data, "type") == "\"snapshot\"") &&
           !response_field(data, "pairs").empty();
}

template <typename Traits> struct symbol_output
{
    void on_response(const nom::event::symbol_information& event)
    {
        std::cout << event.data << '\n';
        std::cout.flush();
        if (!std::cout)
        {
            std::cerr << "Failed to write symbol information\n";
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
            return;
        }
        std::cerr << "Received " << event.data.size() << " bytes from " << event.source << '\n';
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::completed);
    }
};

template <typename Traits> class kraken_spot_client
{
public:
    explicit kraken_spot_client(const app_config& config)
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
            client_.close();
            break;
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::on_connected&)
    {
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::connected);
        if (!connection.is_connected())
            return;
        // Connect -> request -> ignore acknowledgements -> catalog/error -> close.
        if (!client_.send(request_))
        {
            std::cerr << "Request send failed\n";
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        const auto data = event.sv_data();
        if (has_api_error(data))
        {
            std::cerr << "API error: " << data << '\n';
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
            return;
        }
        if (!is_catalog(data))
            return; // Acknowledgements, status messages and heartbeats are not a catalog.
        nom::relay<Traits>::pass_response(
            this, nom::event::symbol_information{.ts = event.ts, .source = connection.session_id(), .data = data});
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        std::cerr << "Disconnected before receiving a catalog: " << event.reason << '\n';
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        std::cerr << event.description << ": " << event.message << '\n';
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

private:
    // instrument provides an initial reference-data snapshot, then changes. Close after that snapshot.
    // Crypto pairs only: omit tokenized assets; v2 uses BTC/USD rather than the v1 XBT name.
    // https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/instrument
    // Example: {"channel":"instrument","type":"snapshot","data":{"pairs":[{"symbol":"BTC/USD"}],"assets":[]}}.
    const std::string request_{
        R"({"method":"subscribe","params":{"channel":"instrument","snapshot":true,"include_tokenized_assets":false},"req_id":1})"};
    nom::web_socket client_;
};

struct kraken_spot_assembly
    : nom::assembly<kraken_spot_assembly, asio_service, app_terminator, symbol_output, kraken_spot_client>
{
};

struct app : nom::relay<kraken_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<kraken_spot_assembly>(config)
    {
    }

    int start()
    {
        request(nom::event::service_command::start);
        // Relay delivery and callbacks share one thread. Drain canceled handlers before node destruction.
        ctx().io.run();
        return ctx().exit_code.value_or(1);
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: kraken_spot_symbol_provider [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
                     "\nDefault endpoint: "
                  << app_config{}.endpoint << '\n';
        std::cout << "Optional: --exit-after-sec SECONDS (default: no runtime limit).\n"
                     "Positive limits count from connection; 0 closes before requesting a catalog.\n"
                     "Ctrl+C/SIGTERM stops during setup or retrieval. Exit 0 requires a written catalog.\n";
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
