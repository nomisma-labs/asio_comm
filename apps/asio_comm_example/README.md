# Logging and relay example

`asio_comm_example` configures the synchronous logger directly in C++ and runs an Assembly relay workflow.

## Build and run

Install the shared [build prerequisites](../../docs/kit/build.md#toolchain), then run from the repository root:

```bash
cmake --workflow --preset dev
./build/dev/bin/asio_comm_example
```

## Workflow

The [example](main.cpp) creates an `app_config` with an application name, a message, and
`nom::log_config`. It initializes the console logger before running a two-node `nom::assembly`
through `nom::relay`:

1. `startup_node` handles `start_app`, logs "app started with config" and the selected
   settings, then forwards the request to `log_messages_node`.
2. `log_messages_node` writes the configured message and example WARN/ERROR logs, then
   sends an `app_finished` response back to `startup_node`.
3. `startup_node` consumes the response by logging completion at ALWAYS level.

The whole workflow runs synchronously; event references remain valid until the request
returns. The warning and error are demonstrations; the app exits successfully.

## Configuration

Settings are plain C++ values in `app_config`; edit [config.h](config.h) and rebuild:

```cpp
struct app_config
{
    std::string_view app_name{"asio_comm_example"};
    std::string_view message{"Hello from asio_comm"};
    nom::log_config log{.level = nom::log_level::info};
};
```

`app_config::validate() const` returns `std::expected<void, std::string>` without changing the settings.
`main()` checks that result, then passes `config.log` to `nom::init_logger`. The example takes no command-line options
and does not load configuration files.

## Validation

`./bootstrap.sh ci` builds the example and runs an integration check for the full relay
workflow. This check is also included in `complete`; the `dev` preset skips it.

See the [project overview](../../README.md) for repository dependencies and shared build instructions.
