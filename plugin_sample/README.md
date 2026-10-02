# Plugin sample

One plugin, **Sample**, with a command for each thing a plugin can do. Right-click a file
or folder and open the **Sample** submenu; each row's title names the case it shows.
[`main.py`](main.py) is split into one section per feature, so read only the one you need.
The protocol behind it is in [PLUGINS.md](../PLUGINS.md).

| Menu row | Demonstrates |
| --- | --- |
| Show a toast | The selection the app hands over (`ctx.items`), a returned string as a toast, `print()` going to the app's log |
| Show an error | `CommandError` as an error toast |
| Show a result dialog | `"result": "dialog"`: a long, multi-line result in a dialog |
| Show an error in the result dialog | The same for an error: first line as the summary, the rest only in the dialog |
| Read details (items.get) | `ctx.get`: an item's current state |
| Count the folder (items.children) | `ctx.children`: a folder's children, page by page |
| Count everything below (items.descendants) | `ctx.descendants`: everything below a folder |
| Fetch previews (items.fetchPreview, files only) | `ctx.fetch_preview`, `NoPreview`, deleting the file once read; `"when": {"targets": "files"}` |
| Tag with the extension (items.update, images only) | `ctx.update(tags_add=...)`; `"when"` with `extensions` |
| Clear tags (items.update) | `ctx.update(tags_remove=...)` |
| Add to favourites (items.update) | `ctx.update(favourite=True)`; the view re-reads after a change |
| Show confirm | `ctx.confirm`: asking first, stopping quietly on Cancel |
| Show confirm (danger) | `danger=True`, `title`, `ok_label` |
| Show progress | `"progress": true`, `ctx.progress` with a total, Cancel (`ctx.check_cancelled`) |
| Show progress, then the result in the dialog | `"progress": true` with `"result": "dialog"`: the progress row turns into the result |
| Show progress without a total | `ctx.progress` with only a message |
| Finish before the progress dialog | A quick command never flashes the dialog |
| Ignore Cancel (Force quit) | A plugin that ignores Cancel: Force quit after 10 s |

The "items.update" rows change the real account (there is no undo): try them on a test
folder.

## Trying it

1. Install [uv](https://docs.astral.sh/uv/); the sample starts as `uv run --quiet main.py`.
2. Copy this folder into the app's plugins folder, e.g. as `plugins\sample\`:
   - a Debug build (the `dev` profile): `%LOCALAPPDATA%\MegaExplorer\MegaExplorer-dev\plugins\`
   - a Release build: `%LOCALAPPDATA%\MegaExplorer\MegaExplorer\plugins\`
3. Restart the app: plugins are found at startup. After that, edits to `main.py` take
   effect on the next click; edits to `plugin.json` need another restart.
4. Right-click a file or folder and pick a row under **Sample**.

What the plugin writes to stderr (including `print()`) lands in `MegaExplorer.log` next to
the plugins folder, prefixed `[plugin:com.example.sample]`.

## The helper

[`megaexplorer_plugin.py`](megaexplorer_plugin.py) wraps the JSON-RPC so a command is just
a function:

```python
from megaexplorer_plugin import Plugin

plugin = Plugin()

@plugin.command("count")          # the command id in plugin.json
def count(ctx):
    return f"{len(ctx.items)} item(s) selected"   # shown as a toast

plugin.run()
```

It is a convenience, not part of the protocol: the app does not know it exists. This copy
is the original. A plugin of your own carries its own copy next to its `main.py`, because a
plugin folder is installed on its own.
