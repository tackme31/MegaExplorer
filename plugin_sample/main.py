"""Sample: one command per plugin feature, in the order of plugin.json.

Each section below is one feature. Its commands are independent, so read only
the section you need; plugin.json says how each command is declared
("progressDialog", "when").

- Basics: the selection (ctx.items), a toast, an error, the result dialog
  (ui.showResult), print() going to the app's log
- Reading items: items.get, items.children, items.descendants
- File contents: items.fetchPreview, items.readRange, items.fetchFile,
  items.streamUrl
- Changing items: items.update (tags, favourites)
- Transfers: transfers.download, items.upload, items.createFolder
- Asking the user: ui.confirm, ui.alert
- Showing an item in the app: ui.reveal
- Searching in the app: ui.search
- Dialogs and cancel: ui.showProgress, ui.updateProgress, ui.hideProgress,
  ui.alert, ui.showResult, ui.toast, $/cancel
"""

import os
import tempfile
import time
import urllib.request
from datetime import datetime

from megaexplorer_plugin import CommandError, NoPreview, Plugin

plugin = Plugin()


def human_size(size):
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024


def folder_of(ctx):
    """The selected folder, or the folder holding the selected file."""
    if not ctx.items:
        raise CommandError("Nothing is selected")
    first = ctx.items[0]
    return first if first.is_folder else first.parent


# --- Basics -------------------------------------------------------------------
# A returned string is shown as a toast (first 3 lines); None shows nothing.
# Raising CommandError shows "<plugin>: <message>" as an error. With
# "result": "dialog" the whole text goes to a dialog instead, errors included.
# print() lands in MegaExplorer.log, prefixed [plugin:<id>].


@plugin.command("show-selection")
def show_selection(ctx):
    print(f"{ctx.command_id}: {len(ctx.items)} item(s) selected")
    if not ctx.items:
        return "Nothing is selected"
    rest = f" and {len(ctx.items) - 1} more" if len(ctx.items) > 1 else ""
    return f"Selected: {ctx.items[0].name}{rest}"


@plugin.command("fail")
def fail(ctx):
    raise CommandError("Failed on purpose")


# A returned message is a toast, which shows only its first lines. A whole report
# goes in ctx.show_result: a dialog the user closes, which stays after the run ends.
@plugin.command("show-long-result")
def show_long_result(ctx):
    lines = [f"{item.type}: {item.path}" for item in ctx.items]
    ctx.show_result("\n".join([f"{len(ctx.items)} item(s) selected", ""] + lines),
                    title="Selection")
    return None


# --- Reading items --------------------------------------------------------------
# ctx.items already carries path, size, tags, favourite... as they were when the
# menu was clicked; ctx.get(x) reads an item as it is now. children and
# descendants fetch page by page as the loop asks, so a large folder costs
# nothing until it is walked. fields=[...] asks for only those attributes (the
# others are None), which keeps the pages of a large tree small. All of these
# read the app's in-memory copy of the account: they do not reach MEGA's servers.


def describe(item):
    lines = [f"{item.path} ({item.type})"]
    if item.is_file:
        lines.append(f"    size: {human_size(item.size)}")
        # Duplicates share size and crc; None when MEGA has no fingerprint for the file.
        lines.append(f"    crc: {item.crc or 'none'}")
    if item.tags:
        lines.append("    tags: " + ", ".join(item.tags))
    if item.favourite:
        lines.append("    favourite")
    if item.description:
        lines.append(f"    description: {item.description}")
    return lines


@plugin.command("inspect")
def inspect(ctx):
    lines = []
    for item in ctx.get_many(ctx.items):
        if lines:
            lines.append("")
        lines.extend(describe(item))
        if not item.is_folder:
            continue
        files = folders = 0
        for child in ctx.children(item, fields=["type"]):
            files += child.is_file
            folders += child.is_folder
        lines.append(f"    directly inside: {files} file(s), {folders} folder(s)")
        count = size = 0
        for below in ctx.descendants(item, type="file", fields=["size"]):
            ctx.check_cancelled()
            count += 1
            size += below.size
            if count % 500 == 0:
                ctx.update_progress(message=f"{item.name}: {count} files so far")
        lines.append(f"    everything below: {count} file(s), {human_size(size)}")
    ctx.show_result("\n".join(lines), title="Inspect")
    return None


