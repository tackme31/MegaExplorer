"""Reports progress with ui.progress and honours $/cancel.

Raw JSON-RPC over stdio, no helper library (same plumbing as item-info). stdin is
read on a thread of its own so $/cancel arrives while a command is busy.
"""

import json
import queue
import sys
import threading
import time

sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")

CANCELLED = -32800
cancelled = threading.Event()
requests = queue.Queue()


def read_stdin():
    for line in sys.stdin:
        message = json.loads(line)
        if message.get("method") == "$/cancel":
            cancelled.set()
        else:
            requests.put(message)
    requests.put(None)


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


def progress(**params):
    send({"jsonrpc": "2.0", "method": "ui.progress", "params": params})


class Cancelled(Exception):
    pass


def check_cancelled():
    if cancelled.is_set():
        raise Cancelled()


def count():
    total = 40
    for i in range(total):
        check_cancelled()
        progress(current=i, total=total, message=f"step {i + 1}")
        time.sleep(0.25)
    progress(current=total, total=total)
    return f"Counted to {total}"


def spin():
    for i in range(6):
        check_cancelled()
        progress(message=f"{6 - i} second(s) left")
        time.sleep(1)
    return "Done without a total"


def quick():
    return "Finished at once"


def stubborn():
    for i in range(60):
        progress(current=i, total=60, message="ignoring Cancel on purpose")
        time.sleep(1)
    return "Finished, cancel or not"


COMMANDS = {"count": count, "spin": spin, "quick": quick, "stubborn": stubborn}

threading.Thread(target=read_stdin, daemon=True).start()
while (request := requests.get()) is not None:
    method = request.get("method")
    if method == "initialize":
        reply(request["id"], {"apiVersion": 1})
    elif method == "command.execute":
        try:
            reply(request["id"], {"message": COMMANDS[request["params"]["commandId"]]()})
        except Cancelled:
            reply(request["id"], error={"code": CANCELLED, "message": "Cancelled"})
    elif method == "shutdown":
        reply(request["id"], {})
        break
