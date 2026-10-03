# Plugin sample

One plugin, **Sample**, with a command for each thing a plugin can do. Right-click a file
or folder and open the **Sample** submenu; each row's title names the case it shows.
[`main.py`](main.py) is split into one section per feature, so read only the one you need.
The protocol behind it is in [PLUGINS.md](../PLUGINS.md).

| Menu row | Demonstrates |
| --- | --- |
| Show the selection | The selection the app hands over (`ctx.items`), a returned string as a toast, `print()` going to the app's log |
| Fail with an error | `CommandError` as an error toast |
| Show a long result | `"result": "dialog"`: a long, multi-line result in a dialog |
| Inspect (items.get / children / descendants) | `ctx.get_many`, `ctx.children` and `ctx.descendants` page by page, asking only for the `fields` needed; a folder's counts |
| Fetch previews (items.fetchPreview, images and videos) | `ctx.fetch_preview`, `NoPreview`, deleting the file once read; `"when"` with `extensions` |
| Read a file (items.readRange / fetchFile) | `ctx.read_range` to sniff the file type from its first bytes, `ctx.fetch_file` for the whole file; `"when": {"targets": "files"}` |
| Toggle the "sample" tag (items.update) | `ctx.update` adding or removing a tag and the favourite flag; running it again undoes it |
| Download (transfers.download) | `ctx.download`: the app's own downloads, into `Downloads\MegaExplorer Sample` |
| Upload a text file here (items.upload) | `ctx.upload`: a generated text file into the selected folder (or the file's folder) |
| Create a folder here (items.createFolder) | `ctx.create_folder` with a dated name and `on_conflict="rename"` |
| Copy next to itself (items.copy) | `ctx.copy` into the item's own folder, where the name clashes with itself and becomes "name (2)" |
| Move up one folder (items.move) | `ctx.move` to the parent's parent; a name already taken there gets " (2)" |
| Move to the Rubbish bin (items.moveToRubbish) | `ctx.confirm` first, since the app asks nothing; needs the `items.rubbish` permission |
| Ask first (ui.confirm) | `ctx.confirm` with `danger`, `title` and `ok_label`; stopping quietly on Cancel |
| Show the largest file below (ui.reveal) | `ctx.reveal`: the app opens the file's folder with it selected; `"when": {"targets": "folders"}` |
| Long task (ui.progress, Cancel) | `"progress": true`: a bar without a total, then `n / total`; Cancel (`ctx.check_cancelled`); the row turning into the result (`"result": "dialog"`) |

The "Toggle", upload, folder, copy, move and Rubbish bin rows change the real account (there is no
undo): try them on a test folder.

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
