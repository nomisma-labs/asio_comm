// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before these module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;

    try
    {
        nom::init_logger(nom::main_logger);
        boost::asio::io_context io;
        nom::web_socket client(io.get_executor(), nom::url(argv[1]));
        auto params = client.params();
        params.active_ping = nom::ws_params::client_ping_probe{};
        client.update_params(params);
        nom::timer deadline(io.get_executor());
        unsigned connected = 0;
        unsigned disconnected = 0;
        unsigned rejected_updates = 0;
        bool saw_disconnecting = false;
        bool failed = false;
        const auto fail = [&](std::string_view reason)
        {
            std::cerr << reason << '\n';
            failed = true;
            client.close();
            deadline.cancel();
        };
        const auto reject_active_update = [&]
        {
            const auto original_agent = client.params().agent_name;
            auto candidate = client.params();
            candidate.agent_name = "rejected-active-update";
            try
            {
                client.update_params(candidate);
            }
            catch (const std::invalid_argument&)
            {
                return false;
            }
            catch (const std::logic_error&)
            {
                ++rejected_updates;
                return client.params().agent_name == original_agent;
            }
            return false;
        };
        const nom::web_socket::ws_delegate callbacks{
            [&](nom::web_socket&, const auto& event)
            {
                using event_type = std::remove_cvref_t<decltype(event)>;
                if constexpr (std::same_as<event_type, nom::event::on_connected>)
                {
                    ++connected;
                    if (!reject_active_update())
                    {
                        fail("Parameter updates must be rejected while connected");
                        return;
                    }
                    if (!client.is_connected() || !client.send("trigger"))
                    {
                        fail("Expected a connected session before the peer reset");
                        return;
                    }

                    // The peer resets TCP after receiving the trigger. Keep writes inside this
                    // callback so a read completion cannot handle that reset before the write fails.
                    const std::string payload(64 * 1024, 'x');
                    bool write_failed = false;
                    for (unsigned attempt = 0; attempt < 32; ++attempt)
                    {
                        if (!client.send(payload))
                        {
                            write_failed = true;
                            break;
                        }
                    }
                    saw_disconnecting = client.status() == nom::ws_status::disconnecting;
                    if (!write_failed || !saw_disconnecting || client.is_connected())
                    {
                        fail("A write failure must enter disconnecting before the close callback");
                        return;
                    }
                    if (!reject_active_update())
                    {
                        fail("Parameter updates must be rejected while disconnecting");
                        return;
                    }
                    if (client.send("after close started"))
                    {
                        fail("send must reject a disconnecting session");
                        return;
                    }
                    if (nom::ws_status::disconnecting != client.status())
                        fail("Rejected sends must leave the session disconnecting");
                }
                else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
                {
                    ++disconnected;
                    if (!saw_disconnecting || client.status() != nom::ws_status::disconnected)
                    {
                        fail("Close completion must transition disconnecting to disconnected");
                        return;
                    }
                    auto next = client.params();
                    next.agent_name = "updated-after-disconnect";
                    client.update_params(next);
                    if (client.params().agent_name != next.agent_name ||
                        client.status() != nom::ws_status::disconnected)
                    {
                        fail("Parameter updates must succeed without changing disconnected status");
                        return;
                    }
                    // Keep the terminal session owned while remaining completions drain, so duplicate
                    // terminal events remain observable. No receive or timer work should start.
                    deadline.cancel();
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                {
                    fail(event.description);
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    fail("Unexpected data from the reset peer");
                }
            }};

        deadline.arm_after(std::chrono::seconds(5), [&] { fail("Timed out waiting for disconnect completion"); });
        client.connect(callbacks);
        io.run();
        return !failed && saw_disconnecting && connected == 1 && disconnected == 1 && rejected_updates == 2 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
