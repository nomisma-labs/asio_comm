#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Check the common exit interface for HTTPS and WebSocket symbol providers using local TLS peers."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

# Reuse the existing exchange wire fixtures; applications themselves never parse JSON with a DOM.
sys.path.insert(0, str(Path(__file__).resolve().parent / "web_socket"))
from web_socket.exchange_apps_test import PROVIDERS, message
from web_socket.loopback_test import handshake, read_frame
from https.symbol_providers_test import fixtures


def response_fixture(client):
    if client.name in fixtures:
        return None, fixtures[client.name]["success"], fixtures[client.name]["error"]
    if client.name == "binance_spot_symbol_provider":
        return ({"id": "exchange-info", "method": "exchangeInfo",
                 "params": {"permissions": ["SPOT"], "showPermissionSets": False}},
                {"id": "exchange-info", "status": 200, "result": {"symbols": [{"symbol": "BTCUSDT"}]}},
                {"id": "exchange-info", "status": 400, "error": {"msg": "Unavailable"}})
    request, _, catalog = PROVIDERS[client.name]
    return request, catalog, {"error": {"message": "Unavailable"}}


def expect_closed(connection):
    try:
        assert connection.recv(4096) == b"", "Expected transport close, not another request"
    except (ConnectionResetError, BrokenPipeError, ssl.SSLEOFError):
        # close() shuts down the transport without a TLS close_notify exchange.
        pass


def run_case(client, context, certificate, mode):
    request, catalog, api_error = response_fixture(client)
    is_ws = request is not None
    ready = threading.Event()
    errors = []
    times = {}
    payload = json.dumps(api_error if mode == "api_error" else catalog, separators=(",", ":")).encode()
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]

        def serve():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(5)
                    if mode == "signal_connecting":
                        assert raw.recv(4096), "Missing TLS ClientHello"
                        ready.set()  # Keep TLS setup pending while the app handles Ctrl+C.
                        try:
                            while raw.recv(4096):
                                pass
                        except ConnectionResetError:
                            pass
                        return
                    if mode == "timeout":
                        # The application runtime budget must not include connection setup.
                        time.sleep(1.2)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        if is_ws:
                            handshake(connection, port, target="/catalog")
                        times["connected"] = time.monotonic()
                        if mode == "zero":
                            expect_closed(connection)  # Any HTTP/WS request bytes would fail this check.
                            return
                        if is_ws:
                            kind, data = read_frame(connection)
                            assert kind == 0x81 and json.loads(data) == request, data
                        else:
                            data = b""
                            while not data.endswith(b"\r\n\r\n"):
                                part = connection.recv(1)
                                assert part, "Closed before HTTP request"
                                data += part
                            assert data.startswith(b"GET /catalog HTTP/1.1\r\n"), data
                        if mode in ("success", "api_error", "output_failure"):
                            if is_ws:
                                message(connection, payload)
                            else:
                                connection.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {len(payload)}\r\n\r\n".encode() + payload)
                        elif mode == "peer_close":
                            if is_ws:
                                connection.sendall(b"\x88\x02\x03\xe8")
                            else:
                                return
                        elif is_ws:
                            # An incomplete catalog and control traffic must not satisfy completion.
                            if client.name != "binance_spot_symbol_provider":
                                message(connection, {"ack": "waiting"})
                            connection.sendall(bytes((0x01, 5)) + payload[:5])
                            connection.sendall(b"\x89\x04wait")
                            assert read_frame(connection) == (0x8A, b"wait")
                        else:
                            # A partial HTTP body must be abandoned when the runtime budget expires.
                            connection.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {len(payload)}\r\n\r\n".encode() + payload[:5])
                        ready.set()
                        expect_closed(connection)
                        times["closed"] = time.monotonic()
            except BaseException as error:
                errors.append(error)
                ready.set()

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        options = [] if mode == "sigterm_default" else ["--exit-after-sec", {
            "zero": "0", "timeout": "1", "sigint": "30", "signal_connecting": "0",
            "peer_close": "4294967295", "success": "30", "api_error": "30", "output_failure": "30",
        }[mode]]
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        command = [str(client), "--endpoint", f"{'wss' if is_ws else 'https'}://localhost:{port}/catalog", *options]
        output_file = open("/dev/full", "wb") if mode == "output_failure" else None
        try:
            process = subprocess.Popen(command, env=env, stdout=output_file or subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                if mode in ("sigint", "sigterm_default", "signal_connecting"):
                    assert ready.wait(5), "Provider did not reach the signal test point"
                    assert not errors, errors
                    if mode == "sigterm_default":
                        time.sleep(1.2)
                        assert process.poll() is None, "Default application runtime should be unlimited"
                    process.send_signal(signal.SIGTERM if mode == "sigterm_default" else signal.SIGINT)
                stdout, stderr = process.communicate(timeout=8)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
        finally:
            if output_file:
                output_file.close()
        worker.join(6)
        assert not worker.is_alive(), "TLS peer did not finish"
        assert not errors, (client.name, mode, errors, stdout, stderr)
        assert process.returncode == (0 if mode == "success" else 1), (client.name, mode, stdout, stderr)
        output = stdout or b""
        if mode == "success":
            if is_ws:
                connection_log, separator, output = output.partition(b"\n")
                assert separator and b" connecting with params {" in connection_log, stdout
            assert output == payload + b"\n", stdout
            assert b"Received " in stderr and b"Stopping" not in stderr, stderr
        elif not is_ws:
            assert not output, stdout
        if mode == "timeout":
            assert 0.85 <= times["closed"] - times["connected"] < 4, times
            assert b"Stopping after 1 seconds connected" in stderr, stderr
        elif mode == "zero":
            assert b"Stopping after connection" in stderr, stderr
        elif mode.startswith("sig"):
            assert b"Stopping on signal" in stderr and b"seconds connected" not in stderr, stderr
        elif mode == "output_failure":
            assert b"Failed to write symbol information" in stderr or b"Failed to write symbol information" in output, stderr
        print(f"{client.name}/{mode}: passed", flush=True)


def check_provider(client, context, certificate):
    for value in (None, "-1", "1.5", "abc", "", "4294967296"):
        command = [str(client), "--exit-after-sec", *([] if value is None else [value])]
        result = subprocess.run(command, capture_output=True, timeout=3)
        assert result.returncode == 1 and result.stderr and not result.stdout, result
    result = subprocess.run([str(client), "--help"], capture_output=True, timeout=3)
    assert result.returncode == 0 and b"--exit-after-sec" in result.stdout
    for mode in ("zero", "timeout", "sigint", "sigterm_default", "signal_connecting", "success", "api_error", "peer_close"):
        run_case(client, context, certificate, mode)
    if Path("/dev/full").exists():
        run_case(client, context, certificate, "output_failure")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, action="append", required=True)
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="asio-provider-exit-") as directory:
        folder = Path(directory)
        certificate, key = folder / "cert.pem", folder / "key.pem"
        subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", str(key), "-out", str(certificate)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        # Do not race immediate HTTPS close with post-handshake TLS 1.3 ticket writes.
        context.num_tickets = 0
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = [pool.submit(check_provider, client, context, certificate) for client in args.client]
            for result in results:
                result.result()


if __name__ == "__main__":
    main()
