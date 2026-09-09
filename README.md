<img src="./resources/mega_explorer_icon.svg" width="100" />

# MEGA Explorer

A Windows Explorer-style desktop client for [MEGA](https://mega.io) cloud storage. It's a browser
for your cloud files, not a sync client.

- Browse your whole account with a folder tree, tabs and drag-and-drop
- Thumbnails and previews without downloading, camera RAW included
- Images, video, audio and PDFs opened in a viewer of their own
- Search and filter across everything you have
- Public links, favourites, and a Rubbish bin you can restore from
- Jump to the matching file in a paired local folder

![](./img/screenshot.png)

**Note:** Windows only, and you'll need a MEGA account. This is a personal project, published in
case it's useful to someone else, and it's still before 1.0, so anything below may change. It
isn't affiliated with MEGA Limited and comes with no warranty (see [LICENSE](LICENSE)). It talks
to your real cloud storage and can permanently delete files there, so use it at your own risk.

## Install

Download the latest `MegaExplorer-<version>-win64.zip` from the
[releases page](https://github.com/tackme31/MegaExplorer/releases), unzip it anywhere, and run
`MegaExplorer.exe`.

To uninstall, delete the folder. Your saved session, the local cache and the log live in
`%LOCALAPPDATA%\MegaExplorer`, and the settings are under `HKCU\Software\MegaExplorer`; delete
those too if you want no trace left.

## Features

### Explorer-style browsing

Folder tree, breadcrumb address bar and tabs, grid and detail views, drag-and-drop, and light and
dark themes following the Windows setting.

### Previews without downloading

Thumbnails in the grid and a preview pane, for images (camera RAW included), video, PDFs, text
files, and the contents of zip archives.

### Built-in viewer

Double-click a file to open it in a window of its own, streamed from MEGA rather than saved to
disk. Images, video, audio and PDFs.

### Search and filtering

![](./img/search_and_filters.png)

Search your whole account or just the current folder, and narrow the results by file type,
category, date modified, or favourite. "Go to folder" takes a result to where it lives.

### Transfers

![](./img/transfers.png)

Drop files or folders onto the window to upload, or download them to disk. Transfers run in
parallel, with per-file progress and cancellation in the transfer flyout.

### Side panel views

Quick access for the folders you pin, plus Favourites and Recent.

### Rubbish bin

Deleted items go here rather than disappearing. Restore them, or delete them for good.

### Public links

Create, copy and remove MEGA share links from the context menu. Files that already have one are
marked in the list.

### Local folder

Point the settings at one local folder that mirrors your MEGA root, and you can open the matching
local file, or reveal it in Windows Explorer. The two are never synced.

## Roadmap

- [ ] Live updates, watching the server for changes made elsewhere
- [ ] More control over public links (visibility, expiry, and so on)
- [ ] Albums
- [ ] Localisation, starting with Japanese

## Build

Windows and the MSVC toolchain only. The MEGA SDK's Windows build doesn't support MinGW.

- Visual Studio 2022, with "Desktop development with C++"
- Qt 6.11 or later, `msvc2022_64`
- CMake 3.21 or later. The copy shipped with Qt (`C:/Qt/Tools/CMake_64/bin/cmake.exe`) works.

```
git clone --recursive https://github.com/tackme31/MegaExplorer.git
cd MegaExplorer
third_party\vcpkg\bootstrap-vcpkg.bat
cmake --preset msvc-debug
cmake --build --preset msvc-release
```

The first configure builds the MEGA SDK's dependencies through vcpkg and takes a while. The binary
lands in `build/msvc-debug/Release/MegaExplorer.exe`.

`CMakePresets.json` expects Qt at `C:/Qt/6.11.1/msvc2022_64`, so edit `CMAKE_PREFIX_PATH` there if
yours is somewhere else. [docs/BUILD.md](docs/BUILD.md) has the detail, and the reasoning behind
each of these constraints.

### Packaging

That binary only runs on a machine that already has Qt. To get one that runs anywhere:

```
powershell -File scripts\package.ps1
```

It builds Release and writes `build/msvc-debug/package/MegaExplorer-<version>-win64.zip`, with Qt,
FFmpeg and the MSVC runtime next to the exe and `LICENSE` / `THIRD-PARTY-NOTICES.txt` at the root.
Unzip it anywhere and run `MegaExplorer.exe`; there's nothing to install.

## Author

Takumi Yamada ([@tackme31](https://github.com/tackme31))

## License

MEGA Explorer is released under the [MIT License](LICENSE).

It links against third-party components under their own terms, notably Qt (LGPLv3), FFmpeg
(LGPLv2.1) and LibRaw (LGPLv2.1). `THIRD-PARTY-NOTICES.txt` lists every component with its license
text and, for the LGPL ones, where to get their sources.
