from pathlib import Path

if __package__:
    from .cleaning import _stage
    from .csv_rows import read_rows
else:
    from cleaning import _stage
    from csv_rows import read_rows


def build_session_tables(con, dataset_dir):
    """Create sessions and user_daily from this connection's clean_events."""
    con.execute("""
        CREATE OR REPLACE TEMP TABLE session_products AS
        WITH gaps AS (
            SELECT user_id, ts_ms, product_id, price_cents,
                   lag(ts_ms) OVER (PARTITION BY user_id ORDER BY ts_ms) AS previous_ms
            FROM clean_events
        ), labeled AS (
            SELECT user_id, price_cents,
                   CASE WHEN price_cents IS NOT NULL THEN product_id END AS purchased_product,
                   max(CASE WHEN previous_ms IS NULL OR ts_ms - previous_ms > 1800000
                            THEN ts_ms END)
                       OVER (PARTITION BY user_id ORDER BY ts_ms
                             RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS session_start_ms
            FROM gaps
        )
        -- RANGE includes every peer at the same millisecond, so each gets the
        -- boundary even when only one peer's predecessor was across the gap.
        SELECT user_id, session_start_ms, purchased_product, count(*) AS events,
               coalesce(sum(price_cents), 0)::BIGINT AS revenue_cents
        FROM labeled
        GROUP BY user_id, session_start_ms, purchased_product
    """)
    con.execute("""
        CREATE OR REPLACE TEMP TABLE sessions AS
        SELECT user_id, session_start_ms, epoch_ms(session_start_ms)::DATE AS day,
               sum(events)::BIGINT AS events, sum(revenue_cents)::BIGINT AS revenue_cents
        FROM session_products
        GROUP BY user_id, session_start_ms
    """)

    product_schema = (("product_id", "VARCHAR"), ("category", "VARCHAR"))
    _stage(con, "product_categories", product_schema, read_rows(Path(dataset_dir) / "products.csv", 2))
    con.execute("""
        CREATE OR REPLACE TEMP TABLE user_daily AS
        WITH totals AS (
            SELECT user_id, day, count(*) AS sessions, sum(events)::BIGINT AS events,
                   sum(revenue_cents)::BIGINT AS revenue_cents
            FROM sessions
            GROUP BY user_id, day
        ), category_totals AS (
            SELECT sp.user_id, epoch_ms(sp.session_start_ms)::DATE AS day,
                   coalesce(pc.category, 'unknown') AS category, sum(sp.revenue_cents) AS cents
            FROM session_products sp
            LEFT JOIN product_categories pc ON pc.product_id = sp.purchased_product
            WHERE sp.purchased_product IS NOT NULL
            GROUP BY ALL
        ), winners AS (
            SELECT user_id, day, category AS top_category
            FROM category_totals
            QUALIFY row_number() OVER (
                PARTITION BY user_id, day ORDER BY cents DESC, encode(category)
            ) = 1
        )
        SELECT totals.*, winners.top_category
        FROM totals LEFT JOIN winners USING (user_id, day)
    """)
    con.execute("DROP TABLE session_products")
    con.execute("DROP TABLE product_categories")
