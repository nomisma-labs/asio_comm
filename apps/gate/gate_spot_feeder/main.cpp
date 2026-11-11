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

// One socket multiplexes public JSON channels; spot.order_book supplies 20-level snapshots at 100 ms.
// spot.book_ticker supplies BBO and spot.trades supplies individual executions.
// https://www.gate.com/docs/developers/apiv4/ws/en/
// Example: {"channel":"spot.trades","event":"update","result":{"currency_pair":"BTC_USDT","price":"60000"}}.
// One request/channel, at 200 ms spacing; sample cap 100 pairs. time is current Unix seconds.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> requests;
    const auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto& ch : config.channels)
    {
        const auto name = ch == "depth" ? "spot.order_book" : ch == "trade" ? "spot.trades" : "spot.book_ticker";
        if (ch == "depth")
        {
            for (const auto& symbol : symbols)
                requests.push_back(R"({"time":)" + std::to_string(now) + R"(,"channel":)" + json_string(name) +
                                   R"(,"event":"subscribe","payload":[)" + json_string(symbol) + R"(,"20","100ms"]})");
        }
        else
            requests.push_back(R"({"time":)" + std::to_string(now) + R"(,"channel":)" + json_string(name) +
                               R"(,"event":"subscribe","payload":)" + json_array(symbols) + '}');
    }
    return requests;
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
        nom::ctx(this).received_payload_bytes += event.data.size();
        LINFO("{}", event.data);
    }
};

template <typename Traits> class gate_spot_client
{
public:
    explicit gate_spot_client(const app_config& config)
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
struct gate_spot_assembly
    : nom::assembly<gate_spot_assembly, asio_service, app_terminator, market_data_logger, gate_spot_client>
{
};

struct app : nom::relay<gate_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<gate_spot_assembly>(config)
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
        std::cout << "Usage: gate_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
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
