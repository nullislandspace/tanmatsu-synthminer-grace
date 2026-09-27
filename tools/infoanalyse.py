#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""
infoanalyse -- read /sd/defuckinfo.txt (see `make pullinfo`) and say where
the stream task's time went.

The counters are cumulative, so every rate here is a difference between two
rows. Columns are read from the file's own header, so an older capture with
fewer columns still works -- the accounting block is simply skipped.

THE POINT IS THE RESIDUAL. `pass` is top-of-loop to top-of-loop; pcr, atk,
amx, enc and mux are the calls inside it. What pass does not account for is
time the task was not running at all, and that number decides between "the
badge is too slow" and "the badge never got the CPU" -- which call for
opposite fixes, and which reading only maxima cannot distinguish.

  ./infoanalyse.py [defuckinfo.txt] [--bucket 10]
"""

import sys


def load(path):
    head, rows = None, []
    for line in open(path):
        if line.startswith("#"):
            continue
        if head is None:
            head = line.split()
            continue
        f = line.split()
        if len(f) == len(head):
            rows.append([int(x) for x in f])
    if not rows:
        sys.exit(f"{path}: no samples")
    return {k: n for n, k in enumerate(head)}, rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0] if args else "defuckinfo.txt"
    bucket = 10.0
    if "--bucket" in sys.argv:
        bucket = float(sys.argv[sys.argv.index("--bucket") + 1])
    i, rows = load(path)
    have = lambda *k: all(x in i for x in k)

    dur = rows[-1][i["ms"]] / 1000.0
    t = rows[-1]
    print(f"{len(rows)} samples over {dur:.1f} s\n")

    print("=== totals ===")
    for k in ("pub", "drop", "enc", "key", "encerr", "ppaerr", "dgram", "dgfail"):
        if k in i:
            print(f"  {k:8s} {t[i[k]]:10d}")
    if "auddrop" in i:
        lost = t[i["auddrop"]] // 1152
        print(f"  auddrop  {lost:10d} frames  ({lost * 0.052245:.1f} s of sound)")

    print("\n=== average cost of each call (us) ===")
    for k, n in (("ppa", "ppan"), ("enc", "encn"), ("mux", "muxn"), ("aud", "audn"),
                 ("pass", "passn"), ("pcr", "pcrn"), ("atk", "atkn"), ("amx", "amxn"),
                 ("copy", "costn"), ("cenc", "costn")):
        if have(k + "sum", n):
            c = t[i[n]]
            mx = f"  max {t[i[k+'max']]:8d}" if k + "max" in i else ""
            print(f"  {k:5s} {t[i[k+'sum']]/max(c,1):9.1f} over {c:7d} calls{mx}")

    print(f"\n=== per {bucket:.0f} s ===")
    hdr = "  window   | offer/s enc/s drop/s | push/s | pass/s"
    if have("passsum", "passn"):
        hdr += " | pass ms  work ms  STARVED ms  (%)"
    print(hdr)
    b = 0.0
    while b < dur * 1000:
        w = [r for r in rows if b <= r[i["ms"]] < b + bucket * 1000]
        if len(w) >= 2:
            a, z = w[0], w[-1]
            dt = (z[i["ms"]] - a[i["ms"]]) / 1000.0
            d = lambda k: z[i[k]] - a[i[k]]
            line = (f"  {b/1000:4.0f}-{b/1000+bucket:4.0f} | {d('pub')/dt:7.1f} "
                    f"{d('enc')/dt:5.1f} {d('drop')/dt:6.1f} | {d('audn')/dt:6.1f} | "
                    f"{d('pcronly')/dt:6.1f}")
            if have("copysum", "costn") and d("costn"):
                line += (f" | copy {d('copysum')/d('costn')/1000:6.2f} "
                         f"codec {d('cencsum')/d('costn')/1000:7.2f} ms")
            if have("passsum", "passn") and d("passn"):
                npass = d("passn")
                p = d("passsum") / npass / 1000.0
                work = sum(d(k + "sum") for k in ("pcr", "atk", "amx", "enc", "mux")
                           if k + "sum" in i) / npass / 1000.0
                line += (f" | {p:7.1f} {work:8.2f} {p-work:11.1f}  "
                         f"{100*(p-work)/p:3.0f}%")
            print(line)
        b += bucket * 1000
    print("\n  work ms is pcr+atk+amx+enc+mux per pass -- everything the task DID.")
    print("  STARVED is the rest of the pass: time it was not on the CPU at all.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
