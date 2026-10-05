"""Recipes: small, useful commands, each one a whole job.

plugin_sample/ shows one API feature per command. Here each command strings
several of them together, written top to bottom in the order the user sees it,
so the flow of a real plugin can be read in one place.

- Export a CSV: walk a folder (progress, Cancel), ask first (ui.confirm),
  write a CSV, upload it into the same folder (items.upload), show it
  selected (ui.reveal)
"""

import csv
import os
import tempfile
from datetime import datetime

from megaexplorer_plugin import Plugin

plugin = Plugin()


def human_size(size):
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024


# --- Export a CSV -----------------------------------------------------------------
# Right-click a folder (or a view's empty space) and every file and folder inside
# it, at any depth, becomes one row of a CSV that is uploaded next to them.
#
#   1. Walk: a folder's size is 0 and no call returns how many items it holds, so
#      the walk is also the count. The progress dialog is up from the start
#      ("progress" in plugin.json), and Cancel is checked once per item.
#   2. Ask: ui.confirm stacks above the progress dialog. Cancel there ends the run
#      quietly: returning None shows no toast.
#   3. Write the rows already collected, so the tree is walked only once.
#   4. Upload into the folder that was walked; a name already taken there gets
#      " (2)". The progress bar has nothing to count during a transfer, so it is
#      left indeterminate (no total).
#   5. Show the uploaded file selected in its folder. upload() returns only once
#      the file is in the account, so reveal() can find it.

COLUMNS = ["path", "name", "type", "size", "modified", "favourite", "tags",
           "description", "crc", "handle", "parent"]


def csv_row(item):
    modified = (datetime.fromtimestamp(item.mtime).isoformat(sep=" ", timespec="seconds")
                if item.mtime else "")
    return [
        item.path,
        item.name,
        item.type,
        item.size if item.is_file else "",
        modified,
        "yes" if item.favourite else "",
        ";".join(item.tags or []),
        item.description or "",
        item.crc or "",
        item.handle,
        item.parent or "",
    ]


@plugin.command("export-csv")
def export_csv(ctx):
    folders = [item for item in ctx.items if item.is_folder]

    # 1. Walk
    walked = []                      # (folder, [Item, ...]) per selected folder
    found = 0
    for folder in folders:
        inside = []
        for item in ctx.descendants(folder):
            ctx.check_cancelled()
            inside.append(item)
            found += 1
            ctx.progress(current=found, message=f"Looking through {folder.name}…")
        walked.append((folder, inside))

    walked = [(folder, inside) for folder, inside in walked if inside]
    if not walked:
        return "Nothing to export: the folder is empty"

    # 2. Ask
    lines = []
    for folder, inside in walked:
        files = [item for item in inside if item.is_file]
        lines.append(f"{folder.path}: {len(files):,} file(s), "
                     f"{len(inside) - len(files):,} folder(s), "
                     f"{human_size(sum(item.size for item in files))}")
    where = "this folder" if len(walked) == 1 else "each folder"
    lines += ["", f"Write them to a CSV file and upload it into {where}?"]
    if not ctx.confirm("\n".join(lines), title="Export a CSV", ok_label="Export"):
        return None

    total = sum(len(inside) for _, inside in walked)
    written = 0
    uploaded = []
    name = f"items-{datetime.now():%Y%m%d-%H%M%S}.csv"
    with tempfile.TemporaryDirectory() as temp:
        for folder, inside in walked:
            # 3. Write. utf-8-sig: Excel reads a UTF-8 CSV without a BOM as ANSI.
            path = os.path.join(temp, f"{folder.handle}.csv")
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(COLUMNS)
                for item in inside:
                    ctx.check_cancelled()
                    writer.writerow(csv_row(item))
                    written += 1
                    ctx.progress(current=written, total=total,
                                 message=f"Writing the CSV for {folder.name}…")

            # 4. Upload
            ctx.progress(message=f"Uploading {name} into {folder.name}…")
            csv_file = ctx.upload(folder, path, name=name)
            uploaded.append(f"{csv_file.path} ({len(inside):,} rows)")

    # 5. Show it; with several folders, the last one uploaded.
    ctx.reveal(csv_file)
    return "Uploaded " + "\n".join(uploaded)


plugin.run()
