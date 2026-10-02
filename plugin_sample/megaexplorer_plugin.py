"""A small helper for writing MegaExplorer plugins in Python.

Hides the JSON-RPC over stdio described in the plugin protocol: the app knows
nothing about this file, so it is a convenience, not part of the protocol.
Standard library only. Copy this file next to your plugin's main.py.

    from megaexplorer_plugin import Plugin

    plugin = Plugin()

    @plugin.command("count")
    def count(ctx):
        return f"{len(ctx.items)} item(s) selected"

    plugin.run()

A command function receives a Context and returns a message (str) for the
user, or None for none. Raising CommandError(message) reports a failure; any
other exception does too, with its traceback written to the app's log.
The message may run to many lines. Where it is shown is the command's
"result" in plugin.json: a toast (the default, first 3 lines) or, with
"result": "dialog", a dialog that shows all of it.

print() is safe to use: it goes to stderr, which the app writes to its log.
"""

import base64
import json
import os
import sys
import threading
import traceback
from pathlib import Path

__all__ = [
    "Plugin",
    "Context",
    "Item",
    "RpcError",
    "NotFound",
    "NoPreview",
    "InvalidParams",
    "MegaError",
    "Conflict",
    "Cancelled",
    "CommandError",
]

API_VERSION = 1
_CANCELLED = -32800
_METHOD_NOT_FOUND = -32601
_NOT_FOUND = -32002
_CONFLICT = -32004
_INVALID_PARAMS = -32602
_MEGA_ERROR = -32010
_COMMAND_FAILED = -32000


