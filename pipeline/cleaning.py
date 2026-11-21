import json
import re
from datetime import datetime
from itertools import islice
from pathlib import Path

if __package__:
    from .csv_rows import read_rows
    from .linked_ids import load_linked_ids
else:
    from csv_rows import read_rows
    from linked_ids import load_linked_ids


ISO_TIME = re.compile(
    r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}"
    r"(?:Z|[+-](?:[01][0-9]|2[0-3]):[0-5][0-9])"
)


def _unsigned(value, maximum):
    if not value.isascii() or not value.isdigit():
        return None
    significant = value.lstrip("0")
    if len(significant) > len(str(maximum)):
        return None
    number = int(significant or "0")
    return number if number <= maximum else None


def _epoch_ms(value):
    # ISO inputs have whole seconds, well within exact floating-point integer range.
    return int(datetime.fromisoformat(value).timestamp()) * 1000


def _timestamp(value):
    if ISO_TIME.fullmatch(value):
        try:
            return _epoch_ms(value)
        except ValueError:
            return None
    return _unsigned(value, 2**63 - 1)


def _event_rows(dataset, identities):
    for path in (dataset / "events").glob("*.csv"):
        for event, user, ts, ingest, kind, product, price in read_rows(path, 7):
            if event:
                yield (event, identities[user], _timestamp(ts), _epoch_ms(ingest), kind,
                       product, _unsigned(price, 10_000_000) if kind == "purchase" else None)


def _click_rows(dataset, identities, campaigns):
    for path in (dataset / "clicks").glob("*.csv"):
        for click, user, campaign, ts in read_rows(path, 4):
            if click:
                yield (click, identities[user], campaign if campaign in campaigns else None,
                       _timestamp(ts))


def _stage(con, name, schema, rows):
    # Send columns in bounded batches. DuckDB owns the spillable cross-file state.
    columns = ", ".join(f"{column} {kind}" for column, kind in schema)
    con.execute(f"CREATE OR REPLACE TEMP TABLE {name} ({columns})")
    values = ", ".join(f"unnest(?::{kind}[])" for _, kind in schema)
    while batch := list(islice(rows, 16384)):
        con.execute(f"INSERT INTO {name} SELECT {values}", [list(col) for col in zip(*batch)])


def build_clean_tables(con, dataset_dir, linked_ids=None):
    """Create clean_events and clean_clicks as temporary tables on con."""
    dataset = Path(dataset_dir)
    with (dataset / "manifest.json").open() as stream:
        manifest = json.load(stream)
    start, end = (_epoch_ms(manifest[key]) for key in ("window_start", "window_end"))
    identities = load_linked_ids(dataset) if linked_ids is None else linked_ids

    event_schema = (
        ("event_id", "VARCHAR"), ("user_id", "VARCHAR"), ("ts_ms", "BIGINT"),
        ("ingest_ms", "BIGINT"), ("event_type", "VARCHAR"), ("product_id", "VARCHAR"),
        ("price_cents", "BIGINT"),
    )
    _stage(con, "parsed_events", event_schema, _event_rows(dataset, identities))
    con.execute("""
        CREATE OR REPLACE TEMP TABLE clean_events AS
        WITH earliest AS (
            SELECT DISTINCT ON (event_id) *
            FROM parsed_events
            ORDER BY event_id, ingest_ms
        )
        SELECT event_id, user_id, ts_ms, event_type, product_id, price_cents
        FROM earliest
        WHERE user_id <> '' AND product_id <> ''
          AND ts_ms >= ? AND ts_ms < ? AND ts_ms >= ingest_ms - 86400000
          AND event_type IN ('view', 'cart', 'purchase')
          AND (event_type <> 'purchase' OR price_cents >= 1)
    """, [start, end])
    con.execute("DROP TABLE parsed_events")

    campaigns = {campaign for campaign, _ in read_rows(dataset / "campaigns.csv", 2)}
    click_schema = (
        ("click_id", "VARCHAR"), ("user_id", "VARCHAR"),
        ("campaign_id", "VARCHAR"), ("ts_ms", "BIGINT"),
    )
    _stage(con, "parsed_clicks", click_schema, _click_rows(dataset, identities, campaigns))
    con.execute("""
        CREATE OR REPLACE TEMP TABLE clean_clicks AS
        WITH unique_clicks AS (SELECT DISTINCT ON (click_id) * FROM parsed_clicks)
        SELECT click_id, user_id, campaign_id, ts_ms
        FROM unique_clicks
        WHERE user_id <> '' AND campaign_id IS NOT NULL
          AND ts_ms >= ? AND ts_ms < ?
    """, [start - 604800000, end])
    con.execute("DROP TABLE parsed_clicks")
