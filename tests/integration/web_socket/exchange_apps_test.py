#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Exercise catalog completion, exchange wire protocols, heartbeats and binary log suppression."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import threading

from loopback_test import handshake, read_frame
from symbol_provider_test import frame_header


PROVIDERS = {
    'coinbase_spot_symbol_provider': (
        {'type': 'subscribe', 'channels': ['status']},
        {'type': 'subscriptions'},
        {'type': 'status', 'products': [{'id': 'BTC-USD'}], 'currencies': []}),
    'kraken_spot_symbol_provider': (
        {'method': 'subscribe', 'params': {'channel': 'instrument', 'snapshot': True,
                                         'include_tokenized_assets': False}, 'req_id': 1},
        {'method': 'subscribe', 'success': True},
        {'channel': 'instrument', 'type': 'snapshot', 'data': {'pairs': [{'symbol': 'BTC/USD'}], 'assets': []}}),
    'deribit_perp_symbol_provider': (
        {'jsonrpc': '2.0', 'id': 1, 'method': 'public/get_instruments',
         'params': {'currency': 'any', 'kind': 'future', 'expired': False}},
        {'jsonrpc': '2.0', 'method': 'heartbeat'},
        {'jsonrpc': '2.0', 'id': 1, 'result': [{'instrument_name': 'BTC-PERPETUAL'}]}),
    'hyperliquid_spot_symbol_provider': (
        {'method': 'post', 'id': 1, 'request': {'type': 'info', 'payload': {'type': 'spotMeta'}}},
        {'channel': 'subscriptionResponse'},
        {'channel': 'post', 'data': {'id': 1, 'response': {'type': 'info',
         'payload': {'type': 'spotMeta', 'data': {'tokens': [], 'universe': []}}}}}),
    'hyperliquid_perp_symbol_provider': (
        {'method': 'post', 'id': 1, 'request': {'type': 'info', 'payload': {'type': 'meta'}}},
        {'channel': 'subscriptionResponse'},
        {'channel': 'post', 'data': {'id': 1, 'response': {'type': 'info',
         'payload': {'type': 'meta', 'data': {'universe': [], 'marginTables': []}}}}}),
}

