# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Exercise the standalone HTTPS catalog apps against local verified TLS peers."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import json
import os
from pathlib import Path
import signal
import socket
import ssl
import subprocess
import tempfile
import threading
from urllib.parse import urlsplit

# Public API response shapes and paths; the production applications do not use a JSON parser.
fixtures = {'binance_perp_symbol_provider': {'endpoint': 'https://fapi.binance.com/fapi/v1/exchangeInfo',
                                  'success': {'timezone': 'UTC',
                                              'symbols': [{'symbol': 'BTCUSDT',
                                                           'contractType': 'PERPETUAL',
                                                           'baseAsset': 'BTC',
                                                           'quoteAsset': 'USDT'}]},
                                  'error': {'code': -1121, 'msg': 'Invalid symbol', 'symbols': []}},
 'upbit_spot_symbol_provider': {'endpoint': 'https://sg-api.upbit.com/v1/market/all?is_details=true',
                                'success': [{'market': 'SGD-BTC',
                                             'english_name': 'Bitcoin',
                                             'market_warning': 'NONE'}],
                                'error': {'error': {'message': 'Unavailable'}}},
 'okx_spot_symbol_provider': {'endpoint': 'https://www.okx.com/api/v5/public/instruments?instType=SPOT',
                              'success': {'code': '0',
                                          'data': [{'instId': 'BTC-USDT',
                                                    'instType': 'SPOT',
                                                    'baseCcy': 'BTC',
                                                    'quoteCcy': 'USDT'}]},
                              'error': {'code': '123456', 'msg': 'Unavailable', 'data': []}},
 'bybit_spot_symbol_provider': {'endpoint': 'https://api.bybit.com/v5/market/instruments-info?category=spot',
                                'success': {'retCode': 0,
                                            'result': {'category': 'spot',
                                                       'list': [{'symbol': 'BTCUSDT',
                                                                 'baseCoin': 'BTC',
                                                                 'quoteCoin': 'USDT',
                                                                 'status': 'Trading'}]}},
                                'error': {'retCode': 10006, 'retMsg': 'Too many visits', 'result': {'list': []}}},
 'bitget_spot_symbol_provider': {'endpoint': 'https://api.bitget.com/api/v3/market/instruments?category=SPOT',
                                 'success': {'code': '00000',
                                             'data': [{'symbol': 'BTCUSDT',
                                                       'category': 'SPOT',
                                                       'baseCoin': 'BTC',
                                                       'quoteCoin': 'USDT'}]},
                                 'error': {'code': '123456', 'msg': 'Unavailable', 'data': []}},
 'gate_spot_symbol_provider': {'endpoint': 'https://api.gateio.ws/api/v4/spot/currency_pairs',
                               'success': [{'id': 'BTC_USDT',
                                            'base': 'BTC',
                                            'quote': 'USDT',
                                            'trade_status': 'tradable'}],
                               'error': {'error': {'message': 'Unavailable'}}},
 'kucoin_spot_symbol_provider': {'endpoint': 'https://api.kucoin.com/api/v2/symbols',
                                 'success': {'code': '200000',
                                             'data': [{'symbol': 'BTC-USDT',
                                                       'baseCurrency': 'BTC',
                                                       'quoteCurrency': 'USDT',
                                                       'enableTrading': True}]},
                                 'error': {'code': '123456', 'msg': 'Unavailable', 'data': []}},
 'mexc_spot_symbol_provider': {'endpoint': 'https://api.mexc.com/api/v3/exchangeInfo',
                               'success': {'timezone': 'UTC',
                                           'symbols': [{'symbol': 'BTCUSDT',
                                                        'baseAsset': 'BTC',
                                                        'quoteAsset': 'USDT',
                                                        'status': '1'}]},
                               'error': {'code': -1121, 'msg': 'Invalid symbol', 'symbols': []}},
 'htx_spot_symbol_provider': {'endpoint': 'https://api.huobi.pro/v2/settings/common/symbols',
                              'success': {'status': 'ok',
                                          'data': [{'sc': 'btcusdt',
                                                    'bc': 'btc',
                                                    'qc': 'usdt',
                                                    'state': 'online'}],
                                          'full': 1},
                              'error': {'status': 'error', 'data': [], 'full': 1}},
 'crypto_com_spot_symbol_provider': {'endpoint': 'https://api.crypto.com/exchange/v1/public/get-instruments',
                                     'success': {'id': -1,
                                                 'method': 'public/get-instruments',
                                                 'code': 0,
                                                 'result': {'data': [{'symbol': 'BTC_USDT',
                                                                      'inst_type': 'CCY_PAIR',
                                                                      'base_ccy': 'BTC',
                                                                      'quote_ccy': 'USDT'}]}},
                                     'error': {'code': 10001, 'result': {'data': []}}},
 'lbank_spot_symbol_provider': {'endpoint': 'https://api.lbank.info/v2/accuracy.do',
                                'success': {'result': 'true',
                                            'data': [{'symbol': 'btc_usdt',
                                                      'priceAccuracy': '2',
                                                      'quantityAccuracy': '5'}],
                                            'error_code': 0},
                                'error': {'result': 'false', 'data': [], 'error_code': 10000}},
 'xt_spot_symbol_provider': {'endpoint': 'https://sapi.xt.com/v4/public/symbol',
                             'success': {'rc': 0,
                                         'mc': 'SUCCESS',
                                         'result': {'version': 'test',
                                                    'symbols': [{'symbol': 'btc_usdt',
                                                                 'state': 'ONLINE',
                                                                 'baseCurrency': 'btc',
                                                                 'quoteCurrency': 'usdt'}]}},
                             'error': {'rc': 1, 'mc': 'FAILURE', 'result': {'symbols': []}}},
 'bitflyer_spot_symbol_provider': {'endpoint': 'https://api.bitflyer.com/v1/markets',
                                   'success': [{'product_code': 'BTC_JPY', 'market_type': 'Spot'}],
                                   'error': {'error': {'message': 'Unavailable'}}}}

