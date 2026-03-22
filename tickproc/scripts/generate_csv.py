#!/usr/bin/env python3
"""Generate a large dummy CSV file of financial tick data.

Usage:
    python generate_csv.py                  # 1 GB default
    python generate_csv.py --size-gb 2.5    # 2.5 GB
    python generate_csv.py --output big.csv --size-gb 0.1

The output format matches what tickproc expects:
    timestamp,symbol,price,volume
"""

import argparse
import os
import random
import sys
import time
from datetime import datetime, timedelta

# ── Symbols and their base prices ──────────────────────────────────────────

SYMBOLS = [
    ("AAPL",  185.00),
    ("MSFT",  410.00),
    ("GOOG",  141.00),
    ("AMZN",  178.00),
    ("TSLA",  245.00),
    ("NVDA",  880.00),
    ("META",  475.00),
    ("JPM",   195.00),
    ("V",     275.00),
    ("JNJ",   156.00),
    ("WMT",    58.00),
    ("PG",    165.00),
    ("UNH",   525.00),
    ("HD",    370.00),
    ("BAC",    34.00),
    ("XOM",   105.00),
    ("PFE",    27.00),
    ("COST",  725.00),
    ("ABBV",  165.00),
    ("CVX",   155.00),
]


def generate_csv(output_path: str, target_bytes: int, seed: int = 42) -> None:
    """Write a CSV file of approximately `target_bytes` size."""
    rng = random.Random(seed)

    # Pre-compute to avoid per-row overhead
    symbols = [s for s, _ in SYMBOLS]
    base_prices = {s: p for s, p in SYMBOLS}

    # Start at market open
    ts = datetime(2024, 1, 15, 9, 30, 0, 0)
    ts_delta = timedelta(microseconds=1)

    # Estimate average row length to pre-calculate row count
    # "2024-01-15T09:30:00.000000,AAPL,185.32,1500\n" ≈ 48 bytes
    avg_row_len = 48
    estimated_rows = target_bytes // avg_row_len

    written = 0
    row_count = 0
    report_interval = max(1, estimated_rows // 20)  # report every 5%

    t0 = time.time()

    with open(output_path, "w", buffering=1 << 20) as f:  # 1 MB buffer
        # Header
        header = "timestamp,symbol,price,volume\n"
        f.write(header)
        written += len(header)

        while written < target_bytes:
            sym = rng.choice(symbols)
            base = base_prices[sym]

            # Realistic price: base ± 2% random walk
            price = base * (1.0 + rng.gauss(0, 0.005))
            price = max(0.01, round(price, 2))

            # Volume: log-normal distribution (many small, few large)
            volume = max(1, int(rng.lognormvariate(5.5, 1.5)))

            # Timestamp with microsecond precision
            ts_str = ts.strftime("%Y-%m-%dT%H:%M:%S.%f")

            line = f"{ts_str},{sym},{price:.2f},{volume}\n"
            f.write(line)
            written += len(line)
            row_count += 1

            ts += ts_delta

            if row_count % report_interval == 0:
                pct = min(100.0, 100.0 * written / target_bytes)
                elapsed = time.time() - t0
                rate = written / (1024 * 1024 * max(elapsed, 0.001))
                sys.stderr.write(
                    f"\r  {pct:5.1f}%  |  {written / (1024**3):.2f} GB  "
                    f"|  {row_count:,} rows  |  {rate:.0f} MB/s"
                )

    elapsed = time.time() - t0
    actual_gb = written / (1024 ** 3)
    sys.stderr.write(
        f"\r  Done!  {actual_gb:.2f} GB  |  {row_count:,} rows  "
        f"|  {elapsed:.1f}s  |  {written / (1024**2) / max(elapsed, 0.001):.0f} MB/s\n"
    )

    print(f"Output: {os.path.abspath(output_path)}")
    print(f"  Size:  {actual_gb:.2f} GB ({written:,} bytes)")
    print(f"  Rows:  {row_count:,}")
    print(f"  Time:  {elapsed:.1f}s")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate dummy financial tick-data CSV files"
    )
    parser.add_argument(
        "--output", "-o",
        default="tick_data.csv",
        help="Output file path (default: tick_data.csv)"
    )
    parser.add_argument(
        "--size-gb",
        type=float,
        default=1.0,
        help="Target file size in GB (default: 1.0)"
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed for reproducibility (default: 42)"
    )
    args = parser.parse_args()

    target_bytes = int(args.size_gb * 1024 ** 3)
    print(f"Generating {args.size_gb:.1f} GB of tick data → {args.output}")
    generate_csv(args.output, target_bytes, args.seed)


if __name__ == "__main__":
    main()
