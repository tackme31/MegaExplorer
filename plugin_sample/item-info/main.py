"""Reads items back from MegaExplorer with items.get / items.children.

Raw JSON-RPC over stdio, no helper library: the plugin also *sends* requests
here, so each call writes one line and reads until the matching response.
"""

import json
import sys

sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")

_next_id = 0


def send(message):
    sys.stdout.write(json.dumps(message, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def reply(msg_id, result=None, error=None):
    message = {"jsonrpc": "2.0", "id": msg_id}
    if error is not None:
        message["error"] = error
    else:
        message["result"] = result
    send(message)


class RpcError(Exception):
    pass


def call(method, params):
    """Calls the app and waits for its answer. Nothing else arrives meanwhile in v1."""
    global _next_id
    _next_id += 1
    my_id = f"p{_next_id}"
    send({"jsonrpc": "2.0", "id": my_id, "method": method, "params": params})
    for line in sys.stdin:
        message = json.loads(line)
        if message.get("id") == my_id and "method" not in message:
            if "error" in message:
                raise RpcError(message["error"]["message"])
            return message["result"]
    raise RpcError("the app closed the pipe")


def human_size(size):
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024


def show(items):
    item = call("items.get", {"handle": items[0]["handle"]})["item"]
    parts = [item["path"]]
    if item["type"] == "file":
        parts.append(human_size(item["size"]))
    if item["tags"]:
        parts.append("tags: " + ", ".join(item["tags"]))
    if item["favourite"]:
        parts.append("favourite")
    return " | ".join(parts)


def count_folder(items):
    first = items[0]
    folder = first["handle"] if first["type"] == "folder" else first["parent"]
    files = folders = 0
    cursor = None
    while True:
        page = call("items.children", {"handle": folder, "cursor": cursor})
        for child in page["items"]:
            if child["type"] == "folder":
                folders += 1
            else:
                files += 1
        cursor = page["nextCursor"]
        if cursor is None:
            break
    name = call("items.get", {"handle": folder})["item"]["path"]
    return f"{name}: {files} files, {folders} folders"


def execute(params):
    items = params["context"]["items"]
    if not items:
        return {"message": "Nothing is selected"}, None
    try:
        if params["commandId"] == "show":
            return {"message": show(items)}, None
        return {"message": count_folder(items)}, None
    except RpcError as error:
        return None, {"code": 1, "message": str(error)}


for line in sys.stdin:
    request = json.loads(line)
    method = request.get("method")
    if method == "initialize":
        reply(request["id"], {"apiVersion": 1})
    elif method == "command.execute":
        result, error = execute(request["params"])
        reply(request["id"], result, error)
    elif method == "shutdown":
        reply(request["id"], {})
        break
