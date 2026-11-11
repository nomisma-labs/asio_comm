#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

"""HTTPS request sequencing, framing, ownership, TLS identity and deadline checks."""

import argparse
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time


def read_request(connection, method, target, body=b""):
    header = b""
    while not header.endswith(b"\r\n\r\n"):
        chunk = connection.recv(1)
        assert chunk, "EOF before request header"
        header += chunk
        assert len(header) < 16384
    lines = header.decode("ascii").split("\r\n")
    assert lines[0] == f"{method} {target} HTTP/1.1", lines[0]
    fields = dict(line.split(": ", 1) for line in lines[1:] if line)
    assert fields["Host"].startswith("localhost:"), fields
    data = b""
    length = int(fields.get("Content-Length", 0))
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        assert chunk
        data += chunk
    assert data == body, data
    return fields


def respond(connection, status=200, close=False):
    connection.sendall(f"HTTP/1.1 {status} Test\r\nContent-Length: 2\r\nConnection: {'close' if close else 'keep-alive'}\r\n\r\nok".encode())


def run_case(client, context, certificate, mode, example=False):
    failures = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        if mode != "tcp_refused":
            listener.listen(2)
        listener.settimeout(6)
        port = listener.getsockname()[1]

        def serve():
            replaced = None
            try:
                if mode == "tcp_refused":
                    # Keep the port reserved without listening, so connect deterministically fails.
                    return
                if mode == "move_assign_active":
                    raw, _ = listener.accept()
                    raw.settimeout(6)
                    replaced = context.wrap_socket(raw, server_side=True)
                    read_request(replaced, "GET", "/abandoned")
                for attempt in range(2 if mode == "reconnect" else 1):
                    raw, _ = listener.accept()
                    with raw:
                        raw.settimeout(6)
                        if mode == "connect_timeout":
                            time.sleep(1.4)
                            return
                        try:
                            connection = context.wrap_socket(raw, server_side=True)
                        except ssl.SSLError:
                            if mode in ("untrusted", "wrong_host"):
                                return
                            raise
                        with connection:
                            assert mode not in ("untrusted", "wrong_host"), "Invalid certificate accepted"
                            if mode == "write_timeout":
                                time.sleep(1.4)
                                return
                            if mode == "write_reset":
                                # Confirm the request started, then abort while its large body is still being written.
                                assert connection.recv(1) == b"P"
                                connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                                return
                            if mode == "connect_callback_throw":
                                assert connection.recv(1024) == b""
                                return
                            read_request(connection, "GET", "/example%2Fpath?q=x" if example else "/one")
                            if mode in ("close_pending", "destroy_pending"):
                                # Leave async_read pending, then observe the client's silent abort.
                                assert connection.recv(1024) == b""
                                return
                            if mode in ("response_timeout", "move_timeout"):
                                time.sleep(1.4)
                                return
                            if mode in ("move_pending", "move_assign_pending"):
                                # Keep the first read and the second queued request outstanding during the move.
                                time.sleep(0.1)
                            if mode == "body_limit":
                                connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2048\r\n\r\n" + b"x" * 2048)
                            elif mode == "header_limit":
                                connection.sendall(b"HTTP/1.1 200 OK\r\nX-Large: " + b"x" * 2048 + b"\r\nContent-Length: 0\r\n\r\n")
                            elif mode == "truncated":
                                connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\npartial")
                                return
                            elif mode == "upgrade":
                                connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nConnection: upgrade\r\nUpgrade: x\r\n\r\n")
                            elif mode == "queue":
                                # Multiple parser completions in one TLS record, with chunked data and a trailer.
                                connection.sendall(b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: </x>\r\n\r\n"
                                                   b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\no\r\n1\r\nk\r\n0\r\nX-End: yes\r\n\r\n")
                                read_request(connection, "POST", "/two", b"abc")
                                respond(connection)
                                read_request(connection, "HEAD", "/three")
                                connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 999\r\nConnection: close\r\n\r\n")
                            else:
                                close = mode in ("peer_close", "non_2xx", "shutdown_stall", "move_shutdown") or (mode == "reconnect" and attempt == 1)
                                respond(connection, status=429 if mode == "non_2xx" else 200, close=close)
                                if mode in ("reentry", "request_close") or (mode.startswith("move_") and mode != "move_shutdown"):
                                    fields = read_request(connection, "POST" if mode == "request_close" else "GET",
                                                          "/two", b"abc" if mode == "request_close" else b"")
                                    if mode == "request_close":
                                        assert fields.get("Connection") == "close", fields
                                    respond(connection, close=mode != "request_close")
                                if example:
                                    read_request(connection, "GET", "/example%2Fpath?q=x")
                                    respond(connection)
                            if mode == "shutdown_stall":
                                time.sleep(1.4)
                                return
                            if example or mode in ("close", "close_queued", "destroy", "callback_throw", "body_limit", "header_limit", "upgrade") or (mode == "reconnect" and attempt == 0):
                                assert connection.recv(1024) == b"", "Unexpected request after close"
                            else:
                                # Complete the client's TLS close_notify exchange.
                                connection.unwrap().close()
            except (BrokenPipeError, ConnectionResetError) as error:
                # The client may abort immediately after TLS completes, before session tickets are flushed.
                if mode != "connect_callback_throw":
                    failures.append(error)
            except BaseException as error:
                failures.append(error)
            finally:
                if replaced is not None:
                    try:
                        assert replaced.recv(1024) == b"", "Replaced session sent its queued request"
                    except BaseException as error:
                        failures.append(error)
                    replaced.close()

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        if mode == "untrusted":
            env["SSL_CERT_FILE"] = str(certificate.parent / "missing")
        host = "127.0.0.1" if mode == "wrong_host" else "localhost"
        target = "/example%2Fpath?q=x" if example else "/"
        result = subprocess.run([client, f"https://{host}:{port}{target}", "2" if example else mode],
                                env=env, text=True, capture_output=True, timeout=8)
        worker.join(7)
        assert not worker.is_alive(), f"{mode}: server did not exit"
        assert not failures, f"{mode}: server {failures}; client {result.stdout} {result.stderr}"
        if mode in ("callback_throw", "connect_callback_throw"):
            # An escaping callback exception reaches the application's fatal logging boundary.
            assert result.returncode == 1, f"{mode}: {result.stdout} {result.stderr}"
            assert result.stderr.strip() == "client callback", f"{mode}: {result.stderr}"
        else:
            assert result.returncode == 0, f"{mode}: {result.stdout} {result.stderr}"
        if example:
            assert "Response 1: HTTP 200" in result.stdout and "Response 2: HTTP 200" in result.stdout
        print(f"{'example' if example else mode}: passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--example", required=True)
    parser.add_argument("--move-client", required=True)
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="asio-https-") as directory:
        directory = Path(directory)
        certificate, key = directory / "cert.pem", directory / "key.pem"
        subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", str(key), "-out", str(certificate)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        modes = ("reentry", "queue", "request_close", "peer_close", "close", "close_queued", "destroy", "reconnect",
                 "callback_throw", "connect_callback_throw", "close_pending", "destroy_pending", "non_2xx", "body_limit", "header_limit", "truncated",
                 "upgrade", "tcp_refused", "untrusted", "wrong_host", "connect_timeout", "response_timeout", "write_timeout", "write_reset", "shutdown_stall")
        for mode in modes:
            run_case(args.client, context, certificate, mode)
        for mode in ("move_connecting", "move_assign_connecting", "move_pending", "move_assign_pending",
                     "move_response", "move_assign_response", "move_shutdown", "move_timeout", "move_assign_active"):
            run_case(args.move_client, context, certificate, mode)
        run_case(args.example, context, certificate, "example", example=True)


if __name__ == "__main__":
    main()
