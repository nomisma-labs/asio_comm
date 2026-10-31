#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Exercise the example against a local TLS WebSocket peer, without external services."""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import threading


def receive_exact(connection, count):
    result = b""
    while len(result) < count:
        chunk = connection.recv(count - len(result))
        if not chunk:
            raise AssertionError("unexpected EOF from client")
        result += chunk
    return result


def read_frame(connection):
    first, second = receive_exact(connection, 2)
    assert second & 0x80, "client frames must be masked"
    size = second & 0x7F
    if size == 126:
        size = struct.unpack("!H", receive_exact(connection, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", receive_exact(connection, 8))[0]
    assert size <= 65536, "unexpected large test frame"
    mask = receive_exact(connection, 4)
    payload = receive_exact(connection, size)
    return first, bytes(value ^ mask[index % 4] for index, value in enumerate(payload))


def handshake(connection, port, expected_headers=None, absent_headers=(), target="/feed%2Fone?topic=two", reject=False):
    request = b""
    while not request.endswith(b"\r\n\r\n"):
        request += receive_exact(connection, 1)
        assert len(request) < 16384
    lines = request.decode("ascii").split("\r\n")
    assert lines[0] == f"GET {target} HTTP/1.1", lines[0]
    headers = {name.lower(): value for name, value in (line.split(": ", 1) for line in lines[1:] if line)}
    expected = {"host": f"localhost:{port}", "user-agent": "nomisma/asio_comm"}
    expected.update(expected_headers or {})
    for name, value in expected.items():
        assert headers.get(name) == value, (name, headers)
    for name in absent_headers:
        assert name not in headers, (name, headers)
    if reject:
        connection.sendall(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
        return
    accept = base64.b64encode(
        hashlib.sha1((headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
    ).decode()
    connection.sendall(
        ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
         f"Connection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n").encode()
    )


def run_case(client, context, certificate, case):
    errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def serve():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(8)
                    try:
                        connection = context.wrap_socket(raw, server_side=True)
                    except ssl.SSLError:
                        if case in ("untrusted", "wrong_host"):
                            return
                        raise
                    with connection:
                        assert case not in ("untrusted", "wrong_host"), "client accepted an invalid certificate"
                        handshake(connection, port)
                        if case == "echo":
                            assert read_frame(connection) == (0x81, b"hello")
                            # Interleave a ping with fragmented text; Beast must assemble one message.
                            connection.sendall(b"\x01\x02he\x89\x01p")
                            assert read_frame(connection) == (0x8A, b"p")
                            connection.sendall(b"\x80\x03llo")
                        elif case == "push":
                            connection.sendall(b"\x81\x07welcome")
                        elif case == "close":
                            connection.sendall(b"\x88\x02\x03\xe8")
                        elif case == "partial":
                            connection.sendall(b"\x81\x08part")
                            return
                        # close() must close the transport without waiting for a close reply.
                        assert connection.recv(1024) == b"", "unexpected data after client close"
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        host = "127.0.0.1" if case == "wrong_host" else "localhost"
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        if case == "untrusted":
            env["SSL_CERT_FILE"] = str(certificate.parent / "missing.pem")
        command = [client, f"wss://{host}:{port}/feed%2Fone?topic=two#local"]
        if case == "echo":
            command.append("hello")
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=12)
        worker.join(timeout=10)
        assert not worker.is_alive(), f"{case}: server did not stop"
        assert not errors, f"{case}: server errors: {errors}; client: {result.stdout} {result.stderr}"
        output = result.stdout + result.stderr
        if case in ("echo", "push"):
            expected = "hello" if case == "echo" else "welcome"
            assert result.returncode == 0 and f"Received: {expected}\n" in output, output
            assert "Disconnected" not in output, output
        else:
            assert result.returncode == 1 and "Received:" not in output, output
            if case in ("untrusted", "wrong_host"):
                assert "SSL handshake error" in output and "Connected:" not in output, output
            else:
                assert "Disconnected before receiving a message:" in output, output
        print(f"{case}: passed")


def run_move_case(client, context, certificate, mode):
    errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def serve():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(8)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        handshake(connection, port, {"x-move": "source"})
                        assert read_frame(connection) == (0x81, b"moved")
                        assert read_frame(connection) == (0x89, b""), "missing heartbeat after move"
                        connection.sendall(b"\x8a\x00\x81\x08accepted")
                        assert connection.recv(1024) == b"", "moved client did not close"
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        result = subprocess.run(
            [client, f"wss://localhost:{port}/feed%2Fone?topic=two#local", mode],
            env=env, text=True, capture_output=True, timeout=10,
        )
        worker.join(timeout=10)
        assert not worker.is_alive(), f"move {mode}: peer did not stop"
        assert not errors, f"move {mode}: {errors}; client: {result.stdout} {result.stderr}"
        assert result.returncode == 0, result.stdout + result.stderr
        print(f"move {mode}: passed")


def run_headers_case(client, context, certificate):
    errors = []
    attempts = (
        ({"authorization": "Bearer updated", "x-attempt": "first", "user-agent": "headers-test/1"}, ()),
        ({"authorization": "Bearer second", "x-attempt": "second", "user-agent": "headers-test/2"}, ()),
        ({"authorization": "Bearer second", "x-attempt": "second", "user-agent": "headers-test/2"}, ()),
        (None, ()),  # Oversized header: client must report the setup exception before sending HTTP.
        ({}, ("authorization", "x-attempt", "x-oversized")),  # Rejected upgrade must remain conn_failed.
        ({}, ("authorization", "x-attempt", "x-oversized")),
    )
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def serve():
            try:
                for attempt, (expected, absent) in enumerate(attempts):
                    raw, _ = listener.accept()
                    with raw:
                        raw.settimeout(8)
                        try:
                            with context.wrap_socket(raw, server_side=True) as connection:
                                if expected is None:
                                    assert connection.recv(1024) == b"", "unexpected HTTP request after setup failure"
                                    continue
                                handshake(connection, port, expected, absent, reject=attempt == 4)
                                if attempt == 4:
                                    assert connection.recv(1024) == b"", "unexpected data after rejected upgrade"
                                    continue
                                if attempt == 2:
                                    # Repeated pings verify that completion rearms the timer.
                                    for _ in range(3):
                                        assert read_frame(connection) == (0x89, b""), "expected configured outbound ping"
                                        connection.sendall(b"\x8a\x00")
                                connection.sendall(b"\x81\x08accepted")
                                assert connection.recv(1024) == b"", "unexpected data after client close"
                        except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
                            # Immediate close after setup failure may interrupt the server's TLS completion.
                            if expected is not None:
                                raise
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        result = subprocess.run(
            [client, f"wss://localhost:{port}/feed%2Fone?topic=two#local"],
            env=env, text=True, capture_output=True, timeout=20,
        )
        worker.join(timeout=10)
        assert not worker.is_alive(), "headers: server did not stop"
        assert not errors, f"headers: server errors: {errors}; client: {result.stdout} {result.stderr}"
        assert result.returncode == 0, result.stdout + result.stderr
        print("params: passed (live headers/agent, pings, reset, setup exception, rejected upgrade, recovery)")


def run_disconnecting_case(client, context, certificate):
    errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def serve():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(8)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        handshake(connection, port)
                        assert read_frame(connection) == (0x81, b"trigger")
                        # Reset TCP after the upgrade to force a write error inside on_connected.
                        connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        result = subprocess.run(
            [client, f"wss://localhost:{port}/feed%2Fone?topic=two"],
            env=env, text=True, capture_output=True, timeout=12,
        )
        worker.join(timeout=10)
        assert not worker.is_alive(), "disconnecting: server did not stop"
        assert not errors, f"disconnecting: server errors: {errors}; client: {result.stdout} {result.stderr}"
        assert result.returncode == 0, result.stdout + result.stderr
        print("disconnecting: passed (write failure, rejected sends, one terminal event, no timer rearm)")


def run_feeder_case(client, context, certificate, options, target, streams):
    errors, payloads = [], ['{"result":null,"id":1}']
    event_names = {"bookTicker": "bookTicker", "trade": "trade", "depth": "depthUpdate"}
    for index, stream in enumerate(streams):
        symbol, channel = stream.split("@", 1)
        event_type = event_names[channel.split("@", 1)[0]]
        message = {"e": event_type, "s": symbol.upper()}
        if event_type == "trade":
            message.update(t=index + 1, p="100", q="1", m=False)
        payloads.append(json.dumps(message, separators=(",", ":")))

    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]

        def serve():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(8)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        handshake(connection, port, target=target)
                        kind, payload = read_frame(connection)
                        assert kind == 0x81, "subscription must be a text frame"
                        assert json.loads(payload) == {"method": "SUBSCRIBE", "params": streams, "id": 1}, payload
                        for message in payloads:
                            encoded = message.encode()
                            assert len(encoded) < 126
                            connection.sendall(bytes((0x81, len(encoded))) + encoded)
                        # The pong confirms reads progressed past every data message on this connection.
                        connection.sendall(b"\x89\x04feed")
                        assert read_frame(connection) == (0x8A, b"feed"), "missing pong with matching payload"
                        connection.sendall(b"\x88\x02\x03\xe8")
                        assert connection.recv(1024) == b"", "feeder did not close after disconnection"
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        result = subprocess.run(
            [client, *options, "--endpoint", f"wss://localhost:{port}{target}"],
            env=env, text=True, capture_output=True, timeout=12,
        )
        worker.join(timeout=10)
        assert not worker.is_alive(), "feeder: server did not stop"
        assert not errors, f"feeder: server errors: {errors}; client: {result.stdout} {result.stderr}"
        assert result.returncode == 1 and "Disconnected:" in result.stdout, result.stdout + result.stderr
        assert result.stdout.count("Connected:") == 1, result.stdout
        for payload in payloads:
            assert payload in result.stdout, f"missing logged payload: {payload}; output: {result.stdout}"
        print(f"{Path(client).name}: passed (one connection, {len(streams)} streams)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--headers-client", required=True)
    parser.add_argument("--disconnecting-client", required=True)
    parser.add_argument("--move-client", required=True)
    parser.add_argument("--feeder-client")
    parser.add_argument("--perp-feeder-client")
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="asio-comm-wss-") as directory:
        root = Path(directory)
        certificate, key = root / "cert.pem", root / "key.pem"
        subprocess.run(
            [args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
             "-keyout", str(key), "-out", str(certificate)],
            check=True, capture_output=True,
        )
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        for case in ("echo", "push", "close", "partial", "untrusted", "wrong_host"):
            run_case(args.client, context, certificate, case)
        run_headers_case(args.headers_client, context, certificate)
        run_disconnecting_case(args.disconnecting_client, context, certificate)
        for mode in ("construct", "assign", "connected", "connected_assign"):
            run_move_case(args.move_client, context, certificate, mode)
        for client, target in ((args.feeder_client, "/ws"), (args.perp_feeder_client, "/public/ws")):
            if not client:
                continue
            run_feeder_case(
                client, context, certificate, [], target,
                ["btcusdt@bookTicker", "btcusdt@trade", "btcusdt@depth@100ms"],
            )
            run_feeder_case(
                client, context, certificate,
                ["--symbols", "BTCUSDT,eThUsDt", "--channels", "trade,depth@100ms"], target,
                ["btcusdt@trade", "btcusdt@depth@100ms", "ethusdt@trade", "ethusdt@depth@100ms"],
            )
            run_feeder_case(
                client, context, certificate, ["--channels", "bookTicker"], target, ["btcusdt@bookTicker"],
            )
            run_feeder_case(
                client, context, certificate, ["--channels", "trade"], target, ["btcusdt@trade"],
            )
            for options in (["--symbols", ""], ["--symbols", 'BTCUSDT"'],
                            ["--channels", "unknown"], ["--channels", "aggTrade"]):
                result = subprocess.run([client, *options], text=True, capture_output=True, timeout=3)
                assert result.returncode == 1 and result.stderr and "Connecting" not in result.stdout, result
            print(f"{Path(client).name}: invalid settings rejected before connecting")



if __name__ == "__main__":
    main()
