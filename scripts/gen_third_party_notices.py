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

# Both are pinned public submodules, which is what lets the notices point at a
# patch set instead of reproducing it.
VCPKG_REPO = "https://github.com/microsoft/vcpkg"
VCPKG_PORTS = REPO_ROOT / "third_party/vcpkg/ports"
SDK_REPO = "https://github.com/meganz/sdk"
SDK_OVERLAY_PORTS = REPO_ROOT / "third_party/sdk/cmake/vcpkg_overlay_ports"

# Pinned in CLAUDE.md / CMakeLists.txt respectively. Kept as constants rather
# than probed from the build tree so that a version bump shows up as a reviewable
# line in `git diff` alongside the license text it belongs to.
QT_VERSION = "6.11.1"
SDK_VERSION = "v10.17.0"
QWINDOWKIT_VERSION = "1.5.0"

# Every license text in here belongs to someone else, so without these the
# distribution never names this program's own author, nor says where its source
# is -- which is what makes the statically linked LGPL components relinkable.
# The About dialog says both, but a dialog is not part of an unpacked archive.
COPYRIGHT_LINE = "MEGA Explorer  Copyright (c) 2026  Takumi Yamada (tackme31)"
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
    # (GPL-2.0-only OR GPL-3.0-only OR FreeImage) -- the FIPL side, whose
    # section 3.6 explicitly allows the executable to be distributed under a
    # license of our choice. It is also the text the port's `copyright` file
    # actually carries.
    "freeimage": "FreeImage",
    # (BSD-3-Clause OR GPL-2.0-only) -- the permissive side.
    "zstd": "BSD-3-Clause",
    # (FTL OR GPL-2.0-or-later) -- the permissive side.
    "freetype": "FTL",
    # (LGPL-2.1-only OR CDDL-1.0) -- the LGPL side. This one reaches the exe
    # statically (the SDK's overlay triplet builds everything but ffmpeg
    # static), so section 6's relinking requirement is met by publishing this
    # app's own source; notices_preamble() says so.
    "libraw": "LGPL-2.1-only",
    # The SDK's overlay port declares Apache-2.0, which contradicts the very
    # text it installs: PDFium's own LICENSE is BSD-3-Clause. The Apache half is
    # real but comes from the vendored Abseil (PDFIUM_BUNDLED below), so the
    # static library as delivered is under both.
    "pdfium": "BSD-3-Clause AND Apache-2.0",
}

# PDFium's static library carries three vendored dependencies whose texts are
# not in the copyright file vcpkg installs -- that file is PDFium's LICENSE
# alone. Their sources are fetched into buildtrees/ at build time and are gone
# after a `vcpkg remove`, so the texts are vendored under licenses/upstream/
# rather than read from there. All three are compiled into pdfium.lib and reach
# the exe (verified against the overlay port's CMakeLists.txt source list).
PDFIUM_BUNDLED = [
    {
        "name": "abseil-cpp",
        "license": "Apache-2.0",
        "homepage": "https://github.com/abseil/abseil-cpp",
        "text": "pdfium-abseil-cpp.txt",
        # Fetched by the port itself, outside vcpkg's versioning, so the commit
        # pinned in portfile.cmake is the only thing that identifies the copy
        # that shipped.
        "ref": "third_party/abseil-cpp",
    },
    {
        "name": "agg23",
        # In PDFium's own tree rather than fetched, and PDFium patches it
        # further; 2.3 is the upstream release it descends from.
        "version": "2.3",
        # PDFium's README.pdfium classifies the Anti-Grain Geometry notice as
        # MIT. The notice itself is reproduced below the label either way.
        "license": "MIT",
        "homepage": "https://sourceforge.net/projects/agg/",
        "text": "pdfium-agg23.txt",
    },
    {
        "name": "fast_float",
        # Offered as Apache-2.0 OR MIT OR BSL-1.0; MIT is the side taken.
        "license": "MIT",
        "homepage": "https://github.com/fastfloat/fast_float",
        "text": "pdfium-fast-float.txt",
        "ref": "third_party/fast_float/src",
    },
]

# Test-only, never linked into MegaExplorer, so it is not part of the
# distribution and carries no attribution duty.
EXCLUDED_PORTS = {"gtest"}

# Qt ships no license file inside the repo (it is an external install), and its
# own bundled third-party set is far too large to reproduce. The LGPLv3 text
# (appended below by qt_text) plus the pointer to Qt's published list cover it.
QT_SOURCE_URL = f"https://download.qt.io/archive/qt/{'.'.join(QT_VERSION.split('.')[:2])}/{QT_VERSION}/single/"

