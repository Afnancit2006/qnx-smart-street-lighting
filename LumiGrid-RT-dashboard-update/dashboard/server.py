#!/usr/bin/env python3
"""Dependency-free local HTTP server for the LumiGrid-RT live dashboard."""

from __future__ import annotations

import argparse
import copy
import json
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


class StateStore:
    def __init__(self, state_path: Path) -> None:
        self.state_path = state_path
        self.last_good: dict = {
            "dashboard_connected": False,
            "emergency": False,
            "rt_fifo": False,
            "zones": [],
            "message": "Waiting for the QNX bridge",
        }
        self.last_mtime_ns = 0
        self.last_received_ms = 0

    def snapshot(self) -> dict:
        try:
            stat = self.state_path.stat()
            if stat.st_mtime_ns != self.last_mtime_ns:
                candidate = json.loads(self.state_path.read_text(encoding="utf-8"))
                if isinstance(candidate, dict) and isinstance(
                    candidate.get("zones"), list
                ):
                    self.last_good = candidate
                    self.last_mtime_ns = stat.st_mtime_ns
                    self.last_received_ms = stat.st_mtime_ns // 1_000_000
        except (FileNotFoundError, OSError, UnicodeError, json.JSONDecodeError):
            pass

        result = copy.deepcopy(self.last_good)
        now_ms = time.time_ns() // 1_000_000
        age_ms = (
            now_ms - self.last_received_ms
            if self.last_received_ms > 0
            else 9_999_999
        )
        result["host_age_ms"] = age_ms
        result["dashboard_connected"] = age_ms <= 2500
        return result


def make_handler(static_directory: Path, store: StateStore):
    class DashboardHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(static_directory), **kwargs)

        def do_GET(self) -> None:  # noqa: N802 - inherited API name
            request_path = urlsplit(self.path).path
            if request_path == "/api/state":
                payload = json.dumps(store.snapshot(), separators=(",", ":"))
                encoded = payload.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(encoded)))
                self.end_headers()
                self.wfile.write(encoded)
                return

            if request_path == "/health":
                encoded = b"ok\n"
                self.send_response(200)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.send_header("Content-Length", str(len(encoded)))
                self.end_headers()
                self.wfile.write(encoded)
                return

            super().do_GET()

        def end_headers(self) -> None:
            self.send_header("X-Content-Type-Options", "nosniff")
            super().end_headers()

        def log_message(self, format: str, *args) -> None:
            return

    return DashboardHandler


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="LumiGrid-RT dashboard server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument(
        "--state",
        type=Path,
        default=Path(__file__).with_name("live_state.json"),
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    static_directory = Path(__file__).resolve().parent
    store = StateStore(arguments.state.resolve())
    handler = make_handler(static_directory, store)
    server = ThreadingHTTPServer((arguments.host, arguments.port), handler)
    print(
        f"LumiGrid-RT dashboard: http://{arguments.host}:{arguments.port}/",
        flush=True,
    )
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
