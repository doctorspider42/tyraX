"""lapsum.py log... : headline-benchmark laps (bench_patch.py). Takes the
cycle-0 drive windows f=2249 and f=2399 (drive frames 0-300) of every log,
averages them, prints work / p50 / p95 and every CLDS scope as ms a frame,
I/D misses a frame and the miss share at pI 51 / pD 16 cycles (294.912 MHz)."""
import sys, re, collections
MHZ = 294.912
win = collections.defaultdict(list)
scopes = collections.defaultdict(lambda: [0.0, 0.0, 0.0, 0])
nwin = 0
for path in sys.argv[1:]:
    take = False
    for line in open(path, encoding="latin-1"):
        if line.startswith("CLD win"):
            kv = dict(re.findall(r"(\w+)=(-?\d+)", line))
            take = kv["cyc"] == "0" and kv["st"] == "2" and kv["f"] in ("2249", "2399")
            if take:
                nwin += 1
                for k in ("work", "p50", "p95", "late"):
                    win[k].append(int(kv[k]))
            continue
        if take and line.startswith("CLDS "):
            p = line.split()
            name, us = p[2], int(p[3])
            im = int(re.search(r"im=(\d+)", line).group(1))
            dm = int(re.search(r"dm=(\d+)", line).group(1))
            s = scopes[name]
            s[0] += us; s[1] += im; s[2] += dm; s[3] += 1
print("windows", nwin)
for k, v in win.items():
    print("%s %.3f" % (k, sum(v) / len(v) / (1000.0 if k != "late" else 1)))
rows = []
for name, (us, im, dm, n) in scopes.items():
    ms = us / nwin / 1000.0
    imf, dmf = im / nwin, dm / nwin
    miss = (imf * 51 + dmf * 16) / MHZ / 1000.0
    rows.append((ms, name, imf, dmf, miss))
for ms, name, imf, dmf, miss in sorted(rows, reverse=True):
    share = 100.0 * miss / ms if ms > 0 else 0.0
    print("%-28s %7.3f ms  I %7.0f  D %7.0f  miss %5.1f %%" % (name, ms, imf, dmf, share))
