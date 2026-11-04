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
    return (!error.empty() && error != "null") || response_field(data, "success") == "false";
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

// V2 symbol identifiers use BTC/USD (not v1 XBT/USD). ticker triggers on BBO changes;
// trade emits executions and book requests a 100-level snapshot followed by updates.
// https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ticker
// https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book
// https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade
// Example: {"channel":"book","type":"update","data":[{"symbol":"BTC/USD","bids":[],"asks":[]}]}.
// Three requests share the same socket; 200 ms pacing, sample cap 100 symbol/channel pairs.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> requests;
    for (const auto& ch : config.channels)
        requests.push_back(R"({"method":"subscribe","params":{"channel":)" + json_string(ch == "depth" ? "book" : ch) +
                           R"(,"symbol":)" + json_array(symbols) + R"(,"snapshot":true)" +
                           (ch == "depth"    ? R"(,"depth":100)"
                            : ch == "ticker" ? R"(,"event_trigger":"bbo")"
                                             : "") +
                           R"(},"req_id":)" + std::to_string(requests.size() + 1) + '}');
    return requests;
}

template <typename Traits> struct market_data_logger
{
    void on_response(const nom::event::market_data& event)
    {
        LINFO("{}", event.data);
    }
};

template <typename Traits> class kraken_spot_client
{
public:
    explicit kraken_spot_client(const app_config& config)
        : requests_(subscription_requests(config)),
          client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)),
          subscribe_timer_(nom::ctx(this).io.get_executor())
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
            subscribe_timer_.cancel();
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
        if (has_api_error(data))
        {
            LERROR("API error: {}", data);
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
            return;
        }
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = data});
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
        if (!send(requests_[next_request_++]))
            return;
        if (next_request_ < requests_.size())
        {
            // Startup pacing only; a dynamic subscriber must also track its exchange's quotas.
            subscribe_timer_.arm_after(std::chrono::milliseconds(200), [this] { subscribe(); });
        }
    }

    std::vector<std::string> requests_;
    std::size_t next_request_{0};
    nom::web_socket client_;
    nom::timer subscribe_timer_;
};

// Keep the terminator before every other stop handler: pass_request only visits later nodes.
struct kraken_spot_assembly
    : nom::assembly<kraken_spot_assembly, asio_service, app_terminator, market_data_logger, kraken_spot_client>
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
        // Drain canceled handlers before destroying the nodes; do not stop the context early.
        ctx().io.run();
        return ctx().exit_code.value_or(1);
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: kraken_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
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
