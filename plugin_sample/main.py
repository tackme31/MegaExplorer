"""Sample: one command per plugin feature, in the order of plugin.json.

Each section below is one feature. Its commands are independent, so read only
the section you need; plugin.json says how each command is declared
("progress", "result", "when").

- Basics: the selection (ctx.items), a toast, an error, the result dialog,
  print() going to the app's log
- Reading items: items.get, items.children, items.descendants
- Previews: items.fetchPreview
- Changing items: items.update (tags, favourites)
- Asking the user: ui.confirm
- Progress and cancel: ui.progress, $/cancel, Force quit
"""

import time

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
# A returned string is shown as a toast (first 3 lines). Raising CommandError
# shows "<plugin>: <message>" as an error. With "result": "dialog" the whole
# text goes to a dialog instead. print() lands in MegaExplorer.log, prefixed
# [plugin:<id>].


@plugin.command("show-toast")
def show_toast(ctx):
    print(f"{ctx.command_id}: {len(ctx.items)} item(s)")
    if not ctx.items:
        return "Nothing is selected"
    rest = f" and {len(ctx.items) - 1} more" if len(ctx.items) > 1 else ""
    return f"Selected: {ctx.items[0].name}{rest}"


@plugin.command("show-error")
def show_error(ctx):
    raise CommandError("Failed on purpose")


@plugin.command("show-result-dialog")
def show_result_dialog(ctx):
    lines = [f"{item.type}: {item.path}" for item in ctx.items]
    return "\n".join([f"{len(ctx.items)} item(s) selected", ""] + lines)


@plugin.command("show-error-dialog")
def show_error_dialog(ctx):
    raise CommandError("Failed on purpose\n\nThe first line is the summary; the rest\n"
                       "is shown only in the dialog.")


# --- Reading items --------------------------------------------------------------
# ctx.items already carries path, size, tags, favourite... as they were when the
# menu was clicked. ctx.get(x) reads any item now (here: the parent folder);
# children/descendants fetch page by page as the loop asks.


@plugin.command("read-details")
def read_details(ctx):
    if not ctx.items:
        return "Nothing is selected"
    item = ctx.get(ctx.items[0])
    parts = [item.path]
    if item.is_file:
        parts.append(human_size(item.size))
    if item.tags:
        parts.append("tags: " + ", ".join(item.tags))
    if item.favourite:
        parts.append("favourite")
    return " | ".join(parts)


@plugin.command("count-folder")
def count_folder(ctx):
    folder = folder_of(ctx)
    files = folders = 0
    for child in ctx.children(folder):
        if child.is_folder:
            folders += 1
        else:
            files += 1
    return f"{ctx.get(folder).path}: {files} files, {folders} folders"


@plugin.command("count-tree")
def count_tree(ctx):
    folder = folder_of(ctx)
    files = 0
    size = 0
    for item in ctx.descendants(folder, type="file"):
        files += 1
        size += item.size
    return f"{ctx.get(folder).path}: {files} files below, {human_size(size)}"


# --- Previews -------------------------------------------------------------------
# fetch_preview returns the Path of a JPEG of up to 1000 px. Only uploads from
# clients that made a preview have one (most images, videos and PDFs; not mp3 or
# zip): NoPreview otherwise. The file is the plugin's, so it is deleted once read
# -- closed first, since Windows cannot delete an open file. Anything left is
# removed by the app when the plugin exits.
# plugin.json's "when": {"targets": "files"} greys the row out for folders.


@plugin.command("fetch-previews")
def fetch_previews(ctx):
    fetched = []
    missing = 0
    for item in ctx.items:
        try:
            path = ctx.fetch_preview(item)
        except NoPreview:
            missing += 1
            continue
        with path.open("rb") as f:
            is_jpeg = f.read(2) == bytes([0xFF, 0xD8])
        size = path.stat().st_size
        print(f"preview of {item.name}: {path} ({size} bytes, jpeg={is_jpeg})")
        fetched.append(size)
        path.unlink()
    return f"Fetched {len(fetched)} preview(s), {sum(fetched) // 1024} KB; {missing} without one"


