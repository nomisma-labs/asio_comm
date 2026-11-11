#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

"""Check public feeder wire protocols and keepalives against local TLS peers."""

import argparse
from concurrent.futures import ThreadPoolExecutor
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


def send_text(connection, text):
    data = text.encode()
    length = len(data)
    header = bytes((0x81, length)) if length < 126 else b'\x81\x7e' + struct.pack('!H', length)
    connection.sendall(header + data)


def run_case(client, context, certificate, options, requests, messages, keepalive=None):
    errors = []
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def serve():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(25)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        handshake(connection, port, target='/public')
                        for request in requests:
                            kind, payload = read_frame(connection)
                            assert kind == 0x81, 'subscription must be a text frame'
                            assert json.loads(payload) == request, payload
                        for message in messages:
                            send_text(connection, message)
                        if keepalive == 'text':
                            assert read_frame(connection) == (0x81, b'ping'), 'expected OKX text ping'
                            send_text(connection, 'pong')
                        elif keepalive == 'control':
                            kind, payload = read_frame(connection)
                            assert kind == 0x89, 'expected outbound WebSocket ping'
                            connection.sendall(bytes((0x8A, len(payload))) + payload)
                        # This pong also confirms all preceding messages were consumed by the client.
                        connection.sendall(b'\x89\x04feed')
                        assert read_frame(connection) == (0x8A, b'feed')
                        connection.sendall(b'\x88\x02\x03\xe8')
                        assert connection.recv(1024) == b'', 'client did not close after peer disconnection'
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / 'empty'))
        result = subprocess.run(
            [client, *options, '--endpoint', f'wss://localhost:{port}/public'],
            env=env, text=True, capture_output=True, timeout=30,
        )
        worker.join(timeout=2)
        output = result.stdout + result.stderr
        assert not worker.is_alive(), 'feeder server did not stop'
        assert not errors, f'{errors}; client: {output}'
        assert result.returncode == 1 and 'Disconnected:' in output, output
        for message in messages + (['pong'] if keepalive == 'text' else []):
            assert message + '\n' in output, f'missing logged payload {message}: {output}'


def subscriptions(exchange, symbols, channels):
    if exchange == 'bitflyer':
        names = {'ticker': ['lightning_ticker'], 'trade': ['lightning_executions'],
                 'depth': ['lightning_board_snapshot', 'lightning_board']}
        streams = [f'{name}_{symbol}' for symbol in symbols for channel in channels for name in names[channel]]
        return [{'jsonrpc': '2.0', 'method': 'subscribe', 'params': {'channel': stream}, 'id': index + 1}
                for index, stream in enumerate(streams)]
    if exchange == 'deribit':
        names = {'ticker': 'ticker', 'trade': 'trades', 'depth': 'book'}
        return [{'jsonrpc': '2.0', 'id': 1, 'method': 'public/subscribe', 'params': {
            'channels': [f'{names[channel]}.{symbol}.100ms' for symbol in symbols for channel in channels]}}]
    assert exchange == 'okex'
    names = {'ticker': 'tickers', 'trade': 'trades', 'depth': 'books'}
    return [{'op': 'subscribe', 'args': [{'channel': names[channel], 'instId': symbol}
                                       for symbol in symbols for channel in channels]}]


def check_exchange(exchange, client, context, certificate):
    symbols, messages, keepalive = {
        'bitflyer': (['BTC_JPY', 'ETH_JPY'], [
            '{"jsonrpc":"2.0","id":1,"result":true}',
            '{"jsonrpc":"2.0","method":"channelMessage","params":{"channel":"lightning_board_BTC_JPY",'
            '"message":{"mid_price":100,"bids":[],"asks":[]}}}'], 'control'),
        'deribit': (['BTC-PERPETUAL', 'ETH-PERPETUAL'], [
            '{"jsonrpc":"2.0","id":1,"result":["book.BTC-PERPETUAL.100ms"]}',
            '{"jsonrpc":"2.0","method":"subscription","params":{"channel":"book.BTC-PERPETUAL.100ms",'
            '"data":{"type":"snapshot","change_id":1,"bids":[],"asks":[]}}}'], 'control'),
        'okex': (['BTC-USDT', 'ETH-USDT'], [
            '{"event":"subscribe","arg":{"channel":"books","instId":"BTC-USDT"}}',
            '{"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot",'
            '"data":[{"asks":[],"bids":[],"seqId":1}]}'], 'text'),
    }[exchange]
    run_case(client, context, certificate, [],
             subscriptions(exchange, symbols[:1], ['ticker', 'trade', 'depth']), messages, keepalive)
    run_case(client, context, certificate,
             ['--symbols', ','.join(symbols).lower(), '--channels', 'trade,depth'],
             subscriptions(exchange, symbols, ['trade', 'depth']), messages)
    # Exchange errors are data too: preserve them for the operator's review.
    run_case(client, context, certificate, ['--channels', 'ticker'],
             subscriptions(exchange, symbols[:1], ['ticker']), ['{"error":{"message":"invalid subscription"}}'])
    invalid_options = [['--symbols', symbols[0] + ','], ['--symbols', ''], ['--symbols', 'BTC"USD'], ['--channels', 'unknown'],
                       ['--channels', ''], ['--symbols', ','], ['--endpoint']]
    for options in invalid_options:
        result = subprocess.run([client, *options], text=True, capture_output=True, timeout=3)
        assert result.returncode == 1 and result.stderr and 'Connecting' not in result.stdout, result
    help_result = subprocess.run([client, '--help'], text=True, capture_output=True, timeout=3)
    assert help_result.returncode == 0 and 'Usage:' in help_result.stdout, help_result
    print(f'{exchange}: subscriptions, two symbols, raw logging, keepalive, close and invalid settings passed')


def main():
    parser = argparse.ArgumentParser()
    for exchange in ('bitflyer', 'deribit', 'okex'):
        parser.add_argument(f'--{exchange}-client')
    parser.add_argument('--openssl', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='asio-comm-feeders-') as directory:
        root = Path(directory)
        certificate, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run(
            [args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
             '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
             '-keyout', str(key), '-out', str(certificate)], check=True, capture_output=True,
        )
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        # Keepalive checks take twenty seconds; the three independent peers run concurrently.
        with ThreadPoolExecutor(max_workers=3) as pool:
            results = [pool.submit(check_exchange, exchange, client, context, certificate)
                       for exchange in ('bitflyer', 'deribit', 'okex')
                       if (client := getattr(args, f'{exchange}_client'))]
            for result in results:
                result.result()


if __name__ == '__main__':
    main()
