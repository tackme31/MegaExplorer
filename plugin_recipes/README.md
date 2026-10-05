# Plugin recipes

One plugin, **Recipes**, with small commands that are useful on their own. Where
[`plugin_sample/`](../plugin_sample/) shows one API feature per command, each command here is a
whole job that strings several of them together, so the flow of a real plugin can be read top to
bottom in [`main.py`](main.py). The protocol behind it is in [PLUGINS.md](../PLUGINS.md).

| Menu row | What it does | Flow |
| --- | --- | --- |
| Export a CSV of everything inside | Writes every file and folder inside the folder, at any depth, to a CSV (path, name, type, size, modified, favourite, tags, description, crc, handle, parent) and uploads it into that folder as `items-<date>-<time>.csv`, then shows it selected | `items.descendants` under a progress dialog with Cancel → `ui.confirm` with the counts → the CSV → `items.upload` → `ui.reveal` selects the uploaded file |

The CSV is UTF-8 with a BOM, so Excel opens names in any language correctly. The export uploads a
file into the account (there is no undo): try it on a test folder.

## Trying it

1. Install [uv](https://docs.astral.sh/uv/); the plugin starts as `uv run --quiet main.py`.
2. Copy this folder into the app's plugins folder, e.g. as `plugins\recipes\`:
   - a Debug build (the `dev` profile): `%LOCALAPPDATA%\MegaExplorer\MegaExplorer-dev\plugins\`
   - a Release build: `%LOCALAPPDATA%\MegaExplorer\MegaExplorer\plugins\`
3. Restart the app: plugins are found at startup.
4. Right-click a folder, or a folder view's empty space, and pick a row under **Recipes**.

[`megaexplorer_plugin.py`](megaexplorer_plugin.py) is a copy of the helper in `plugin_sample/`,
since a plugin folder is installed on its own; see that folder's README for how to use it.