def run_case(client, fixture, mode, context, certificate):
    payload = json.dumps(fixture["error" if mode == "api_error" else "success"], indent=1).encode()
    if mode == "large":
        payload = payload[:1] + b" " * (1024 * 1024 + 128) + payload[1:]
    if mode == "unexpected":
        payload = b"<html>upstream unavailable</html>"
    if mode == "compressed":
        payload = gzip.compress(payload)
    if mode == "partial":
        partial = dict(fixture["success"], full=0)
        payload = json.dumps(partial).encode()
    expected_success = mode in ("success", "large")
    failures = []
    request_seen = threading.Event()
    address = urlsplit(fixture["endpoint"])
    target = address.path + ("?" + address.query if address.query else "")
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
                    request = b""
                    while b"\r\n\r\n" not in request:
                        fragment = connection.recv(4096)
                        assert fragment, "Client closed before request"
                        request += fragment
                    header, extra = request.split(b"\r\n\r\n", 1)
                    lines = header.decode().split("\r\n")
                    assert lines[0] == f"GET {target} HTTP/1.1", lines[0]
                    fields = dict(line.lower().split(": ", 1) for line in lines[1:])
                    assert fields["host"] == f"localhost:{port}"
                    assert fields["accept"] == "application/json"
                    assert fields["accept-encoding"] == "identity"
                    assert "authorization" not in fields
                    assert not extra, extra
                    request_seen.set()
                    if mode == "disconnect":
                        return
                    if mode != "signal":
                        status = "429 Too Many Requests" if mode == "http_error" else "200 OK"
                        encoding = "Content-Encoding: gzip\r\n" if mode == "compressed" else ""
                        # Both content-length and split chunked responses must remain one complete catalog.
                        if mode == "success":
                            wire = f"HTTP/1.1 {status}\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n".encode()
                            for chunk in (payload[:9], payload[9:]):
                                wire += f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n"
                            wire += b"0\r\n\r\n"
                        else:
                            wire = f"HTTP/1.1 {status}\r\nContent-Type: application/json\r\n{encoding}Content-Length: {len(payload)}\r\n\r\n".encode() + payload
                        try:
                            connection.sendall(wire[:17])
                            connection.sendall(wire[17:])
                        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                            if mode != "limit":
                                raise
                    try:
                        assert not connection.recv(4096), "Expected close, not another request"
                    except (ConnectionResetError, ssl.SSLError):
                        pass
            except BaseException as error:
                failures.append(error)
                request_seen.set()

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        env = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(certificate.parent / "empty"))
        command = [str(client), "--endpoint", f"https://localhost:{port}{target}"]
        if mode == "limit":
            command += ["--max-response-size", "32"]
        process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            if mode == "signal":
                assert request_seen.wait(5), "No request before cancellation"
                process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=8)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        worker.join(6)
        assert not worker.is_alive(), "Server did not finish"
        assert not failures, f"{failures}; {stderr.decode(errors='replace')}"
        assert process.returncode == (0 if expected_success else 1), stderr.decode(errors='replace')
        if expected_success:
            assert stdout == payload + b"\n", "Catalog was changed or mixed with diagnostics"
            assert b"Received " in stderr
        else:
            assert not stdout, "Failure published catalog data"
            if mode != "signal":
                assert stderr, "Missing failure diagnostic"
            if mode == "http_error":
                assert b"HTTP 429" in stderr
        return f"{client.name}: {mode} passed"


def check_client(client, context, certificate):
    fixture = fixtures[client.name]
    help_result = subprocess.run([client, "--help"], capture_output=True, text=True, timeout=5)
    assert help_result.returncode == 0 and fixture["endpoint"] in help_result.stdout
    for arguments in (("--max-response-size", "0"), ("--max-response-size", "4294967296"),
                      ("--endpoint", "wss://localhost/"), ("--endpoint",), ("--unknown", "x")):
        result = subprocess.run([client, *arguments], capture_output=True, timeout=5)
        assert result.returncode == 1 and not result.stdout and result.stderr, arguments
    modes = ["success", "large", "api_error", "http_error", "unexpected", "compressed", "limit", "disconnect", "signal"]
    if client.name == "htx_spot_symbol_provider":
        modes.append("partial")
    return [run_case(client, fixture, mode, context, certificate) for mode in modes]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", action="append", required=True, type=Path)
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="asio-https-symbols-") as temporary:
        folder = Path(temporary)
        certificate, key = folder / "cert.pem", folder / "key.pem"
        subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", str(key), "-out", str(certificate)], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        with ThreadPoolExecutor(max_workers=4) as workers:
            for results in workers.map(lambda client: check_client(client, context, certificate), args.client):
                print("\n".join(results))


if __name__ == "__main__":
    main()
