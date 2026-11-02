// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
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
    if (!error.empty() && error != "null")
        return true;
    return (response_field(data, "result") == "false") || (response_field(data, "result") == "\"false\"");
}

std::string json_string(std::string_view value)
{
    return '"' + std::string(value) + '"';
}

// Public V2 endpoint and uppercase SUBSCRIBE follow the channel-specific examples.
// ticker pushes only on trades; request obu depth 1 as well when BBO is selected.
// Depth uses 50-level snapshots/100 ms, not the deprecated unbounded increment channel.
// https://www.kucoin.com/docs-new/3470355w0
// https://www.kucoin.com/docs-new/3470359w0
// https://www.kucoin.com/docs-new/3470354w0
// https://www.kucoin.com/docs-new/websocket-api/introduction
// Example: {"T":"trade.SPOT","d":{"s":"BTC-USDT","p":"60000","q":"0.01"}}.
// Sample cap 100 pairs; startup requests are spaced by 200 ms; application ping every 15 s.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> requests;
    const auto add = [&](const std::string& symbol, std::string_view channel, std::string_view depth)
    {
        std::string request = R"({"id":)" + json_string(std::to_string(requests.size() + 1)) +
                              R"(,"action":"SUBSCRIBE","tradeType":"SPOT","channel":)" + json_string(channel) +
                              R"(,"symbol":)" + json_string(symbol);
        if (!depth.empty())
            request += R"(,"depth":)" + json_string(depth);
        requests.push_back(request + '}');
    };
    for (const auto& symbol : symbols)
        for (const auto& ch : config.channels)
        {
            add(symbol, ch == "depth" ? "obu" : ch, ch == "depth" ? "50" : "");
            if (ch == "ticker")
                add(symbol, "obu", "1");
        }
    return requests;
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

template <typename Traits> class kucoin_spot_client
{
public:
    explicit kucoin_spot_client(const app_config& config)
        : requests_(subscription_requests(config)),
          client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)), ping_timer_(nom::ctx(this).io),
          subscribe_timer_(nom::ctx(this).io)
    {
        auto params = client_.params();
        params.max_message_size = config.max_message_size;
        params.receive_watchdog = nom::ws_params::receive_data_watchdog{};
        client_.update_params(params);
    }

    void on_request(nom::event::service_command command)
    {
        switch (command)
        {
        case nom::event::service_command::start:
            client_.connect(nom::web_socket::delegate{std::ref(*this)});
            break;
        case nom::event::service_command::stop:
            ping_timer_.cancel();
            subscribe_timer_.cancel();
            nom::ctx(this).signals.cancel();
            client_.close();
            break;
        }
    }

    void operator()(nom::web_socket&, const nom::event::on_connected&)
    {
        // One connection: subscribe -> acknowledgements/errors -> continuous updates.
        // send() confirms the local write only; responses are handled in the data callback.
        start_ping_timer();
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
        if (!send(requests_[next_request_++]))
            return;
        if (next_request_ < requests_.size())
        {
            // Startup pacing only; a dynamic subscriber must also track its exchange's quotas.
            subscribe_timer_.expires_after(std::chrono::milliseconds(200));
            subscribe_timer_.async_wait(
                [this](const boost::system::error_code& error)
                {
                    if (!error)
                        subscribe();
                });
        }
    }

    void start_ping_timer()
    {
        ping_timer_.expires_after(std::chrono::seconds(15));
        ping_timer_.async_wait(
            [this](const boost::system::error_code& error)
            {
                if (error || !client_.is_connected())
                    return;
                if (send(R"({"op":"ping","id":"keepalive"})"))
                    start_ping_timer();
            });
    }

    std::vector<std::string> requests_;
    std::size_t next_request_{0};
    nom::web_socket client_;
    boost::asio::steady_timer ping_timer_;
    boost::asio::steady_timer subscribe_timer_;
};

struct kucoin_spot_assembly : nom::assembly<kucoin_spot_assembly, asio_service, market_data_logger, kucoin_spot_client>
{
};

struct app : nom::relay<kucoin_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<kucoin_spot_assembly>(config)
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
        std::cout << "Usage: kucoin_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
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
