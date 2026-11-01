#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Validate one-shot symbol retrieval and large messages with deterministic local TLS peers."""

import argparse
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import threading

from loopback_test import handshake, read_frame


def frame_header(opcode, size):
    if size < 126:
        return bytes([opcode, size])
    if size < 65536:
        return bytes([opcode, 126]) + struct.pack('!H', size)
    return bytes([opcode, 127]) + struct.pack('!Q', size)


def send_message(connection, payload, fragmented):
    if not fragmented:
        connection.sendall(frame_header(0x81, len(payload)) + payload)
        return
    # Control frames between continuation frames must not split delivered data messages.
    chunk_size = 16381
    for offset in range(0, len(payload), chunk_size):
        chunk = payload[offset:offset + chunk_size]
        final = offset + len(chunk) == len(payload)
        opcode = (1 if offset == 0 else 0) | (0x80 if final else 0)
        connection.sendall(frame_header(opcode, len(chunk)) + chunk)
        if offset == 0 and not final:
            connection.sendall(b'\x89\x04size')
            assert read_frame(connection) == (0x8a, b'size')


def check_rejection(connection, code, teardown):
    first, body = read_frame(connection)
    assert first == 0x88 and body[:2] == struct.pack('!H', code), (first, body)
    if teardown == 'reply':
        # A WebSocket close reply during TLS shutdown must not hide the protocol error.
        connection.sendall(frame_header(0x88, len(body)) + body)
        connection.unwrap().close()
    elif teardown == 'clean':
        connection.unwrap().close()
    else:
        assert teardown == 'drop'
        connection.shutdown(socket.SHUT_RDWR)


def run_peer(context, certificate, client, arguments, serve, expected_code):
    errors = []
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(12)
        port = listener.getsockname()[1]

        def worker():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(12)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        handshake(connection, port, target='/ws-api/v3')
                        serve(connection)
            except BaseException as error:
                errors.append(repr(error))

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / 'empty'))
        endpoint = f'wss://localhost:{port}/ws-api/v3'
        result = subprocess.run([client, *arguments(endpoint)], env=env, capture_output=True, timeout=18)
        thread.join(timeout=14)
        assert not thread.is_alive(), 'TLS peer did not finish'
        assert not errors, (errors, result.returncode, result.stderr, result.stdout[:1000])
        assert result.returncode == expected_code, (result.returncode, result.stderr, result.stdout[:1000])
        return result


def response(size):
    data = (b'{"id":"exchange-info","status":200,"result":{"symbols":[{"symbol":"BTCUSDT",'
            b'"baseAsset":"BTC","quoteAsset":"USDT"}],"metadata":""}}')
    assert size >= len(data)
    return data[:-3] + b'x' * (size - len(data)) + data[-3:]


