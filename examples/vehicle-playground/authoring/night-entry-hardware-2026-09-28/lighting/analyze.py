from pathlib import Path
import csv, statistics
root = Path(__file__).resolve().parent
for family, arms in [("production", ["control", "fixed", "repeat"]), ("diagnostic", ["control", "eligible", "cache", "repeat"])]:
    for arm in arms:
        rows = list(csv.DictReader((root / family / arm / "frame-cost.csv").open()))[720:960]
        work = sorted(float(r["total_ms"]) - float(r["present_ms"]) for r in rows)
        print(f"{family}/{arm}: median={statistics.median(work):.6f} ms p95={work[227]:.6f} ms over20={sum(v > 20 for v in work)}/240")
