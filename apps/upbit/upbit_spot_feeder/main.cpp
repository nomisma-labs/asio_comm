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
    struct market_data
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
    return (!error.empty() && error != "null");
}

std::string json_string(std::string_view value)
{
    return '"' + std::string(value) + '"';
}

std::string json_array(const std::vector<std::string>& values)
{
    std::string result{"["};
    for (const auto& value : values)
    {
        if (result.size() > 1)
            result += ',';
        result += json_string(value);
    }
    return result + ']';
}

// Singapore market: quote precedes base (SGD-BTC). Other regional endpoints need matching pairs.
// DEFAULT requests full JSON field names; snapshot and real-time data are both requested.
// https://global-docs.upbit.com/reference/websocket-ticker
// https://global-docs.upbit.com/reference/websocket-orderbook
// Example: {"type":"trade","code":"SGD-BTC","trade_price":90000,"trade_volume":0.01}.
// One ticket/request carries all types and symbols; sample budget 100 pairs, no repeated batching.
// Upbit may transport JSON in binary WS frames; this is still JSON, not a binary market-data codec.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::string request = R"([{"ticket":"nomisma-md"})";
    for (const auto& ch : config.channels)
        request +=
            R"(,{"type":)" + json_string(ch == "depth" ? "orderbook" : ch) + R"(,"codes":)" + json_array(symbols) + '}';
    return std::vector<std::string>{request + R"(,{"format":"DEFAULT"}])"};
}

template <typename Traits> struct asio_service
{
    struct context_mixin
    {
        // Context services outlive the assembly's nodes and their asynchronous operations.
        boost::asio::io_context io;
        boost::asio::signal_set signals{io, SIGINT, SIGTERM};
    };
};

template <typename Traits> struct market_data_logger
{
    void on_response(const nom::event::market_data& event)
    {
        LINFO("{}", event.data);
    }
};

template <typename Traits> class upbit_spot_client
{
public:
    explicit upbit_spot_client(const app_config& config)
        : requests_(subscription_requests(config)), client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
        auto params = client_.params();
        params.max_message_size = config.max_message_size;
        params.active_ping = nom::ws_params::client_ping_probe{};
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
        // One connection: subscribe -> acknowledgements/errors -> continuous updates.
        // send() confirms the local write only; responses are handled in the data callback.
        subscribe();
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        const auto data = event.sv_data();
        if (has_api_error(data))
        {
            LERROR("API error: {}", data);
            on_request(nom::event::service_command::stop);
            return;
        }
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = data});
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        LWARN("Disconnected: {}", event.reason);
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        on_request(nom::event::service_command::stop);
    }

private:
    bool send(const std::string& message)
    {
        if (client_.send(message))
            return true;
        LERROR("Send failed");
        on_request(nom::event::service_command::stop);
        return false;
    }

    void subscribe()
    {
        for (const auto& request : requests_)
            if (!send(request))
                return;
    }

    std::vector<std::string> requests_;
    nom::web_socket client_;
};

struct upbit_spot_assembly : nom::assembly<upbit_spot_assembly, asio_service, market_data_logger, upbit_spot_client>
{
};

struct app : nom::relay<upbit_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<upbit_spot_assembly>(config)
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
        return 1; // An ended connection interrupts this continuous feed.
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: upbit_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
                     " [--symbols SYMBOL,...] [--channels ticker,trade,depth]"
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
