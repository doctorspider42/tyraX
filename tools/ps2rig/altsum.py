"""altsum.py <bench|district> log... : the alternating renderNow ceiling.
CLDW (per-frame work, 0.1 ms units) precedes its CLD win line. Per window:
mean work of even-index and odd-index frames. bench: cycle-0 drive windows
f=2249 / 2399 (start frame 2100, so index parity = frame parity). district:
every drive window, by arm (arm 1 skips odd frames, arm 0 is the control)."""
import sys, re, collections
mode = sys.argv[1]
acc = collections.defaultdict(lambda: [0.0, 0, 0.0, 0])
for path in sys.argv[2:]:
    series = None
    for line in open(path, encoding="latin-1"):
        if line.startswith("CLDW"):
            series = [int(x) for x in line.split()[1:]]
            continue
        if line.startswith("CLD win") and series is not None:
            kv = dict(re.findall(r"(\w+)=(-?\d+)", line))
            f, n = int(kv["f"]), int(kv["n"])
            start = f - n + 1
            if mode == "bench":
                ok = kv["cyc"] == "0" and kv["st"] == "2" and kv["f"] in ("2249", "2399")
                key = "bench"
            else:
                ok = kv["st"] == "2" and int(kv["cyc"]) >= 1
                key = "arm" + kv["arm"]
            if ok:
                a = acc[key]
                for i, w in enumerate(series):
                    if (start + i) & 1:
                        a[2] += w; a[3] += 1
                    else:
                        a[0] += w; a[1] += 1
            series = None
for k, (se, ne, so, no) in sorted(acc.items()):
    me, mo = se / max(ne, 1) / 10.0, so / max(no, 1) / 10.0
    print("%s even frames %.3f ms (n=%d)  odd frames %.3f ms (n=%d)  odd-even %.3f ms"
          % (k, me, ne, mo, no, mo - me))
