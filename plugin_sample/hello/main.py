"""The smallest plugin: reads the selection and answers with a toast."""

from megaexplorer_plugin import CommandError, Plugin

plugin = Plugin()


@plugin.command("hello")
def hello(ctx):
    print(f"hello from {ctx.command_id}")  # goes to the app's log
    if not ctx.items:
        return "Nothing is selected"
    rest = f" and {len(ctx.items) - 1} more" if len(ctx.items) > 1 else ""
    return f"Selected: {ctx.items[0].name}{rest}"


@plugin.command("fail")
def fail(ctx):
    raise CommandError("Failed on purpose")


plugin.run()