QT_NOTICE = f"""\
The Qt Toolkit is Copyright (C) 2026 The Qt Company Ltd. and other
contributors.
Contact: https://www.qt.io/licensing/

MEGA Explorer uses the Qt Community Edition under the terms of the GNU
Lesser General Public License, version 3 (LGPLv3). Only Qt modules available
under that license are used; no GPL-only Qt module is linked.

You may use, distribute and copy the Qt libraries used by this application
under the terms of the LGPLv3, whose full text is reproduced immediately
below, followed by the GPLv3 text it builds upon. The Qt libraries are
shipped as separate dynamic libraries and are not modified, so a recipient
may replace them with a modified, interface-compatible build. The
corresponding Qt {QT_VERSION} sources are published by The Qt Company at:
  {QT_SOURCE_URL}

Qt itself bundles certain third-party code that is licensed under separate
terms from their original authors, independent of the LGPL/GPL terms above.
The full list and texts of these components are published by The Qt Company
at:
  https://doc.qt.io/qt-6/licenses-used-in-qt.html

Qt is a trademark of The Qt Company Ltd. in Finland and/or other countries
worldwide.
"""

BANNER = "=" * 79
RULE = "-" * 79


def port_patch_count(port_dir: Path) -> int:
    """How many patches a vcpkg port applies.

    Read out of portfile.cmake's PATCHES block rather than by counting *.patch
    files: a port can carry one it no longer applies, and the notices may only
    claim what actually shaped the binary. Counted rather than written into the
    prose by hand so a vcpkg bump cannot leave the number behind."""
    portfile = port_dir / "portfile.cmake"
    if not portfile.is_file():
        sys.exit(f"error: {portfile} not found; cannot count patches for the notices")
    block = re.search(r"^\s*PATCHES\s*$(.*?)^\s*\)",
                      portfile.read_text(encoding="utf-8"), re.S | re.M)
    if not block:
        sys.exit(f"error: no PATCHES block in {portfile}")
    return len(re.findall(r"^\s*[^\s#]\S*\.patch", block.group(1), re.M))


def pdfium_vendored_ref(destination: str) -> str:
    """The commit PDFium's port pins one of its fetched dependencies to."""
    portfile = SDK_OVERLAY_PORTS / "pdfium/portfile.cmake"
    if not portfile.is_file():
        sys.exit(f"error: {portfile} not found; cannot resolve {destination}")
    call = re.compile(
        r"pdfium_from_git\((?:(?!\)).)*?DESTINATION\s+" + re.escape(destination)
        + r"(?:(?!\)).)*?REF\s+([0-9a-f]{7,40})", re.S)
    match = call.search(portfile.read_text(encoding="utf-8"))
    if not match:
        sys.exit(f"error: no pdfium_from_git block for {destination} in {portfile}")
    return match.group(1)[:12]


