"""Read items: items.get and items.children.

Shows:
- ctx.get(item): the item as it is now, with path, size, tags, favourite...
  (ctx.items only has what the menu knew when it was clicked)
- ctx.children(folder): every child, fetched page by page as the loop asks
"""

from megaexplorer_plugin import Plugin

plugin = Plugin()


def human_size(size):
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024


@plugin.command("details")
def details(ctx):
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


plugin.run()
