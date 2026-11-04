// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    const auto size = std::stoul(argv[2]);
    std::string payload(size, ' ');
    for (std::size_t i = 0; i < size; ++i)
        payload[i] = static_cast<char>('a' + i % 26);
    nom::web_socket client(io.get_executor(), nom::url(argv[1]));
    int result = 1;
    client.connect(nom::web_socket::ws_delegate{[&](nom::web_socket& connection, const auto& event)
                                                {
                                                    using event_type = std::remove_cvref_t<decltype(event)>;
                                                    if constexpr (std::same_as<event_type, nom::event::on_connected>)
                                                    {
                                                        if (!connection.send(payload))
                                                            connection.close();
                                                    }
                                                    else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                                                    {
                                                        if (event.sv_data() == payload)
                                                        {
                                                            result = 0;
                                                            std::cout << "Verified " << size << " bytes\n";
                                                        }
                                                        connection.close();
                                                    }
                                                    else
                                                        connection.close();
                                                }});
    io.run();
    return result;
}