# Each fixture fixes a documented channel selection on one socket. Two-symbol cases also check
# wire casing, symbol syntax and multiplexing; this deliberately avoids a JSON parser in the apps.
FEEDERS = {
    'coinbase_spot_feeder': ('BTC-USD,ETH-USD', [
        {'type': 'subscribe', 'product_ids': ['BTC-USD', 'ETH-USD'], 'channels': ['heartbeat', 'matches']}]),
    'upbit_spot_feeder': ('SGD-BTC,SGD-ETH', [[
        {'ticket': 'nomisma-md'}, {'type': 'trade', 'codes': ['SGD-BTC', 'SGD-ETH']}, {'format': 'DEFAULT'}]]),
    'bybit_spot_feeder': ('BTCUSDT,ETHUSDT', [
        {'op': 'subscribe', 'args': ['publicTrade.BTCUSDT', 'publicTrade.ETHUSDT']}]),
    'bitget_spot_feeder': ('BTCUSDT,ETHUSDT', [{'op': 'subscribe', 'args': [
        {'instType': 'spot', 'topic': 'publicTrade', 'symbol': 'BTCUSDT'},
        {'instType': 'spot', 'topic': 'publicTrade', 'symbol': 'ETHUSDT'}]}]),
    'gate_spot_feeder': ('BTC_USDT,ETH_USDT', [
        {'channel': 'spot.trades', 'event': 'subscribe', 'payload': ['BTC_USDT', 'ETH_USDT']}]),
    'kucoin_spot_feeder': ('BTC-USDT,ETH-USDT', [
        {'id': '1', 'action': 'SUBSCRIBE', 'tradeType': 'SPOT', 'channel': 'trade', 'symbol': 'BTC-USDT'},
        {'id': '2', 'action': 'SUBSCRIBE', 'tradeType': 'SPOT', 'channel': 'trade', 'symbol': 'ETH-USDT'}]),
    'mexc_spot_feeder': ('BTCUSDT,ETHUSDT', [{'method': 'SUBSCRIPTION', 'params': [
        'spot@public.aggre.deals.v3.api.pb@100ms@BTCUSDT',
        'spot@public.aggre.deals.v3.api.pb@100ms@ETHUSDT']}]),
    'htx_spot_feeder': ('BTCUSDT,ETHUSDT', [
        {'sub': 'market.btcusdt.trade.detail', 'id': '1'}, {'sub': 'market.ethusdt.trade.detail', 'id': '2'}]),
    'crypto_com_spot_feeder': ('BTC_USDT,ETH_USDT', [
        {'id': 1, 'method': 'subscribe', 'params': {'channels': ['trade.BTC_USDT', 'trade.ETH_USDT']}}]),
    'kraken_spot_feeder': ('BTC/USD,ETH/USD', [
        {'method': 'subscribe', 'params': {'channel': 'trade', 'symbol': ['BTC/USD', 'ETH/USD'],
                                         'snapshot': True}, 'req_id': 1}]),
    'lbank_spot_feeder': ('BTC_USDT,ETH_USDT', [
        {'action': 'subscribe', 'subscribe': 'trade', 'pair': 'btc_usdt'},
        {'action': 'subscribe', 'subscribe': 'trade', 'pair': 'eth_usdt'}]),
    'xt_spot_feeder': ('BTC_USDT,ETH_USDT', [
        {'method': 'subscribe', 'params': ['trade@btc_usdt', 'trade@eth_usdt'], 'id': '1'}]),
    'hyperliquid_spot_feeder': ('@107,@151', [
        {'method': 'subscribe', 'subscription': {'type': 'trades', 'coin': '@107'}},
        {'method': 'subscribe', 'subscription': {'type': 'trades', 'coin': '@151'}}]),
    'hyperliquid_perp_feeder': ('BTC,ETH', [
        {'method': 'subscribe', 'subscription': {'type': 'trades', 'coin': 'BTC'}},
        {'method': 'subscribe', 'subscription': {'type': 'trades', 'coin': 'ETH'}}]),
    'binance_spot_sbe_feeder': ('BTCUSDT,ETHUSDT', [
        {'method': 'SUBSCRIBE', 'params': ['btcusdt@trade', 'ethusdt@trade'], 'id': 1}]),
}
HEARTBEATS = {
    'bybit_spot_feeder': {'op': 'ping'},
    'bitget_spot_feeder': 'ping',
    'kucoin_spot_feeder': {'op': 'ping', 'id': 'keepalive'},
    'mexc_spot_feeder': {'method': 'PING'},
    'xt_spot_feeder': 'ping',
    'hyperliquid_spot_feeder': {'method': 'ping'},
    'hyperliquid_perp_feeder': {'method': 'ping'},
}


def message(connection, data, binary=False):
    payload = data if isinstance(data, bytes) else json.dumps(data).encode()
    connection.sendall(frame_header(0x82 if binary else 0x81, len(payload)) + payload)


