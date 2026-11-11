// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include "../../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.core;

#include "config.h"

namespace nom::event
{
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
    return !response_field(data, "code").empty();
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

// Only Ed25519 API keys are accepted. The header needs no signature or trading permission.
// https://github.com/binance/binance-spot-api-docs/blob/master/sbe-market-data-streams.md
// Subscriptions/acks use JSON; market data uses SBE, little-endian 8-byte message headers.
// https://github.com/binance/binance-spot-api-docs/blob/master/sbe/schemas/stream_1_0.xml
// Only template type and total size are logged; no market-data fields are decoded or logged.
// BBO may cull obsolete queued events; depth is 20 ms. Maximum 1024 streams, 5 commands/s
// including control Ping/Pong; one SUBSCRIBE is sent and Beast answers server control Pings.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> streams;
    for (const auto& symbol : symbols)
        for (const auto& ch : config.channels)
            streams.push_back(symbol + '@' + (ch == "ticker" ? "bestBidAsk" : ch == "trade" ? "trade" : "depth"));
    return std::vector<std::string>{R"({"method":"SUBSCRIBE","params":)" + json_array(streams) + R"(,"id":1})"};
}

std::string message_type(std::string_view data)
{
    const auto word = [&](std::size_t i)
    { return unsigned(static_cast<unsigned char>(data[i])) | unsigned(static_cast<unsigned char>(data[i + 1])) << 8; };
    if (data.size() >= 8 && word(4) == 1)
    {
        const auto id = word(2);
        const auto name = id == 10000   ? "Trades"
                          : id == 10001 ? "BestBidAsk"
                          : id == 10002 ? "DepthSnapshot"
                          : id == 10003 ? "DepthDiff"
                                        : "unknown";
        return std::string("SBE:") + name + ":template=" + std::to_string(id);
    }
    return data.starts_with('{') ? "" : "SBE:unknown-or-truncated";
}

template <typename Traits> struct market_data_logger
{
    struct context_mixin
    {
        // Count relayed application payloads, which may include acknowledgements and heartbeats.
        std::uint64_t received_messages{};
        std::uint64_t received_payload_bytes{};
    };

    void on_response(const nom::event::market_data& event)
    {
        ++nom::ctx(this).received_messages;
        nom::ctx(this).received_payload_bytes += event.size;
        LINFO("type={} size={}", event.type, event.size);
    }
};

template <typename Traits> class binance_spot_sbe_client
{
public:
    explicit binance_spot_sbe_client(const app_config& config)
        : requests_(subscription_requests(config)), client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
        auto params = client_.params();
        params.max_message_size = config.max_message_size;
        params.headers.push_back({"X-MBX-APIKEY", config.api_key});
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
        LINFO("Connected: {}", connection.session_id());
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::connected);
        // A response handler may close the connection before subscription.
        if (!connection.is_connected())
            return;
        // One connection: subscribe -> acknowledgements/errors -> continuous updates.
        // send() confirms the local write only; responses are handled in the data callback.
        subscribe();
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        const auto data = event.sv_data();
        const auto type = message_type(data);
        if (type.empty() && has_api_error(data))
        {
            LERROR("API error: {}", data);
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
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
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

private:
    bool send(const std::string& message)
    {
        if (client_.send(message))
            return true;
        LERROR("Send failed");
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
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

// Keep the terminator before every other stop handler: pass_request only visits later nodes.
struct binance_spot_sbe_assembly
    : nom::
          assembly<binance_spot_sbe_assembly, asio_service, app_terminator, market_data_logger, binance_spot_sbe_client>
{
};

struct app : nom::relay<binance_spot_sbe_assembly>
{
    explicit app(const app_config& config) : nom::relay<binance_spot_sbe_assembly>(config)
    {
    }

    int start()
    {
        request(nom::event::service_command::start);
        // Drain canceled handlers before destroying the nodes; do not stop the context early.
        ctx().io.run();
        // Stable stderr summary for tools/asio_comm/conn_test.sh; exclude log and transport overhead.
        std::cerr << "CONN_STATS messages=" << ctx().received_messages
                  << " payload_bytes=" << ctx().received_payload_bytes << '\n';
        return ctx().exit_code.value_or(1);
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: binance_spot_sbe_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
                     " [--symbols SYMBOL,...] [--channels ticker,trade,depth]"
                     "\nDefault endpoint: "
                  << app_config{}.endpoint << '\n';
        std::cout << "Optional: --exit-after-sec SECONDS (default: no limit).\n"
                     "0 exits after connection before subscribing; positive values count from connection.\n"
                     "Ctrl+C/SIGTERM and timeout exit successfully; failures exit with status 1.\n";
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
