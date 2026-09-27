#!/usr/bin/env python3
"""Check that an app-repository directory holds everything metadata.json
promises a device that installs from it.

    python3 tools/apprepocheck.py ../tanmatsu-app-repository/<slug>

`make apprepo` runs this after copying, so a missing file is caught here
rather than on a stranger's badge -- which is where the last one was
caught, after a repository install came up with no textures and no music
because the copy step only ever handled the metadata, the icons and
app.so.

Stale files are reported but NOT deleted. The target is a path outside
this checkout, given by a variable, and a tool that removes things from
somewhere it was merely pointed at is a tool waiting to remove the wrong
directory. Renaming an asset therefore leaves its old copy behind; the
warning is there so a person can decide.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
META = ROOT / "metadata" / "metadata.json"


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    repo = Path(sys.argv[1])
    meta = json.loads(META.read_text())

    want = {"metadata.json"}
    want.update(meta["icon"].values())
    for app in meta["application"]:
        want.update(a["target_file"] for a in app["assets"])

    missing = sorted(f for f in want if not (repo / f).is_file())
    have = {str(p.relative_to(repo)) for p in repo.rglob("*") if p.is_file()}
    stale = sorted(have - want)

    for f in missing:
        print(f"  MISSING from {repo}: {f}")
    for f in stale:
        print(f"  stale in {repo} (not deleted): {f}")
    if missing:
        print(f"{len(missing)} file(s) metadata.json promises are not there")
        return 1
    print(f"  {len(want)} file(s) checked, all present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
