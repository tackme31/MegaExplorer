# MEGA Explorer

A Windows Explorer-style desktop client for [MEGA](https://mega.io) cloud storage.

MEGA Explorer browses your MEGA account the way you browse a local drive: a folder tree on the
left, a thumbnail grid or detail list on the right, double-click to open a folder, tabs for the
places you keep coming back to. It is a *browser* for your cloud files, not a sync client — there
is no continuous watching of a local directory, only a background refresh when you open a folder.

## Status: pre-release

This project has not been released yet. There are no builds to download, the feature set is still
moving, and anything described below may change or disappear without notice. Expect rough edges.

## Disclaimer

MEGA Explorer is a personal project, written for my own everyday use and published in case it is
useful to someone else. It is not affiliated with, endorsed by, or supported by MEGA Limited.

It is provided **as is, without warranty of any kind**, as stated in the [LICENSE](LICENSE). It
talks to your real cloud storage and can move, rename and permanently delete files there — use it
at your own risk, and keep backups of anything you cannot afford to lose.

## Features

- **Explorer-style browsing** — folder tree, breadcrumb address bar, grid and detail views, tabs,
  and a preview pane for the selected file.
- **Thumbnails and previews** for images, video, RAW and PDF files, so a folder of photos is
  recognisable at a glance.
- **Search and filter** across your account, with filters for narrowing results down.
- **File management** — new folder, rename, copy/cut/paste, move to rubbish bin, restore, empty
  rubbish, and permanent delete, with conflict handling when names collide.
- **Transfers** — upload by drag-and-drop, download to disk, with progress shown in a transfer
  flyout.
- **Public links** — create and remove MEGA share links from the context menu.
- **Quick access** — pin folders and mark favourites to keep them one click away.
- **Local folder pairing** — point the settings at one local folder that mirrors your MEGA root,
  and "Open local location" reveals the matching item in Windows Explorer. It is a naming
  convention only: nothing is compared, copied or synced between the two.
- **Light and dark themes**, following the Windows setting.

## Author

Takumi Yamada ([@tackme31](https://github.com/tackme31))

## License

MEGA Explorer is released under the [MIT License](LICENSE).

It links against third-party components under their own terms — notably Qt (LGPLv3), FFmpeg
(LGPLv2.1) and LibRaw (LGPLv2.1). `THIRD-PARTY-NOTICES.txt` lists every component with its license
text and, for the LGPL ones, where to get their sources.
