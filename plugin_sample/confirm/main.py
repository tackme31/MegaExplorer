"""Confirm: ui.confirm.

Shows:
- ctx.confirm(message): the plugin waits until the user answers; True for OK
- title / ok_label / danger=True: a destructive question (red OK, Cancel focused)
- returning None on Cancel: the user stopped it, so no toast
"""

from megaexplorer_plugin import Plugin

plugin = Plugin()


@plugin.command("ask")
def ask(ctx):
    if not ctx.confirm(f"Count the {len(ctx.items)} selected item(s)?"):
        return None
    return f"{len(ctx.items)} item(s)"


@plugin.command("ask-danger")
def ask_danger(ctx):
    names = ", ".join(item.name for item in ctx.items[:3])
    if not ctx.confirm(f"Pretend to delete {names}? Nothing is changed.",
                       title="Delete for real?", ok_label="Delete", danger=True):
        return None
    return "Pretended to delete them (nothing was changed)"


plugin.run()
