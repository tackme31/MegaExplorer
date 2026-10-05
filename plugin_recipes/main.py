# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///
"""Recipes: small, useful commands, each one a whole job.

plugin_sample/ shows one API feature per command. Here each command strings
several of them together, written top to bottom in the order the user sees it,
so the flow of a real plugin can be read in one place.

- Export a CSV: walk a folder (progress, Cancel), ask first (ui.confirm),
  write a CSV, upload it into the same folder (items.upload), show it
  selected (ui.reveal)
- Slideshow: a window of the plugin's own ("progress": "never") that shows the
  previews of a folder's images in random order (items.descendants and
  items.fetchPreview on worker threads, ui.reveal from a button)

The block at the top is inline script metadata: uv reads it and installs Pillow
on the first run, so the window can show JPEG previews (Tk alone cannot).
"""

import csv
import os
import queue
import random
import tempfile
import threading
import tkinter as tk
from datetime import datetime
from tkinter import ttk

from PIL import Image, ImageTk

from megaexplorer_plugin import NotFound, Plugin

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


# --- Slideshow --------------------------------------------------------------------
# Right-click a folder and a window shows a random image from anywhere inside it
# every few seconds, with Play/Pause, Next, and Show in MEGA Explorer.
#
# "progress": "never" in plugin.json: no progress dialog covers the app while the
# window is open, and none offers a Cancel, so closing the window is what ends the
# command. The window therefore reports its own progress (the count collected).
#
# Tk runs on the main thread, inside the command function. Calls into the app wait
# for their answer, so two worker threads make them: one walks the folder, the
# other fetches previews one ahead into a queue. Tk is only ever touched on the
# main thread, which polls that queue with after().

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tif", ".tiff",
                    ".heic", ".heif", ".avif"}
INTERVAL_MS = 5000
POLL_MS = 100


