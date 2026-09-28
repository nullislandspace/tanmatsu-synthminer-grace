#!/usr/bin/env python3
"""Read SynthMiner's flight recorder (/sd/synthminer/trace.txt).

    make pulltrace                     # fetch it off the card
    python3 tools/traceanalyse.py      # or name a file

The recorder is always on, so the file is what you have AFTER noticing
something rather than a measurement you set up beforehand. This reads
the keys rather than the column order, so a file from an older build
still opens -- the same rule infoanalyse.py follows, and for the same
reason: the format will grow.

What it looks for, in the order it matters:

  DROPS       a frame whose geometry lists filled. Anything here is a
              hole in the picture.
  EDITS WITH NO MESH   a block placed or broken whose chunk section was
              never rebuilt while the trace ran. That is the difference
              between "drawn late" and "never drawn", and it is the
              thing a player reports as "what I placed isn't there".
  SLOW MESHES how long the other edits waited to become visible.
"""

import sys
from pathlib import Path

DEFAULT = Path(__file__).resolve().parent.parent / "trace.txt"
SLOW_MS = 500


def fields(line):
    """key=value pairs, whatever else is on the line."""
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            out[k] = v
    return out


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    if not path.exists():
        sys.exit(f"{path}: not there. `make pulltrace` fetches it off the card.")

    header, ticks, edits, meshes, drops = [], [], [], [], []
    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        if not line:
            continue
        kind = line[0]
        f = fields(line)
        if kind == "H":
            header.append(line[2:])
        elif kind == "T":
            ticks.append(f)
        elif kind in "PB":
            f["kind"] = kind
            edits.append(f)
        elif kind == "M":
            meshes.append(f)
        elif line.startswith("drop="):
            # A continuation of the T above it, so it carries no time of
            # its own; take the one from the second it belongs to.
            if ticks:
                f.setdefault("t", ticks[-1].get("t", "?"))
            drops.append(f)
        elif ticks and "flat" in f:
            ticks[-1].update(f)          # the continuation of the T above

    print(f"=== {path} ===")
    for h in header:
        print(f"  {h}")
    if not ticks and not edits:
        print("  nothing recorded")
        return

    def num(d, k, cast=float, default=0):
        try:
            return cast(d.get(k, default))
        except ValueError:
            return default

    # --- the session ------------------------------------------------
    if ticks:
        t0, t1 = num(ticks[0], "t"), num(ticks[-1], "t")
        fps = [num(d, "fps") for d in ticks if "fps" in d]
        print(f"\n{len(ticks)} seconds recorded, {t0:.0f} to {t1:.0f} s")
        if fps:
            print(f"  fps  min {min(fps):.1f}  mean {sum(fps)/len(fps):.1f}  max {max(fps):.1f}")

        def peak(key):
            best, cap = 0, 0
            for d in ticks:
                if key not in d:
                    continue
                got, _, c = d[key].partition("/")
                best = max(best, int(got or 0))
                cap = int(c or 0)
            return best, cap

        for key, what in (("flat", "flat"), ("tex", "textured")):
            got, cap = peak(key)
            if cap:
                print(f"  {what:9} peak {got}/{cap} ({100*got//cap}%)")

    # --- the things that mean something went wrong ------------------
    print("\n=== is anything missing from the picture? ===")
    dropped = [d for d in ticks if d.get("drop")] + drops
    if dropped:
        print(f"  *** GEOMETRY DROPPED in {len(dropped)} second(s) ***")
        for d in dropped[:10]:
            print(f"      t={d.get('t','?')} drop={d.get('drop')}")
    else:
        print("  no geometry was dropped: the lists never filled")

    # An edit is answered by a mesh for the same chunk and section.
    answered = {(m.get("ch"), m.get("s")): m for m in meshes}
    lonely = [e for e in edits if (e.get("ch"), e.get("s")) not in answered]
    print(f"\n  {len(edits)} edit(s), {len(meshes)} answering mesh(es)")
    if lonely:
        print(f"  *** {len(lonely)} EDIT(S) NEVER REBUILT -- these would not be visible ***")
        for e in lonely[:10]:
            print(f"      {e['kind']} t={e.get('t')} {e.get('blk')} at "
                  f"{e.get('x')},{e.get('y')},{e.get('z')} chunk {e.get('ch')} section {e.get('s')}")
    lags = sorted((int(m["lag"].rstrip("ms")), m) for m in meshes if "lag" in m)
    if lags:
        worst = lags[-1][0]
        med = lags[len(lags) // 2][0]
        print(f"  mesh lag: median {med} ms, worst {worst} ms")
        slow = [m for ms, m in lags if ms >= SLOW_MS]
        if slow:
            print(f"  {len(slow)} edit(s) took {SLOW_MS} ms or more to become visible:")
            for m in slow[-5:]:
                print(f"      t={m.get('t')} chunk {m.get('ch')} section {m.get('s')} {m.get('lag')}")


if __name__ == "__main__":
    main()
