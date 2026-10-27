#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Build and run consumers of a static library that links the kit's Boost libraries privately."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


def run(*args: str) -> None:
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(args)}\n{result.stdout}\n{result.stderr}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'compiler', 'generator', 'kit'):
        parser.add_argument(f'--{name}', required=True)
    args = parser.parse_args()

    with TemporaryDirectory(prefix='nomisma-boost-package-') as temporary:
        root = Path(temporary)
        library = root / 'library'
        library.mkdir()
        kit = Path(args.kit).resolve().as_posix()
        # The static library's exported link interface carries its private Boost libraries, so
        # package consumers need the components nom_find_boost() found.
        (library / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.30)
project(boost_package VERSION 1.0.0 LANGUAGES CXX)
include("{kit}/nom_kit.cmake")
nom_project_options()
nom_find_boost()
nom_lib(with_boost STATIC MODULES with_boost.cppm SOURCES with_boost.cpp
    BASE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}"
    PRIVATE_DEPS Boost::program_options Boost::filesystem Boost::iostreams)
nom_install_package(TARGETS with_boost DEPENDENCIES Boost)
''')
        (library / 'with_boost.cppm').write_text('''module;
#include <string>
export module nom.with_boost;
export namespace nom
{
    int parse_value(int argc, const char* const* argv);
    std::string extension(const std::string& file);
    std::string gzip_round_trip(const std::string& text);
}
''')
        # Boost stays in the implementation unit's global module fragment.
        (library / 'with_boost.cpp').write_text('''module;
#include <boost/filesystem/path.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/program_options.hpp>
#include <string>
module nom.with_boost;
namespace nom
{
    int parse_value(int argc, const char* const* argv)
    {
        namespace po = boost::program_options;
        po::options_description options;
        options.add_options()("value", po::value<int>());
        po::variables_map values;
        po::store(po::parse_command_line(argc, argv, options), values);
        return values["value"].as<int>();
    }

    std::string extension(const std::string& file)
    {
        return boost::filesystem::path(file).extension().string();
    }

    std::string gzip_round_trip(const std::string& text)
    {
        namespace io = boost::iostreams;
        std::string compressed;
        io::filtering_ostream out;
        out.push(io::gzip_compressor());
        out.push(io::back_inserter(compressed));
        out << text;
        out.reset();
        io::filtering_istream in;
        in.push(io::gzip_decompressor());
        in.push(io::array_source(compressed.data(), compressed.size()));
        std::string restored;
        io::copy(in, io::back_inserter(restored));
        return restored;
    }
}
''')
        consumer = root / 'consumer'
        consumer.mkdir()
        (consumer / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.30)
project(boost_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MODE STREQUAL "package")
    find_package(boost_package CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(boost_package SOURCE_DIR "{library.as_posix()}")
    FetchContent_MakeAvailable(boost_package)
endif()
add_executable(check main_test.cxx)
target_link_libraries(check PRIVATE nom::with_boost)
''')
        (consumer / 'main_test.cxx').write_text('''#include <string>
import nom.with_boost;
int main()
{
    const char* const argv[] = {"check", "--value", "42"};
    const bool ok = nom::parse_value(3, argv) == 42 && nom::extension("settings.json.gz") == ".gz" &&
                    nom::gzip_round_trip("hello, Boost") == "hello, Boost";
    return ok ? 0 : 1;
}
''')

        build = root / 'build'
        prefix = root / 'prefix'

        def consume(mode: str) -> None:
            consumer_build = root / mode
            run(args.cmake, '-S', str(consumer), '-B', str(consumer_build), '-G', args.generator,
                f'-DCMAKE_CXX_COMPILER={args.compiler}', f'-DMODE={mode}', f'-DCMAKE_PREFIX_PATH={prefix}')
            run(args.cmake, '--build', str(consumer_build), '--parallel', '2')
            run(str(consumer_build / 'check'))
            print(f'PASS: {mode} consumer links the library and its Boost libraries')

        def install_and_consume() -> None:
            run(args.cmake, '-S', str(library), '-B', str(build), '-G', args.generator,
                f'-DCMAKE_CXX_COMPILER={args.compiler}')
            run(args.cmake, '--build', str(build), '--parallel', '2')
            run(args.cmake, '--install', str(build), '--prefix', str(prefix))
            consume('package')

        # The source consumer builds its own copy of the library, so it needn't wait for the install.
        with ThreadPoolExecutor() as pool:
            for job in [pool.submit(install_and_consume), pool.submit(consume, 'source')]:
                job.result()


if __name__ == '__main__':
    main()
