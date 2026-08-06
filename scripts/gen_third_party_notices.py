#!/usr/bin/env python3
"""Regenerate the third-party license inventory.

Outputs (all committed to the repo):
    licenses/manifest.json      one entry per component, drives LicenseDialog
    licenses/texts/<id>.txt     the license text of each component
    licenses/licenses.cmake     the qt_add_qml_module RESOURCES list
    THIRD-PARTY-NOTICES.txt     the same data rendered as one flat file

Usage:
    python scripts/gen_third_party_notices.py
    python scripts/gen_third_party_notices.py --check    # non-zero if stale

Run it after bumping Qt, the MEGA SDK submodule, QWindowKit, or any vcpkg
dependency. The generated files are committed on purpose: the vcpkg input lives
under build/ which is gitignored, so a build-time generator would silently
produce a binary with no notices in it on a clean clone.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_VCPKG_INSTALLED = REPO_ROOT / "build/msvc-debug/vcpkg_installed/x64-windows-mega"

# Pinned in CLAUDE.md / CMakeLists.txt respectively. Kept as constants rather
# than probed from the build tree so that a version bump shows up as a reviewable
# line in `git diff` alongside the license text it belongs to.
QT_VERSION = "6.11.1"
SDK_VERSION = "v10.17.0"
QWINDOWKIT_VERSION = "1.5.0"

# LICENSE is the FSF's document and every other text belongs to someone else, so
# without these the distribution never names this program's own author, nor says
# where GPLv3 section 6's corresponding source is. The About dialog says both,
# but a dialog is not part of an unpacked archive.
COPYRIGHT_LINE = "MEGA Explorer  Copyright (C) 2026  Takumi Yamada"
SOURCE_URL = "https://github.com/tackme31/MegaExplorer"

# vcpkg's own metadata is unusable for these, either because it failed to reduce
# the port to an SPDX expression (LicenseRef-vcpkg-null) or because the port is
# dual-licensed and *we* have to declare which side we took.
LICENSE_OVERRIDES = {
    # copyright is the full LGPL-2.1 text; the port's `gpl` feature is not
    # requested by third_party/sdk/vcpkg.json, so this is the LGPL build.
    "ffmpeg": "LGPL-2.1-or-later",
    # copyright names it in its first line.
    "jasper": "JasPer-2.0",
    # XZ Utils 5.8; the core liblzma sources are public-domain-equivalent.
    "liblzma": "0BSD",
    # (GPL-2.0-only OR GPL-3.0-only OR FreeImage) -- GPL-3.0 to match this app.
    "freeimage": "GPL-3.0-only",
    # (BSD-3-Clause OR GPL-2.0-only) -- the permissive side.
    "zstd": "BSD-3-Clause",
    # (FTL OR GPL-2.0-or-later) -- the permissive side.
    "freetype": "FTL",
    # (LGPL-2.1-only OR CDDL-1.0) -- CDDL is GPL-incompatible, so LGPL-2.1,
    # which section 3 lets us relicense up to the GPL this app ships under.
    "libraw": "LGPL-2.1-only",
}

# Test-only, never linked into appMegaExplorer, so it is not part of the
# distribution and carries no attribution duty.
EXCLUDED_PORTS = {"gtest"}

# Qt ships no license file inside the repo (it is an external install), and its
# own bundled third-party set is far too large to reproduce. Both the GPLv3 text
# and the pointer to Qt's published list cover it.
QT_NOTICE = """\
The Qt Toolkit is Copyright (C) 2026 The Qt Company Ltd. and other
contributors.
Contact: https://www.qt.io/licensing/

MEGA Explorer uses the Qt Community Edition under the terms of the GNU
General Public License, version 3 (GPLv3).

You may use, distribute and copy the Qt libraries used by this application
under the terms of the GNU General Public License version 3, the full text
of which is reproduced in the "MegaExplorer" entry of this document and in
the accompanying "LICENSE" file of this distribution.

Qt itself bundles certain third-party code that is licensed under separate
terms from their original authors, independent of the LGPL/GPL terms above.
The full list and texts of these components are published by The Qt Company
at:
  https://doc.qt.io/qt-6/licenses-used-in-qt.html

Qt is a trademark of The Qt Company Ltd. in Finland and/or other countries
worldwide.
"""

NOTICES_PREAMBLE = f"""\
MEGA Explorer
Third-Party Software Notices and Information

{COPYRIGHT_LINE}

This application, MEGA Explorer, is licensed under the GNU General Public
License version 3 (GPLv3). A copy of that license is included in the file
named "LICENSE" in the root of this distribution. This program comes with
ABSOLUTELY NO WARRANTY; see sections 15 and 16 of that license.

The complete corresponding source code is available at
{SOURCE_URL}

