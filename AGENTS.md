# AGENTS.md

<!-- repo-description:begin -->
`asio_comm` provides C++ TLS WebSocket and asynchronous HTTPS/HTTP/1.1 clients, plus a
generation-safe timer, for connectivity to cryptocurrency exchanges. The repository includes
public market-data feeders, WebSocket and HTTPS symbol providers, communication/timer/logging
examples, and a live connectivity test runner. `nom_core` is the sole direct repository
dependency and supplies the `nom.core` module and `nom::core` target. Boost.Asio/Beast is discovered through the
shared kit; OpenSSL supplies TLS and Catch2 is available for tests.
<!-- repo-description:end -->

Read and follow the rules every Nomisma repo shares: @docs/kit/agents.md

Rules for this repo alone follow below.

- Describe the current implementation on its own terms; do not refer to previous implementations
  or archived projects in source comments or documentation.
