"""Reports progress with ui.progress so the app shows its progress dialog.

Raw JSON-RPC over stdio, no helper library (same plumbing as item-info).
"""

import json
import sys
import time

sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")


def send(message):
    sys.stdout.write(json.dumps(message, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def reply(msg_id, result):
    send({"jsonrpc": "2.0", "id": msg_id, "result": result})


def progress(**params):
    send({"jsonrpc": "2.0", "method": "ui.progress", "params": params})


def count():
    total = 40
    for i in range(total):
        progress(current=i, total=total, message=f"step {i + 1}")
        time.sleep(0.25)
    progress(current=total, total=total)
    return f"Counted to {total}"


def spin():
    for i in range(6):
        progress(message=f"{6 - i} second(s) left")
        time.sleep(1)
    return "Done without a total"


def quick():
    return "Finished at once"


COMMANDS = {"count": count, "spin": spin, "quick": quick}

for line in sys.stdin:
    request = json.loads(line)
    method = request.get("method")
    if method == "initialize":
        reply(request["id"], {"apiVersion": 1})
    elif method == "command.execute":
        reply(request["id"], {"message": COMMANDS[request["params"]["commandId"]]()})
    elif method == "shutdown":
        reply(request["id"], {})
        break
