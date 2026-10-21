#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
"""
stdio <-> HTTP bridge for the chiaki-ng MCP server.

chiaki-ng serves MCP over Streamable HTTP (http://127.0.0.1:9339/mcp). Some MCP
clients only launch stdio servers; point them at this script instead:

    python3 chiaki-mcp-stdio.py [--url URL] [--token TOKEN] [--launch PATH_TO_CHIAKI]

With --launch the bridge starts chiaki-ng (with --mcp) when it is not running yet.
Only the Python standard library is used.
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = "http://127.0.0.1:9339/mcp"


def log(message):
    print(f"[chiaki-mcp-stdio] {message}", file=sys.stderr, flush=True)


class Bridge:
    def __init__(self, url, token, launch):
        self.url = url
        self.token = token
        self.launch = launch
        self.session_id = None
        self.launched = False

    def post(self, payload, timeout=120):
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        request = urllib.request.Request(self.url, data=payload, headers=headers, method="POST")
        with urllib.request.urlopen(request, timeout=timeout) as response:
            session_id = response.headers.get("Mcp-Session-Id")
            if session_id:
                self.session_id = session_id
            return response.read()

    def ensure_running(self):
        if not self.launch or self.launched:
            return False
        self.launched = True
        port = self.url.rsplit(":", 1)[-1].split("/")[0]
        log(f"starting {self.launch}")
        kwargs = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}
        if os.name == "nt":
            kwargs["creationflags"] = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
        else:
            kwargs["start_new_session"] = True
        subprocess.Popen([self.launch, "--mcp", "--mcp-port", port], **kwargs)
        deadline = time.time() + 30
        while time.time() < deadline:
            time.sleep(0.5)
            try:
                self.post(json.dumps({"jsonrpc": "2.0", "id": "probe", "method": "ping"}).encode(), timeout=2)
                return True
            except (urllib.error.URLError, ConnectionError, TimeoutError):
                continue
        return False

    def forward(self, line):
        try:
            message = json.loads(line)
        except json.JSONDecodeError as error:
            return {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": f"Parse error: {error}"}}

        payload = json.dumps(message).encode()
        for attempt in range(2):
            try:
                body = self.post(payload)
                return json.loads(body) if body.strip() else None
            except urllib.error.HTTPError as error:
                detail = error.read().decode(errors="replace")
                try:
                    return json.loads(detail)
                except json.JSONDecodeError:
                    return self.error_for(message, f"chiaki-ng returned HTTP {error.code}: {detail}")
            except (urllib.error.URLError, ConnectionError, TimeoutError) as error:
                if attempt == 0 and self.ensure_running():
                    continue
                return self.error_for(
                    message,
                    f"Could not reach chiaki-ng at {self.url} ({error}). Start chiaki-ng with --mcp or enable "
                    "Settings > Video > AI Control (MCP Server).",
                )
        return None

    @staticmethod
    def error_for(message, text):
        requests = message if isinstance(message, list) else [message]
        errors = [
            {"jsonrpc": "2.0", "id": m.get("id"), "error": {"code": -32000, "message": text}}
            for m in requests
            if isinstance(m, dict) and "id" in m
        ]
        if not errors:
            log(text)
            return None
        return errors if isinstance(message, list) else errors[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", default=os.environ.get("CHIAKI_MCP_URL", DEFAULT_URL))
    parser.add_argument("--token", default=os.environ.get("CHIAKI_MCP_TOKEN"))
    parser.add_argument("--launch", default=os.environ.get("CHIAKI_MCP_LAUNCH"),
                        help="Path to the chiaki executable to start when it is not running")
    args = parser.parse_args()

    bridge = Bridge(args.url, args.token, args.launch)
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer
    for raw in stdin:
        line = raw.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        response = bridge.forward(line)
        if response is None:
            continue
        stdout.write(json.dumps(response, separators=(",", ":")).encode() + b"\n")
        stdout.flush()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
