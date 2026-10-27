// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// Usage: hello [build/dev/config/hello/settings.json]
#include <example/macros.h>

#include <expected>
#include <filesystem>
#include <string>

import nom.example;

int main(int argc, char* argv[])
{
    nom::example::settings greeting;
    if (argc > 1)
    {
        auto loaded = nom::example::load_settings(argv[1]);
        if (!loaded)
        {
            LPRINT("error: {}", loaded.error());
            return 1;
        }
        greeting = *loaded;
    }

    nom::example::counter greetings;
    for (int i = 0; i < greeting.repeat; ++i)
    {
        greetings.increment();
        LPRINT("{}", nom::example::join(" ", "hello,", greeting.name, greetings.value()));
    }
    LPRINT("settings as JSON: {}", nom::example::serialize_settings(greeting));
    return 0;
}
