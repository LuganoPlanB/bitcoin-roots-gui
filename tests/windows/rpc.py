# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Native Windows transport round trip against a disposable loopback server."""

import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading


class Fixture(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_POST(self):
        authorization = "Basic " + base64.b64encode(b"fixture:only-for-tests").decode()
        if self.headers.get("Authorization") != authorization:
            self.send_error(401)
            return
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path != "/" or body["method"] != "getnetworkinfo":
            self.send_error(400)
            return
        reply = (
            '{"result":{"amount":0.00000001},"error":null,"id":'
            + json.dumps(body["id"])
            + "}"
        ).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)


def main():
    with tempfile.TemporaryDirectory(prefix="roots-rpc-") as scratch:
        cookie = Path(scratch) / "\u0394-cookie"
        cookie.write_bytes(b"fixture:only-for-tests\r\n")
        with ThreadingHTTPServer(("127.0.0.1", 0), Fixture) as server:
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            try:
                result = subprocess.run(
                    [sys.argv[1], "127.0.0.1", str(server.server_port), str(cookie),
                     "plain", "getnetworkinfo", "node", "2000", "read", "direct", "0"],
                    capture_output=True, text=True, check=True, timeout=10,
                )
                reply = json.loads(result.stdout)
                assert reply["error"] == "success" and reply["sats"] == 1, reply
            finally:
                server.shutdown()
                worker.join()
    print("PASS authenticated loopback RPC with Unicode cookie path and exact amount")


if __name__ == "__main__":
    main()
