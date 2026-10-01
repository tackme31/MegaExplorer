"""Update items: items.update -- tags and favourites.

Shows:
- ctx.update(item, tags_add=[...]) / tags_remove / favourite / name / description
- only what is passed is changed, and adding a tag the item already has is a no-op
- after a command that changed something, the app re-reads the folder on screen
"""

from megaexplorer_plugin import Plugin

plugin = Plugin()


@plugin.command("tag-extension")
def tag_extension(ctx):
    files = [item for item in ctx.items if item.is_file and "." in item.name]
    for item in files:
        ctx.update(item, tags_add=[item.name.rsplit(".", 1)[1].lower()])
    return f"Tagged {len(files)} file(s) with their extension"


@plugin.command("clear-tags")
def clear_tags(ctx):
    tagged = [item for item in ctx.items if item.tags]
    for item in tagged:
        ctx.update(item, tags_remove=item.tags)
    return f"Cleared tags on {len(tagged)} item(s)"


@plugin.command("favourite")
def favourite(ctx):
    for item in ctx.items:
        ctx.update(item, favourite=True)
    return f"Added {len(ctx.items)} item(s) to favourites"


plugin.run()
