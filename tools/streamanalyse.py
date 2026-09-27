#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""
streamanalyse -- read a .smcap from streamcap.py and say what went wrong.

The point of having arrival times AND full content in one file is that the
interesting failures are only visible when the two are compared:

  * datagrams stop arriving          -> the badge stopped sending
  * datagrams arrive, PTS stops      -> the badge is sending, not producing
  * PTS advances, arrival is bursty  -> the link is stalling, not the encoder
  * continuity counter jumps         -> packets lost between the two

So this reports arrival gaps and stream content on the same timeline, and
says which of those four each gap was.

Stdlib only.  ./streamanalyse.py capture.smcap
"""

import struct, sys, collections

MAGIC = b"SMCAP002\n"
REC = struct.Struct("<QIIHH")
PKT = 188


def read_cap(path):
    d = open(path, "rb").read()
    if not d.startswith(MAGIC):
        sys.exit(f"{path}: not a streamcap file")
    o = len(MAGIC)
    wall, mono0 = struct.unpack_from("<QQ", d, o)
    o += 16
    out = []
    while o + REC.size <= len(d):
        ns, ln, ip, port, _ = REC.unpack_from(d, o)
        o += REC.size
        if o + ln > len(d):
            print("  (truncated final record ignored)")
            break
        out.append((ns, d[o:o + ln]))
        o += ln
    return wall, out


def parse_pts(p, o):
    """33-bit PTS/DTS out of a PES header at offset o."""
    return (((p[o] >> 1) & 7) << 30) | (p[o + 1] << 22) | ((p[o + 2] >> 1) << 15) \
           | (p[o + 3] << 7) | (p[o + 4] >> 1)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    wall, recs = read_cap(sys.argv[1])
    if not recs:
        sys.exit("capture is empty")
    dur = recs[-1][0] / 1e9
    nb = sum(len(p) for _, p in recs)
    print(f"{len(recs)} datagrams, {nb/1e6:.2f} MB, {dur:.1f} s, "
          f"{nb*8/dur/1e6:.2f} Mbit/s average\n")

    # ---- arrival gaps -------------------------------------------------
    gaps = []
    for i in range(1, len(recs)):
        g = (recs[i][0] - recs[i - 1][0]) / 1e6
        if g > 100.0:
            gaps.append((recs[i - 1][0] / 1e9, g))
    print(f"=== arrival gaps over 100 ms: {len(gaps)} ===")
    for t, g in gaps[:15]:
        print(f"  at {t:8.2f} s   {g:9.1f} ms of silence")
    if len(gaps) > 15:
        print(f"  ... and {len(gaps)-15} more")
    print()

    # ---- walk the transport stream ------------------------------------
    cc_prev, cc_err = {}, collections.Counter()
    pcrs, vpts, apts = [], [], []
    bad_sync = 0
    pid_bytes = collections.Counter()
    for ns, pay in recs:
        if len(pay) % PKT:
            bad_sync += 1
        for off in range(0, len(pay) - PKT + 1, PKT):
            p = pay[off:off + PKT]
            if p[0] != 0x47:
                bad_sync += 1
                continue
            pid = ((p[1] & 0x1F) << 8) | p[2]
            pid_bytes[pid] += PKT
            afc = (p[3] >> 4) & 3
            cc = p[3] & 0x0F
            has_pay = bool(afc & 1)
            if pid in cc_prev and has_pay:
                if ((cc_prev[pid] + 1) & 0x0F) != cc:
                    cc_err[pid] += 1
            if has_pay or afc == 2:
                if has_pay:
                    cc_prev[pid] = cc
            o = 4
            if afc & 2:
                aflen = p[4]
                if aflen >= 7 and (p[5] & 0x10):
                    b = p[6:12]
                    base = (b[0] << 25) | (b[1] << 17) | (b[2] << 9) | (b[3] << 1) | (b[4] >> 7)
                    pcrs.append((ns / 1e9, base / 90000.0))
                o = 5 + aflen
            if not has_pay or o >= PKT:
                continue
            if p[1] & 0x40:  # PUSI: a PES header starts here
                q = p[o:]
                if len(q) > 13 and q[0] == 0 and q[1] == 0 and q[2] == 1:
                    flags = q[7]
                    if flags & 0x80 and len(q) >= 14:
                        t = parse_pts(q, 9) / 90000.0
                        (vpts if pid == 0x100 else apts).append((ns / 1e9, t))

    print("=== bytes per pid ===")
    names = {0: "PAT", 0x1000: "PMT", 0x100: "video", 0x101: "audio", 0x102: "PCR"}
    for pid, n in sorted(pid_bytes.items()):
        print(f"  0x{pid:04x} {names.get(pid,''):6s} {n/1e6:8.3f} MB  {n*8/dur/1e6:6.2f} Mbit/s")
    print()
    print(f"=== integrity ===\n  bad sync bytes / short datagrams: {bad_sync}")
    if cc_err:
        for pid, n in cc_err.items():
            print(f"  continuity errors on 0x{pid:04x}: {n}  <-- packets lost in transit")
    else:
        print("  continuity counters: clean on every pid")
    print()

    def cadence(v, label, limit):
        if len(v) < 2:
            print(f"=== {label}: only {len(v)} sample(s) ===\n")
            return
        d = [(v[i+1][1] - v[i][1]) * 1000 for i in range(len(v)-1)]
        over = sum(1 for x in d if x > limit)
        back = sum(1 for x in d if x < 0)
        print(f"=== {label}: {len(v)} samples ===")
        print(f"  step ms: min {min(d):8.1f}  median {sorted(d)[len(d)//2]:8.1f}  max {max(d):9.1f}")
        print(f"  over {limit:g} ms: {over}   NEGATIVE (went backwards): {back}")
        print()

    cadence(pcrs, "PCR (the clock a receiver runs on)", 100)
    cadence(vpts, "video PTS", 500)
    cadence(apts, "audio PTS", 200)

    # ---- the correlation that matters ---------------------------------
    if gaps and vpts:
        # The honest discriminator is arrival MINUS pts. The badge stamps a
        # frame with its own capture time and sends it immediately, so that
        # difference is constant while the link is keeping up, and it GROWS
        # the moment anything between here and there starts buffering. A
        # gap with no change in skew was the badge not producing a frame;
        # only a rising skew is the link's fault.
        skew = [(a - p) * 1000.0 for a, p in vpts]
        print("=== is it the link, or the badge? (arrival minus pts) ===")
        print(f"  skew ms: first {skew[0]:.0f}  last {skew[-1]:.0f}  "
              f"min {min(skew):.0f}  max {max(skew):.0f}  drift {skew[-1]-skew[0]:+.0f}")
        print("  a constant skew means nothing buffered on the way: every gap in"
              "\n  the picture is a frame the badge never made.\n")

        print("=== what each arrival gap actually was ===")
        for t, g in gaps[:10]:
            before = [p for a, p in vpts if a <= t]
            after = [p for a, p in vpts if a >= t + g / 1000.0]
            if before and after:
                produced = (after[0] - before[-1]) * 1000.0
                print(f"  {t:8.2f}s  gap {g:8.1f} ms, video advanced {produced:8.1f} ms"
                      f"  ({produced/g*100:3.0f}% of real time)")
        print()

    if vpts and apts:
        print("=== audio against video, on the stream's own clock ===")
        print(f"{'arrival(s)':>11} {'video pts':>10} {'audio pts':>10} {'a-v(ms)':>9}")
        step = max(len(apts) // 12, 1)
        for k in range(0, len(apts), step):
            at, ap = apts[k]
            cand = [p for a, p in vpts if a <= at]
            if cand:
                print(f"{at:11.2f} {cand[-1]:10.3f} {ap:10.3f} {(ap-cand[-1])*1000:9.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
