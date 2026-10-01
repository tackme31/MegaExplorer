"""Minimal MegaExplorer plugin: raw JSON-RPC over stdio, no helper library."""

import json
import sys

# Windows pipes default to the ANSI code page; the protocol is UTF-8.
sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")


def reply(msg_id, result=None, error=None):
    message = {"jsonrpc": "2.0", "id": msg_id}
    if error is not None:
        message["error"] = error
    else:
        message["result"] = result
    sys.stdout.write(json.dumps(message, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def execute(params):
    items = params["context"]["items"]
    if params["commandId"] == "fail":
        return None, {"code": 1, "message": "Failed on purpose"}
    if not items:
        return {"message": "Nothing is selected"}, None
    first = items[0]["name"]
    rest = f" and {len(items) - 1} more" if len(items) > 1 else ""
    return {"message": f"Selected: {first}{rest}"}, None


for line in sys.stdin:
    request = json.loads(line)
    method = request.get("method")
    print(f"received {method}", file=sys.stderr, flush=True)
    if method == "initialize":
        reply(request["id"], {"apiVersion": 1})
    elif method == "command.execute":
        result, error = execute(request["params"])
        reply(request["id"], result, error)
    elif method == "shutdown":
        reply(request["id"], {})
        break
