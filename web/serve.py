#!/usr/bin/env python3
"""Local web server for the browser port: serves this folder with caching off,
so a rebuilt kkrieger.wasm or edited script is picked up on reload.

usage: serve.py [--port 8737]
"""
import argparse
import functools
import http.server
from pathlib import Path


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8737)
    args = ap.parse_args()
    handler = functools.partial(NoCacheHandler, directory=str(Path(__file__).resolve().parent))
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler).serve_forever()


if __name__ == "__main__":
    main()
