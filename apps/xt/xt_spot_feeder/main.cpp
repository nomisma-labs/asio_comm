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
    if (!error.empty() && error != "null")
        return true;
    const auto code = response_field(data, "code");
    const auto rc = response_field(data, "rc");
    return (!code.empty() && code != "0") || (!rc.empty() && rc != "0");
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

// Lowercase symbols; current public endpoint uses JSON subscriptions and text ping, not gzip/base64.
// depth@symbol,20 is a 20-level snapshot each second; ticker is 1 s, trade event-driven.
// https://doc.xt.com/docs/spot/WebSocket%20Public/wss-general
// https://doc.xt.com/docs/spot/WebSocket%20Public/RequestMessageFormat
// https://doc.xt.com/docs/spot/WebSocket%20Public/LimitedDepth
// Example: {"topic":"trade","event":"trade@btc_usdt","data":{"p":"60000","q":"0.01"}}.
// One batch, sample cap 100 pairs. Application ping every 20 s (server requires within one minute).
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> topics;
    for (const auto& symbol : symbols)
        for (const auto& ch : config.channels)
            topics.push_back(ch + '@' + symbol + (ch == "depth" ? ",20" : ""));
    return std::vector<std::string>{R"({"method":"subscribe","params":)" + json_array(topics) + R"(,"id":"1"})"};
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

template <typename Traits> class xt_spot_client
{
public:
    explicit xt_spot_client(const app_config& config)
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
        start_ping_timer();
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
                                  if (send("ping"))
                                      start_ping_timer();
                              });
    }

    std::vector<std::string> requests_;
    nom::web_socket client_;
    nom::timer ping_timer_;
};

// Keep the terminator before every other stop handler: pass_request only visits later nodes.
struct xt_spot_assembly
    : nom::assembly<xt_spot_assembly, asio_service, app_terminator, market_data_logger, xt_spot_client>
{
};

struct app : nom::relay<xt_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<xt_spot_assembly>(config)
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
        std::cout << "Usage: xt_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
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
