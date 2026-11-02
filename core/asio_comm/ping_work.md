**WebSocket heartbeat review and interface proposal — 2026-11-02**

Research for [WS-06](ws_current_work.md#ws-06), principally `WS-PING` and `WS-IDLE`.
This is a proposal, not an implemented contract. Exchange requirements below were checked
against official documentation; authenticated connections and order submission were not tested.

We need three independent observations: **transport activity**, **exchange heartbeat health**,
and **useful application progress**. A control Pong does not establish that subscriptions are
working, and a healthy private feed may have no order/account updates for a long time.

**1. Subscriber review findings**

The reviewed subscriber logic exhibits five patterns:

| Pattern | Observed behavior | Consequence for our design |
| --- | --- | --- |
| Server control Ping | Binance-style RFC Ping/Pong exchange. | Keep automatic transport replies independent of optional client probes. |
| Client control Ping | Periodic or receive-idle probes; some handlers also measure RTT from the echoed payload. | Sending a Ping and receiving its matching Pong are separate events. |
| Client application ping | OKX-style text `ping` / `pong`. | Ordinary WebSocket data messages, handled above the transport. |
| Server application ping | HTX-style timestamp echo and LBank-style identifier echo; HTX MD requires decompression first. | Recognize and reply on the same connection, preserving the required value. |
| One-way heartbeat | Coinbase/Kraken-style heartbeat notifications, also seen with Bitfinex. | Record activity without replying or treating the notification as market data. |

The reviewed receive-idle logic generally treated any incoming traffic as proof of life;
it was not uniformly a matched-Pong watchdog. Connection keepalive, MD freshness, and
application heartbeat replies therefore need explicit policies. HTTP heartbeat settings
found alongside subscribers do not establish WebSocket behavior. The matrix below supplies
the current endpoint-specific requirements rather than deriving them from these patterns.

**2. Exchange requirements**

**Control** means RFC Ping/Pong frames; **application** means ordinary text/binary messages.
Intervals are server requirements or recommendations as marked, not proposed library defaults.
“REST entry” describes the cited API family; it is not a claim about every product offered
by that exchange. A private order-update channel alone does not provide order entry.

| Exchange / API family | Market data | Private feeds | Order entry / differences |
| --- | --- | --- | --- |
| **Binance — Spot, USD-M** | Server **control** Ping: Spot every **20 s**, Pong within **1 min**; USD-M every **3 min**, Pong within **10 min**. Echo the payload. [Spot](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#general-wss-information), [USD-M](https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-market-streams/Connect). | Spot supports user-data subscriptions on its WS API. USD-M listen keys need renewal within **60 min**, independently of socket Pongs. [Spot API](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md), [USD-M lifecycle](https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/user-data-streams). | Both have WS order APIs. Spot retains **20 s / 1 min**, USD-M **3 min / 10 min** control policy. Connections have **24 h** lifetime. JSON RPC `ping` or listen-key renewal is distinct from control Pong. [USD-M WS API](https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-api-general-info). |
| **Coinbase — Advanced Trade** | Subscribe to application `heartbeats`: approximately **1 s**, no reply. Prevents quiet subscriptions closing after **60–90 s**. [Endpoints](https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/websocket/websocket-endpoints). | The separate user endpoint also supports `heartbeats`; subscribe within **5 s** of connecting. [Overview](https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/websocket/websocket-overview). | [REST entry](https://docs.cdp.coinbase.com/api-reference/advanced-trade-api/rest-api/orders/create-order); WS delivers order updates. This row does not prescribe behavior for Coinbase Exchange, International, or other API families. |
| **Upbit** | **120 s** idle limit; control Ping supported. Text `PING` is an alternative that starts `{"status":"UP"}` notifications every **10 s**, rather than a single Pong. [Guide](https://global-docs.upbit.com/reference/websocket-guide). | Authenticated private endpoint follows the connection guide; order events can legitimately be absent. [MyOrder](https://global-docs.upbit.com/reference/websocket-myorder). | [REST entry](https://global-docs.upbit.com/reference/api-overview); WS subscriptions provide market/account events. |
| **OKX — v5** | Application text `ping` → `pong`: after receive silence of **N < 30 s**, send Ping, then reconnect if no Pong within N. [Connection rules](https://www.okx.com/docs-v5/en/#overview-websocket-connect). | Same connection rule on private/business connections; authentication and subscriptions are separate from heartbeat. | WS `order` on authenticated private connection; same text heartbeat. [Place order](https://www.okx.com/docs-v5/en/#order-book-trading-trade-ws-place-order). |
| **Bybit — v5** | Application JSON `op:ping`, recommended every **20 s**. Response shape varies by endpoint. [Connect](https://bybit-exchange.github.io/docs/v5/ws/connect). | Private and trade endpoints support configurable `max_active_time` (**30–600 s**); default idle allowance is **10 min**. | Separate WS trade endpoint; JSON ping/pong also applies, with its own response envelope. [Trade guide](https://bybit-exchange.github.io/docs/v5/websocket/trade/guideline). |
| **Bitget — UTA** | Text `ping` → `pong` every **30 s** recommended; disconnect after **2 min** without Ping. [Quick start](https://www.bitget.com/docs/uta/quick-start). | Same policy on UTA private connection. | WS `op:trade`, including `place-order`, uses the private connection. Acceptance acknowledgement is separate from execution updates. [Order channel](https://www.bitget.com/docs/uta/websocket/private/Place-Order-Channel). |
| **Gate — Spot v4 / Futures** | Server **control** Ping requires Pong. Optional application `spot.ping` / `futures.ping` provides additional checks; no fixed cadence is assumed here. [Spot](https://www.gate.com/docs/developers/apiv4/ws/en/#application-ping-pong), [Futures](https://www.gate.com/docs/developers/futures/ws/en/#ping-and-pong). | Authenticated subscriptions share the corresponding family's connection rules. | WS `spot.order_place` / `futures.order_place` supported; use the same family-specific heartbeat, not a universal JSON payload. [Spot entry](https://www.gate.com/docs/developers/apiv4/ws/en/#order-place), [Futures API](https://www.gate.com/docs/developers/futures/ws/en/). |
| **KuCoin — Classic / UTA** | Classic uses JSON `type:ping`; read `pingInterval` and `pingTimeout` from token/endpoint discovery. UTA uses `op:ping` and welcome-message settings. [Classic](https://www.kucoin.com/docs-new/websocket-api/base-info/introduction), [UTA](https://www.kucoin.com/docs-new/websocket-api/introduction). | Each connection owns its negotiated schedule. Current examples show **18 s / 10 s**; do not hard-code them as universal constants. | UTA provides a WS trade connection. Its heartbeat envelope differs from Classic; negotiated settings belong to that adapter instance. |
| **MEXC — Spot v3 / Futures** | Spot JSON `method:PING` → `msg:PONG`; inactive subscriptions can close after **60 s**, and an unsubscribed connection after **30 s**. Data is Protobuf. [Spot streams](https://www.mexc.com/api-docs/spot-v3/websocket-market-streams), [Ping format](https://mexcdevelop.github.io/apidocs/spot_v3_en/#ping-pong-mechanism). Futures uses lowercase `method:ping`, recommended **10–20 s**, closing after **60 s** without Ping. [Futures commands](https://www.mexc.com/api-docs/futures/websocket-api/command-details-for-data-exchange). | Spot listen keys additionally expire after **60 min** without renewal; connection lifetime **24 h**. [User streams](https://www.mexc.com/api-docs/spot-v3/websocket-user-data-streams/). | [Spot REST entry](https://www.mexc.com/api-docs/spot-v3/spot-account-trade/new-order), [Futures trade API](https://www.mexc.com/api-docs/futures/trade/); private push streams are distinct from submission. |
| **HTX — Spot / derivatives** | Spot server JSON `ping` timestamp every **5 s**; reply with matching `pong`; two missed heartbeats disconnect. MD arrives gzip-compressed in binary messages. [Spot heartbeat](https://huobiapi.github.io/docs/spot/v1/en/#heartbeat-and-connection). | Spot `/ws/v2` uses uncompressed `action:ping`, nested `data.ts`, approximately **20 s**; reply `action:pong`. Derivatives private feeds use an `op:ping` / `op:pong` envelope. [Spot private](https://huobiapi.github.io/docs/spot/v1/en/#websocket-account-and-order), [Derivatives](https://huobiapi.github.io/docs/dm/v1/en/#websocket-heartbeat-and-authentication-interface). | Order submission is REST in these documented families; WS account/order sections describe updates. Do not reuse the MD heartbeat classifier for private messages. |
| **Crypto.com — Exchange v1** | Server application `public/heartbeat` every **30 s**; send `public/respond-heartbeat` with the same ID within **5 s**. [WS specification](https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket-single-page/). | Same protocol on `/user` as `/market`. | WS `private/create-order` uses `/user`; keep heartbeat replies prompt even during order bursts. Cancel-on-disconnect is a separate trading policy. [WS methods](https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket-single-page/). |
| **Kraken — Spot v2 / Futures** | Spot emits an application heartbeat about every **1 s when other updates are absent**, after subscribing; no reply. Optional JSON `method:ping` returns Pong. [Heartbeat](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/heartbeat), [Ping](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ping). | Spot authenticated subscriptions have the same application Ping option. Futures separately requires a Ping at least every **60 s**; its official client demonstrates **control** pings. [Futures guide](https://docs.kraken.com/exchange/guides/futures/websockets), [client](https://github.com/CryptoFacilities/WebSocket-v1-Python/blob/master/cfWebSocketApiV1.py). | Spot v2 supports authenticated WS [add_order](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/add_order). Do not infer Futures WS order-entry support from the streaming guide. |
| **LBank — Spot v2** | Server JSON `action:ping` includes an identifier; reply `action:pong` with it within **1 min**. Client-initiated application Ping is also supported. [Specification](https://www.lbank.com/docs/). | Same WS family; additionally renew `subscribeKey` before its **60 min** expiry. | REST entry; WS `orderUpdate` is notification. [Trading and private streams](https://www.lbank.com/docs/). |
| **XT.com — Spot / Futures** | Spot text `ping` → `pong`, with **1 min** maximum silence between Pings. Futures requires text Ping within **30 s**. [Spot](https://doc.xt.com/docs/spot/WebSocket%20Public/Heartbeat), [Futures](https://doc.xt.com/docs/futures/WebsocKetV2/General_WSS_information). | Futures private repeats the **30 s** rule. Spot private heartbeat cadence is **not explicit in the reviewed pages**; its access token lasts **2 days**. [Futures private](https://doc.xt.com/docs/futures/UserWebsocket/General_WSS_information), [Spot private](https://doc.xt.com/docs/spot/WebSocket%20Private/GeneralWSSInfo), [token](https://doc.xt.com/docs/spot/WebSocket%20Private/GetWsToken). | [Spot REST entry](https://doc.xt.com/docs/spot/Order/SubmitOrder). Confirm Spot private heartbeat behavior before adopting the public interval. |
| **bitFlyer — Lightning JSON-RPC** | Raw JSON-RPC over WebSocket is available. The reviewed official pages do **not specify a numeric heartbeat cadence**. Keep RFC replies enabled; any client probe schedule is our policy pending verification. [Realtime API](https://bf-lightning-api.readme.io/docs/realtime-api), [JSON-RPC](https://bf-lightning-api.readme.io/docs/endpoint-json-rpc). | Authenticate and subscribe to private channels over JSON-RPC. Socket.IO is an alternative protocol with additional framing, not interchangeable with raw WS. | [HTTP order entry](https://lightning.bitflyer.com/docs#send-a-new-order); private WS channels deliver order events. |
| **Deribit — JSON-RPC v2** | Enable `public/set_heartbeat`, interval **10–300 s**. Informational heartbeat needs no reply; `test_request` requires `public/test`. Control Ping/Pong does **not** satisfy this application mechanism. [Heartbeat contract](https://docs.deribit.com/api-reference/session-management/public-set_heartbeat). | Same connection mechanism before/after authentication; client JSON activity affects the timer. | WS [private/buy](https://docs.deribit.com/api-reference/trading/private-buy) and corresponding trading methods. Heartbeat disconnect can cancel orders if cancel-on-disconnect was enabled. |

Two details matter when implementing these profiles:

- **Deribit:** the detailed method contract distinguishes no server output for `interval`
  (informational heartbeat) from no client JSON input for `interval + 500 ms` (test request).
  Continued client silence closes at `2 × interval + 500 ms`. Sending control Pongs cannot
  prevent that application timeout. [Detailed contract](https://docs.deribit.com/api-reference/session-management/public-set_heartbeat).
- **Rate/lifetime budgets:** heartbeat messages can consume the same incoming-message budget
  as subscriptions, e.g. Binance Spot's **5 messages/s** includes control Ping/Pong and JSON.
  Heartbeats also do not extend a mandatory connection lifetime or renew a private-feed lease.
  [Binance limits](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#websocket-limits).

**3. RFC and Beast behavior**

RFC 6455 defines Ping/Pong as unfragmented control frames with at most **125 payload bytes**.
A Pong answering a Ping echoes its payload; unsolicited Pongs are allowed and need no reply.
Control frames can appear between fragments of a data message. The RFC prescribes no periodic
heartbeat interval and no JSON/text heartbeat format. [RFC 6455 §5.5](https://www.rfc-editor.org/rfc/rfc6455.html#section-5.5).

Beast handles incoming control frames during reads and automatically replies to Ping.
The control callback observes the frame; it does not replace that reply. A read must be
active, and the executor must keep running. Its internal coordination permits a read, a data
write, and a ping/pong operation to coexist under the documented operation rules; it does not
permit arbitrary concurrent operations. [Control frames](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/control_frames.html).

The installed Boost **1.90.0** implementation was inspected: `websocket/impl/read.hpp`
constructs the automatic Pong with the received payload; `impl/ping.hpp` coordinates its
write with other asynchronous writers. `async_ping` completes when the Ping is written or
fails, **not when a Pong arrives**. Do not initiate another explicit ping/pong operation until
it completes. [async_ping contract](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/async_ping.html).

Beast can also supply receive-idle timeouts and automatic idle probes, but our agreed policy
is to keep both disabled and own the timer/state transitions. Its handshake/close timeout
remains separate. TCP keepalive is yet another mechanism and cannot replace exchange application
heartbeats. [Beast timeouts](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/timeouts.html).

**4. Our interface: gaps and proposed changes**

Current [parameters/events](web_socket.cppm) and [session implementation](web_socket.cpp):

- `enable_outbound_ping` controls periodic empty control Pings only. Automatic Pong replies
  still work when it is false, and our idle timer still runs.
- Idle checking derives its interval and tolerance from `ping_interval_sec`. Complete data
  messages and incoming Pings refresh activity; Pongs refresh it only when outbound Ping is
  enabled. Partial-message progress does not refresh it. Ping failure logs and rearms.
- There is no matched-Pong deadline or public control-frame event. `ws_data` supplies bytes
  without text/binary metadata. `send()` blocks and reports local write completion.
- Peer close/read errors can leave a ping timer pending until expiry. This remains the first
  small lifecycle fix in WS-06.

Recommended responsibility split:

| Layer | Owns |
| --- | --- |
| `web_socket` session | RFC framing/replies, continuous reading, optional control probes, transport inactivity, setup/close deadlines, terminal cleanup and diagnostics. |
| Exchange connection adapter | Application heartbeat encoding/replies, negotiated intervals, authentication, subscription acknowledgement, token renewal and reconnect readiness. |
| MD/order consumer | Per-channel/symbol freshness, sequence continuity, order-response deadlines and reconciliation of unknown outcomes. |

**Minimal interface direction:**

1. Separate an independently disableable **receive-idle timeout** from the optional outbound
   **control-ping interval**. Name both explicitly; disabling one must not implicitly disable
   the other. Use precise steady-clock deadlines. Recommend counting all received control
   activity and partial data progress for transport liveness; track useful MD separately.
   Partial progress may require `async_read_some` internally while retaining complete-message
   delivery and the existing message-size limit.
2. Add text/binary kind to `ws_data`. HTX gzip and MEXC Protobuf need adapter decoding; gzip
   here is application compression, separate from WebSocket compression. Bound decoded output
   as well as the received message. Preserve the existing borrowed-payload lifetime contract.
3. Keep application heartbeats on ordinary `send()` and receive events. Start with small
   adapter-owned timers and bounded string recognition for known heartbeat envelopes; no DOM
   parser or exchange JSON policy in `web_socket`. Match required IDs/timestamps, tolerate
   documented whitespace/order variation, and do not classify arbitrary messages by a loose
   search for `ping`/`pong`. Reply before expensive downstream processing.
4. Keep runtime-negotiated settings, such as KuCoin welcome values, in the adapter. They do
   not require weakening `update_params()`'s prohibition on active-session changes. Extract a
   shared application-heartbeat helper only after concrete adapters establish common needs.

**Control-probe options:** retain periodic probes as the smallest change, or send them only
after receive silence to reduce traffic. A strict matched-Pong deadline is an additional
opt-in contract, useful for RTT/bidirectional checks; it is unnecessary when any incoming
activity satisfies the requirement. If selected, allow one outstanding probe, correlate its
payload, and distinguish write failure, missing Pong, and unsolicited/wrong-payload Pong.
Add public control observation/manual Ping only when a consumer needs it; application heartbeat
support does not require either API. Never disable automatic replies to server control Pings.

**Sending and orders:** a blocking write or long delegate callback can delay reads, automatic
Pongs, application replies and timers. Meeting bounded response times under a stalled peer may
require an owned, bounded asynchronous send path. That is a separate design decision;
[WS-03](ws_current_work.md#ws-03) remains closed as unconfirmed. Define queue acceptance, local
write completion and exchange acknowledgement separately, and coordinate data/control/close
operations. Prioritizing a heartbeat between queued messages cannot preempt bytes already being
written. Do not replay an order automatically after a timeout/reconnect: its outcome may be
unknown. Reconcile by client/order ID; choose cancel-on-disconnect explicitly in the order adapter.

Report distinct reasons for receive-idle expiry, an optional Pong deadline, heartbeat protocol
failure, send failure and peer closure. A private-feed lease expiry and an order-acknowledgement
timeout belong above the transport. A busy symbol must not hide another subscription's stalled
data. Use the future [timer/execution abstraction](current_work.md) when available without making
its implementation a prerequisite for WS-06.

**5. Suggested implementation order and evidence**

1. **Small lifecycle fix:** reproduce terminal ping/idle timer retention, cancel both on every
   terminal path, and verify one terminal event with no late rearm after reconnect.
2. **Transport policy:** decouple idle/probe settings; test control-only traffic, quiet but
   responsive peers, silence, unsolicited Pongs, slow fragments, queued expiry and delayed
   callback delivery. Keep Beast idle management disabled.
3. **Application profiles:** local peers for text ping/pong, timestamp/ID echo, server challenges,
   one-way heartbeat, binary/compressed heartbeat and negotiated intervals. Verify healthy
   private silence separately from missing MD. Add text/binary metadata with focused coverage.
4. **Load and failure boundaries:** subscription bursts, heartbeat behind large/stalled writes,
   rate limits, token renewal and planned server disconnect. Decide whether bounded asynchronous
   sending or strict Pong correlation is needed from this evidence. Use simulated order replies;
   live orders are unnecessary for heartbeat testing.

The broader connection-deadline and network-loss tests remain owned by WS-06. This document
records research and options; no source TODO is marked complete by creating it.
