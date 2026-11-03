#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Verify passive control-Ping deadlines with local TLS peers."""

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


CASES = ('silent', 'traffic', 'refresh', 'disabled', 'peer_close', 'read_error',
         'close_connected', 'close_after_ping', 'late_handshake', 'reconnect', 'move',
         'slow_handler', 'graceful')


def await_disconnect(connection):
    # A deadline aborts TCP without depending on a WebSocket Close or TLS shutdown reply.
    try:
        while connection.recv(4096):
            pass
    except ConnectionResetError:
        pass


def ping(connection, payload):
    connection.sendall(bytes((0x89, len(payload))) + payload)
    assert read_frame(connection) == (0x8a, payload), 'automatic Pong must echo the Ping payload'


def run_case(client, context, certificate, case):
    errors = []
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(2)
        listener.settimeout(6)
        port = listener.getsockname()[1]

        def serve():
            try:
                for attempt in range(2 if case == 'reconnect' else 1):
                    raw, _ = listener.accept()
                    with raw:
                        raw.settimeout(6)
                        with context.wrap_socket(raw, server_side=True) as connection:
                            if case == 'late_handshake':
                                time.sleep(2.2)
                            handshake(connection, port)
                            if case == 'refresh':
                                for payload in (b'one', b'two', b'three'):
                                    time.sleep(0.7)
                                    ping(connection, payload)
                            elif case == 'traffic':
                                # Data and the reply to an active Ping must not refresh the passive deadline.
                                for _ in range(6):
                                    connection.sendall(b'\x81\x04data')
                                    time.sleep(0.25)
                                opcode, payload = read_frame(connection)
                                assert opcode == 0x89 and payload, 'active Ping must remain independent'
                                connection.sendall(bytes((0x8a, len(payload))) + payload)
                            elif case == 'peer_close':
                                connection.sendall(b'\x88\x02\x03\xe8')
                                assert read_frame(connection)[0] == 0x88
                                connection.unwrap().close()
                                return
                            elif case == 'read_error':
                                return
                            elif case == 'graceful':
                                opcode, payload = read_frame(connection)
                                assert opcode == 0x88, 'receive watchdog must start graceful close'
                                connection.sendall(bytes((0x88, len(payload))) + payload)
                                connection.unwrap().close()
                                return
                            elif case in ('disabled', 'close_after_ping', 'late_handshake') or attempt == 1:
                                ping(connection, b'probe')
                                if case == 'disabled':
                                    time.sleep(2.2)
                                connection.sendall(b'\x81\x06finish')
                            await_disconnect(connection)
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / 'empty'))
        started = time.monotonic()
        result = subprocess.run(
            [client, f'wss://localhost:{port}/feed%2Fone?topic=two', case],
            env=env, capture_output=True, text=True, timeout=10,
        )
        elapsed = time.monotonic() - started
        worker.join(timeout=1)
        output = result.stdout + result.stderr
        assert not worker.is_alive(), f'{case}: peer did not stop; {output}'
        assert not errors, f'{case}: {errors}; {output}'
        assert result.returncode == 0, f'{case}: {output}'
        if case in ('peer_close', 'read_error', 'close_connected', 'close_after_ping'):
            assert elapsed < 5, f'{case}: terminal cleanup left the ten-second watchdog waiting'
        print(f'passive Ping {case}: passed', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--client', required=True)
    parser.add_argument('--openssl', required=True)
    parser.add_argument('--case', choices=CASES)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='asio-comm-passive-ping-') as directory:
        root = Path(directory)
        certificate, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run(
            [args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
             '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
             '-keyout', str(key), '-out', str(certificate)], check=True, capture_output=True,
        )
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        with ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(lambda case: run_case(args.client, context, certificate, case),
                          (args.case,) if args.case else CASES))


if __name__ == '__main__':
    main()
