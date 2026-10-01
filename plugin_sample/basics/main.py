"""Basics: the menu, the selection the app hands over, a toast, and an error.

The smallest plugin there is. Shows:
- a command function returning a string -> the app shows it as a toast
- ctx.items: what was selected when the menu was clicked (name, type, tags, ...)
- raising CommandError -> the app shows "<plugin>: <message>" as an error toast
- print() -> the app's log (MegaExplorer.log, prefixed [plugin:<id>])
"""

from megaexplorer_plugin import CommandError, Plugin

plugin = Plugin()


@plugin.command("show-selection")
def show_selection(ctx):
    print(f"{ctx.command_id}: {len(ctx.items)} item(s)")
    if not ctx.items:
        return "Nothing is selected"
    rest = f" and {len(ctx.items) - 1} more" if len(ctx.items) > 1 else ""
    return f"Selected: {ctx.items[0].name}{rest}"


@plugin.command("fail")
def fail(ctx):
    raise CommandError("Failed on purpose")


plugin.run()
