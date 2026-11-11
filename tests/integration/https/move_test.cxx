// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    try
    {
        nom::init_logger(nom::main_logger);
        const std::string_view mode(argv[2]);
        const bool assign = mode.starts_with("move_assign");
        const bool connecting = mode.ends_with("connecting");
        const bool in_response = mode.ends_with("response") || mode == "move_shutdown";
        const bool active_destination = mode == "move_assign_active";
        boost::asio::io_context io;
        const nom::url address(argv[1]);
        auto source = std::make_unique<nom::https>(io.get_executor(), address);
        auto params = source->params();
        params.request_timeout_sec = 1;
        source->update_params(params);
        std::optional<nom::https> destination;
        nom::https* expected_owner = source.get();
        nom::timer transfer_timer(io.get_executor());
        unsigned connected{}, responses{}, disconnected{}, replaced_connected{};
        std::string source_id;

        const auto transfer = [&]
        {
            if (assign)
            {
                const auto replaced_id = destination->session_id();
                *destination = std::move(*source);
                require(source->session_id() == replaced_id, "Replaced session identity");
                require(source->status() ==
                            (active_destination ? nom::https_status::disconnected : nom::https_status::conn_failed),
                        "Replaced session must close silently");
            }
            else
            {
                destination.emplace(std::move(*source));
                require(source->status() == nom::https_status::init, "Fresh source session");
                require(source->session_id() != source_id, "Fresh source identity");
            }
            expected_owner = &*destination;
            require(destination->session_id() == source_id, "Transferred session identity");
            require(destination->params().request_timeout_sec == 1, "Transferred parameters");
            // Destroy the old wrapper while its former session still has pending handlers.
            source.reset();
            auto& same_destination = *destination;
            *destination = std::move(same_destination);
            require(destination->session_id() == source_id, "Self-move preserves the session");
        };

        const nom::https::https_delegate callbacks{
            [&](nom::https& client, const nom::event::on_connected&)
            {
                require(&client == expected_owner, "Connected callback owner");
                ++connected;
                require(client.send(nom::http_request{.target = "/one"}) == 1, "First request ID");
                if (mode != "move_shutdown")
                    require(client.send(nom::http_request{.target = "/two"}) == 2, "Queued request ID");
                if (active_destination)
                    transfer();
                else if (!connecting && !in_response)
                    transfer_timer.arm_after(std::chrono::milliseconds{20}, transfer);
            },
            [&](nom::https& client, const nom::event::on_disconnected& event)
            {
                require(&client == expected_owner && destination, "Disconnected callback owner");
                require(client.status() == nom::https_status::disconnected, "Terminal session state");
                if (mode == "move_timeout")
                    require(event.reason.find("timeout") != std::string::npos, "Transferred request deadline");
                ++disconnected;
            },
            [&](nom::https&, const nom::event::network_error& event)
            { throw std::runtime_error(event.description + ": " + event.message); },
            [&](nom::https& client, nom::https::request_id id, const nom::http_response& response)
            {
                require(&client == expected_owner, "Response callback owner");
                ++responses;
                require(id == responses, "FIFO request ID after move");
                require(response.status_code == 200 && response.body == "ok", "Response after move");
                if (in_response && responses == 1)
                {
                    if (mode == "move_shutdown")
                        require(client.status() == nom::https_status::disconnecting, "Move during TLS shutdown");
                    transfer();
                }
            }};
        const auto connect_source = [&]
        {
            source->connect(callbacks);
            source_id = source->session_id();
        };

        if (assign)
        {
            destination.emplace(io.get_executor(), address);
            if (active_destination)
            {
                const auto unexpected = [](auto&&...) { throw std::runtime_error("Replaced session must not notify"); };
                destination->connect(
                    nom::https::https_delegate{[&](nom::https& client, const nom::event::on_connected&)
                                               {
                                                   ++replaced_connected;
                                                   client.send(nom::http_request{.target = "/abandoned"});
                                                   client.send(nom::http_request{.target = "/never_sent"});
                                                   connect_source();
                                               },
                                               unexpected,
                                               unexpected,
                                               unexpected});
            }
            else
            {
                // Cancel this attempt before its executor runs; the peer sees only the source connection.
                destination->connect(nom::https::https_delegate{
                    [&](auto&&...) { throw std::runtime_error("Canceled connection attempt must not notify"); }});
                // Later transfers also exercise the owner link of an already terminal replacement.
                if (!connecting)
                    destination->close();
            }
        }
        if (!active_destination)
        {
            connect_source();
            if (connecting)
                transfer();
        }
        io.run();
        require(connected == 1 && disconnected == 1, "Lifecycle callback counts");
        require(replaced_connected == unsigned(active_destination), "Replaced connection count");
        require(responses == (mode == "move_timeout" ? 0u : mode == "move_shutdown" ? 1u : 2u), "Response count");
        require(!source && destination && destination->status() == nom::https_status::disconnected,
                "Moved session completion");
        std::cout << mode << ": passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
