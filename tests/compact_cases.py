"""Regression fixtures for compact storage and long, cross-midnight timelines."""
from datetime import datetime
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STAMP = "2026-10-03T23:59:59Z"
START = int(datetime.fromisoformat(STAMP.replace("Z", "+00:00")).timestamp()) * 1000 + 999
GAP = 1_800_000


def write(path, header, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as out:
        print(header, file=out)
        for row in rows:
            print(*row, sep=",", file=out)


def case(root, categories, end):
    data = root / "input"
    write(data / "products.csv", "product_id,category",
          ((f"p{i}", f"k{i:03}") for i in range(1, categories + 1)))
    write(data / "campaigns.csv", "campaign_id,channel",
          [("c_1", "ads"), ("c_01", "web"), ("c_1000000", "mail")])
    write(data / "links/part.csv", "user_id_a,user_id_b", [("u2", "u1"), ("u1", "person")])
    (data / "manifest.json").write_text(json.dumps({
        "window_start": "2026-10-01T00:00:00Z", "window_end": end
    }))
    events = [(f"e{i}", "u2", START, STAMP, "view", "p1", "") for i in range(150)]
    events.extend([("paid", "u1", START + GAP, STAMP, "purchase", f"p{categories}", 10000000),
                   ("other", "u2", START + GAP, STAMP, "purchase", "absent", 10000000),
                   ("split", "u2", START + 2 * GAP + 1, STAMP, "view", "p1", "")])
    write(data / "events/part.csv", "event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents",
          reversed(events))
    write(data / "clicks/part.csv", "click_id,user_id,campaign_id,ts", [
        ("k1", "u1", "c_01", START), ("k10", "u1", "c_1", START),
        ("k123456789", "u2", "c_1000000", START), ("k1", "u1", "c_01", START)])
    write(root / "expected/user_daily.csv", "user_id,day,sessions,events,revenue_cents,top_category", [
        ("person", "2026-10-03", 1, 152, 20000000, f"k{categories:03}"),
        ("person", "2026-10-04", 1, 1, 0, "")])
    write(root / "expected/campaign_daily.csv",
          "campaign_id,channel,day,sessions,purchasing_sessions,revenue_cents,assisted_sessions",
          [(cid, channel, day, int(win), int(win and first), 20000000 if win and first else 0, 1)
           for day, first in [("2026-10-03", True), ("2026-10-04", False)]
           for cid, channel, win in [("c_1", "ads", False), ("c_01", "web", True),
                                     ("c_1000000", "mail", False)]])


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        for name, cats, end in [("compact", 255, "2026-10-15"), ("category-limit", 256, "2026-10-15"),
                                ("wide-window", 8, "2400-01-01")]:
            case(Path(tmp) / name, cats, end + "T00:00:00Z")
        subprocess.run([sys.executable, ROOT / "scripts/run_fixtures.py", tmp, "--", *sys.argv[1:]],
                       check=True)
