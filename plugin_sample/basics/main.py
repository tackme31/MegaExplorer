"""Basics: the menu, the selection the app hands over, a toast, and an error.

The smallest plugin there is. Shows:
- a command function returning a string -> the app shows it as a toast
- ctx.items: what was selected when the menu was clicked (name, type, tags, ...)
- raising CommandError -> the app shows "<plugin>: <message>" as an error toast
- "result": "dialog" in plugin.json -> the same string (or error), every line of
  it, in a dialog the user closes; a toast shows only the first 3 lines
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


@plugin.command("list-selection")
def list_selection(ctx):
    lines = [f"{item.type}: {item.path}" for item in ctx.items]
    return "\n".join([f"{len(ctx.items)} item(s) selected", ""] + lines)


@plugin.command("fail-long")
def fail_long(ctx):
    raise CommandError("Failed on purpose\n\nThe first line is the summary; the rest\n"
                       "is shown only in the dialog.")


plugin.run()
