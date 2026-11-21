"""Compare every final-size output row with bounded memory, one UTC day at a time."""
from pathlib import Path
import sys
import polars as pl

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from run_fixtures import SCHEMAS

def scan(folder, table, schema):
    files = sorted((folder / table).glob("*.parquet"))
    assert files
    for path in files:
        actual = pl.read_parquet_schema(path)
        assert set(actual) == set(schema), path
        for col, dtype in schema.items():
            assert (actual[col] == dtype or
                    dtype == pl.Int128 and actual[col].is_integer() or
                    dtype == pl.String and actual[col].base_type() in (pl.String, pl.Categorical, pl.Enum)), (path, col, actual[col])
    return pl.scan_parquet(files).select(list(schema)).cast(schema)

for table, schema in SCHEMAS.items():
    a, b = [scan(Path(folder), table, schema) for folder in sys.argv[1:]]
    keys = [key for key in ("user_id", "campaign_id", "day") if key in schema]
    days = [set(x.select(pl.col("day").unique()).collect()["day"]) for x in (a, b)]
    assert days[0] == days[1] and None not in days[0]
    total = 0
    for day in sorted(days[0]):
        left, right = [x.filter(pl.col("day") == day).collect().sort(keys) for x in (a, b)]
        assert left.height == left.n_unique(subset=keys) and left.equals(right), (table, day)
        total += left.height
    print(f"{table} MATCH across {total} rows", flush=True)
