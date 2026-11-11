// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include "../../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

#include "config.h"

namespace nom::event
{
    // Borrowed views are consumed synchronously by the relay before the WebSocket callback returns.
    struct market_data
    {
        network_timestamp ts;
        std::string_view source;
        std::string_view data;
    };
} // namespace nom::event

std::string subscription_request(const app_config& config, unsigned id)
{
    // Initial: {"method":"SUBSCRIBE","params":["btcusdt@trade"],"id":1}.
    // Follow-up: {"method":"SUBSCRIBE","params":["ethusdt@trade"],"id":2}; SUBSCRIBE adds streams.
    // IDs distinguish replies; removing BTC would require a separate UNSUBSCRIBE.
    // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#live-subscribingunsubscribing-to-streams
    // Other supported channel choices and depth cadence are explained in binance_spot_feeder/main.cpp.
    // Limits: 1024 active streams/connection; 5 client-to-server messages/s, counting JSON commands
    // and control Ping/Pong frames. Each subscription below is one command, regardless of symbol count.
    // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#websocket-limits
    // app_config::validate checks each request and the cumulative stream count.
    // Neither request is split into batches; this diagnostic has no rate limiter.
    std::string request = R"({"method":"SUBSCRIBE","params":[)";
    bool first = true;
    for (const auto& value : config.symbols)
    {
        const auto symbol = normalized_symbol(value);
        // Validated symbols and channel names need no JSON escaping.
        for (const auto& channel : config.channels)
        {
            if (!first)
                request += ',';
            first = false;
            request += '"' + symbol + '@' + channel + '"';
        }
    }
    request += "],\"id\":" + std::to_string(id) + '}';
    return request;
}

std::string resubscription_request(const app_config& config)
{
    auto next = config;
    next.symbols = config.resubscribe_symbols;
    return subscription_request(next, 2);
}

template <typename Traits> struct market_data_logger
{
    void on_response(const nom::event::market_data& event)
    {
        LINFO("{}", event.data);
    }
};

template <typename Traits> class resubscribe_client
{
public:
    explicit resubscribe_client(const app_config& config)
        : request_(subscription_request(config, 1)), resubscribe_request_(resubscription_request(config)),
          remaining_messages_(config.resubscribe_after_messages),
          client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
    }

    void on_request(nom::event::service_command command)
    {
        switch (command)
        {
        case nom::event::service_command::start:
        {
            std::ostringstream address;
            address << client_.url();
            LINFO("Connecting to {}", address.str());
            client_.connect(nom::web_socket::ws_delegate{std::ref(*this)});
            break;
        }
        case nom::event::service_command::stop:
            client_.close();
            break;
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::on_connected&)
    {
        // Connect -> initial subscription -> data -> second subscription inside the Nth data callback.
        // Replies are logged, not used as gates; a successful local send is not Binance's acknowledgement.
        LINFO("Connected: {}", connection.session_id());
        LINFO("Subscribing: {}", request_);
        if (!connection.send(request_))
        {
            LERROR("Subscription send failed");
            on_request(nom::event::service_command::stop);
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});

        // Example trade (selected fields): {"e":"trade","s":"BTCUSDT","t":7,"p":"60000","q":"0.01"}.
        // Acks are {"result":null,"id":1} or id:2; exclude them from the trigger count.
        // This string check is specific to the supported raw messages, not a general JSON classifier.
        // N received updates trigger exactly one extra send; N is not a time-based rate budget.
        if (remaining_messages_ != 0 && event.sv_data().find("\"s\"") != std::string_view::npos &&
            --remaining_messages_ == 0)
        {
            LINFO("Resubscribing from ws_data: {}", resubscribe_request_);
            if (connection.send(resubscribe_request_))
                LINFO("Resubscription send returned true");
            else
            {
                LERROR("Resubscription send returned false");
                on_request(nom::event::service_command::stop);
            }
        }
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        // Beast handles server control Pings while reading. End the diagnostic on disconnection;
        // reconnecting would start a different session and would not extend this re-entry experiment.
        LWARN("Disconnected: {}", event.reason);
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        on_request(nom::event::service_command::stop);
    }

private:
    std::string request_;
    std::string resubscribe_request_;
    unsigned remaining_messages_;
    nom::web_socket client_;
};

struct resubscribe_assembly : nom::assembly<resubscribe_assembly, asio_service, market_data_logger, resubscribe_client>
{
};

struct app : nom::relay<resubscribe_assembly>
{
    explicit app(const app_config& config) : nom::relay<resubscribe_assembly>(config)
    {
    }

    void start()
    {
        request(nom::event::service_command::start);
        // Relay calls and all WebSocket callbacks stay on this one io_context thread.
        ctx().io.run();
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: binance_spot_resubscribe_test [--symbols BTCUSDT] [--channels trade]\n"
                     "                                     [--resubscribe-after N] [--resubscribe-symbols ETHUSDT]\n"
                     "                                     [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTCUSDT trade; add ETHUSDT after 3 market-data messages.\n"
                     "Send directly from the Nth data callback on the same connection.\n"
                     "Keep logging until disconnected or stopped with Ctrl+C.\n";
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
        app(config).start();
        nom::flush_logger(nom::main_logger);
        return 1; // Disconnection ends this continuous diagnostic run.
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
