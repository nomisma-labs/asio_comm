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
    const auto code = response_field(data, "code");
    return !code.empty() && code != "0";
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

// Public market server: wait one second after connection before subscribing, as the guide advises.
// ticker includes BBO; trade carries executions; book depth 10 uses initial snapshot then updates.
// https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket-single-page/
// Example: {"method":"subscribe","code":0,"result":{"channel":"trade","data":[{"p":"60000","q":"0.01"}]}}.
// One batched request, sample cap 100 pairs. Echo heartbeat ID within five seconds.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> channels;
    for (const auto& symbol : symbols)
        for (const auto& ch : config.channels)
            channels.push_back((ch == "depth"   ? "book."
                                : ch == "trade" ? "trade."
                                                : "ticker.") +
                               symbol + (ch == "depth" ? ".10" : ""));
    return std::vector<std::string>{R"({"id":1,"method":"subscribe","params":{"channels":)" + json_array(channels) +
                                    R"(},"nonce":)" +
                                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       std::chrono::system_clock::now().time_since_epoch())
                                                       .count()) +
                                    '}'};
}

std::string heartbeat_reply(std::string_view data)
{
    const auto id = response_field(data, "id");
    return (response_field(data, "method") == "\"public/heartbeat\"") && !id.empty()
               ? R"({"id":)" + std::string(id) + R"(,"method":"public/respond-heartbeat"})"
               : "";
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

template <typename Traits> class crypto_com_spot_client
{
public:
    explicit crypto_com_spot_client(const app_config& config)
        : requests_(subscription_requests(config)),
          client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)), subscribe_timer_(nom::ctx(this).io)
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
        // The market API asks clients to wait one second before the first subscription.
        subscribe_timer_.expires_after(std::chrono::milliseconds(1000));
        subscribe_timer_.async_wait(
            [this](const boost::system::error_code& error)
            {
                if (!error)
                    subscribe();
            });
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        const auto data = event.sv_data();
        const auto pong = heartbeat_reply(data);
        if (!pong.empty())
        {
            send(pong);
            return;
        }
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
    boost::asio::steady_timer subscribe_timer_;
};

struct crypto_com_spot_assembly
    : nom::assembly<crypto_com_spot_assembly, asio_service, market_data_logger, crypto_com_spot_client>
{
};

struct app : nom::relay<crypto_com_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<crypto_com_spot_assembly>(config)
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
        std::cout << "Usage: crypto_com_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
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
