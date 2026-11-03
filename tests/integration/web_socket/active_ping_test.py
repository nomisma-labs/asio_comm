#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Verify active control-Ping state transitions and Pong deadlines with local TLS peers."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading
import time

from loopback_test import handshake, read_frame
from passive_ping_test import await_disconnect


CASES = ('silent', 'traffic', 'different_payload', 'stale', 'unsolicited', 'duplicate', 'late', 'healthy', 'slow_pong',
         'queued', 'send_completion_delay', 'disabled', 'peer_close_interval', 'peer_close_pending', 'read_error_interval',
         'read_error_pending', 'close_pending', 'graceful', 'move', 'reconnect', 'send_timeout', 'send_unblocked')


def frame(connection, opcode, payload):
    assert len(payload) <= 125
    connection.sendall(bytes((opcode, len(payload))) + payload)


def read_ping(connection):
    opcode, payload = read_frame(connection)
    assert opcode == 0x89 and payload, f'expected identifiable control Ping, got {opcode}, {payload!r}'
    return payload


def serve_probe(connection, case, client_finished):
    if case in ('send_timeout', 'send_unblocked'):
        # Automatic Pongs fill the small TCP buffers while this peer does not read.
        # Beast's pending control write then holds up the client's active Ping.
        peer_payload = b'x' * 125
        connection.sendall((bytes((0x89, len(peer_payload))) + peer_payload) * 1024)
        if case == 'send_timeout':
            assert client_finished.wait(13), 'client did not finish while its Ping write was blocked'
        else:
            time.sleep(1.5)
            for attempt in range(2):
                while True:
                    opcode, payload = read_frame(connection)
                    if opcode == 0x89:
                        break
                    assert (opcode, payload) == (0x8a, peer_payload), 'expected automatic Pong while draining'
                if attempt == 0:
                    # Reply after the original send deadline, but within the new Pong deadline.
                    time.sleep(1.6)
                frame(connection, 0x8a, payload)
            frame(connection, 0x81, b'finish')
        await_disconnect(connection)
        return
    if case == 'disabled':
        frame(connection, 0x8a, b'unsolicited')
        frame(connection, 0x89, b'peer')
        assert read_frame(connection) == (0x8a, b'peer')
        time.sleep(1.2)
        frame(connection, 0x81, b'finish')
        assert connection.recv(4096) == b'', 'disabled active_ping sent data'
        return
    if case == 'unsolicited':
        frame(connection, 0x8a, b'')
        await_disconnect(connection)
        return
    if case.endswith('_interval'):
        payload = None
    else:
        waiting_since = time.monotonic()
        payload = read_ping(connection)
        if case == 'send_completion_delay':
            assert time.monotonic() - waiting_since < 2, 'fixture paused before initiating the Ping write'

    if case.startswith('peer_close'):
        frame(connection, 0x88, b'\x03\xe8')
        assert read_frame(connection)[0] == 0x88
        connection.unwrap().close()
        return
    if case.startswith('read_error'):
        return
    if case == 'graceful':
        opcode, close_payload = read_frame(connection)
        assert opcode == 0x88, 'receive watchdog must close while a probe is pending'
        frame(connection, 0x88, close_payload)
        connection.unwrap().close()
        return
    if case in ('healthy', 'slow_pong', 'queued', 'send_completion_delay'):
        for attempt in range(3):
            if case == 'slow_pong':
                # Longer than the Ping interval: no second probe may replace this pending deadline.
                time.sleep(1.3)
            if case == 'send_completion_delay' and attempt == 0:
                # The client pauses for 2.2 s before processing write completion. A response
                # after 2.5 s must satisfy the fresh 2 s Pong deadline armed by that completion.
                time.sleep(2.5)
            frame(connection, 0x8a, payload)
            if case == 'queued' and attempt == 0:
                frame(connection, 0x81, b'stall')
            if attempt != 2:
                previous = payload
                started = time.monotonic()
                payload = read_ping(connection)
                assert payload != previous, 'each probe needs a fresh payload'
                assert time.monotonic() - started >= 0.85, 'overlapping probe changed cadence'
        frame(connection, 0x81, b'finish')
    elif case == 'close_pending':
        frame(connection, 0x81, b'finish')
    elif case == 'stale':
        frame(connection, 0x8a, payload)
        current = read_ping(connection)
        assert current != payload
        frame(connection, 0x8a, payload)
        assert read_ping(connection) != current, 'a Pong with an old payload must complete the current probe'
        frame(connection, 0x81, b'finish')
    elif case == 'different_payload':
        for response in (b'different', b''):
            frame(connection, 0x8a, response)
            previous = payload
            payload = read_ping(connection)
            assert payload != previous, 'every Pong payload must complete a probe in send_done'
        frame(connection, 0x81, b'finish')
    elif case == 'duplicate':
        # The first Pong completes the probe; the second reaches waiting_to_ping and fails it.
        pong = bytes((0x8a, len(payload))) + payload
        connection.sendall(pong + pong)
    elif case == 'late':
        # Deliver data and then a Pong in one TLS write. The data callback holds the executor
        # beyond the deadline. Pong is dispatched first while still in send_done and completes it.
        connection.sendall(b'\x81\x04late' + bytes((0x8a, len(payload))) + payload)
        assert read_ping(connection) != payload, 'state dispatch must decide between Pong and timeout events'
        frame(connection, 0x81, b'finish')
    elif case == 'traffic':
        for _ in range(5):
            frame(connection, 0x81, b'data')
            frame(connection, 0x89, b'peer')
            assert read_frame(connection) == (0x8a, b'peer'), 'automatic Pong must remain independent'
            time.sleep(0.25)
    # Silent/move/reconnect peers withhold the required Pong.
    await_disconnect(connection)


def run_case(client, context, certificate, case):
    errors = []
    client_finished = threading.Event()
    with socket.socket() as listener:
        if case in ('send_timeout', 'send_unblocked'):
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def serve():
            try:
                for attempt in range(2 if case == 'reconnect' else 1):
                    raw, _ = listener.accept()
                    with raw:
                        raw.settimeout(8)
                        with context.wrap_socket(raw, server_side=True) as connection:
                            handshake(connection, port)
                            serve_probe(connection, 'healthy' if attempt else case, client_finished)
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / 'empty'))
        started = time.monotonic()
        try:
            result = subprocess.run([client, f'wss://localhost:{port}/feed%2Fone?topic=two', case],
                                    env=env, capture_output=True, text=True, timeout=14)
        finally:
            client_finished.set()
        elapsed = time.monotonic() - started
        worker.join(timeout=1)
        output = result.stdout + result.stderr
        assert not worker.is_alive(), f'{case}: peer did not stop; {output}'
        assert not errors, f'{case}: {errors}; {output}'
        assert result.returncode == 0, f'{case}: {output}'
        if case.startswith(('peer_close', 'read_error')) or case in ('close_pending', 'graceful'):
            assert elapsed < 6, f'{case}: terminal cleanup retained the ten-second timer'
        print(f'active Ping {case}: passed', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', required=True)
    parser.add_argument('--openssl', required=True)
    parser.add_argument('--case', choices=CASES)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='asio-comm-active-ping-') as directory:
        root = Path(directory)
        certificate, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run([args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
                        '-keyout', str(key), '-out', str(certificate)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(lambda case: run_case(args.client, context, certificate, case),
                          (args.case,) if args.case else CASES))


if __name__ == '__main__':
    main()