# --- Changing items -------------------------------------------------------------
# Only what is passed to ctx.update is changed, and adding a tag the item already
# has is a no-op. After a command that changed something, the app re-reads the
# folder on screen. tag-extension's "when" limits it to a few image extensions.


@plugin.command("tag-extension")
def tag_extension(ctx):
    for item in ctx.items:
        ctx.update(item, tags_add=[item.name.rsplit(".", 1)[1].lower()])
    return f"Tagged {len(ctx.items)} file(s) with their extension"


@plugin.command("clear-tags")
def clear_tags(ctx):
    tagged = [item for item in ctx.items if item.tags]
    for item in tagged:
        ctx.update(item, tags_remove=item.tags)
    return f"Cleared tags on {len(tagged)} item(s)"


@plugin.command("add-favourite")
def add_favourite(ctx):
    for item in ctx.items:
        ctx.update(item, favourite=True)
    return f"Added {len(ctx.items)} item(s) to favourites"


# --- Asking the user ------------------------------------------------------------
# ctx.confirm waits until the user answers: True for OK. danger=True makes OK red
# and focuses Cancel. Returning None on Cancel ends the run without a toast.


@plugin.command("show-confirm")
def show_confirm(ctx):
    if not ctx.confirm(f"Count the {len(ctx.items)} selected item(s)?"):
        return None
    return f"{len(ctx.items)} item(s)"


@plugin.command("show-confirm-danger")
def show_confirm_danger(ctx):
    names = ", ".join(item.name for item in ctx.items[:3])
    if not ctx.confirm(f"Pretend to delete {names}? Nothing is changed.",
                       title="Delete for real?", ok_label="Delete", danger=True):
        return None
    return "Pretended to delete them (nothing was changed)"


# --- Progress and cancel --------------------------------------------------------
# "progress": true opens a progress dialog once the command has run for 300 ms
# (so finish-quickly never shows one). ctx.progress(current, total, message)
# draws "12 / 40"; without a total the bar just moves. ctx.check_cancelled()
# raises once the user pressed Cancel, and the helper reports the command as
# cancelled (no toast). ignore-cancel never checks: after 10 s the dialog offers
# Force quit, which kills it. With "result": "dialog" as well, the progress dialog's
# row turns into the result when the command ends, instead of a toast.


@plugin.command("show-progress")
def show_progress(ctx):
    total = 40
    for i in range(total):
        ctx.check_cancelled()
        ctx.progress(i, total, f"step {i + 1}")
        time.sleep(0.25)
    ctx.progress(total, total)
    return f"Counted to {total}"


@plugin.command("show-progress-then-result")
def show_progress_then_result(ctx):
    total = 12
    lines = []
    for i in range(total):
        ctx.check_cancelled()
        ctx.progress(i, total, f"step {i + 1}")
        time.sleep(0.25)
        lines.append(f"step {i + 1}: {'ok' if i % 5 else 'skipped'}")
    ctx.progress(total, total)
    skipped = sum(line.endswith("skipped") for line in lines)
    return "\n".join([f"Done {total - skipped} of {total} steps ({skipped} skipped)", ""] + lines)


@plugin.command("show-progress-no-total")
def show_progress_no_total(ctx):
    for i in range(6):
        ctx.check_cancelled()
        ctx.progress(message=f"{6 - i} second(s) left")
        time.sleep(1)
    return "Done without a total"


@plugin.command("finish-quickly")
def finish_quickly(ctx):
    return "Finished at once"


@plugin.command("ignore-cancel")
def ignore_cancel(ctx):
    for i in range(60):
        ctx.progress(i, 60, "ignoring Cancel on purpose")
        time.sleep(1)
    return "Finished, cancel or not"


plugin.run()
