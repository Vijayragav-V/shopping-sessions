"""Check distinct catalog spellings, sparse identifiers and long CSV records."""
import csv
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path

import polars as pl


def write_csv(path, columns, rows):
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(columns)
        writer.writerows(rows)


def check(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        root.chmod(0o755)
        for folder in ("events", "clicks", "links"):
            (root / folder).mkdir()
        (root / "manifest.json").write_text(
            '{"window_start":"1970-01-01T00:00:00Z","window_end":"1970-01-02T00:00:00Z"}'
        )
        keys = ["1", "01", "0", "00000000001", "987654321", "x", "1 ", "ünicode"]
        products = [("p" + key, "category" + key) for key in keys]
        campaigns = [("c_" + key, "channel" + key) for key in keys]
        write_csv(root / "products.csv", ["product_id", "category"], products)
        write_csv(root / "campaigns.csv", ["campaign_id", "channel"], campaigns)
        write_csv(root / "links/empty.csv", ["user_id_a", "user_id_b"], [])
        events, clicks, expected_users, expected_campaigns = [], [], [], []
        when = "1970-01-01T01:00:00Z"
        for i, ((product, category), (campaign, channel)) in enumerate(zip(products, campaigns)):
            user = f"u{i}"
            # This last record crosses multiple 512 KiB chunk boundaries.
            event_id = "e" + ("L" * 1_100_000 if i + 1 == len(keys) else str(i))
            events.append((event_id, user, when, when, "purchase", product, str(i + 1)))
            clicks.append((f"k{i}", user, campaign, when))
            expected_users.append((user, date(1970, 1, 1), 1, 1, i + 1, category))
            expected_campaigns.append((campaign, channel, date(1970, 1, 1), 1, 1, i + 1, 1))
        write_csv(root / "events/part.csv",
                  ["event_id", "user_id", "ts", "ingest_ts", "event_type", "product_id", "price_cents"], events)
        write_csv(root / "clicks/part.csv", ["click_id", "user_id", "campaign_id", "ts"], clicks)
        output = root / "output"
        subprocess.run([binary, root, output], check=True)
        for name, columns, expected in [
            ("user_daily", ["user_id", "day", "sessions", "events", "revenue_cents", "top_category"], expected_users),
            ("campaign_daily", ["campaign_id", "channel", "day", "sessions", "purchasing_sessions",
                                "revenue_cents", "assisted_sessions"], expected_campaigns),
        ]:
            actual = pl.read_parquet(output / name / "*.parquet", columns=columns).rows()
            if sorted(expected) != sorted(actual):
                raise AssertionError((name, expected, actual))


if __name__ == "__main__":
    check(Path(sys.argv[1]).resolve())
