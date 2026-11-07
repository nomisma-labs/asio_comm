// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/error_code.hpp>
#include <csignal>
#include <spd_logger/logging.h>

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

    enum class feed_event
    {
        connected,
        completed,
        failed
    };
} // namespace nom::event

template <typename Traits> struct asio_service
{
    struct context_mixin
    {
        // The assembly constructs context services before nodes and destroys them after nodes.
        boost::asio::io_context io;
    };
};

// Place after asio_service and before every other stop handler: pass_request visits later nodes.
// Config supplies exit_after_sec and require_completion; the timer starts on feed_event::connected.
// Providers require a completed event for success and keep stop diagnostics on stderr.
template <typename Traits> class app_terminator
{
public:
    struct context_mixin
    {
        // Unset while running; the first stop fixes the process result.
        std::optional<int> exit_code;
    };

    template <typename Config>
    explicit app_terminator(const Config& config)
        : signals_(nom::ctx(this).io.get_executor(), SIGINT, SIGTERM), exit_after_sec_(config.exit_after_sec),
          require_completion_(config.require_completion)
    {
    }

    void on_request(nom::event::service_command command)
    {
        switch (command)
        {
        case nom::event::service_command::start:
            signals_.async_wait(
                [this](const boost::system::error_code& error, int signal_number)
                {
                    if (!error && !nom::ctx(this).exit_code)
                    {
                        report_stop(std::format("Stopping on signal {}", signal_number));
                        stop(require_completion_ ? 1 : 0);
                    }
                });
            nom::relay<Traits>::pass_request(this, command);
            break;
        case nom::event::service_command::stop:
            stop(require_completion_ ? 1 : 0);
            break;
        }
    }

    void on_response(nom::event::feed_event event)
    {
        switch (event)
        {
        case nom::event::feed_event::connected:
            if (!exit_after_sec_)
                break;
            if (*exit_after_sec_ == 0)
            {
                report_stop("Stopping after connection (--exit-after-sec 0)");
                stop(require_completion_ ? 1 : 0);
                break;
            }
            exit_timer_.emplace(nom::ctx(this).io.get_executor());
            exit_timer_->arm_after(std::chrono::seconds{*exit_after_sec_},
                                   [this]
                                   {
                                       report_stop(
                                           std::format("Stopping after {} seconds connected", *exit_after_sec_));
                                       stop(require_completion_ ? 1 : 0);
                                   });
            break;
        case nom::event::feed_event::completed:
            stop(0);
            break;
        case nom::event::feed_event::failed:
            stop(1);
            break;
        }
    }

private:
    void report_stop(std::string_view message)
    {
        if (require_completion_)
            std::cerr << message << '\n';
        else
            LINFO("{}", message);
    }

    void stop(int exit_code)
    {
        // The first stop fixes the result, even if another signal or failure is already queued.
        if (nom::ctx(this).exit_code)
            return;
        nom::ctx(this).exit_code = exit_code;
        if (exit_timer_)
            exit_timer_->cancel();
        signals_.cancel();
        nom::relay<Traits>::pass_request(this, nom::event::service_command::stop);
    }

    boost::asio::signal_set signals_;
    std::optional<unsigned> exit_after_sec_;
    std::optional<nom::timer> exit_timer_;
    bool require_completion_;
};