class Slideshow:
    def __init__(self, ctx, folder):
        self.ctx = ctx
        self.folder = folder
        self.lock = threading.Condition()
        self.found = []                      # every image collected so far
        self.pool = []                       # those not shown yet in this round
        self.collecting = True
        self.ready = queue.Queue(maxsize=1)  # (Item, PIL image), or None: nothing to show
        self.closed = False

        self.playing = True
        self.current = None
        self.picture = None
        self.photo = None                    # Tk drops an image nothing references
        self.timer = None
        self.waiting = False
        self.shown = 0

        self.build_window()
        threading.Thread(target=self.collect, daemon=True).start()
        threading.Thread(target=self.fetch, daemon=True).start()

    # --- worker threads -----------------------------------------------------------

    def collect(self):
        try:
            for item in self.ctx.descendants(self.folder, type="file", fields=["name", "path"]):
                if self.closed:
                    return
                if os.path.splitext(item.name)[1].lower() in IMAGE_EXTENSIONS:
                    with self.lock:
                        self.found.append(item)
                        self.pool.append(item)
                        self.lock.notify_all()
        finally:
            with self.lock:
                self.collecting = False
                self.lock.notify_all()

    def pick(self):
        """A random image not shown yet in this round; None once there are none at all."""
        with self.lock:
            while not self.pool:
                if not self.collecting:
                    if not self.found:
                        return None
                    self.pool = list(self.found)
                    break
                self.lock.wait()
            i = random.randrange(len(self.pool))
            self.pool[i], self.pool[-1] = self.pool[-1], self.pool[i]
            return self.pool.pop()

    def fetch(self):
        misses = 0
        while not self.closed:
            item = self.pick()
            if item is None:
                self.ready.put(None)
                return
            try:
                path = self.ctx.fetch_preview(item)
            except NotFound:                 # no preview, or deleted since
                misses += 1
                with self.lock:
                    hopeless = not self.collecting and misses >= len(self.found)
                if hopeless:
                    self.ready.put(None)
                    return
                continue
            misses = 0
            with Image.open(path) as image:
                picture = image.convert("RGB")
            path.unlink()                    # after the close: Windows refuses an open file
            self.ready.put((item, picture))

    # --- the window (main thread) -------------------------------------------------

    def build_window(self):
        self.root = tk.Tk()
        self.root.title(f"Slideshow - {self.folder.name}")
        dpi = self.root.winfo_fpixels("1i") / 96   # pixels are physical once DPI-aware
        self.root.geometry(f"{int(960 * dpi)}x{int(720 * dpi)}")
        self.root.minsize(int(480 * dpi), int(360 * dpi))
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self.canvas = tk.Canvas(self.root, background="black", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda _event: self.draw())

        bar = ttk.Frame(self.root, padding=8)
        bar.pack(fill="x")
        self.play_button = ttk.Button(bar, text="Pause", width=8, command=self.toggle)
        self.play_button.pack(side="left")
        ttk.Button(bar, text="Next", command=self.next).pack(side="left", padx=(8, 0))
        ttk.Button(bar, text="Show in MEGA Explorer", command=self.reveal).pack(side="left", padx=(8, 0))
        self.status = ttk.Label(bar, text="Collecting images...")
        self.status.pack(side="left", fill="x", expand=True, padx=(16, 0))

        self.root.bind("<space>", lambda _event: self.toggle())
        self.root.bind("<Right>", lambda _event: self.next())

        # Started by the app in the background, the window would open behind it.
        self.root.lift()
        self.root.attributes("-topmost", True)
        self.root.after(300, lambda: self.root.attributes("-topmost", False))
        self.root.focus_force()

    def run(self):
        self.next()
        self.root.mainloop()

    def close(self):
        self.closed = True
        self.root.destroy()

    def next(self):
        if self.timer is not None:
            self.root.after_cancel(self.timer)
            self.timer = None
        try:
            ready = self.ready.get_nowait()
        except queue.Empty:
            self.waiting = True
            self.update_status()
            self.timer = self.root.after(POLL_MS, self.next)
            return
        self.waiting = False
        if ready is None:
            self.status.config(text="No image with a preview in this folder")
            return
        self.current, self.picture = ready
        self.shown += 1
        self.draw()
        self.update_status()
        if self.playing:
            self.timer = self.root.after(INTERVAL_MS, self.next)

    def toggle(self):
        self.playing = not self.playing
        self.play_button.config(text="Pause" if self.playing else "Play")
        if self.waiting:
            return                           # the poll already running shows the next one
        if self.timer is not None:
            self.root.after_cancel(self.timer)
            self.timer = None
        if self.playing:
            self.timer = self.root.after(INTERVAL_MS, self.next)

    def reveal(self):
        if self.current is None:
            return
        if self.playing:
            self.toggle()                    # ui.reveal leaves the app behind this window
        self.ctx.reveal(self.current)

    def draw(self):
        if self.picture is None:
            return
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        scale = min(width / self.picture.width, height / self.picture.height)
        size = (max(1, int(self.picture.width * scale)), max(1, int(self.picture.height * scale)))
        self.photo = ImageTk.PhotoImage(self.picture.resize(size, Image.LANCZOS))
        self.canvas.delete("all")
        self.canvas.create_image(width // 2, height // 2, image=self.photo)

    def update_status(self):
        with self.lock:
            total, collecting = len(self.found), self.collecting
        count = f"{total:,} image(s)" + (" so far" if collecting else "")
        if self.current is None:
            self.status.config(text=f"Collecting images... {count}")
        else:
            loading = " (loading...)" if self.waiting else ""
            self.status.config(text=f"{self.current.path}{loading}   [{self.shown:,} shown, {count}]")


def make_dpi_aware():
    # Without this Windows scales the window up as a bitmap, and every image blurs.
    try:
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except (AttributeError, OSError):
        pass


@plugin.command("slideshow")
def slideshow(ctx):
    make_dpi_aware()
    Slideshow(ctx, ctx.items[0]).run()
    return None


plugin.run()