# --- File contents ----------------------------------------------------------------
# fetch_preview saves a JPEG of up to 1000 px, if the uploader's client made one
# (most images, videos and PDFs): NoPreview otherwise. read_range returns up to
# 1 MiB of a file as bytes; fetch_file saves a whole file, or a range of it. The
# saved files are the plugin's: delete them once read (close them first on
# Windows). Anything left is removed when the plugin exits.
# These download from MEGA, one at a time; Cancel stops the one running.


@plugin.command("fetch-previews")
def fetch_previews(ctx):
    fetched = []
    missing = 0
    for item in ctx.items:
        ctx.check_cancelled()
        try:
            path = ctx.fetch_preview(item)
        except NoPreview:
            missing += 1
            continue
        size = path.stat().st_size
        print(f"preview of {item.name}: {path} ({size} bytes)")
        fetched.append(size)
        path.unlink()
    return f"Fetched {len(fetched)} preview(s), {human_size(sum(fetched))}; {missing} without one"


SIGNATURES = [
    (b"\xff\xd8\xff", "JPEG image"),
    (b"\x89PNG", "PNG image"),
    (b"GIF8", "GIF image"),
    (b"%PDF", "PDF document"),
    (b"PK\x03\x04", "ZIP archive (or docx/xlsx/...)"),
    (b"RIFF", "RIFF (WebP, WAV, AVI)"),
]
# Files this size or smaller are also fetched whole, to show fetch_file.
FETCH_WHOLE_LIMIT = 20 * 1024 * 1024


def kind_of(head):
    for signature, kind in SIGNATURES:
        if head.startswith(signature):
            return kind
    if head[4:8] == b"ftyp":
        return "MP4/MOV video"
    return "unknown, starts with " + head[:8].hex(" ")


@plugin.command("read-file")
def read_file(ctx):
    lines = []
    total = len(ctx.items)
    for i, item in enumerate(ctx.items):
        ctx.update_progress(message=item.name, value=i / total, detail=f"{i} / {total}")
        head = ctx.read_range(item, 0, 16)
        line = f"{item.name}: {kind_of(head)}"
        if item.size <= FETCH_WHOLE_LIMIT:
            path = ctx.fetch_file(item)
            fetched = path.stat().st_size
            path.unlink()
            line += f"; fetched {human_size(fetched)}, " + ("size matches" if fetched == item.size else "SIZE DIFFERS")
        else:
            line += f"; {human_size(item.size)}, too large to fetch whole here"
        lines.append(line)
    ctx.show_result("\n".join(lines), title="Read a file")
    return None


# items.streamUrl hands out a URL any program can open (a viewer, mpv, ffmpeg),
# with Range support. The URL is readable by anything on this computer, so it is
# never printed or shown here: a result dialog's text goes to the app's log.
@plugin.command("stream-url")
def stream_url(ctx):
    lines = []
    for item, url in zip(ctx.items, ctx.stream_urls(ctx.items)):
        if url is None or item.size == 0:
            lines.append(f"{item.name}: " + ("no URL" if url is None else "empty, nothing to read"))
            continue
        length = min(item.size, 4096)
        offset = item.size - length
        request = urllib.request.Request(url, headers={"Range": f"bytes={offset}-{item.size - 1}"})
        with urllib.request.urlopen(request, timeout=60) as response:
            status, body = response.status, response.read()
        same = body == ctx.read_range(item, offset, length)
        lines.append(f"{item.name}: HTTP {status}, last {len(body)} bytes "
                     + ("match items.readRange" if same else "DIFFER from items.readRange"))
    ctx.show_result("\n".join(lines), title="Read through a local URL")
    return None