def notices_preamble() -> str:
    """The claims this file makes about what was modified and where to get it.

    Generated rather than stored as a constant because the patch counts come
    from the pinned submodules: an unqualified "unmodified" here is the one
    error in this file that is a licence problem rather than a typo."""
    ffmpeg_patches = port_patch_count(VCPKG_PORTS / "ffmpeg")
    freeimage_patches = port_patch_count(VCPKG_PORTS / "freeimage")
    libraw_patches = port_patch_count(VCPKG_PORTS / "libraw")

    return f"""\
MEGA Explorer
Third-Party Software Notices and Information

{COPYRIGHT_LINE}

This application, MEGA Explorer, is licensed under the MIT License. A copy of
that license is included in the file named "LICENSE" in the root of this
distribution, and the software is provided without warranty of any kind.

The complete source code of MEGA Explorer, including the build instructions
needed to rebuild it, is available at
{SOURCE_URL}

MEGA Explorer incorporates or links against third-party software components
that are subject to separate copyright and license terms, as detailed below.
Reproducing this file, unmodified, alongside the LICENSE file satisfies the
attribution requirements of those components.

How the third-party components were built
------------------------------------------
Qt is used exactly as published by The Qt Company: the official Qt {QT_VERSION}
binary release for MSVC, unmodified. Its corresponding sources are at
  {QT_SOURCE_URL}

Everything else is built from source through vcpkg, and most of those ports
apply patches -- build fixes, MSVC portability, and redirecting a component's
bundled copies of other libraries to external builds of them. Those patches are
therefore part of what this distribution contains, and each one is a file in
the port directory of a submodule of the source tree above:

  ports/<component>/                     {VCPKG_REPO}
  cmake/vcpkg_overlay_ports/<component>/ {SDK_REPO}

Both submodules are pinned to a specific commit of a public repository, so the
exact patched sources of any component here can be reconstructed from the
source tree of MEGA Explorer.

Components under the GNU Lesser General Public License
------------------------------------------------------
Three components are used under the LGPL.

  Qt ({QT_VERSION}, LGPLv3) and FFmpeg (LGPLv2.1) are shipped as separate
  dynamic libraries, so a recipient may substitute a modified,
  interface-compatible build of either without relinking MEGA Explorer. Qt is
  unmodified, as described above. FFmpeg is built from vcpkg's "ffmpeg" port,
  which applies {ffmpeg_patches} patches; its upstream sources are at
  https://ffmpeg.org/download.html.

  LibRaw (LGPLv2.1), reached through FreeImage, is linked statically and is
  built from vcpkg's "libraw" port, which applies {libraw_patches} patches. To satisfy
  section 6 of that license, the complete source code of MEGA Explorer is
  published at the address above under the MIT License, which permits
  modification and imposes no restriction on reverse engineering, so a
  recipient can rebuild this program against a modified LibRaw. LibRaw's own
  sources are published at https://www.libraw.org/download.

Notice required by the FreeImage Public License
------------------------------------------------
FreeImage 3.18.0 is used under the FreeImage Public License, version 1.0
(section 3.6 of which permits the executable form to be distributed under a
different license). It is modified: the build recipe used here is vcpkg's
"freeimage" port, and its {freeimage_patches} patches are the modifications that
section 3.2 requires to be made available. They are published as files in
ports/freeimage of the vcpkg repository named above, and they redirect
FreeImage's bundled copies of libjpeg, libpng, libtiff, OpenJPEG, OpenEXR,
WebP, JPEG-XR and LibRaw to external builds of those libraries. FreeImage's
own source code is available under the FreeImage license at
https://sourceforge.net/projects/freeimage/files/.

This file is generated by scripts/gen_third_party_notices.py -- do not edit it
by hand.
"""


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


def qt_text() -> str:
    """LGPLv3 is written as a set of additional permissions on top of GPLv3, so
    both texts have to travel with the distribution. They used to be covered by
    this app's own LICENSE being GPLv3; since that is MIT, the Qt entry carries
    them itself."""
    upstream = REPO_ROOT / "licenses/upstream"
    # Narrower than RULE: this one is read inside the license dialog's text pane,
    # which wraps at ~76 columns and would break a 79-column rule onto two lines.
    rule = "-" * 70
    return "\n".join([
        QT_NOTICE,
        rule,
        read_text(upstream / "LGPL-3.0.txt"),
        rule,
        read_text(upstream / "GPL-3.0.txt"),
    ])


def fixed_components() -> list[dict]:
    """Everything that does not come from vcpkg. Order here is the order the
    dialog shows: this app first, then the frameworks, then what the SDK vendors."""
    sdk_root = REPO_ROOT / "third_party/sdk"
    vendored_root = sdk_root / "third_party"

    components = [
        {
            "name": "MegaExplorer",
            "version": app_version(),
            "license": "MIT",
            "homepage": "https://github.com/tackme31/MegaExplorer",
            "text": read_text(REPO_ROOT / "LICENSE"),
        },
        {
            "name": "Qt",
            "version": QT_VERSION,
            "license": "LGPL-3.0-only",
            "homepage": "https://www.qt.io/",
            "text": qt_text(),
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
    parts = [notices_preamble()]
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


def pdfium_bundled_components() -> list[dict]:
    """PDFIUM_BUNDLED rendered as components."""
    upstream = REPO_ROOT / "licenses/upstream"
    return [
        {
            # Same "(via ...)" convention as the SDK's vendored set: these are
            # not dependencies this app picked.
            "name": f"{entry['name']} (via PDFium)",
            "version": entry.get("version") or pdfium_vendored_ref(entry["ref"]),
            "license": entry["license"],
            "homepage": entry["homepage"],
            "text": read_text(upstream / entry["text"]),
        }
        for entry in PDFIUM_BUNDLED
    ]


def build_outputs(installed: Path) -> dict[str, str]:
    components = fixed_components() + vcpkg_components(installed)

    pdfium = next((c for c in components if c["name"] == "pdfium"), None)
    if pdfium is None:
        sys.exit("error: no pdfium component; PDFIUM_BUNDLED has nothing to attach to")
    at = components.index(pdfium) + 1
    components[at:at] = pdfium_bundled_components()

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
