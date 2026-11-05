// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <zlib.h>

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
    return (response_field(data, "status") == "\"error\"");
}

std::string json_string(std::string_view value)
{
    return '"' + std::string(value) + '"';
}

// This public endpoint compresses JSON with gzip, including application ping messages.
// Decode only to identify the channel/error/heartbeat; log type and compressed size, not data.
// https://huobiapi.github.io/docs/spot/v1/en/#websocket-market-data
// bbo is event-driven, trade.detail contains executions, depth.step0 supplies ungrouped snapshots.
// Example decoded heartbeat: {"ping":123}; reply {"pong":123} immediately on this connection.
// Lowercase wire symbols; sample cap 100 pairs and 200 ms startup request spacing.
std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    std::vector<std::string> requests;
    for (const auto& symbol : symbols)
        for (const auto& ch : config.channels)
            requests.push_back(R"({"sub":)" +
                               json_string("market." + symbol + '.' +
                                           (ch == "ticker"  ? "bbo"
                                            : ch == "trade" ? "trade.detail"
                                                            : "depth.step0")) +
                               R"(,"id":)" + json_string(std::to_string(requests.size() + 1)) + '}');
    return requests;
}

std::string inflate_message(std::string_view data)
{
    if (data.size() < 2 || static_cast<unsigned char>(data[0]) != 0x1f || static_cast<unsigned char>(data[1]) != 0x8b)
        return std::string(data);
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        throw std::runtime_error("Cannot initialize gzip decoder");
    struct cleanup
    {
        z_stream* stream;
        ~cleanup()
        {
            inflateEnd(stream);
        }
    } guard{&stream};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    std::string result;
    std::array<char, 16384> chunk;
    for (;;)
    {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = chunk.size();
        const auto status = inflate(&stream, Z_NO_FLUSH);
        const auto count = chunk.size() - stream.avail_out;
        if (result.size() + count > 16 * 1024 * 1024)
            throw std::runtime_error("HTX gzip output exceeds the example's 16 MiB decompression budget");
        result.append(chunk.data(), count);
        if (status == Z_STREAM_END && !stream.avail_in)
            return result;
        if (status != Z_OK || (!stream.avail_in && !count))
            throw std::runtime_error("Invalid or truncated HTX gzip message");
    }
}

std::string heartbeat_reply(std::string_view data)
{
    const auto ping = response_field(data, "ping");
    return ping.empty() ? "" : R"({"pong":)" + std::string(ping) + '}';
}

std::string message_type(std::string_view data)
{
    const auto channel = response_field(data, "ch");
    return channel.empty() ? std::string("gzip:control") : "gzip:" + std::string(channel);
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

template <typename Traits> class htx_spot_client
{
public:
    explicit htx_spot_client(const app_config& config)
        : requests_(subscription_requests(config)),
          client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)),
          subscribe_timer_(nom::ctx(this).io.get_executor())
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
        const auto decoded = inflate_message(event.sv_data());
        const std::string_view data{decoded};
        const auto pong = heartbeat_reply(data);
        if (!pong.empty())
        {
            send(pong);
            return;
        }
        const auto type = message_type(data);
        if (has_api_error(data))
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
struct htx_spot_assembly
    : nom::assembly<htx_spot_assembly, asio_service, app_terminator, market_data_logger, htx_spot_client>
{
};

struct app : nom::relay<htx_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<htx_spot_assembly>(config)
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
        std::cout << "Usage: htx_spot_feeder [--endpoint wss://HOST/PATH] [--max-message-size BYTES]"
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