# --- Changing items -------------------------------------------------------------
# Only what is passed to ctx.update is changed. Tags match ignoring case, as MEGA
# compares them, and adding a tag the item has (or removing one it lacks) is a
# no-op. After a command that changed something, the app re-reads the folder on
# screen. There is no undo, which is why this command undoes itself when run
# again.

TAG = "sample"


@plugin.command("toggle-tag")
def toggle_tag(ctx):
    added = removed = 0
    for item in ctx.items:
        if any(tag.lower() == TAG for tag in item.tags or []):
            ctx.update(item, tags_remove=[TAG], favourite=False)
            removed += 1
        else:
            ctx.update(item, tags_add=[TAG], favourite=True)
            added += 1
    return f'Tagged "{TAG}" and favourited {added}; untagged and unfavourited {removed}'


# --- Transfers -------------------------------------------------------------------
# ctx.download hands files to the app's own downloads: they appear in its transfer
# list, land in the user's Downloads folder (here in a "MegaExplorer Sample"
# folder below it) and carry on after the plugin exits. ctx.upload and
# ctx.create_folder wait for the result and return the new Item. Both work in the
# selected folder, or the one holding the selected file. A name already taken
# gets " (2)" ("rename"), so neither ever collides.


@plugin.command("download")
def download(ctx):
    result = ctx.download(ctx.items, sub_path="MegaExplorer Sample")
    return f"Queued {result['queued']} download(s), skipped {result['skipped']}"


@plugin.command("upload-text")
def upload_text(ctx):
    folder = folder_of(ctx)
    stamp = datetime.now()
    fd, path = tempfile.mkstemp(suffix=".txt")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(f"Uploaded by the Sample plugin at {stamp:%Y-%m-%d %H:%M:%S}.\n")
        item = ctx.upload(folder, path, name=f"sample-{stamp:%Y%m%d-%H%M%S}.txt")
    finally:
        os.remove(path)
    return f"Uploaded {item.path}"


@plugin.command("create-folder")
def create_folder(ctx):
    folder = folder_of(ctx)
    name = f"Sample folder {datetime.now():%Y-%m-%d %H%M%S}"
    item, _ = ctx.create_folder(folder, name, on_conflict="rename")
    return f"Created {item.path}"


# --- Copying, moving and the Rubbish bin -----------------------------------------
# ctx.copy and ctx.move wait for MEGA and return the item where it now is; a name
# already taken there gets " (2)" ("rename"). Moving into the folder an item is
# already in is a no-op (moved is False).
# ctx.move_to_rubbish needs the "items.rubbish" permission, and the app asks
# nothing first: confirming is the plugin's job.

@plugin.command("copy-here")
def copy_here(ctx):
    copies = [ctx.copy(item, item.parent) for item in ctx.items]
    return "Copied to " + ", ".join(copy.name for copy in copies)


@plugin.command("move-up")
def move_up(ctx):
    for item in ctx.items:
        parent = ctx.get(item.parent)
        if parent.parent is None:
            raise CommandError(f"{item.name} is already at the top")
        ctx.move(item, parent.parent)
    return f"Moved {len(ctx.items)} up one folder"


@plugin.command("move-to-rubbish")
def move_to_rubbish(ctx):
    names = ", ".join(item.name for item in ctx.items[:3])
    if len(ctx.items) > 3:
        names += f" and {len(ctx.items) - 3} more"
    if not ctx.confirm(f"Move {names} to the Rubbish bin?",
                       title="Move to Rubbish bin?", ok_label="Move", danger=True):
        return None
    for item in ctx.items:
        ctx.move_to_rubbish(item)
    return f"Moved {len(ctx.items)} to the Rubbish bin"


# --- Asking the user ------------------------------------------------------------
# ctx.confirm waits until the user answers: True for OK. danger=True makes OK red
# and focuses Cancel; title and ok_label replace the plugin's name and "OK".
# ctx.alert has only OK and waits for it. Returning None ends the run without a toast.


