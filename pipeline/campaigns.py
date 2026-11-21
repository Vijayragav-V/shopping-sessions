from pathlib import Path

if __package__:
    from .cleaning import _stage
    from .csv_rows import read_rows
else:
    from cleaning import _stage
    from csv_rows import read_rows


def build_campaign_daily(con, dataset_dir):
    """Aggregate credit and assists from sessions and clean_clicks on con."""
    campaign_schema = (("campaign_id", "VARCHAR"), ("channel", "VARCHAR"))
    _stage(con, "campaign_channels", campaign_schema, read_rows(Path(dataset_dir) / "campaigns.csv", 2))

    con.execute("""
        CREATE OR REPLACE TEMP TABLE credited_daily AS
        WITH click_times AS (
            SELECT user_id, ts_ms, arg_min(campaign_id, encode(click_id)) AS campaign_id
            FROM clean_clicks
            GROUP BY user_id, ts_ms
        ), credits AS (
            SELECT s.day, s.revenue_cents,
                   CASE WHEN c.ts_ms >= s.session_start_ms - 604800000
                        THEN c.campaign_id ELSE 'organic' END AS campaign_id
            FROM sessions s
            ASOF LEFT JOIN click_times c
              ON c.user_id = s.user_id AND s.session_start_ms >= c.ts_ms
        )
        SELECT campaign_id, day, count(*) AS sessions,
               count(*) FILTER (WHERE revenue_cents > 0) AS purchasing_sessions,
               sum(revenue_cents)::BIGINT AS revenue_cents
        FROM credits
        GROUP BY campaign_id, day
    """)

    con.execute("""
        CREATE OR REPLACE TEMP TABLE campaign_daily AS
        WITH predecessors AS (
            SELECT user_id, campaign_id, ts_ms,
                   lag(ts_ms) OVER (PARTITION BY user_id, campaign_id ORDER BY ts_ms) AS prev_ms
            FROM clean_clicks
        ), labeled AS (
            SELECT user_id, campaign_id, ts_ms,
                   max(CASE WHEN prev_ms IS NULL OR ts_ms - prev_ms > 604800000
                            THEN ts_ms END)
                       OVER (PARTITION BY user_id, campaign_id ORDER BY ts_ms
                             RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS first_ms
            FROM predecessors
        ), coverage AS (
            -- Merge overlapping seven-day windows before joining to sessions.
            -- A session can match at most one interval for a given campaign.
            SELECT user_id, campaign_id, first_ms, max(ts_ms) + 604800000 AS last_ms
            FROM labeled
            GROUP BY user_id, campaign_id, first_ms
        ), assists AS (
            SELECT c.campaign_id, s.day, count(*) AS assisted_sessions
            FROM coverage c JOIN sessions s
              ON c.user_id = s.user_id AND s.session_start_ms BETWEEN c.first_ms AND c.last_ms
            GROUP BY c.campaign_id, s.day
        ), totals AS (
            SELECT campaign_id, day, coalesce(cr.sessions, 0) AS sessions,
                   coalesce(cr.purchasing_sessions, 0) AS purchasing_sessions,
                   coalesce(cr.revenue_cents, 0) AS revenue_cents,
                   coalesce(a.assisted_sessions, 0) AS assisted_sessions
            FROM credited_daily cr FULL JOIN assists a USING (campaign_id, day)
        )
        SELECT t.campaign_id, coalesce(c.channel, 'organic') AS channel, t.day,
               t.sessions, t.purchasing_sessions, t.revenue_cents, t.assisted_sessions
        FROM totals t LEFT JOIN campaign_channels c USING (campaign_id)
    """)
    con.execute("DROP TABLE credited_daily")
    con.execute("DROP TABLE campaign_channels")