def fragmented(connection, payload):
    cut = max(1, len(payload) // 2)
    connection.sendall(frame_header(0x01, cut) + payload[:cut])
    connection.sendall(b'\x89\x04test')
    assert read_frame(connection) == (0x8a, b'test')
    connection.sendall(frame_header(0x80, len(payload) - cut) + payload[cut:])


def run_peer(client, context, certificate, options, serve, expected):
    errors = []
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def worker():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(24)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        headers = {'x-mbx-apikey': 'loopback-test-only'} if 'sbe' in client.name else {}
                        handshake(connection, port, expected_headers=headers, target='/public')
                        serve(connection)
            except BaseException as error:
                errors.append(repr(error))

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / 'empty'),
                   BINANCE_SBE_API_KEY='loopback-test-only')
        process = subprocess.Popen([str(client), '--endpoint', f'wss://localhost:{port}/public', *options],
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = process.communicate(timeout=28)
        thread.join(timeout=5)
        assert not thread.is_alive(), 'local TLS peer did not finish'
        assert not errors, (client.name, errors, stdout, stderr)
        assert process.returncode == expected, (client.name, process.returncode, stdout, stderr)
        return stdout, stderr


def check_provider(client, context, cert):
    request, ack, catalog = PROVIDERS[client.name]
    encoded = json.dumps(catalog, separators=(',', ':')).encode()

    def serve(connection):
        kind, data = read_frame(connection)
        assert kind == 0x81 and json.loads(data) == request, data
        message(connection, ack)
        fragmented(connection, encoded)
        assert connection.recv(1024) == b'', 'catalog provider did not close'

    stdout, _ = run_peer(client, context, cert, [], serve, 0)
    connection_log, separator, output = stdout.partition(b'\n')
    assert separator and b' connecting with params {' in connection_log, (client.name, stdout)
    assert output == encoded + b'\n', (client.name, stdout)

    def error(connection):
        read_frame(connection)
        message(connection, {'error': {'message': 'catalog unavailable'}})
        assert connection.recv(1024) == b''

    stdout, stderr = run_peer(client, context, cert, [], error, 1)
    connection_log, separator, output = stdout.partition(b'\n')
    assert separator and b' connecting with params {' in connection_log, (client.name, stdout)
    assert not output and b'API error' in stderr, (client.name, stdout, stderr)

    # An acknowledgement without a catalog must never become a successful empty result.
    def ack_only(connection):
        read_frame(connection)
        message(connection, ack)

    stdout, _ = run_peer(client, context, cert, [], ack_only, 1)
    connection_log, separator, output = stdout.partition(b'\n')
    assert separator and b' connecting with params {' in connection_log, (client.name, stdout)
    assert not output, (client.name, stdout)
    print(client.name, 'catalog, fragmented input, ack exclusion and API errors passed', flush=True)


def check_feeder(client, context, cert):
    symbols, requests = FEEDERS[client.name]
    options = ['--symbols', symbols.lower() if 'hyperliquid' not in client.name else symbols, '--channels', 'trade']
    binary = client.name in ('binance_spot_sbe_feeder', 'mexc_spot_feeder', 'htx_spot_feeder')
    marker = 'MARKET_BODY_MUST_STAY_HIDDEN'

    def serve(connection):
        for expected in requests:
            kind, body = read_frame(connection)
            assert kind == 0x81
            actual = json.loads(body)
            if client.name == 'gate_spot_feeder':
                assert isinstance(actual.pop('time'), int)
            if client.name == 'crypto_com_spot_feeder':
                assert isinstance(actual.pop('nonce'), int)
            assert actual == expected, (client.name, actual, expected)
        # A fragmented JSON reply with an interleaved protocol Ping must remain one data event.
        fragmented(connection, b'{"ack":"complete"}')
        if client.name == 'lbank_spot_feeder':
            message(connection, {'action': 'ping', 'ping': 'echo-token'})
            assert json.loads(read_frame(connection)[1]) == {'action': 'pong', 'pong': 'echo-token'}
        elif client.name == 'crypto_com_spot_feeder':
            message(connection, {'id': 42, 'method': 'public/heartbeat', 'code': 0})
            assert json.loads(read_frame(connection)[1]) == {'id': 42, 'method': 'public/respond-heartbeat'}
        elif client.name == 'htx_spot_feeder':
            message(connection, gzip.compress(b'{"ping":42}'), True)
            assert json.loads(read_frame(connection)[1]) == {'pong': 42}
        if client.name == 'binance_spot_sbe_feeder':
            payload = struct.pack('<HHHH', 123, 10000, 1, 0) + marker.encode()
            message(connection, payload, True)
            message(connection, b'\x01', True)  # A short header is classified without reading past it.
        elif client.name == 'mexc_spot_feeder':
            channel = b'spot@public.aggre.deals.v3.api.pb@100ms@BTCUSDT'
            message(connection, b'\x0a' + bytes([len(channel)]) + channel + b'\x12' + bytes([len(marker)]) + marker.encode(), True)
            message(connection, b'\x0a\x80', True)  # A truncated varint cannot expose payload bytes.
        elif client.name == 'htx_spot_feeder':
            message(connection, gzip.compress(json.dumps({'ch': 'market.btcusdt.trade.detail', 'data': marker}).encode()), True)
        else:
            message(connection, {'market_sample': marker})
        if client.name in HEARTBEATS:
            # Check the actual timer rather than invoking a helper with the same implementation.
            kind, body = read_frame(connection)
            assert kind == 0x81
            expected = HEARTBEATS[client.name]
            assert (body.decode() if isinstance(expected, str) else json.loads(body)) == expected, body
        connection.sendall(b'\x88\x02\x03\xe8')

    stdout, stderr = run_peer(client, context, cert, options, serve, 1)
    if binary:
        assert marker.encode() not in stdout + stderr and b'type=' in stdout and b'size=' in stdout, stdout
        expected_type = {
            'binance_spot_sbe_feeder': b'SBE:Trades:template=10000',
            'mexc_spot_feeder': b'protobuf:spot@public.aggre.deals.v3.api.pb@100ms@BTCUSDT',
            'htx_spot_feeder': b'gzip:"market.btcusdt.trade.detail"',
        }[client.name]
        assert expected_type in stdout, stdout
    else:
        assert marker.encode() in stdout, stdout

    # A Spot ticker alone does not provide BBO on these APIs. The ticker selection must also
    # subscribe to the documented level-one book, on the same connection.
    ticker_requests = {
        'bybit_spot_feeder': [
            {'op': 'subscribe', 'args': ['tickers.BTCUSDT', 'orderbook.1.BTCUSDT']}],
        'kucoin_spot_feeder': [
            {'id': '1', 'action': 'SUBSCRIBE', 'tradeType': 'SPOT', 'channel': 'ticker', 'symbol': 'BTC-USDT'},
            {'id': '2', 'action': 'SUBSCRIBE', 'tradeType': 'SPOT', 'channel': 'obu', 'symbol': 'BTC-USDT', 'depth': '1'}],
    }
    if client.name in ticker_requests:
        def ticker(connection):
            for expected in ticker_requests[client.name]:
                kind, body = read_frame(connection)
                assert kind == 0x81 and json.loads(body) == expected, body
            connection.sendall(b'\x88\x02\x03\xe8')

        run_peer(client, context, cert, ['--symbols', symbols.split(',')[0], '--channels', 'ticker'], ticker, 1)
    print(client.name, 'two-symbol wire format, capture, keepalive and close passed', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', type=Path, action='append', required=True)
    parser.add_argument('--openssl', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='asio-exchange-apps-') as folder:
        root = Path(folder)
        cert, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run([args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
                        '-keyout', str(key), '-out', str(cert)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)

        def check(client):
            for options in (['--help'], ['--max-message-size', '0'], ['--endpoint']):
                r = subprocess.run([str(client), *options], capture_output=True, timeout=4)
                assert r.returncode == (0 if options == ['--help'] else 1), (client, r)
            if client.name in PROVIDERS:
                check_provider(client, context, cert)
            else:
                for options in (['--symbols', ''], ['--symbols', 'BTC"'], ['--channels', 'unknown']):
                    r = subprocess.run([str(client), *options], capture_output=True, timeout=4)
                    assert r.returncode == 1 and r.stderr, (client, r)
                # Bybit counts the extra level-one subscriptions in its ten-argument request limit.
                if client.name == 'bybit_spot_feeder':
                    r = subprocess.run([str(client), '--symbols', 'A,B,C,D,E,F', '--channels', 'ticker'],
                                       capture_output=True, timeout=4)
                    assert r.returncode == 1 and b'ten subscription args' in r.stderr, r
                check_feeder(client, context, cert)

        with ThreadPoolExecutor(max_workers=10) as pool:
            list(pool.map(check, args.client))


if __name__ == '__main__':
    main()
