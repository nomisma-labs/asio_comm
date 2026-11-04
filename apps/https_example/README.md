# HTTPS example

A small Assembly/relay application: connect over verified TLS, GET the URL's encoded target,
relay and print the complete response, then close. An optional request count repeats the GET
from the response callback on the same connection, demonstrating safe re-entry without retrying.
Configuration and nonthrowing validation are in [config.h](config.h).
Connection and error diagnostics use the default INFO-level console logger.

```sh
cmake --preset dev
cmake --build --preset dev --target https_example
build/dev/bin/https_example
build/dev/bin/https_example https://example.com/ 2
```

Defaults: `https://example.com/`, one GET. Success requires all requested responses to be 2xx.
Non-2xx responses are still printed, then the app exits unsuccessfully. Redirects are not followed.
The endpoint must keep the connection alive for repeated requests; the example does not reconnect,
parse JSON, authenticate, sign orders, retry, or enforce exchange-specific quotas.

The Assembly nodes are the Asio context, response output, and HTTPS client; relay delivery is
synchronous and response references must not be retained. The client provides connection/request
and internal TLS shutdown deadlines using [the HTTPS contract](../../docs/asio_comm/https.md). The example
requires no application connection timer. It is included in normal build presets, including `dev`.

For a chosen exchange, use its REST endpoint, method, request fields and rate-limit rules.
This generic example makes no exchange API assumptions. See [HTTP semantics](https://www.rfc-editor.org/rfc/rfc9110.html)
and [HTTP/1.1 framing](https://www.rfc-editor.org/rfc/rfc9112.html).
