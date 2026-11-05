# TODO

## Work plans

- [Communication work status and execution/simulation proposal](core/asio_comm/current_work.md)
- [WebSocket stages and deferred validation](core/asio_comm/ws_current_work.md)

## Connectivity automation

- [ ] Integrate [conn_test.sh](tools/asio_comm/conn_test.sh) into the CI/CD pipeline for scheduled
  and manually triggered live connectivity checks. Publish `summary.md` and per-app logs as run
  artifacts; record the revision, runner location and test settings. Define how failures, empty
  samples and credential-dependent skips affect CI status, separately from deterministic tests.
- [ ] Explore a GitHub-hosted dashboard showing the latest connectivity results and recent
  history, with per-app status, payload statistics, run timestamps and links to reports.
  Choose the hosting and publication approach when implementing CI/CD integration.
