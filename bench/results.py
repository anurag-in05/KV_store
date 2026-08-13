"""kvstore_bench results, captured on a 10-physical-core machine,
Release build. Analysis: docs/benchmark-results.md. Raw text:
bench/results.txt.

Each record is one (workload, thread count) run.
"""

RESULTS = [
    {"workload": "SET only", "threads": 1, "total_ops": 200000, "elapsed_ms": 17.0, "ops_per_sec": 11752262},
    {"workload": "SET only", "threads": 2, "total_ops": 400000, "elapsed_ms": 45.1, "ops_per_sec": 8861230},
    {"workload": "SET only", "threads": 4, "total_ops": 800000, "elapsed_ms": 135.2, "ops_per_sec": 5917587},
    {"workload": "SET only", "threads": 8, "total_ops": 1600000, "elapsed_ms": 966.2, "ops_per_sec": 1655911},
    {"workload": "SET only", "threads": 16, "total_ops": 3200000, "elapsed_ms": 4789.4, "ops_per_sec": 668136},

    {"workload": "GET only", "threads": 1, "total_ops": 200000, "elapsed_ms": 6.5, "ops_per_sec": 30687983},
    {"workload": "GET only", "threads": 2, "total_ops": 400000, "elapsed_ms": 31.6, "ops_per_sec": 12654207},
    {"workload": "GET only", "threads": 4, "total_ops": 800000, "elapsed_ms": 124.0, "ops_per_sec": 6450757},
    {"workload": "GET only", "threads": 8, "total_ops": 1600000, "elapsed_ms": 824.1, "ops_per_sec": 1941444},
    {"workload": "GET only", "threads": 16, "total_ops": 3200000, "elapsed_ms": 3808.6, "ops_per_sec": 840212},

    {"workload": "90% GET / 10% SET", "threads": 1, "total_ops": 200000, "elapsed_ms": 6.6, "ops_per_sec": 30210718},
    {"workload": "90% GET / 10% SET", "threads": 2, "total_ops": 400000, "elapsed_ms": 35.1, "ops_per_sec": 11401073},
    {"workload": "90% GET / 10% SET", "threads": 4, "total_ops": 800000, "elapsed_ms": 129.8, "ops_per_sec": 6164357},
    {"workload": "90% GET / 10% SET", "threads": 8, "total_ops": 1600000, "elapsed_ms": 910.3, "ops_per_sec": 1757608},
    {"workload": "90% GET / 10% SET", "threads": 16, "total_ops": 3200000, "elapsed_ms": 4188.6, "ops_per_sec": 763972},
]

if __name__ == "__main__":
    for row in RESULTS:
        print(f"{row['workload']:<20} {row['threads']:>3} threads  {row['ops_per_sec']:>12,} ops/s")
