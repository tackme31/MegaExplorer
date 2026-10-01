"""Progress and cancel: the progress dialog, ui.progress and $/cancel.

Shows:
- "progress": true on a command in plugin.json -> the app opens a progress dialog
  once the command has run for 300 ms (so "quick" never shows one)
- ctx.progress(current, total, message): a bar with "12 / 40"; without a total
  the bar just moves. Report as often as you like; the app redraws a few times a second.
- ctx.check_cancelled(): raises once the user pressed Cancel, and the helper tells
  the app the command was cancelled (no toast)
- "stubborn" never checks: after 10 s the dialog offers Force quit, which kills it
"""

import time

from megaexplorer_plugin import Plugin

plugin = Plugin()


@plugin.command("count")
def count(ctx):
    total = 40
    for i in range(total):
        ctx.check_cancelled()
        ctx.progress(i, total, f"step {i + 1}")
        time.sleep(0.25)
    ctx.progress(total, total)
    return f"Counted to {total}"


@plugin.command("spin")
def spin(ctx):
    for i in range(6):
        ctx.check_cancelled()
        ctx.progress(message=f"{6 - i} second(s) left")
        time.sleep(1)
    return "Done without a total"


@plugin.command("quick")
def quick(ctx):
    return "Finished at once"


@plugin.command("stubborn")
def stubborn(ctx):
    for i in range(60):
        ctx.progress(i, 60, "ignoring Cancel on purpose")
        time.sleep(1)
    return "Finished, cancel or not"


plugin.run()