@plugin.command("ask-first")
def ask_first(ctx):
    names = ", ".join(item.name for item in ctx.items[:3])
    if len(ctx.items) > 3:
        names += f" and {len(ctx.items) - 3} more"
    if not ctx.confirm(f"Pretend to delete {names}? Nothing is changed.",
                       title="Delete?", ok_label="Delete", danger=True):
        return None
    return "Pretended to delete them (nothing was changed)"


# --- Showing an item in the app ---------------------------------------------------
# ctx.reveal opens the item's folder in the app's current tab and selects the item.
# It needs no permission.


@plugin.command("reveal-largest")
def reveal_largest(ctx):
    largest = None
    for item in ctx.items:
        for below in ctx.descendants(item, type="file", fields=["name", "size"]):
            if largest is None or below.size > largest.size:
                largest = below
    if largest is None:
        return "No files in there"
    ctx.reveal(largest)
    return f"Largest: {largest.name} ({human_size(largest.size)})"


# --- Searching in the app ---------------------------------------------------------
# ctx.search runs a search in the app's current tab, as if the user had typed the
# query and set the filter popup; it replaces whatever search the tab had. It needs
# no permission. Fixed criteria here: photos tagged "sample" (see toggle-tag).


@plugin.command("search-sample-photos")
def search_sample_photos(ctx):
    ctx.search("tag:sample", type="files", category="photo")
    return None


# --- Dialogs and cancel -----------------------------------------------------------
# Every dialog is the plugin's to open. "progressDialog": true (the default) starts
# the run with a count-less progress dialog already open; from then on
# ctx.update_progress replaces what it shows -- pass everything each time, a field
# left out is cleared -- including when a new stage begins. ctx.hide_progress closes
# it and ctx.show_progress opens it again (it fails if one is open). value is 0..1;
# without it the bar has no count. The counts, rate and times in detail are yours
# to write: ctx.elapsed_text() and ctx.remaining_text() are helpers.
# ctx.check_cancelled() raises once the user pressed Cancel, and the helper reports
# the command as cancelled (no toast). Cancel lives only on the progress dialog.


@plugin.command("long-task")
def long_task(ctx):
    for second in range(3, 0, -1):
        ctx.check_cancelled()
        ctx.update_progress(title="Preparing", message=f"{second} s left (no count yet)")
        time.sleep(1)
    total = 20
    ctx.alert(f"Found {total} steps to run.", title="Ready")
    lines = []
    for i in range(total):
        ctx.check_cancelled()
        ctx.update_progress(title="Running", message=f"step {i + 1}", value=i / total,
                            detail=f"{i} / {total}  {ctx.elapsed_text()} elapsed")
        time.sleep(0.25)
        lines.append(f"step {i + 1}: {'skipped' if i % 5 == 0 else 'ok'}")
    skipped = sum(line.endswith("skipped") for line in lines)
    ctx.update_progress(title="Tidying up")
    time.sleep(1)
    ctx.hide_progress()
    ctx.show_result("\n".join(lines), title=f"Done {total - skipped} of {total} steps",
                    kind="warning" if skipped else "info")
    return None


# "progressDialog": false: nothing is shown unless the plugin asks, and there is no
# Cancel. ctx.toast reports while the run goes on; the returned message is a toast too.
@plugin.command("toast-steps")
def toast_steps(ctx):
    for step in ("first", "second"):
        time.sleep(1)
        ctx.toast(f"Finished the {step} step")
    time.sleep(1)
    return "Finished all three steps"


# Each ctx.show_result is a dialog of its own, shown in turn as the user closes them.
@plugin.command("several-results")
def several_results(ctx):
    files = [item for item in ctx.items if item.is_file]
    folders = [item for item in ctx.items if item.is_folder]
    ctx.show_result("\n".join(item.name for item in files) or "none", title=f"{len(files)} file(s)")
    ctx.show_result("\n".join(item.name for item in folders) or "none", title=f"{len(folders)} folder(s)")
    return None


plugin.run()
