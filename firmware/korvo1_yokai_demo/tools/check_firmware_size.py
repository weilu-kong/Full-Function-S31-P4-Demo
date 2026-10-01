#!/usr/bin/env python3
"""Check partition capacity and keep an explicit application Flash reserve."""
import argparse
import csv
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("--reserve-kib", type=int, default=1024,
                    help="project policy: retain 1 MiB in the application partition")
args = parser.parse_args()
if args.reserve_kib < 0:
    parser.error("reserve must be nonnegative")
project = Path(__file__).resolve().parents[1]
partitions = {}
for row in csv.reader(line for line in (project / "partitions.csv").read_text().splitlines() if line.strip() and not line.lstrip().startswith("#")):
    value = row[4].strip()
    multiplier = 1024 if value.endswith("K") else 1024 * 1024 if value.endswith("M") else 1
    partitions[row[0].strip()] = int(value.rstrip("KM"), 0) * multiplier
for name, image, reserve in (
    ("factory", "korvo1_yokai_demo.bin", args.reserve_kib * 1024),
    ("model", "srmodels/srmodels.bin", 0),
):
    path = args.build / image
    if not path.is_file():
        raise SystemExit(f"Missing image: {path}")
    size = path.stat().st_size
    free = partitions[name] - size
    print(f"{name}: image={size} partition={partitions[name]} free={free} required_reserve={reserve}")
    if free < reserve:
        raise SystemExit(f"{name} violates partition/resource budget")
