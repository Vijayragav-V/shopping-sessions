#!/usr/bin/env python3
"""
Shopping Sessions: aggregate linked-user activity and campaign attribution by day.

Reads  $DATA_DIR    (default /data)
Writes $OUTPUT_DIR  (default /output): user_daily/ and campaign_daily/ as Parquet
Temp   $SCRATCH_DIR (default /scratch)
"""
import os

import duckdb

from campaigns import build_campaign_daily
from cleaning import build_clean_tables
from sessions import build_session_tables

DATA = os.environ.get("DATA_DIR", "/data")
OUT = os.environ.get("OUTPUT_DIR", "/output")
SCRATCH = os.environ.get("SCRATCH_DIR", "/scratch")
NCPU = int(os.environ.get("NCPU", os.cpu_count() or 1))
MEM_GB = int(os.environ.get("MEM_GB", "8"))

con = duckdb.connect()  # in-memory database
con.execute(f"SET threads = {NCPU}")
con.execute(f"SET memory_limit = '{max(1, int(MEM_GB * 0.8))}GB'")  # leave headroom for Python
con.execute(f"SET temp_directory = '{os.path.join(SCRATCH, 'duckdb_tmp')}'")
con.execute("SET preserve_insertion_order = false")

build_clean_tables(con, DATA)
build_session_tables(con, DATA)
build_campaign_daily(con, DATA)

# Write both tables with the exact schema the checker expects.
os.makedirs(os.path.join(OUT, "user_daily"), exist_ok=True)
os.makedirs(os.path.join(OUT, "campaign_daily"), exist_ok=True)
con.execute(f"""
    COPY user_daily TO '{os.path.join(OUT, "user_daily", "part-0.parquet")}' (FORMAT parquet)
""")
con.execute(f"""
    COPY campaign_daily TO '{os.path.join(OUT, "campaign_daily", "part-0.parquet")}' (FORMAT parquet)
""")
print("wrote user_daily and campaign_daily")
