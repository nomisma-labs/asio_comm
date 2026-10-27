// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// Usage: boost_example [--settings <file>] [--name <name>] [--repeat <n>] [--gzip <file>]
// Boost parses the options, checks and creates paths, and reads and writes gzip-compressed
// settings. Its headers come before the import, like any other header.
#include <boost/filesystem.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/program_options.hpp>

#include <exception>
#include <expected>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

import nom.example;

namespace
{
    namespace fs = boost::filesystem;
    namespace io = boost::iostreams;
    namespace po = boost::program_options;

    // A file's text, decompressed when its name ends in .gz.
    std::string read_text(const fs::path& path)
    {
        if (!fs::is_regular_file(path))
            throw std::runtime_error("cannot open " + path.string());
        io::filtering_istream in;
        if (path.extension() == ".gz")
            in.push(io::gzip_decompressor());
        in.push(io::file_source(path.string(), std::ios::binary));
        std::string text;
        io::copy(in, io::back_inserter(text));
        return text;
    }

    // Writes text gzip-compressed, creating the file's directory when needed.
    void write_gzip(const fs::path& path, std::string_view text)
    {
        if (path.has_parent_path())
            fs::create_directories(path.parent_path());
        io::file_sink file(path.string(), std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("cannot write " + path.string());
        io::filtering_ostream out;
        out.push(io::gzip_compressor());
        out.push(file);
        out << text;
        out.reset(); // flushes the compressor, writes the gzip trailer and closes the file
    }
} // namespace

int main(int argc, char* argv[])
{
    po::options_description options("boost_example options");
    options.add_options()("help", "show this help")(
        "settings", po::value<std::string>(), "settings JSON, gzip-compressed if the name ends in .gz")(
        "name", po::value<std::string>(), "override the name")(
        "repeat", po::value<int>(), "override the number of greetings")(
        "gzip", po::value<std::string>(), "write the settings to this file as gzip-compressed JSON");
    try
    {
        po::variables_map arguments;
        po::store(po::parse_command_line(argc, argv, options), arguments);
        po::notify(arguments);
        if (arguments.contains("help"))
        {
            std::cout << options;
            return 0;
        }

        nom::example::settings greeting;
        if (arguments.contains("settings"))
        {
            const fs::path path(arguments["settings"].as<std::string>());
            auto loaded = nom::example::parse_settings(read_text(path));
            if (!loaded)
                throw std::runtime_error(loaded.error() + " in " + path.string());
            greeting = *loaded;
        }
        if (arguments.contains("name"))
            greeting.name = arguments["name"].as<std::string>();
        if (arguments.contains("repeat"))
            greeting.repeat = arguments["repeat"].as<int>();

        nom::example::counter greetings;
        for (int i = 0; i < greeting.repeat; ++i)
        {
            greetings.increment();
            std::cout << nom::example::join(" ", "hello,", greeting.name, greetings.value()) << '\n';
        }

        if (arguments.contains("gzip"))
        {
            const fs::path path(arguments["gzip"].as<std::string>());
            write_gzip(path, nom::example::serialize_settings(greeting));
            std::cout << "wrote " << fs::file_size(path) << " bytes to " << path.string() << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
