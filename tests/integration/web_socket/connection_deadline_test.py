#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Check one DNS-through-upgrade budget, setup lifetime and cancellation with local peers."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

from loopback_test import handshake, receive_exact


CASES = ("tls_timeout", "upgrade_timeout", "combined_timeout", "success", "reject", "close",
         "destroy", "reconnect", "move", "move_assign", "late_close")
if sys.platform.startswith("linux"):
    CASES += ("dns_timeout", "dns_close", "dns_destroy", "tcp_timeout")


def await_abort(connection):
    # Ignore already-written setup bytes; the client must abort without a peer reply.
    try:
        while connection.recv(4096):
            pass
    except ConnectionResetError:
        pass


def run_case(client, context, certificate, mode):
    failures = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]
        fillers = []
        if mode == "tcp_timeout":
            # Linux admits backlog + 1 completed connections. Fill it without accepting,
            # so the tested connection's SYNs remain pending on loopback.
            fillers = [socket.create_connection(("127.0.0.1", port), timeout=1) for _ in range(2)]

        def serve():
            try:
                for attempt in range(2 if mode == "reconnect" else 1):
                    raw, _ = listener.accept()
                    with raw:
                        raw.settimeout(5)
                        if mode in ("tls_timeout", "close", "destroy", "move", "move_assign"):
                            await_abort(raw)
                            return
                        if mode == "combined_timeout":
                            time.sleep(0.65)
                        with context.wrap_socket(raw, server_side=True) as connection:
                            if mode in ("upgrade_timeout", "combined_timeout") or (mode == "reconnect" and attempt == 0):
                                request = b""
                                while not request.endswith(b"\r\n\r\n"):
                                    request += receive_exact(connection, 1)
                                    assert len(request) < 16384
                                assert request.startswith(b"GET /feed%2Fone?topic=two HTTP/1.1\r\n")
                                await_abort(connection)
                            else:
                                handshake(connection, port, reject=mode == "reject")
                                if mode != "reject":
                                    # The successful connection must survive its former setup deadline.
                                    time.sleep(1.3)
                                    connection.sendall(b"\x81\x06finish")
                                await_abort(connection)
            except BaseException as error:
                failures.append(error)

        worker = None
        if not mode.startswith("dns_") and mode not in ("tcp_timeout", "late_close"):
            worker = threading.Thread(target=serve, daemon=True)
            worker.start()
        host = "slow-dns.test" if mode.startswith("dns_") else "localhost"
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        try:
            result = subprocess.run([client, f"wss://{host}:{port}/feed%2Fone?topic=two", mode],
                                    env=env, capture_output=True, text=True, timeout=7)
            if worker:
                worker.join(6)
                assert not worker.is_alive(), f"{mode}: peer did not exit"
            assert not failures, f"{mode}: {failures}; {result.stdout} {result.stderr}"
            assert result.returncode == 0, f"{mode}: {result.stdout} {result.stderr}"
            if fillers:
                # Prove the tested connection never completed TCP (rather than stalling in TLS).
                expected_ports = {connection.getsockname()[1] for connection in fillers}
                for _ in fillers:
                    accepted, address = listener.accept()
                    accepted.close()
                    assert address[1] in expected_ports
                    expected_ports.remove(address[1])
                listener.settimeout(0.1)
                try:
                    unexpected, _ = listener.accept()
                except TimeoutError:
                    pass
                else:
                    unexpected.close()
                    raise AssertionError("Tested TCP connection unexpectedly completed")
        finally:
            for connection in fillers:
                connection.close()
        print(f"connection deadline {mode}: passed", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--openssl", required=True)
    parser.add_argument("--case", choices=CASES)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="asio-comm-connection-deadline-") as directory:
        root = Path(directory)
        certificate, key = root / "cert.pem", root / "key.pem"
        subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", str(key), "-out", str(certificate)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        with ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(lambda mode: run_case(args.client, context, certificate, mode),
                          (args.case,) if args.case else CASES))


if __name__ == "__main__":
    main()
