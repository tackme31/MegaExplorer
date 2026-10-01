# Plugin samples

One plugin per feature. Each shows up in the file context menu as **Sample: …**, with
command titles naming what they exercise. Every `main.py` opens with a docstring
listing what it demonstrates.

| Folder | Menu | Demonstrates |
| --- | --- | --- |
| `basics/` | Sample: Basics | The selection the app hands over (`ctx.items`), a toast from the return value, an error toast from `CommandError`, `print()` going to the app's log |
| `read-items/` | Sample: Read items | `items.get` (`ctx.get`) for an item's current details; `items.children` (`ctx.children`) paged through a folder; `items.descendants` (`ctx.descendants`) for everything below it |
| `fetch-preview/` | Sample: Fetch preview | `items.fetchPreview` (`ctx.fetch_preview`): the preview JPEG as a file, `NoPreview`, deleting the file once read |
| `update-items/` | Sample: Update items | `items.update` (`ctx.update`): adding and removing tags, favourites; the view re-reads after a change |
| `progress-cancel/` | Sample: Progress and cancel | `"progress": true`, `ui.progress` (`ctx.progress`) with and without a total, Cancel (`ctx.check_cancelled`), Force quit for a plugin that ignores it |
| `_helper/` | — | `megaexplorer_plugin.py`, the Python helper every sample uses, and `sync.py` |

## Trying one

1. Install [uv](https://docs.astral.sh/uv/); every sample starts as `uv run --quiet main.py`.
2. Copy the sample's folder into the app's plugins folder:
   - a Debug build (the `dev` profile): `%LOCALAPPDATA%\MegaExplorer\MegaExplorer-dev\plugins\`
   - a Release build: `%LOCALAPPDATA%\MegaExplorer\MegaExplorer\plugins\`
3. Restart the app: plugins are found at startup. After that, edits to `main.py` take
   effect on the next click; edits to `plugin.json` need another restart.
4. Right-click a file or folder and pick the sample's submenu.

What a plugin writes to stderr (including `print()`) lands in `MegaExplorer.log` next to
the plugins folder, prefixed `[plugin:<id>]`.

## The helper

`megaexplorer_plugin.py` wraps the JSON-RPC so a command is just a function:

```python
from megaexplorer_plugin import Plugin

plugin = Plugin()

@plugin.command("count")          # the command id in plugin.json
def count(ctx):
    return f"{len(ctx.items)} item(s) selected"   # shown as a toast

plugin.run()
```

It is a convenience, not part of the protocol: the app does not know it exists.
Each plugin carries its own copy, because a plugin folder is installed on its own.
`_helper/` holds the original; after editing it, run `python plugin_sample/_helper/sync.py`
to copy it into every sample.
