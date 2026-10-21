#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
"""
Smoke test for the chiaki-ng MCP server. Start chiaki-ng with --mcp, then run:

    python3 chiaki-mcp-smoke-test.py [--url URL] [--token TOKEN] [--out DIR] [--interact]

It checks the MCP handshake and every read-only tool, saves the screenshots it
takes to --out (default: ./chiaki-mcp-smoke) and prints a pass/fail summary.
Nothing in the app is changed unless --interact is given, which additionally
sends harmless input (mouse move, a Tab key press and back, wait_for).
Only the Python standard library is used.
"""

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request


class Client:
    def __init__(self, url, token):
        self.url = url
        self.token = token
        self.next_id = 0
        self.session_id = None

    def rpc(self, method, params=None, notify=False, timeout=60):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if not notify:
            self.next_id += 1
            message["id"] = self.next_id
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        request = urllib.request.Request(self.url, data=json.dumps(message).encode(), headers=headers)
        with urllib.request.urlopen(request, timeout=timeout) as response:
            self.session_id = response.headers.get("Mcp-Session-Id") or self.session_id
            body = response.read()
            return response.status, (json.loads(body) if body else None)

    def call(self, name, arguments=None, timeout=60):
        _, response = self.rpc("tools/call", {"name": name, "arguments": arguments or {}}, timeout=timeout)
        if "error" in response:
            raise RuntimeError(response["error"].get("message"))
        return response["result"]


def texts(result):
    return [c["text"] for c in result.get("content", []) if c.get("type") == "text"]


def images(result):
    return [c for c in result.get("content", []) if c.get("type") == "image"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", default=os.environ.get("CHIAKI_MCP_URL", "http://127.0.0.1:9339/mcp"))
    parser.add_argument("--token", default=os.environ.get("CHIAKI_MCP_TOKEN"))
    parser.add_argument("--out", default="chiaki-mcp-smoke")
    parser.add_argument("--interact", action="store_true", help="Also send harmless input events")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    client = Client(args.url, args.token)
    results = []

    def step(name, fn):
        start = time.time()
        try:
            detail = fn()
            results.append((name, "PASS", detail or "", time.time() - start))
        except Exception as error:  # noqa: BLE001 - report every failure and keep going
            results.append((name, "FAIL", str(error), time.time() - start))

    def save_images(result, stem):
        paths = []
        for i, image in enumerate(images(result)):
            ext = "png" if image["mimeType"] == "image/png" else "jpg"
            path = os.path.join(args.out, f"{stem}{'' if i == 0 else '_' + str(i)}.{ext}")
            with open(path, "wb") as f:
                f.write(base64.b64decode(image["data"]))
            paths.append(path)
        return paths

    try:
        status, init = client.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                                 "clientInfo": {"name": "chiaki-mcp-smoke-test", "version": "1"}})
    except (urllib.error.URLError, ConnectionError) as error:
        print(f"Cannot reach {args.url}: {error}\nStart chiaki-ng with --mcp (or enable it in Settings > Video).")
        return 2
    client.rpc("notifications/initialized", notify=True)
    server = init["result"]["serverInfo"]
    print(f"Connected to {server.get('name')} {server.get('version', '')} (protocol {init['result']['protocolVersion']})")

    tool_names = []

    def list_tools():
        tools = client.rpc("tools/list")[1]["result"]["tools"]
        tool_names.extend(t["name"] for t in tools)
        missing = [t for t in ("take_screenshot", "get_ui_tree", "mcp_self_check", "controller_input") if t not in tool_names]
        if missing:
            raise RuntimeError(f"missing tools: {missing}")
        return f"{len(tools)} tools"

    def prompts():
        listed = client.rpc("prompts/list")[1]["result"]["prompts"]
        return ", ".join(p["name"] for p in listed) or "none"

    def self_check():
        report = json.loads(texts(client.call("mcp_self_check"))[0])
        bad = [f"{c['check']}={c['status']}: {c['detail']}" for c in report["checks"] if c["status"] != "pass"]
        if report["overall"] == "fail":
            raise RuntimeError("; ".join(bad))
        return report["overall"] + ("" if not bad else " (" + "; ".join(bad) + ")")

    def screenshot():
        result = client.call("take_screenshot", {"save_path": os.path.abspath(os.path.join(args.out, "full_resolution.png"))})
        meta = json.loads(texts(result)[0])
        paths = save_images(result, "screenshot")
        if not paths:
            raise RuntimeError("no image returned")
        if meta["image_width"] < 100 or meta["image_height"] < 100:
            raise RuntimeError(f"suspiciously small image {meta['image_width']}x{meta['image_height']}")
        return (f"{meta['image_width']}x{meta['image_height']} {meta['mime_type']}, {len(meta['layers'])} layer(s), "
                f"location {meta.get('location', {}).get('screen')} -> {paths[0]}")

    def annotated():
        result = client.call("take_screenshot", {"annotate": True, "max_width": 1600})
        meta = json.loads(texts(result)[0])
        save_images(result, "annotated")
        return f"{len(meta.get('marks', []))} marks"

    def region():
        result = client.call("take_screenshot", {"region": {"x": 0, "y": 0, "width": 200, "height": 120}})
        meta = json.loads(texts(result)[0])
        save_images(result, "region")
        return f"zoom {meta['zoom']:.2f}"

    def ui_tree():
        text = texts(client.call("get_ui_tree", {"interactive_only": True}))[0]
        return f"{len(text.splitlines())} lines"

    def screen_text():
        text = texts(client.call("read_screen_text"))[0]
        return text.splitlines()[0]

    def app_state():
        state = json.loads(texts(client.call("get_app_state"))[0])
        return f"view {state.get('view')}, {state.get('consoles')} console(s), location {state.get('location', {}).get('screen')}"

    def hosts():
        listed = json.loads(texts(client.call("list_hosts"))[0])
        return ", ".join(f"{h['index']}:{h.get('name')} ({h.get('state')})" for h in listed) or "none"

    def guide():
        return f"{len(texts(client.call('get_usage_guide'))[0])} characters"

    def interact():
        client.call("mouse_move", {"x": 0.5, "y": 0.5, "coordinate_space": "normalized"})
        client.call("press_key", {"key": "Tab", "activate_window": False})
        client.call("press_key", {"key": "shift+Tab", "activate_window": False})
        waited = json.loads(texts(client.call("wait_for", {"until": "screen_stable", "timeout_ms": 5000, "screenshot": False}))[0])
        return f"input delivered, screen_stable={waited['condition_met']} after {waited['elapsed_ms']} ms"

    def history():
        text = texts(client.call("get_action_history", {"limit": 5}))[0]
        return f"{len(text.splitlines())} recent entries"

    step("tools/list", list_tools)
    step("prompts/list", prompts)
    step("mcp_self_check", self_check)
    step("take_screenshot", screenshot)
    step("take_screenshot annotate", annotated)
    step("take_screenshot region", region)
    step("get_ui_tree", ui_tree)
    step("read_screen_text", screen_text)
    step("get_app_state", app_state)
    step("list_hosts", hosts)
    step("get_usage_guide", guide)
    if args.interact:
        step("input + wait_for", interact)
    step("get_action_history", history)

    width = max(len(r[0]) for r in results)
    for name, status, detail, seconds in results:
        print(f"{status}  {name.ljust(width)}  {seconds * 1000:6.0f} ms  {detail}")
    failures = sum(1 for r in results if r[1] == "FAIL")
    print(f"\n{len(results) - failures}/{len(results)} passed. Images saved in {os.path.abspath(args.out)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
