// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include "../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

#include "config.h"

namespace example
{
    template <typename Traits> struct tick_output
    {
        void on_response()
        {
            std::cout << "Tick\n";
        }
    };

    template <typename Traits> class timer_source
    {
    public:
        struct context_mixin
        {
            bool success{};
        };

        explicit timer_source(const app_config& config)
            : timer_(nom::ctx(this).io.get_executor()), delay_(config.delay_ms), remaining_(config.ticks)
        {
        }

        void on_request(nom::event::service_command action)
        {
            switch (action)
            {
            case nom::event::service_command::start:
                arm();
                break;
            case nom::event::service_command::stop:
                timer_.cancel();
                break;
            }
        }

    private:
        void arm()
        {
            // One-shot timers make the periodic policy explicit: this is fixed delay after each callback.
            timer_.arm_after(delay_,
                             [this]
                             {
                                 nom::relay<Traits>::pass_response(this);
                                 if (--remaining_)
                                     arm();
                                 else
                                     nom::ctx(this).success = true;
                             });
        }

        nom::timer timer_;
        std::chrono::milliseconds delay_;
        unsigned remaining_;
    };

    struct app_assembly : nom::assembly<app_assembly, asio_service, tick_output, timer_source>
    {
    };
    struct app : nom::relay<app_assembly>
    {
        explicit app(const app_config& config) : nom::relay<app_assembly>(config)
        {
        }
        int run()
        {
            request(nom::event::service_command::start);
            ctx().io.run();
            return ctx().success ? 0 : 1;
        }
    };
} // namespace example

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: timer_example [DELAY_MS [TICKS]]\n";
        return 0;
    }
    try
    {
        nom::init_logger(nom::main_logger);
        const auto config = parse_arguments(argc, argv);
        if (const auto valid = config.validate(); !valid)
            throw std::invalid_argument(valid.error());
        example::app application(config);
        return application.run();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
