#!/usr/bin/env python3
"""Bring the asset list in metadata/metadata.json up to date with what is
actually on disk.

    python3 tools/make_metadata.py           # rewrite the list
    python3 tools/make_metadata.py --check   # fail if it is stale (CI)

WHY THIS EXISTS. metadata.json tells the app repository which files to
hand a device that installs SynthMiner from there; `make install` over
BadgeLink instead globs textures/ and assets/music/ in the Makefile. Two
lists of the same thing, one maintained by hand, and the hand-maintained
one lost: by the time anyone looked it named 25 of the 54 textures and
none of the 13 pieces of music, so a repository install came up with no
item icons, no furnace, no chest, no birch -- and in silence -- while
the badge on the developer's desk had everything and looked fine.

So the list is generated from the same directories the Makefile globs.
Everything else in the file -- name, description, version, author, the
icons, the interpreter -- is hand-written and left exactly as it is;
this rewrites one array and nothing else.

WHAT SHIPS. Every textures/*.png, because texcache loads them by name
and a missing one is a missing block. Every assets/music/*.mid, because
music_init scans the directory it finds them in. Not MUSIC.md, which is
provenance for humans, and not manifest.json, which get_music.py writes
and nothing at runtime reads.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "metadata" / "metadata.json"

# Where a kind of asset lives here, and where it lands on the device.
# Both halves matter: the source path is relative to this checkout, the
# target path to the app's install directory.
SOURCES = (
    ("textures", "*.png", "textures"),
    ("assets/music", "*.mid", "music"),
)

# Assets that are not swept from a directory, in the order they should
# appear. app.so is the app itself; the icons are named by the "icon"
# object instead and are not listed here.
FIXED = ("app.so",)


def wanted():
    out = [{"source_file": f, "target_file": f} for f in FIXED]
    for src_dir, pattern, target_dir in SOURCES:
        for path in sorted((ROOT / src_dir).glob(pattern)):
            out.append({
                "source_file": f"{target_dir}/{path.name}",
                "target_file": f"{target_dir}/{path.name}",
            })
    return out


def main():
    raw = META.read_text()
    meta = json.loads(raw)

    for app in meta["application"]:
        app["assets"] = wanted()

    out = json.dumps(meta, indent=2) + "\n"
    if out == raw:
        print(f"{META.relative_to(ROOT)}: up to date "
              f"({len(meta['application'][0]['assets'])} assets)")
        return 0

    if "--check" in sys.argv:
        have = {a["source_file"] for a in json.loads(raw)["application"][0]["assets"]}
        want = {a["source_file"] for a in wanted()}
        for f in sorted(want - have):
            print(f"  missing from metadata.json: {f}")
        for f in sorted(have - want):
            print(f"  listed but not on disk:     {f}")
        print("metadata.json is stale -- run 'make metadata'")
        return 1

    META.write_text(out)
    print(f"wrote {META.relative_to(ROOT)} "
          f"({len(meta['application'][0]['assets'])} assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