class RpcError(Exception):
    """The app answered a call with an error."""

    def __init__(self, code, message, data=None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.data = data if isinstance(data, dict) else {}


class NotFound(RpcError):
    """The item does not exist (any more)."""


class NoPreview(NotFound):
    """fetch_preview: the item has no server-side preview, or is gone."""


class InvalidParams(RpcError):
    """The call's arguments were rejected."""


class MegaError(RpcError):
    """MEGA refused or failed the change. Part of it may already be applied."""


class Conflict(RpcError):
    """upload / create_folder: the name is taken and on_conflict did not resolve it.
    Nothing was changed. reason is "exists", or "versioningDisabled" when
    on_conflict="version" would have deleted the old file for good."""

    @property
    def reason(self):
        return self.data.get("reason")


class Cancelled(Exception):
    """Raised by Context.check_cancelled() once the user pressed Cancel, and by a
    transfer (fetch_file, read_range, upload) the app stopped for that reason."""


class CommandError(Exception):
    """Raise to fail a command with a message for the user (no traceback logged).
    Like a returned message, it may run to many lines."""


def _command_failed(text):
    # JSON-RPC wants error.message to be one sentence; the whole text goes in data.
    error = {"code": _COMMAND_FAILED, "message": text.split("\n", 1)[0]}
    if "\n" in text:
        error["data"] = {"message": text}
    return error


_ERRORS = {_NOT_FOUND: NotFound, _CONFLICT: Conflict, _INVALID_PARAMS: InvalidParams, _MEGA_ERROR: MegaError}


class Item:
    """An item in the account.

    Every item carries handle, name, type, parent, size, mtime, path, favourite,
    description and tags. Those in ctx.items are as they were when the menu was
    clicked; call get() for the state now.
    """

    def __init__(self, data):
        self.data = data
        self.handle = data["handle"]
        self.name = data.get("name")
        self.type = data.get("type")
        self.parent = data.get("parent")
        self.size = data.get("size")
        self.mtime = data.get("mtime")
        self.path = data.get("path")
        self.favourite = data.get("favourite")
        self.description = data.get("description")
        self.tags = data.get("tags")

    @property
    def is_file(self):
        return self.type == "file"

    @property
    def is_folder(self):
        return self.type == "folder"

    def __repr__(self):
        return f"Item({self.name!r}, {self.type}, {self.handle})"


def _handle(x):
    return x.handle if isinstance(x, Item) else x


class _Connection:
    """The JSON-RPC stream. Requests from the app go to a queue for the main
    thread; answers to our own calls wake the caller; $/cancel sets a flag at once,
    so a command busy computing still sees it."""

    def __init__(self):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
        # The real stdout is kept for the protocol; fd 1 and sys.stdout become
        # stderr so a stray print() cannot corrupt the stream.
        self._out = os.dup(1)
        os.dup2(2, 1)
        sys.stdout = sys.stderr

        self._write_lock = threading.Lock()
        self._lock = threading.Lock()
        self._pending = {}
        self._next_id = 0
        self._requests = []
        self._requests_ready = threading.Condition(self._lock)
        self._closed = False
        self.cancelled = threading.Event()
        # os.read, not sys.stdin.buffer: a daemon thread blocked in a
        # BufferedReader dies with a fatal error at interpreter exit.
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buffer = b""
        while True:
            chunk = os.read(0, 65536)
            if not chunk:
                break
            buffer += chunk
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                if line.strip():
                    self._dispatch(json.loads(line.decode("utf-8")))
        with self._lock:
            self._closed = True
            for slot in self._pending.values():
                slot["event"].set()
            self._requests_ready.notify_all()

    def _dispatch(self, message):
        method = message.get("method")
        if method is None:
            with self._lock:
                slot = self._pending.get(message.get("id"))
            if slot is not None:
                slot["message"] = message
                slot["event"].set()
            return
        if "id" not in message:
            if method == "$/cancel":
                self.cancelled.set()
            return
        with self._lock:
            self._requests.append(message)
            self._requests_ready.notify()

    def next_request(self):
        with self._lock:
            while not self._requests and not self._closed:
                self._requests_ready.wait()
            return self._requests.pop(0) if self._requests else None

    def send(self, message):
        data = (json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8")
        with self._write_lock:
            while data:
                data = data[os.write(self._out, data):]

    def reply(self, msg_id, result=None, error=None):
        message = {"jsonrpc": "2.0", "id": msg_id}
        if error is not None:
            message["error"] = error
        else:
            message["result"] = result
        self.send(message)

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def call(self, method, params):
        event = threading.Event()
        with self._lock:
            if self._closed:
                raise RpcError(0, "the app closed the connection")
            self._next_id += 1
            msg_id = f"p{self._next_id}"
            slot = {"event": event, "message": None}
            self._pending[msg_id] = slot
        self.send({"jsonrpc": "2.0", "id": msg_id, "method": method, "params": params})
        event.wait()
        with self._lock:
            del self._pending[msg_id]
        message = slot["message"]
        if message is None:
            raise RpcError(0, "the app closed the connection")
        if "error" in message:
            error = message["error"]
            if error.get("code") == _CANCELLED:
                raise Cancelled()
            raise _ERRORS.get(error.get("code"), RpcError)(
                error.get("code"), error.get("message", ""), error.get("data")
            )
        return message.get("result")


class Context:
    """What a command function gets: the clicked items and the calls into the app."""

    def __init__(self, connection, params, plugin_info):
        self._connection = connection
        self.command_id = params["commandId"]
        self.invocation_id = params.get("invocationId")
        context = params.get("context") or {}
        self.site = context.get("site")
        self.items = [Item(data) for data in context.get("items", [])]
        self.plugin_dir = Path(plugin_info["dir"]) if plugin_info.get("dir") else None

    @property
    def item(self):
        """The one selected item, or None when zero or several are selected."""
        return self.items[0] if len(self.items) == 1 else None

    # --- cancel and progress --------------------------------------------------

    @property
    def cancelled(self):
        return self._connection.cancelled.is_set()

    def check_cancelled(self):
        """Raises Cancelled once the user pressed Cancel; the helper then answers -32800."""
        if self.cancelled:
            raise Cancelled()

    def progress(self, current=None, total=None, message=None):
        """Updates the progress dialog (needs "progress": true on the command).
        Call it as often as you like: the app only redraws a few times a second."""
        params = {"invocationId": self.invocation_id}
        if current is not None:
            params["current"] = current
        if total is not None:
            params["total"] = total
        if message is not None:
            params["message"] = message
        self._connection.notify("ui.progress", params)

    # --- asking the user ----------------------------------------------------------

    def confirm(self, message, title=None, ok_label=None, danger=False):
        """Asks the user and waits for the answer: True for OK, False for Cancel.
        danger=True marks the OK button as destructive and focuses Cancel.
        The title defaults to the plugin's name, ok_label to "OK"."""
        params = {"message": message, "danger": danger}
        if title is not None:
            params["title"] = title
        if ok_label is not None:
            params["okLabel"] = ok_label
        return bool(self.call("ui.confirm", params)["ok"])

    # --- items ------------------------------------------------------------------

    def call(self, method, params):
        """A raw protocol call, for methods this helper does not wrap yet."""
        params = dict(params)
        params.setdefault("invocationId", self.invocation_id)
        return self._connection.call(method, params)

    def get(self, x):
        """The current state of one item (handle or Item)."""
        return self.get_many([x])[0]

    def get_many(self, xs):
        result = self.call("items.get", {"handles": [_handle(x) for x in xs]})
        return [Item(data) for data in result["items"]]

    def children(self, x, type=None):
        """The items in folder x, fetched page by page as you iterate.
        type: None for both, "file" or "folder"."""
        return self._pages("items.children", x, type)

    def descendants(self, x, type=None):
        """Everything under folder x, at any depth, fetched page by page as you
        iterate. A folder comes before its contents (depth-first). The list is
        fixed when iteration starts; items deleted since are left out."""
        return self._pages("items.descendants", x, type)

    def _pages(self, method, x, type):
        cursor = None
        while True:
            params = {"handle": _handle(x), "cursor": cursor}
            if type is not None:
                params["type"] = type
            page = self.call(method, params)
            for data in page["items"]:
                yield Item(data)
            cursor = page.get("nextCursor")
            if cursor is None:
                return

    def update(self, x, name=None, description=None, favourite=None, tags_add=None, tags_remove=None):
        """Changes item x and returns it as it is afterwards. Only what you pass
        is touched; adding a tag it has, or removing one it lacks, is a no-op.

        The tags end up as "current minus tags_remove, plus tags_add", so a tag
        in both lists is kept. Tags match ignoring case, as MEGA compares them.
        Only the difference is sent, so replacing a set of tags is simply
        tags_remove=<all the old ones>, tags_add=<all the new ones>."""
        params = {"handle": _handle(x)}
        if name is not None:
            params["name"] = name
        if description is not None:
            params["description"] = description
        if favourite is not None:
            params["favourite"] = favourite
        tags = {}
        if tags_add:
            tags["add"] = list(tags_add)
        if tags_remove:
            tags["remove"] = list(tags_remove)
        if tags:
            params["tags"] = tags
        return Item(self.call("items.update", params)["item"])

    def fetch_preview(self, x):
        """Saves item x's preview (a JPEG of up to 1000 px) and returns its Path.

        The file is yours: delete it once read (close the image first on
        Windows). The app removes whatever is left when the plugin exits.
        Raises NoPreview when there is none."""
        try:
            return Path(self.call("items.fetchPreview", {"handle": _handle(x)})["path"])
        except NotFound as error:
            raise NoPreview(error.code, error.message) from None

    # --- file contents ------------------------------------------------------------
    # The app runs fetch_file, read_range and upload one at a time, and stops the
    # running one when the user presses Cancel: the call then raises Cancelled.
    # Nothing is shown while they run; give a long command "progress": true and
    # report progress yourself.

    def fetch_file(self, x, offset=None, length=None):
        """Downloads file x for the plugin to work on and returns its Path.

        With offset and/or length, only that byte range is saved (length is cut
        at the end of the file). Like fetch_preview, the file is yours and the
        app removes whatever is left when the plugin exits."""
        params = {"handle": _handle(x)}
        if offset is not None:
            params["offset"] = offset
        if length is not None:
            params["length"] = length
        return Path(self.call("items.fetchFile", params)["path"])

    def read_range(self, x, offset, length):
        """Up to 1 MiB of file x, as bytes, without a file in between; fewer
        bytes at the end of the file. For more, use fetch_file(x, offset, length)."""
        result = self.call("items.readRange", {"handle": _handle(x), "offset": offset, "length": length})
        return base64.b64decode(result["data"])

    def upload(self, parent, local_path, name=None, on_conflict=None):
        """Uploads the file at local_path into folder parent and returns the new Item.

        name defaults to the local file's name. When a file of that name is already
        there, on_conflict says what happens: "rename" (the default, "name (2).txt"),
        "fail" (raises Conflict) or "version" (the upload becomes the existing
        file's new version; raises Conflict if versioning is off for the account).
        The local file is left alone."""
        params = {"parent": _handle(parent), "localPath": str(Path(local_path).resolve())}
        if name is not None:
            params["name"] = name
        if on_conflict is not None:
            params["onConflict"] = on_conflict
        return Item(self.call("items.upload", params)["item"])

    def create_folder(self, parent, name, on_conflict=None):
        """Creates folder name in folder parent; returns (Item, created).

        When a folder of that name is already there, on_conflict says what happens:
        "existing" (the default: that folder is returned, created is False), "fail"
        (raises Conflict) or "rename" ("name (2)")."""
        params = {"parent": _handle(parent), "name": name}
        if on_conflict is not None:
            params["onConflict"] = on_conflict
        result = self.call("items.createFolder", params)
        return Item(result["item"]), bool(result["created"])

    # --- downloads for the user -----------------------------------------------------

    def download(self, xs, sub_path=None, on_conflict=None):
        """Queues files for the user in the app's own downloads, as the menu's
        Download does, and returns at once with {"queued": n, "skipped": n}.

        They land in the user's Downloads folder, in sub_path below it if given
        (folders are created). An entry of xs may also be an (item, sub_path)
        pair, to place each file on its own. When a file of that name is already
        there, on_conflict decides: "rename" (the default, "name (1).txt"),
        "skip" or "overwrite" (the old file goes to the Recycle Bin). The
        transfers carry on after the plugin exits; it is not told when they end."""
        entries = []
        for x in xs:
            own_path = sub_path
            if isinstance(x, tuple):
                x, own_path = x
            entry = {"handle": _handle(x)}
            if own_path:
                entry["subPath"] = str(own_path)
            entries.append(entry)
        params = {"items": entries}
        if on_conflict is not None:
            params["onConflict"] = on_conflict
        return self.call("transfers.download", params)


class Plugin:
    def __init__(self):
        self._connection = _Connection()
        self._commands = {}
        self.info = {}

    def command(self, command_id):
        """Decorator: registers a function for the command id in plugin.json."""

        def register(function):
            self._commands[command_id] = function
            return function

        return register

    def run(self):
        """Serves the app until it says shutdown or closes the pipe."""
        connection = self._connection
        while (request := connection.next_request()) is not None:
            method = request.get("method")
            params = request.get("params") or {}
            if method == "initialize":
                self.info = params.get("plugin", {})
                connection.reply(request["id"], {"apiVersion": API_VERSION})
            elif method == "command.execute":
                result, error = self._execute(params)
                connection.reply(request["id"], result, error)
            elif method == "shutdown":
                connection.reply(request["id"], {})
                break
            else:
                connection.reply(
                    request["id"],
                    error={"code": _METHOD_NOT_FOUND, "message": f"Method not found: {method}"},
                )
        sys.stderr.flush()

    def _execute(self, params):
        function = self._commands.get(params.get("commandId"))
        if function is None:
            return None, {"code": _METHOD_NOT_FOUND, "message": f"Unknown command: {params.get('commandId')}"}
        self._connection.cancelled.clear()
        try:
            message = function(Context(self._connection, params, self.info))
        except Cancelled:
            return None, {"code": _CANCELLED, "message": "Cancelled"}
        except CommandError as error:
            return None, _command_failed(str(error))
        except Exception as error:  # noqa: BLE001 -- any failure becomes the command's error
            traceback.print_exc()
            return None, _command_failed(str(error) or type(error).__name__)
        return ({"message": message} if message else {}), None
