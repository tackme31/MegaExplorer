"""Copies megaexplorer_plugin.py into every sample plugin folder.

Each plugin gets its own copy because a plugin folder is installed on its own:
an import reaching into ../_helper would break once it is copied elsewhere.
Run after editing the helper: python plugin_sample/_helper/sync.py
"""

import shutil
from pathlib import Path

helper = Path(__file__).with_name("megaexplorer_plugin.py")
for manifest in sorted(helper.parent.parent.glob("*/plugin.json")):
    shutil.copy2(helper, manifest.parent / helper.name)
    print(f"synced {manifest.parent.name}")
