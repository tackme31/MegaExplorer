"""Changes items with items.update: tags, favourites.

Raw JSON-RPC over stdio, no helper library (same plumbing as item-info).
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


def tag_extension(items):
    tagged = 0
    for item in items:
        if item["type"] != "file" or "." not in item["name"]:
            continue
        extension = item["name"].rsplit(".", 1)[1].lower()
        call("items.update", {"handle": item["handle"], "tags": {"add": [extension]}})
        tagged += 1
    return f"Tagged {tagged} file(s) with their extension"


def clear_tags(items):
    current = call("items.get", {"handles": [i["handle"] for i in items]})["items"]
    cleared = 0
    for item in current:
        if item["tags"]:
            call("items.update", {"handle": item["handle"], "tags": {"remove": item["tags"]}})
            cleared += 1
    return f"Cleared tags on {cleared} item(s)"


def favourite(items):
    for item in items:
        call("items.update", {"handle": item["handle"], "favourite": True})
    return f"Added {len(items)} item(s) to favourites"


COMMANDS = {"tag-extension": tag_extension, "clear-tags": clear_tags, "favourite": favourite}


def execute(params):
    items = params["context"]["items"]
    if not items:
        return {"message": "Nothing is selected"}, None
    try:
        return {"message": COMMANDS[params["commandId"]](items)}, None
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
