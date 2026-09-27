#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""
streamcap -- capture the badge's livestream losslessly, with arrival times.

Run this on the machine the Tanmatsu is plugged into. It listens on the same
UDP port OBS would, and records EVERY DATAGRAM IN FULL together with the
instant it arrived, because the two questions we have need both:

  * content -- is the transport stream itself malformed, are there gaps in
    the continuity counters, do the timestamps make sense;
  * timing -- when did datagrams stop arriving, for how long, and what was
    the stream doing either side of that.

Nothing is sampled, truncated or summarised. The raw bytes go to a .smcap
file with one timestamped record per datagram, and a plain .ts is written
alongside so ffmpeg and ffprobe can be pointed straight at it.

IT ALSO REPORTS WHETHER THE CAPTURE ITSELF LOST ANYTHING. A UDP receiver
that cannot keep up drops datagrams in the kernel and says nothing, which
would look exactly like the badge stalling. The socket buffer is set as
large as the system allows and the kernel's own drop counter is read at the
end; if it is not zero, the capture is suspect and says so.

Stdlib only -- no pip install, any Python 3.

  ./streamcap.py                      # capture until Ctrl-C
  ./streamcap.py --secs 120           # or for two minutes
  ./streamcap.py --scp user@host:/tmp # and copy it somewhere when done
"""

import argparse, os, socket, struct, subprocess, sys, time

MAGIC = b"SMCAP002\n"
REC = struct.Struct("<QIIHH")  # mono_ns, len, srcip, srcport, pad


def rcvbuf_drops(sock):
    """The kernel's drop counter for this socket, or None if unreadable.
    Without this a capture that could not keep up looks like a badge that
    stopped sending."""
    try:
        import array
        # SO_MEMINFO (Linux): index 9 is sk_drops on kernels that have it.
        buf = sock.getsockopt(socket.SOL_SOCKET, 46, 4 * 16)  # SO_MEMINFO
        vals = array.array("I")
        vals.frombytes(buf)
        if len(vals) > 9:
            return int(vals[9])
    except Exception:
        pass
    try:
        want = sock.getsockname()[1]
        with open("/proc/net/udp") as f:
            next(f)
            for ln in f:
                c = ln.split()
                if int(c[1].split(":")[1], 16) == want:
                    return int(c[12])
    except Exception:
        pass
    return None


def main():
    ap = argparse.ArgumentParser(description="Capture the badge livestream, losslessly.")
    ap.add_argument("--port", type=int, default=5000)
    ap.add_argument("--secs", type=float, default=0.0, help="0 = until Ctrl-C")
    ap.add_argument("--out", default=".", help="directory for the capture files")
    ap.add_argument("--name", default=None, help="basename (default: stream-<date>)")
    ap.add_argument("--scp", default=None, metavar="TARGET", help="scp the .smcap here when done")
    ap.add_argument("--put", default=None, metavar="URL", help="HTTP PUT the .smcap here when done")
    ap.add_argument("--no-ts", action="store_true", help="skip the plain .ts copy")
    a = ap.parse_args()

    base = a.name or time.strftime("stream-%Y%m%d-%H%M%S")
    cap_path = os.path.join(a.out, base + ".smcap")
    ts_path = os.path.join(a.out, base + ".ts")

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # As much socket buffer as the system will give. Anything the kernel
    # drops here is indistinguishable from the badge not sending it.
    for want in (64 << 20, 32 << 20, 16 << 20, 8 << 20, 4 << 20):
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, want)
            break
        except OSError:
            continue
    got = s.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    s.bind(("0.0.0.0", a.port))
    s.settimeout(0.5)

    print(f"listening on udp/{a.port}, socket buffer {got // 1024} KiB")
    print(f"  {cap_path}")
    if not a.no_ts:
        print(f"  {ts_path}")
    print("Ctrl-C to stop." if a.secs == 0 else f"stopping after {a.secs:g} s.")
    drops0 = rcvbuf_drops(s)

    cap = open(cap_path, "wb", buffering=1 << 20)
    ts = None if a.no_ts else open(ts_path, "wb", buffering=1 << 20)
    t_wall = time.time_ns()
    t0 = time.monotonic_ns()
    cap.write(MAGIC + struct.pack("<QQ", t_wall, t0))

    n = 0
    nbytes = 0
    last_ns = None
    worst_gap_ms = 0.0
    worst_at = 0.0
    t_report = time.monotonic()
    stopping = False
    try:
        while not stopping:
            try:
                data, addr = s.recvfrom(65535)
            except socket.timeout:
                if a.secs and (time.monotonic_ns() - t0) / 1e9 >= a.secs:
                    break
                continue
            now = time.monotonic_ns()
            if last_ns is not None:
                gap = (now - last_ns) / 1e6
                if gap > worst_gap_ms:
                    worst_gap_ms = gap
                    worst_at = (now - t0) / 1e9
            last_ns = now
            ip = struct.unpack("<I", socket.inet_aton(addr[0]))[0]
            cap.write(REC.pack(now - t0, len(data), ip, addr[1], 0))
            cap.write(data)
            if ts is not None:
                ts.write(data)
            n += 1
            nbytes += len(data)

            if time.monotonic() - t_report >= 1.0:
                el = (now - t0) / 1e9
                print(f"\r  {el:7.1f}s  {n:7d} dgram  {nbytes/1e6:7.2f} MB  "
                      f"{nbytes*8/el/1e6:5.2f} Mbit/s  worst gap {worst_gap_ms:6.1f} ms @ {worst_at:.1f}s   ",
                      end="", flush=True)
                t_report = time.monotonic()
            if a.secs and (now - t0) / 1e9 >= a.secs:
                stopping = True
    except KeyboardInterrupt:
        print("\n  stopped")

    cap.close()
    if ts is not None:
        ts.close()
    drops1 = rcvbuf_drops(s)
    s.close()

    el = max((time.monotonic_ns() - t0) / 1e9, 1e-9)
    print(f"\n{n} datagrams, {nbytes/1e6:.2f} MB in {el:.1f} s "
          f"({nbytes*8/el/1e6:.2f} Mbit/s), worst arrival gap {worst_gap_ms:.1f} ms at {worst_at:.1f} s")
    if drops0 is not None and drops1 is not None:
        d = drops1 - drops0
        if d:
            print(f"*** WARNING: the KERNEL dropped {d} datagram(s) -- this capture is "
                  f"missing data and gaps in it may be mine, not the badge's ***")
        else:
            print("kernel dropped nothing: the capture is complete")
    else:
        print("could not read the kernel drop counter; treat gaps with mild suspicion")
    print(f"\ncapture: {cap_path}")

    if a.scp:
        print(f"scp -> {a.scp}")
        subprocess.run(["scp", cap_path, a.scp], check=False)
    if a.put:
        import urllib.request
        with open(cap_path, "rb") as f:
            req = urllib.request.Request(a.put, data=f.read(), method="PUT")
            try:
                with urllib.request.urlopen(req) as r:
                    print(f"PUT {a.put} -> {r.status}")
            except Exception as e:
                print(f"PUT failed: {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
