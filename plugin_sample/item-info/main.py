"""Reads items: items.get, items.children (paged), items.fetchPreview."""

from megaexplorer_plugin import NoPreview, Plugin

plugin = Plugin()


def human_size(size):
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024


@plugin.command("show")
def show(ctx):
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
    if not ctx.items:
        return "Nothing is selected"
    first = ctx.items[0]
    folder = first if first.is_folder else first.parent
    files = folders = 0
    for child in ctx.children(folder):
        if child.is_folder:
            folders += 1
        else:
            files += 1
    return f"{ctx.get(folder).path}: {files} files, {folders} folders"


@plugin.command("preview")
def fetch_previews(ctx):
    sizes = []
    missing = 0
    for item in ctx.items:
        if not item.is_file:
            continue
        try:
            path = ctx.fetch_preview(item)
        except NoPreview:
            missing += 1
            continue
        with path.open("rb") as f:
            is_jpeg = f.read(2) == bytes([0xFF, 0xD8])
        print(f"preview {item.name} -> {path} ({path.stat().st_size} bytes, jpeg={is_jpeg})")
        sizes.append(path.stat().st_size)
        # Left in place on purpose: the app removes the run's folder when the plugin exits.
    return f"Fetched {len(sizes)} preview(s), {human_size(sum(sizes))}; {missing} without one"


plugin.run()
