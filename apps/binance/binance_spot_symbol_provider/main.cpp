// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include "../../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.core;

#include "config.h"

namespace nom::event
{
    // The relay consumes these borrowed views before the WebSocket callback returns.
    struct symbol_information
    {
        network_timestamp ts;
        std::string_view source;
        std::string_view data;
    };
} // namespace nom::event

// Only inspect the expected response fields; the payload remains opaque to the application.
// This is basic error identification, not JSON/nesting/schema or request-ID validation.
// Success uses status:200 with result; failures carry status and error:{code,msg}.
// https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md#response-format
std::string_view response_field(std::string_view data, std::string_view name) noexcept
{
    const auto key = data.find(name);
    if (key == std::string_view::npos)
        return {};
    const auto colon = data.find_first_not_of(" \t\r\n", key + name.size());
    if (colon == std::string_view::npos || data[colon] != ':')
        return {};
    const auto value = data.find_first_not_of(" \t\r\n", colon + 1);
    return value == std::string_view::npos ? std::string_view{} : data.substr(value);
}

std::string_view response_error(std::string_view data) noexcept
{
    if (!response_field(data, "\"error\"").empty())
        return "exchangeInfo API error";
    const auto status = response_field(data, "\"status\"");
    if (status.empty())
        return "exchangeInfo response has no status";
    if (status.substr(0, status.find_first_of(",} \t\r\n")) != "200")
        return "exchangeInfo response status must be 200";
    return {};
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

template <typename Traits> class binance_spot_client
{
public:
    explicit binance_spot_client(const app_config& config)
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
        // One-shot workflow: connect -> exchangeInfo -> result/error -> relay the result -> close.
        // There is no subscription ack or update stream. send() success only confirms the local write;
        // Beast continues reading (and answering server control Pings) until the response arrives.
        // exchangeInfo costs 20 request weight; opening this WS API connection costs another 2.
        // Weight is shared per IP across connections; rateLimits reports the applicable budgets.
        // This is a request API, so the feeder's 1024-stream allowance does not apply.
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md#exchange-information
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md#ip-limits
        // This tool sends once and exits on an API error; it does not track quotas or parse retryAfter.
        // A caller repeating runs must honor 429/retryAfter and coordinate the shared IP budget.
        if (!connection.send(request_))
        {
            LERROR("exchangeInfo request send failed");
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        // Illustrative response with selected fields; the full envelope is preserved on stdout:
        // {"id":"exchange-info","status":200,"result":{"symbols":[{"symbol":"BTCUSDT","status":"TRADING",
        //   "baseAsset":"BTC","quoteAsset":"USDT","filters":[{"filterType":"LOT_SIZE","stepSize":"0.00001"}]}]}}
        // Filters specify allowed price/quantity steps; precision fields alone are not those trading rules.
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md#exchange-information
        // https://github.com/binance/binance-spot-api-docs/blob/master/filters.md
        // This single-request tool checks the envelope's error/status tokens, without constructing a symbol model.
        const auto error = response_error(event.sv_data());
        if (error.empty())
        {
            nom::relay<Traits>::pass_response(this,
                                              nom::event::symbol_information{.ts = event.ts,
                                                                             .source = connection.session_id(),
                                                                             .data = event.sv_data()});
        }
        else
        {
            LERROR("{}: {}", error, event.sv_data());
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
        }
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        LERROR("Disconnected before receiving symbol information: {}", event.reason);
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

private:
    // Omit symbol/symbols to fetch the catalog; permissions:["SPOT"] filters for Spot eligibility.
    // showPermissionSets:false suppresses bulky permission-set contents while retaining trading filters.
    // Omit symbolStatus so halted/non-trading symbols remain visible; consumers select tradable instruments.
    // The fixed ID names this tool's sole outstanding request; it is not an authentication credential.
    // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md#exchange-information
    const std::string request_{
        R"({"id":"exchange-info","method":"exchangeInfo","params":{"permissions":["SPOT"],"showPermissionSets":false}})"};
    nom::web_socket client_;
};

struct binance_spot_assembly
    : nom::assembly<binance_spot_assembly, asio_service, app_terminator, symbol_output, binance_spot_client>
{
};

struct app : nom::relay<binance_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<binance_spot_assembly>(config)
    {
    }

    int start()
    {
        request(nom::event::service_command::start);
        ctx().io.run();
        return ctx().exit_code.value_or(1);
    }
};

/* Example run:
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ time bin/binance_spot_symbol_provider > raw_symbols.json
Received 6710072 bytes from WS[wss://ws-api.binance.com:443/ws-api/v3:0]

real	0m6.910s
user	0m0.063s
sys	0m0.026s
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ la -la *symbols.json
-rw-rw-r-- 1 alex alex  6710073 Nov  1 13:22 raw_symbols.json <-- without permissions
-rw-rw-r-- 1 alex alex 39619402 Nov  1 13:05 symbols.json     <-- with permissions (fetched earlier)

Pretty print symbols (inflates the size)
lex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ time cat raw_symbols.json | json_pp > symbols.json

real	0m2.999s
user	0m2.903s
sys	0m0.101s
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ la -la *symbols.json
-rw-rw-r-- 1 alex alex  6710073 Nov  1 13:22 raw_symbols.json
-rw-rw-r-- 1 alex alex 13410073 Nov  1 13:24 symbols.json

In order to try disconnection due to small message size:
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ time bin/binance_spot_symbol_provider --max-message-size
3000000 > raw_symbols.json

real	0m0.474s
user	0m0.014s
sys	0m0.008s
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ cat raw_symbols.json
2026-11-01 05:52:37.566121Z ERROR    main.cpp:167 Disconnected before receiving symbol information: Incoming WebSocket
message exceeds max_message_size=3000000 bytes; TLS shutdown error: application data after close notify (SSL routines)

*/
int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: binance_spot_symbol_provider [--endpoint wss://HOST/ws-api/v3]\n"
                     "                                    [--max-message-size BYTES]\n"
                     "Fetch Spot exchangeInfo once, write its JSON response, then close.\n"
                     "Defaults: wss://ws-api.binance.com:443/ws-api/v3, 64 MiB receive limit.\n";
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
