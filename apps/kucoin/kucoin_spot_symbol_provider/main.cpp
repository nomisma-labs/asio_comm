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
    // The borrowed response is consumed synchronously by the relay.
    struct symbol_information
    {
        network_timestamp ts;
        std::string_view source;
        std::string_view data;
    };
} // namespace nom::event

// Inspect known fields with string searches only; this does not validate JSON or nesting.
// Catalog contents remain opaque; no DOM or normalized symbol model is built.
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

bool is_catalog(std::string_view data)
{
    const auto first = data.find_first_not_of(" \t\r\n");
    if (first == data.npos)
        return false;
    data = data.substr(first, data.find_last_not_of(" \t\r\n") - first + 1);
    if (!data.starts_with('{') || !data.ends_with('}'))
        return false;
    return response_field(data, "code") == R"("200000")" && response_field(data, "data").starts_with('[');
}

template <typename Traits> struct symbol_output
{
    void on_response(const nom::event::symbol_information& event)
    {
        std::cout << event.data << '\n';
        std::cout.flush();
        if (!std::cout)
        {
            std::cerr << "Failed to write symbol information\n";
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
            return;
        }
        std::cerr << "Received " << event.data.size() << " bytes from " << event.source << '\n';
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::completed);
    }
};

template <typename Traits> class kucoin_spot_client
{
public:
    explicit kucoin_spot_client(const app_config& config)
        : client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
        auto params = client_.params();
        params.max_response_body_size = config.max_response_size;
        params.max_pending_requests = 1;
        client_.update_params(params);
    }

    void on_request(nom::event::service_command action)
    {
        switch (action)
        {
        case nom::event::service_command::start:
            client_.connect(nom::https::https_delegate{std::ref(*this)});
            break;
        case nom::event::service_command::stop:
            client_.close();
            break;
        }
    }

    void operator()(nom::https& connection, const nom::event::on_connected&)
    {
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::connected);
        if (!connection.is_connected())
            return;
        // GET /api/v2/symbols
        // GET /api/v2/symbols without market returns all Spot symbols. No token bootstrap or authentication is
        // needed. Hyphenated symbol names and priceIncrement/baseIncrement/minFunds remain in the original
        // response.
        // Request weight 4 in the public REST resource pool; share its budget across processes. See the endpoint
        // rate-limit weight and KuCoin rate-limit rules. Example entry:
        // {"symbol":"BTC-USDT","baseCurrency":"BTC","quoteCurrency":"USDT","enableTrading":true}.
        // https://www.kucoin.com/en-au/docs-new/rest/spot-trading/market-data/get-all-symbols
        // One request per run; no refresh loop, retries or automatic rate-limit recovery.
        // Ask for uncompressed JSON because the HTTPS client does not decompress response bodies.
        connection.send(
            nom::http_request{.target = connection.url().target(),
                              .headers = {{"Accept", "application/json"}, {"Accept-Encoding", "identity"}}});
    }

    void operator()(nom::https& connection, nom::https::request_id, const nom::http_response& response)
    {
        // Connect -> GET -> complete response -> check status/envelope -> relay -> close.
        if (response.status_code != 200)
        {
            std::cerr << "Catalog request failed: HTTP " << response.status_code << ": "
                      << std::string_view(response.body).substr(0, 512) << '\n';
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
        }
        else if (!is_catalog(response.body))
        {
            std::cerr << "API error or unexpected catalog response: " << std::string_view(response.body).substr(0, 512)
                      << '\n';
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
        }
        else
            nom::relay<Traits>::pass_response(this,
                                              nom::event::symbol_information{.ts = response.ts,
                                                                             .source = connection.session_id(),
                                                                             .data = response.body});
    }

    void operator()(nom::https&, const nom::event::on_disconnected& event)
    {
        std::cerr << "Disconnected before receiving a catalog: " << event.reason << '\n';
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

    void operator()(nom::https&, const nom::event::network_error& event)
    {
        std::cerr << event.description << ": " << event.message << '\n';
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

private:
    nom::https client_;
};

struct kucoin_spot_assembly
    : nom::assembly<kucoin_spot_assembly, asio_service, app_terminator, symbol_output, kucoin_spot_client>
{
};

struct app : nom::relay<kucoin_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<kucoin_spot_assembly>(config)
    {
    }

    int start()
    {
        request(nom::event::service_command::start);
        // Drain canceled completions while the assembly and its nodes remain alive.
        ctx().io.run();
        return ctx().exit_code.value_or(1);
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: kucoin_spot_symbol_provider [--endpoint https://HOST/PATH] [--max-response-size BYTES]\n"
                     "Default endpoint: "
                  << app_config{}.endpoint << '\n';
        std::cout << "Optional: --exit-after-sec SECONDS (default: no runtime limit).\n"
                     "Positive limits count from connection; 0 closes before requesting a catalog.\n"
                     "Ctrl+C/SIGTERM stops during setup or retrieval. Exit 0 requires a written catalog.\n";
        return 0;
    }
    try
    {
        const auto config = parse_arguments(argc, argv);
        if (const auto valid = config.validate(); !valid)
            throw std::invalid_argument(valid.error());
        nom::init_logger(nom::main_logger);
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
