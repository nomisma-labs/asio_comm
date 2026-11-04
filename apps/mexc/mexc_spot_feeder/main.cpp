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
        std::string_view type;
        std::size_t size;
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

// Protobuf market data; JSON is used only for subscription replies and application ping/pong.
// 100 ms channels batch deliveries; do not infer trade aggregation semantics without decoding.
// https://mexcdevelop.github.io/apidocs/spot_v3_en/#websocket-market-streams
// https://github.com/mexcdevelop/websocket-proto/blob/main/PushDataV3ApiWrapper.proto
// Only identify the wrapper's channel (field 1) and byte count; never log/decode the market body.
// Maximum 30 streams/connection; one SUBSCRIPTION selects every symbol/channel pair.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> streams;
    for (const auto& symbol : symbols)
        for (const auto& ch : config.channels)
            streams.push_back(std::string("spot@public.aggre.") +
                              (ch == "depth"   ? "depth"
                               : ch == "trade" ? "deals"
                                               : "bookTicker") +
                              ".v3.api.pb@100ms@" + symbol);
    return std::vector<std::string>{R"({"method":"SUBSCRIPTION","params":)" + json_array(streams) + '}'};
}

std::string message_type(std::string_view data)
{
    if (data.starts_with('{'))
        return {};
    if (data.size() > 1 && static_cast<unsigned char>(data[0]) == 10)
    {
        std::size_t length = 0, pos = 1;
        for (unsigned shift = 0; shift < 28 && pos < data.size(); shift += 7)
        {
            const auto byte = static_cast<unsigned char>(data[pos++]);
            length |= std::size_t(byte & 127) << shift;
            if (!(byte & 128))
            {
                if (length <= data.size() - pos && length <= 256)
                    return "protobuf:" + std::string(data.substr(pos, length));
                break;
            }
        }
    }
    return "protobuf:unknown";
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
        LINFO("type={} size={}", event.type, event.size);
    }
};

template <typename Traits> class mexc_spot_client
{
public:
    explicit mexc_spot_client(const app_config& config)
        : requests_(subscription_requests(config)),
          client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)),
          ping_timer_(nom::ctx(this).io.get_executor())
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
            ping_timer_.cancel();
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
        const auto type = message_type(data);
        if (type.empty() && has_api_error(data))
        {
            LERROR("API error: {}", data);
            on_request(nom::event::service_command::stop);
            return;
        }
        nom::relay<Traits>::pass_response(
            this,
            nom::event::market_data{.ts = event.ts,
                                    .source = connection.session_id(),
                                    .type = type.empty() ? std::string_view("JSON:control") : std::string_view(type),
                                    .size = event.sv_data().size()});
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

    void start_ping_timer()
    {
        ping_timer_.arm_after(std::chrono::seconds(20),
                              [this]
                              {
                                  if (!client_.is_connected())
                                      return;
                                  if (send(R"({"method":"PING"})"))
                                      start_ping_timer();
                              });
    }

    std::vector<std::string> requests_;
    nom::web_socket client_;
    nom::timer ping_timer_;
};

struct mexc_spot_assembly : nom::assembly<mexc_spot_assembly, asio_service, market_data_logger, mexc_spot_client>
{
};

struct app : nom::relay<mexc_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<mexc_spot_assembly>(config)
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
        std::cout << "Usage: mexc_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
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