def provider_case(context, certificate, client, payload, success, limit, fragmented=False, partial=False,
                  default_limit=False):
    def serve(connection):
        first, body = read_frame(connection)
        assert first == 0x81
        assert body == (b'{"id":"exchange-info","method":"exchangeInfo",'
                        b'"params":{"permissions":["SPOT"],"showPermissionSets":false}}')
        if len(payload) > limit:
            if fragmented:
                # The accumulated message exceeds the limit even though each frame fits it.
                cut = len(payload) // 2
                connection.sendall(frame_header(0x01, cut) + payload[:cut])
                connection.sendall(frame_header(0x80, len(payload) - cut))
            else:
                connection.sendall(frame_header(0x81, len(payload)))
            check_rejection(connection, 1009, 'reply')
        elif partial:
            connection.sendall(frame_header(0x81, len(payload)) + payload[:len(payload) // 2])
        else:
            send_message(connection, payload, fragmented)
            assert connection.recv(1024) == b'', 'provider did not close after one response'

    result = run_peer(context, certificate, client,
                      lambda endpoint: ['--endpoint', endpoint] +
                      ([] if default_limit else ['--max-message-size', str(limit)]),
                      serve, 0 if success else 1)
    if success:
        assert result.stdout == payload + b'\n', (len(result.stdout), len(payload), result.stdout[:500])
        assert f'Received {len(payload)} bytes'.encode() in result.stderr
    else:
        assert b'Received ' not in result.stderr and not result.stdout.startswith(b'{'), result.stdout[:1000]
        assert result.stdout or result.stderr, 'failure must have a diagnostic'
        if len(payload) > limit:
            diagnostic = f'Incoming WebSocket message exceeds max_message_size={limit} bytes'.encode()
            assert result.stdout.count(diagnostic) == 1, result.stdout[:1000]


def roundtrip_case(context, certificate, client, size):
    expected = (b'abcdefghijklmnopqrstuvwxyz' * ((size + 25) // 26))[:size]

    def serve(connection):
        actual = bytearray()
        first_frame = True
        while True:
            first, body = read_frame(connection)
            assert first & 15 == (1 if first_frame else 0), first
            actual.extend(body)
            assert len(actual) <= size
            first_frame = False
            if first & 0x80:
                break
        assert actual == expected
        send_message(connection, expected, True)
        assert connection.recv(1024) == b''

    result = run_peer(context, certificate, client, lambda endpoint: [endpoint, str(size)], serve, 0)
    assert result.stdout == f'Verified {size} bytes\n'.encode(), result.stdout[:1000]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', required=True)
    parser.add_argument('--example-client', required=True)
    parser.add_argument('--large-client', required=True)
    parser.add_argument('--openssl', required=True)
    args = parser.parse_args()
    max_unsigned = (1 << (8 * struct.calcsize('@I'))) - 1
    with tempfile.TemporaryDirectory(prefix='asio-comm-symbols-') as directory:
        root = Path(directory)
        certificate, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run(
            [args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
             '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
             '-keyout', str(key), '-out', str(certificate)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        for size in (65535, 65536, 65537, 2 * 1024 * 1024 + 17):
            roundtrip_case(context, certificate, args.large_client, size)
        for size in (255, 256, 257):
            for fragmented in (False, True):
                provider_case(context, certificate, args.client, response(size), size <= 256, 256, fragmented)
        # A realistic snapshot exceeds the library default but fits the provider's 64 MiB setting.
        provider_case(context, certificate, args.client, response(17 * 1024 * 1024 + 3), True, 64 * 1024 * 1024,
                      default_limit=True)
        provider_case(context, certificate, args.client, response(3 * 1024 * 1024 + 7), True, 64 * 1024 * 1024, True)
        provider_case(context, certificate, args.client, response(256), False, 256, partial=True)
        provider_case(context, certificate, args.client, response(256), True, max_unsigned)

        invalid_responses = [
            b'{', b'[]',
            b'{"id":"exchange-info","status":429,"error":{"code":-1003,"msg":"Too many requests"}}',
            b'{ "status" \t : 400, "error" \n : {"code":-1,"msg":"Bad request"} }',
            b'{"status":200,"error":{"code":-1,"msg":"Unexpected error"}}',
            b'{"status":500}', b'{"status":2000}', b'{"status":200.5}', b'{"status":"200"}',
        ]
        for payload in invalid_responses:
            provider_case(context, certificate, args.client, payload, False, 4096)

        # Preserve the body without interpreting its symbol entries or unknown fields.
        payload = b'{ "status" \t : \n200, "result":{"symbols":[],"unknown":[null,{"future":true}]} }'
        provider_case(context, certificate, args.client, payload, True, 4096)

        for teardown in ('reply', 'clean', 'drop'):
            def reject_default(connection):
                connection.sendall(frame_header(0x81, 16 * 1024 * 1024 + 1))
                check_rejection(connection, 1009, teardown)

            result = run_peer(context, certificate, args.example_client, lambda endpoint: [endpoint], reject_default, 1)
            output = result.stdout + result.stderr
            diagnostic = b'Incoming WebSocket message exceeds max_message_size=16777216 bytes'
            assert output.count(diagnostic) == 1, output
            assert b'Received:' not in output and output.count(b'Disconnected before receiving a message:') == 1
            assert (b'TLS shutdown error:' in output) == (teardown != 'clean'), output

        def bad_opcode(connection):
            connection.sendall(b'\x83\x00')
            check_rejection(connection, 1002, 'reply')

        result = run_peer(context, certificate, args.example_client, lambda endpoint: [endpoint], bad_opcode, 1)
        output = result.stdout + result.stderr
        assert b'opcode' in output and b'TLS shutdown error:' in output, output
        assert b'max_message_size' not in output, output
        assert output.count(b'Disconnected before receiving a message:') == 1

    for options in (['--endpoint'], ['--unknown'], ['--max-message-size', '0'], ['--max-message-size', '-1'],
                    ['--max-message-size', '12x'], ['--max-message-size', str(max_unsigned + 1)],
                    ['--max-message-size', '999999999999999999999999']):
        result = subprocess.run([args.client, *options], capture_output=True, timeout=3)
        assert result.returncode == 1 and result.stderr, result
    result = subprocess.run([args.client, '--help'], capture_output=True, timeout=3)
    assert result.returncode == 0 and b'Usage:' in result.stdout
    print('symbol provider: large roundtrips, snapshot, fragmentation, limits, API failures and arguments passed')


if __name__ == '__main__':
    main()
