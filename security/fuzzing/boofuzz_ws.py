#!/usr/bin/env python3
"""Loopback-only boofuzz probes of RustyRig's native TLS/WebSocket boundary.

Run against a disposable rrserver with no rig/PTT outputs and no real peers.
This deliberately exercises unauthenticated framing only; it does not attempt
login or mesh enrollment and is not a substitute for protocol state fuzzing.
"""
import argparse
import base64
import os
from pathlib import Path
import ssl

from boofuzz import Bytes, Request, Session, SSLSocketConnection, Static, String, Target


class UpgradedWebSocket(SSLSocketConnection):
    """Perform a fresh, valid HTTP Upgrade before each mutated WS frame."""

    def open(self):
        super().open()
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            "GET /ws HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + key + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: rustyrig.v2\r\n\r\n"
        ).encode("ascii")
        try:
            self._sock.sendall(request)
            response = bytearray()
            while b"\r\n\r\n" not in response and len(response) < 8192:
                chunk = self._sock.recv(1024)
                if not chunk:
                    break
                response.extend(chunk)
            if not response.startswith(b"HTTP/1.1 101 ") and not response.startswith(b"HTTP/1.0 101 "):
                raise RuntimeError("WebSocket Upgrade failed: " + repr(bytes(response[:150])))
        except Exception:
            self.close()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("upgrade", "frames"), required=True)
    parser.add_argument("--host", choices=("127.0.0.1", "::1"), default="127.0.0.1",
                        help="loopback only; never fuzz production stations")
    parser.add_argument("--port", type=int, default=4420)
    parser.add_argument("--ca-file", help="CA certificate for verifying the test server")
    parser.add_argument("--server-name", default="localhost", help="TLS certificate hostname")
    parser.add_argument("--insecure-loopback-tls", action="store_true",
                        help="explicitly allow self-signed certificates on loopback ONLY")
    parser.add_argument("--cases", type=int, default=150, help="maximum cases per invocation")
    args = parser.parse_args()
    if not (1 <= args.port <= 65535 and 1 <= args.cases <= 100000):
        parser.error("port must be 1..65535 and cases must be 1..100000")
    if args.insecure_loopback_tls:
        context = ssl._create_unverified_context()
    else:
        context = ssl.create_default_context(cafile=args.ca_file)
        context.check_hostname = True
    connection_type = SSLSocketConnection if args.mode == "upgrade" else UpgradedWebSocket
    target = Target(connection=connection_type(
        args.host, args.port, sslcontext=context, server_hostname=args.server_name,
        send_timeout=2.0, recv_timeout=2.0))
    output = Path("security/fuzzing/results")
    output.mkdir(parents=True, exist_ok=True)
    session = Session(
        target=target, index_end=args.cases,
        db_filename=str(output / ("boofuzz-" + args.mode + ".db")),
        receive_data_after_each_request=False,
        receive_data_after_fuzz=False,
        web_address="127.0.0.1", web_port=26000,
        keep_web_open=False,
    )
    if args.mode == "upgrade":
        # One malformed request per TLS session; never send credentials.
        request = Request("ws-upgrade", children=(
            Static("method", "GET "),
            String("path", "/ws", max_len=256),
            Static("http_version", " HTTP/1.1\r\nHost: localhost\r\n"),
            String("upgrade", "Upgrade: websocket\r\n", max_len=256),
            String("connection", "Connection: Upgrade\r\n", max_len=256),
            String("ws_key", "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n", max_len=256),
            String("version", "Sec-WebSocket-Version: 13\r\n", max_len=128),
            String("protocol", "Sec-WebSocket-Protocol: rustyrig.v2\r\n", max_len=256),
            Static("end", "\r\n"),
        ))
    else:
        # Valid masked client text frame containing "{}" (5 + 2 bytes).
        # Mutations intentionally cover bad mask/length/opcodes and payloads.
        raw_frame = b"\x81\x82\x12\x34\x56\x78" + bytes((ord("{") ^ 0x12, ord("}") ^ 0x34))
        request = Request("ws-frame", children=(
            Bytes("frame", raw_frame, max_len=2048),
        ))
    session.connect(request)
    session.fuzz()


if __name__ == "__main__":
    main()
