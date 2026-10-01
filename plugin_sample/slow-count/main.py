"""Reports progress and honours Cancel."""

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