MEGA Explorer incorporates or links against third-party software components
that are subject to separate copyright and license terms, as detailed below.
Reproducing this file, unmodified, alongside the LICENSE file satisfies the
attribution requirements of those components.

Components covered by the GNU Lesser General Public License (FFmpeg) are
distributed as separate dynamic libraries, so a modified build of them can be
substituted without relinking MEGA Explorer.

This file is generated by scripts/gen_third_party_notices.py -- do not edit it
by hand.
"""

BANNER = "=" * 79
RULE = "-" * 79


def slug(name: str) -> str:
    """Filesystem/qrc-safe id. Collisions would silently drop a license text, so
    the caller checks for them."""
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def read_text(path: Path) -> str:
    """Normalize to LF and a single trailing newline so output is byte-stable
    regardless of how git checked the input out."""
    raw = path.read_text(encoding="utf-8", errors="replace")
    return raw.replace("\r\n", "\n").replace("\r", "\n").rstrip("\n") + "\n"


def app_version() -> str:
    """Single-source the app version from project() rather than repeating it."""
    text = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(MegaExplorer\s+VERSION\s+([0-9.]+)", text)
    if not match:
        sys.exit("error: could not read VERSION from CMakeLists.txt's project()")
    return match.group(1)


def fixed_components() -> list[dict]:
    """Everything that does not come from vcpkg. Order here is the order the
    dialog shows: this app first, then the frameworks, then what the SDK vendors."""
    sdk_root = REPO_ROOT / "third_party/sdk"
    vendored_root = sdk_root / "third_party"

    components = [
        {
            "name": "MegaExplorer",
            "version": app_version(),
            "license": "GPL-3.0-only",
            "homepage": "https://github.com/tackme31/MegaExplorer",
            "text": read_text(REPO_ROOT / "LICENSE"),
        },
        {
            "name": "Qt",
            "version": QT_VERSION,
            "license": "GPL-3.0-only",
            "homepage": "https://www.qt.io/",
            "text": QT_NOTICE,
        },
        {
            "name": "MEGA C++ SDK",
            "version": SDK_VERSION,
            "license": "BSD-2-Clause",
            "homepage": "https://github.com/meganz/sdk",
            "text": read_text(sdk_root / "LICENSE"),
        },
        {
            "name": "QWindowKit",
            "version": QWINDOWKIT_VERSION,
            "license": "Apache-2.0",
            "homepage": "https://github.com/stdware/qwindowkit",
            "text": read_text(REPO_ROOT / "third_party/qwindowkit/LICENSE"),
        },
    ]

    # Vendored inside the SDK. third_party/sdk/third_party/CMakeLists.txt gates
    # two of the seven out of this build and they are deliberately absent here:
    # `evt-tls` needs USE_LIBUV (OFF -- use-libuv is not in our vcpkg feature
    # list), and `glob` is `if(NOT WIN32 ...)`. Neither is linked, so neither is
    # distributed. `glob` also ships no license file at all, which would need
    # chasing upstream if it ever became reachable.
    vendored = [
        ("ccronexpr", "LICENSE", "Apache-2.0", "https://github.com/staticlibs/ccronexpr"),
        ("csv", "LICENSE", "MIT", "https://github.com/ben-strasser/fast-cpp-csv-parser"),
        ("http_parser", "LICENSE-MIT", "MIT", "https://github.com/nodejs/http-parser"),
        ("utf8proc", "LICENSE", "MIT", "https://github.com/JuliaStrings/utf8proc"),
        ("zxcvbn-c", "LICENSE.txt", "MIT", "https://github.com/tsyrogit/zxcvbn-c"),
    ]
    for name, license_file, license_id, homepage in vendored:
        components.append({
            # Disambiguated in the UI: these are not direct dependencies of this
            # app, they arrive through the SDK.
            "name": f"{name} (via MEGA SDK)",
            "version": SDK_VERSION,
            "license": license_id,
            "homepage": homepage,
            "text": read_text(vendored_root / name / license_file),
        })

    return components


def vcpkg_components(installed: Path) -> list[dict]:
    """One entry per real vcpkg port.

    Only directories carrying a vcpkg.spdx.json are ports; the rest (WebP, jpeg,
    png, lcms2, unofficial-*, doc) are aliases that would otherwise be counted
    twice."""
    share = installed / "share"
    if not share.is_dir():
        sys.exit(
            f"error: {share} not found.\n"
            "Configure and build once so vcpkg materializes the license texts, "
            "or pass --vcpkg-installed."
        )

    components = []
    for spdx_path in sorted(share.glob("*/vcpkg.spdx.json")):
        package = json.loads(spdx_path.read_text(encoding="utf-8"))["packages"][0]
        name = package["name"]
        if name in EXCLUDED_PORTS:
            continue

        copyright_path = spdx_path.parent / "copyright"
        if not copyright_path.is_file():
            sys.exit(f"error: {name} has no copyright file next to {spdx_path}")

        license_id = LICENSE_OVERRIDES.get(name, package.get("licenseConcluded", ""))
        if "LicenseRef-" in license_id or not license_id:
            sys.exit(
                f"error: {name} has no usable license id ({license_id!r}); "
                "add it to LICENSE_OVERRIDES with a rationale."
            )

        components.append({
            "name": name,
            # versionInfo carries vcpkg's port revision ("1.1.0#1"); the upstream
            # version is what the notice is about, so drop the suffix.
            "version": package.get("versionInfo", "").split("#")[0],
            "license": license_id,
            "homepage": package.get("homepage", ""),
            "text": read_text(copyright_path),
        })

    return components


def render_notices(components: list[dict]) -> str:
    parts = [NOTICES_PREAMBLE]
    for index, component in enumerate(components, start=1):
        parts.append(
            f"\n{BANNER}\n{index}. {component['name']}\n{BANNER}\n\n"
            f"Project:    {component['name']}\n"
            f"Version:    {component['version']}\n"
            f"Homepage:   {component['homepage']}\n"
            f"License:    {component['license']}\n\n"
            f"{RULE}\n{component['text']}{RULE}\n"
        )
    parts.append(f"\n{BANNER}\nEnd of Third-Party Notices\n{BANNER}\n")
    return "".join(parts)


def render_cmake(components: list[dict]) -> str:
    lines = [
        "# Generated by scripts/gen_third_party_notices.py -- do not edit.",
        "#",
        "# The RESOURCES list lives here rather than inline in CMakeLists.txt so the",
        "# set of embedded files cannot drift away from what manifest.json references.",
        "set(MEGAEXPLORER_LICENSE_RESOURCES",
        "    licenses/manifest.json",
    ]
    lines += [f"    licenses/texts/{c['id']}.txt" for c in components]
    lines.append(")")
    return "\n".join(lines) + "\n"


def build_outputs(installed: Path) -> dict[str, str]:
    components = fixed_components() + vcpkg_components(installed)

    seen = {}
    for component in components:
        component["id"] = slug(component["name"])
        if component["id"] in seen:
            sys.exit(
                f"error: id collision {component['id']!r} between "
                f"{seen[component['id']]!r} and {component['name']!r}"
            )
        seen[component["id"]] = component["name"]

    manifest = {
        "components": [
            {
                "id": c["id"],
                "name": c["name"],
                "version": c["version"],
                "license": c["license"],
                "homepage": c["homepage"],
                "text": f"texts/{c['id']}.txt",
            }
            for c in components
        ]
    }

    outputs = {
        "licenses/manifest.json": json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        "licenses/licenses.cmake": render_cmake(components),
        "THIRD-PARTY-NOTICES.txt": render_notices(components),
    }
    for component in components:
        outputs[f"licenses/texts/{component['id']}.txt"] = component["text"]
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vcpkg-installed", type=Path, default=DEFAULT_VCPKG_INSTALLED)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the committed files match; write nothing",
    )
    args = parser.parse_args()

    outputs = build_outputs(args.vcpkg_installed)

    if args.check:
        def matches(name: str, content: str) -> bool:
            path = REPO_ROOT / name
            if not path.is_file():
                return False
            # newline="" so an LF file is not silently compared as CRLF-free;
            # Path.read_text() only grew the argument in 3.13.
            with path.open(encoding="utf-8", newline="") as handle:
                return handle.read() == content

        stale = [name for name, content in outputs.items() if not matches(name, content)]
        # A leftover text file for a dependency that was dropped still gets
        # embedded, so treat extras as staleness too.
        expected = {name for name in outputs if name.startswith("licenses/texts/")}
        stale += [
            f"licenses/texts/{p.name} (no longer referenced)"
            for p in sorted((REPO_ROOT / "licenses/texts").glob("*.txt"))
            if f"licenses/texts/{p.name}" not in expected
        ]
        if stale:
            print("Third-party notices are out of date:", file=sys.stderr)
            for name in stale:
                print(f"  {name}", file=sys.stderr)
            print("Run: python scripts/gen_third_party_notices.py", file=sys.stderr)
            return 1
        print(f"Up to date ({len(outputs)} files).")
        return 0

    texts_dir = REPO_ROOT / "licenses/texts"
    texts_dir.mkdir(parents=True, exist_ok=True)
    for path in texts_dir.glob("*.txt"):
        if f"licenses/texts/{path.name}" not in outputs:
            path.unlink()
    for name, content in outputs.items():
        (REPO_ROOT / name).write_text(content, encoding="utf-8", newline="")

    print(f"Wrote {len(outputs)} files ({len(outputs) - 3} license texts).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
