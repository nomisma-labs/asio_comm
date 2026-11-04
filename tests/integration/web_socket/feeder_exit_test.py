#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Check every feeder's runtime limit, relay-driven shutdown and exit status using local TLS peers."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import signal
import socket
import ssl
import subprocess
import tempfile
import threading
import time

from loopback_test import handshake, read_frame
from exchange_apps_test import FEEDERS
from feeders_test import subscriptions


def feeder_subscription(client):
    if client.name in FEEDERS:
        return FEEDERS[client.name]
    if client.name in ("binance_spot_feeder", "binance_perp_feeder"):
        return "BTCUSDT,ETHUSDT", [{"method": "SUBSCRIBE", "params": ["btcusdt@trade", "ethusdt@trade"], "id": 1}]
    exchange = client.name.split("_")[0]
    symbols = {"bitflyer": ["BTC_JPY", "ETH_JPY"], "deribit": ["BTC-PERPETUAL", "ETH-PERPETUAL"],
               "okex": ["BTC-USDT", "ETH-USDT"]}[exchange]
    return ",".join(symbols), subscriptions(exchange, symbols, ["trade"])


def run_case(client, context, certificate, mode):
    errors = []
    ready = threading.Event()
    times = {}
    symbols, requests = feeder_subscription(client)
    # Crypto.com deliberately waits one second before subscribing.
    runtime_sec = 2 if client.name == "crypto_com_spot_feeder" else 1
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]

        def serve():
            try:
                raw, _ = listener.accept()
                raw.settimeout(5)
                with context.wrap_socket(raw, server_side=True) as connection:
                    if mode == "signal_connecting":
                        request = b""
                        while not request.endswith(b"\r\n\r\n"):
                            part = connection.recv(1)
                            assert part, "Closed before upgrade request"
                            request += part
                        ready.set()  # Hold the upgrade pending while Ctrl+C cancels setup.
                    else:
                        if mode == "timeout":
                            # A one-second runtime limit must not expire during connection setup.
                            time.sleep(1.2)
                        headers = {"x-mbx-apikey": "loopback-test-only"} if "sbe" in client.name else {}
                        handshake(connection, port, expected_headers=headers,
                                  target="/public/ws", reject=mode == "rejected")
                        times["connected"] = time.monotonic()
                        if mode not in ("zero", "rejected"):
                            if mode != "signal_subscribing":
                                for request in requests:
                                    opcode, payload = read_frame(connection)
                                    assert opcode == 0x81
                                    actual = json.loads(payload)
                                    if isinstance(actual, dict):
                                        actual.pop("time", None)
                                        actual.pop("nonce", None)
                                    assert actual == request, (actual, request)
                            # Confirm the read loop is active. No market data is needed for timed exit.
                            connection.sendall(b"\x89\x04exit")
                            assert read_frame(connection) == (0x8A, b"exit")
                            ready.set()
                            if mode == "peer_close":
                                connection.sendall(b"\x88\x02\x03\xe8")
                    assert connection.recv(1024) == b"", "Expected transport close with no additional frames"
                    times["closed"] = time.monotonic()
            except BaseException as error:
                errors.append(error)
                ready.set()

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        options = [] if mode == "sigterm_default" else ["--exit-after-sec", {
            "zero": "0", "timeout": str(runtime_sec), "sigint": "30", "signal_connecting": "0",
            "signal_subscribing": "30",
            "peer_close": "4294967295", "rejected": "0",
        }[mode]]
        command = [str(client), "--symbols", symbols, "--channels", "trade", *options,
                   "--endpoint", f"wss://localhost:{port}/public/ws"]
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"),
                   BINANCE_SBE_API_KEY="loopback-test-only")
        process = subprocess.Popen(command, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            if mode in ("sigint", "sigterm_default", "signal_connecting", "signal_subscribing"):
                assert ready.wait(5), "Client did not reach the signal test point"
                assert not errors, errors
                if mode == "sigterm_default":
                    time.sleep(1.2)
                    assert process.poll() is None, "Default runtime should be unlimited"
                process.send_signal(signal.SIGTERM if mode == "sigterm_default" else signal.SIGINT)
            stdout, stderr = process.communicate(timeout=8)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        worker.join(6)
        output = stdout + stderr
        assert not worker.is_alive(), "Server did not finish"
        assert not errors, f"{client.name}/{mode}: {errors}; {output}"
        assert process.returncode == (1 if mode in ("peer_close", "rejected") else 0), output
        assert output.count("Connected:") == (0 if mode in ("signal_connecting", "rejected") else 1), output
        if mode in ("zero", "signal_connecting", "signal_subscribing", "rejected"):
            assert "Subscribing:" not in output, output
        if mode == "timeout":
            assert runtime_sec - 0.15 <= times["closed"] - times["connected"] < runtime_sec + 3, times
            assert f"Stopping after {runtime_sec} seconds connected" in output, output
        elif mode == "zero":
            assert "Stopping after connection" in output, output
        elif mode in ("sigint", "sigterm_default", "signal_connecting", "signal_subscribing"):
            assert "Stopping on signal" in output and "seconds connected" not in output, output
        else:
            assert "Stopping after" not in output, output
        print(f"{client.name}/{mode}: passed", flush=True)


def check_feeder(client, context, certificate):
    for options in (["--exit-after-sec"], *(["--exit-after-sec", value]
                    for value in ("-1", "1.5", "abc", "", "4294967296"))):
        result = subprocess.run([str(client), *options], capture_output=True, text=True, timeout=3)
        assert result.returncode == 1 and result.stderr and "Connecting" not in result.stdout, result
    result = subprocess.run([str(client), "--help"], capture_output=True, text=True, timeout=3)
    assert result.returncode == 0 and "--exit-after-sec" in result.stdout
    for mode in ("zero", "timeout", "sigint", "sigterm_default", "signal_connecting", "peer_close", "rejected"):
        run_case(client, context, certificate, mode)
    if client.name == "crypto_com_spot_feeder":
        run_case(client, context, certificate, "signal_subscribing")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, action="append", required=True)
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="asio-feeder-exit-") as temporary:
        folder = Path(temporary)
        certificate, key = folder / "cert.pem", folder / "key.pem"
        subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", str(key), "-out", str(certificate)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = [pool.submit(check_feeder, client, context, certificate) for client in args.client]
            for result in results:
                result.result()


if __name__ == "__main__":
    main()
