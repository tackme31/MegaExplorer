"""Fetch preview: items.fetchPreview, the image an image-processing plugin starts from.

Shows:
- ctx.fetch_preview(item) -> Path of a JPEG of up to 1000 px, in a folder the app
  made for this run
- NoPreview for items without one (only uploads from clients that made a preview
  have one: most images, videos and PDFs; not mp3 or zip)
- tidying up: the file is the plugin's, so it deletes it once read. Close the file
  first -- Windows cannot delete an open file. Anything left is removed by the app
  when the plugin exits.
"""

from megaexplorer_plugin import NoPreview, Plugin

plugin = Plugin()


@plugin.command("fetch")
def fetch(ctx):
    fetched = []
    missing = 0
    for item in ctx.items:
        if not item.is_file:
            continue
        try:
            path = ctx.fetch_preview(item)
        except NoPreview:
            missing += 1
            continue
        with path.open("rb") as f:
            is_jpeg = f.read(2) == bytes([0xFF, 0xD8])
        size = path.stat().st_size
        print(f"preview of {item.name}: {path} ({size} bytes, jpeg={is_jpeg})")
        fetched.append(size)
        path.unlink()
    return f"Fetched {len(fetched)} preview(s), {sum(fetched) // 1024} KB; {missing} without one"


plugin.run()
