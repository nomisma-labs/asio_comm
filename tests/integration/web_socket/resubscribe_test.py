#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Exercise receive-callback subscriptions with a local TLS peer."""

import argparse
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading

from loopback_test import handshake, read_frame


def run_resubscribe_case(client, context, certificate, count, fragmented, options):
    errors = []
    acknowledgement = '{"result":null,"id":1}'
    data = [f'{{"e":"trade","s":"BTCUSDT","t":{index},"p":"100","q":"1","m":false}}'
            for index in range(1, count + 1)]
    after = ['{"result":null,"id":2}',
             '{"e":"trade","s":"ETHUSDT","t":1001,"p":"50","q":"2","m":false}',
             '{"e":"trade","s":"BTCUSDT","t":1002,"p":"101","q":"1","m":false}']

    def text_frame(message):
        payload = message.encode()
        assert len(payload) < 126
        return bytes((0x81, len(payload))) + payload

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
                        handshake(connection, port, target="/ws")
                        assert read_frame(connection) == (
                            0x81, b'{"method":"SUBSCRIBE","params":["btcusdt@trade"],"id":1}')
                        # An acknowledgement must not count towards the N market-data messages.
                        connection.sendall(text_frame(acknowledgement) + b'\x89\x03ack')
                        assert read_frame(connection) == (0x8A, b"ack")
                        for index, message in enumerate(data, 1):
                            if fragmented:
                                payload = message.encode()
                                cut = len(payload) // 2
                                connection.sendall(bytes((0x01, cut)) + payload[:cut] + b'\x89\x03mid')
                                assert read_frame(connection) == (0x8A, b"mid")
                                frame = bytes((0x80, len(payload) - cut)) + payload[cut:]
                            else:
                                frame = text_frame(message)
                            # Leave the next ping buffered while the data callback sends its request.
                            connection.sendall(frame + b'\x89\x04next')
                            if index == count:
                                assert read_frame(connection) == (
                                    0x81, b'{"method":"SUBSCRIBE","params":["ethusdt@trade"],"id":2}')
                            assert read_frame(connection) == (0x8A, b"next")
                        for message in after:
                            connection.sendall(text_frame(message))
                        connection.sendall(b'\x89\x04done')
                        assert read_frame(connection) == (0x8A, b"done")
                        connection.sendall(b'\x88\x02\x03\xe8')
                        assert connection.recv(1024) == b""
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        result = subprocess.run(
            [client, *options, "--endpoint", f"wss://localhost:{port}/ws"],
            env=env, text=True, capture_output=True, timeout=12,
        )
        worker.join(timeout=10)
        output = result.stdout + result.stderr
        assert not worker.is_alive(), "resubscribe: server did not stop"
        assert not errors, f"resubscribe: {errors}; client: {output}"
        assert result.returncode == 1 and "Disconnected:" in output, output
        assert output.count("Connected:") == 1, output
        assert output.count("Resubscribing from ws_data:") == 1, output
        assert output.count("Resubscription send returned true") == 1, output
        assert "Resubscription send returned false" not in output, output
        for message in [acknowledgement, *data, *after]:
            assert message in output, f"missing logged payload: {message}; client: {output}"
        print(f"resubscribe: passed (N={count}, fragmented={fragmented}, same connection, new symbol data)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="asio-comm-resubscribe-") as directory:
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
        run_resubscribe_case(args.client, context, certificate, 3, False, [])
        for count, fragmented in ((1, False), (3, True), (100, False)):
            options = ["--resubscribe-after", str(count), "--resubscribe-symbols", "eThUsDt"]
            run_resubscribe_case(args.client, context, certificate, count, fragmented, options)
    for options in (["--resubscribe-after", "0"], ["--resubscribe-after", "-1"],
                    ["--resubscribe-after", "abc"], ["--resubscribe-after"],
                    ["--resubscribe-symbols", ""], ["--resubscribe-symbols", 'ETH"USDT'],
                    ["--symbols", ""], ["--channels", "unknown"]):
        result = subprocess.run([args.client, *options], text=True, capture_output=True, timeout=3)
        assert result.returncode == 1 and result.stderr and "Connecting" not in result.stdout, result
    result = subprocess.run([args.client, "--help"], text=True, capture_output=True, timeout=3)
    assert result.returncode == 0 and "binance_spot_resubscribe_test" in result.stdout, result


if __name__ == "__main__":
    main()
