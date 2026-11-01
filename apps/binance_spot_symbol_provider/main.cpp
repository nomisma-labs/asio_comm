// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <spd_logger/logging.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

namespace nom::event
{
    enum class service_command
    {
        start,
        stop
    };

    // The relay consumes these borrowed views before the WebSocket callback returns.
    struct symbol_information
    {
        network_timestamp ts;
        std::string_view source;
        std::string_view data;
    };
} // namespace nom::event

struct app_config
{
    std::string endpoint{"wss://ws-api.binance.com:443/ws-api/v3"};
    unsigned max_message_size{64 * 1024 * 1024};
    nom::log_config log{.level = nom::log_level::info};
};

app_config parse_arguments(int argc, char** argv)
{
    app_config config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view option{argv[i]};
        if (option != "--endpoint" && option != "--max-message-size")
            throw std::invalid_argument("Unknown option: " + std::string(option));
        if (++i == argc)
            throw std::invalid_argument("Missing value for " + std::string(option));
        if (option == "--endpoint")
            config.endpoint = argv[i];
        else
        {
            const std::string_view value{argv[i]};
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), config.max_message_size);
            if (error != std::errc{} || end != value.data() + value.size() || config.max_message_size == 0)
                throw std::invalid_argument("--max-message-size must be a positive unsigned byte count");
        }
    }
    return config;
}

// Only inspect the expected response fields; the payload remains opaque to the application.
std::string_view response_field(std::string_view data, std::string_view name) noexcept
{
    const auto key = data.find(name);
    if (key == std::string_view::npos)
        return {};
    const auto colon = data.find_first_not_of(" \t\r\n", key + name.size());
    if (colon == std::string_view::npos || data[colon] != ':')
        return {};
    const auto value = data.find_first_not_of(" \t\r\n", colon + 1);
    return value == std::string_view::npos ? std::string_view{} : data.substr(value);
}

std::string_view response_error(std::string_view data) noexcept
{
    if (!response_field(data, "\"error\"").empty())
        return "exchangeInfo API error";
    const auto status = response_field(data, "\"status\"");
    if (status.empty())
        return "exchangeInfo response has no status";
    if (status.substr(0, status.find_first_of(",} \t\r\n")) != "200")
        return "exchangeInfo response status must be 200";
    return {};
}

template <typename Traits> struct asio_service
{
    struct context_mixin
    {
        boost::asio::io_context io;
        bool success{false};
    };
};

template <typename Traits> struct symbol_output
{
    void on_response(const nom::event::symbol_information& event)
    {
        std::cout << event.data << '\n';
        std::cout.flush();
        if (!std::cout)
        {
            LERROR("Failed to write symbol information");
            return;
        }
        nom::ctx(this).success = true;
        std::cerr << "Received " << event.data.size() << " bytes from " << event.source << '\n';
    }
};

template <typename Traits> class binance_spot_client
{
public:
    explicit binance_spot_client(const app_config& config)
        : client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
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
            client_.connect(nom::web_socket::delegate{std::ref(*this)});
            break;
        case nom::event::service_command::stop:
            client_.close();
            break;
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::on_connected&)
    {
        if (!connection.send(request_))
        {
            LERROR("exchangeInfo request send failed");
            on_request(nom::event::service_command::stop);
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        const auto error = response_error(event.sv_data());
        if (error.empty())
        {
            nom::relay<Traits>::pass_response(this,
                                              nom::event::symbol_information{.ts = event.ts,
                                                                             .source = connection.session_id(),
                                                                             .data = event.sv_data()});
        }
        else
            LERROR("{}: {}", error, event.sv_data());
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        LERROR("Disconnected before receiving symbol information: {}", event.reason);
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        on_request(nom::event::service_command::stop);
    }

private:
    const std::string request_{
        R"({"id":"exchange-info","method":"exchangeInfo","params":{"permissions":["SPOT"],"showPermissionSets":false}})"};
    nom::web_socket client_;
};

struct binance_spot_assembly : nom::assembly<binance_spot_assembly, asio_service, symbol_output, binance_spot_client>
{
};

struct app : nom::relay<binance_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<binance_spot_assembly>(config)
    {
    }

    int start()
    {
        request(nom::event::service_command::start);
        ctx().io.run();
        return ctx().success ? 0 : 1;
    }
};

/* Example run:
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ time bin/binance_spot_symbol_provider > raw_symbols.json
Received 6710072 bytes from WS[wss://ws-api.binance.com:443/ws-api/v3:0]

real	0m6.910s
user	0m0.063s
sys	0m0.026s
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ la -la *symbols.json
-rw-rw-r-- 1 alex alex  6710073 Nov  1 13:22 raw_symbols.json <-- without permissions
-rw-rw-r-- 1 alex alex 39619402 Nov  1 13:05 symbols.json     <-- with permissions (fetched earlier)

Pretty print symbols (inflates the size)
lex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ time cat raw_symbols.json | json_pp > symbols.json

real	0m2.999s
user	0m2.903s
sys	0m0.101s
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ la -la *symbols.json
-rw-rw-r-- 1 alex alex  6710073 Nov  1 13:22 raw_symbols.json
-rw-rw-r-- 1 alex alex 13410073 Nov  1 13:24 symbols.json

In order to try disconnection due to small message size:
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ time bin/binance_spot_symbol_provider --max-message-size
3000000 > raw_symbols.json

real	0m0.474s
user	0m0.014s
sys	0m0.008s
alex@kallang-cubi:~/work/nomisma-labs/asio_comm/build/dev$ cat raw_symbols.json
2026-11-01 05:52:37.566121Z ERROR    main.cpp:167 Disconnected before receiving symbol information: Incoming WebSocket
message exceeds max_message_size=3000000 bytes; TLS shutdown error: application data after close notify (SSL routines)

*/
int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: binance_spot_symbol_provider [--endpoint wss://HOST/ws-api/v3]\n"
                     "                                    [--max-message-size BYTES]\n"
                     "Fetch Spot exchangeInfo once, write its JSON response, then close.\n"
                     "Defaults: wss://ws-api.binance.com:443/ws-api/v3, 64 MiB receive limit.\n";
        return 0;
    }

    try
    {
        const auto config = parse_arguments(argc, argv);
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
