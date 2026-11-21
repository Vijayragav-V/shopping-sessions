"""Identify a reference table independently of Parquet encoding, parts and row order."""
import hashlib
from pathlib import Path
import sys

import polars as pl
from run_fixtures import SCHEMAS


def fingerprint(directory, table):
    # All reference counts fit signed 64 bits. Strict casts reject overflow.
    columns = SCHEMAS[table]
    schema = {k: pl.Int64 if t == pl.Int128 else t for k, t in columns.items()}
    keys = [k for k in ("user_id", "campaign_id", "day") if k in columns]
    rows = pl.scan_parquet(str(directory / table / "*.parquet")).select(list(schema)).cast(schema)
    days = rows.select(pl.col("day").unique()).collect()["day"].to_list()
    digest = hashlib.sha256()
    count = 0
    for day in sorted(days):
        part = rows.filter(pl.col("day") == day).sort(keys).collect()
        if part.height != part.n_unique(subset=keys):
            raise ValueError(f"duplicate reference key in {table} on {day}")
        # NDJSON distinguishes a null from an empty string and escapes embedded newlines.
        digest.update(part.write_ndjson().encode("utf-8"))
        count += part.height
    return count, digest.hexdigest()


if __name__ == "__main__":
    seed, folder = sys.argv[1:]
    for table in SCHEMAS:
        count, digest = fingerprint(Path(folder), table)
        print(seed, table, count, digest, flush=True)
