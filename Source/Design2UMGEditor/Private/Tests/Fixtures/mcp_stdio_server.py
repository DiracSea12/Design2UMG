"""Test-side MCP stdio process; no dependency on an installed Pencil editor."""
import json
import os
import sys
import time

mode = sys.argv[1] if len(sys.argv) > 1 else "normal"
initialized = False

def send(message, fragmented=False):
    data = (json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    if fragmented:
        split = data.index("中".encode("utf-8")) + 1
        os.write(1, data[:split])
        time.sleep(0.03)
        os.write(1, data[split:])
    else:
        os.write(1, data)

for line in sys.stdin.buffer:
    request = json.loads(line.decode("utf-8"))
    method = request["method"]
    if method == "notifications/initialized":
        initialized = True
        continue
    request_id = request["id"]
    if method == "initialize":
        if mode == "exit":
            sys.exit(3)
        if mode == "malformed":
            os.write(1, b"not json\n")
            continue
        result = {"protocolVersion": "2024-11-05", "capabilities": {}, "serverInfo": {"name": "fixture", "version": "1"}}
        if mode == "bad_handshake":
            result = {}
        send({"jsonrpc": "2.0", "id": request_id, "result": result})
        continue
    assert initialized, "tools/call arrived before notifications/initialized"
    name = request["params"]["name"]
    if name == "exit":
        sys.exit(4)
    if name == "slow":
        time.sleep(0.08)
    if name == "rpc_error":
        send({"jsonrpc": "2.0", "id": request_id, "error": {"code": -32000, "message": "fixture failure"}})
        continue
    if name == "missing_content":
        result = {}
    else:
        result = {"content": [{"type": "text", "text": "中文结果" if name == "fragmented" else name}]}
    if name == "tool_error":
        result["isError"] = True
    if name == "fragmented":
        assert request["params"]["arguments"]["label"] == "中文请求"
        # A notification and a wrong response id must not consume the real request.
        send({"jsonrpc": "2.0", "method": "notifications/message", "params": {}})
        send({"jsonrpc": "2.0", "id": request_id + 900, "result": {"content": [{"type": "text", "text": "wrong"}]}})
        os.write(2, b"stderr is not the JSON-RPC channel\n")
    send({"jsonrpc": "2.0", "id": request_id, "result": result}, name == "fragmented")
