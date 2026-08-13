# Benchmark results — does the Phase 4 lock scale?

`bench/bench.cpp` measures `KVStore` directly (no sockets), so the
numbers isolate the lock's cost from network/thread-spawn overhead.
Methodology: 3 workloads × thread counts {1, 2, 4, 8, 16}, all threads
contending on the same pool of 100 keys (deliberately worst-case —
real traffic spread across more keys would contend less). Machine: 10
physical cores. Built with `-DCMAKE_BUILD_TYPE=Release` — an
unoptimized build would measure debug overhead, not the lock.

## Results

```
workload             threads     total ops        elapsed       throughput
--------------------------------------------------------------------------
SET only                   1        200000        17.0 ms         11752262 ops/s
SET only                   2        400000        45.1 ms          8861230 ops/s
SET only                   4        800000       135.2 ms          5917587 ops/s
SET only                   8       1600000       966.2 ms          1655911 ops/s
SET only                  16       3200000      4789.4 ms           668136 ops/s

GET only                   1        200000         6.5 ms         30687983 ops/s
GET only                   2        400000        31.6 ms         12654207 ops/s
GET only                   4        800000       124.0 ms          6450757 ops/s
GET only                   8       1600000       824.1 ms          1941444 ops/s
GET only                  16       3200000      3808.6 ms           840212 ops/s

90% GET / 10% SET          1        200000         6.6 ms         30210718 ops/s
90% GET / 10% SET          2        400000        35.1 ms         11401073 ops/s
90% GET / 10% SET          4        800000       129.8 ms          6164357 ops/s
90% GET / 10% SET          8       1600000       910.3 ms          1757608 ops/s
90% GET / 10% SET         16       3200000      4188.6 ms           763972 ops/s
```

## Reading it

**Throughput doesn't scale with threads — it collapses.** Going from 1
to 16 threads on GET is a 40x drop in aggregate ops/sec (30.7M → 0.84M),
despite 16x the workers. More threads made the store *slower*, not
faster: every added thread spends more of its time waiting on the lock
and paying context-switch cost, not doing useful work. This is the
"the benchmark number is the point" DESIGN.md asked for — the lock is
unambiguously the bottleneck under contention.

**GET-only and the 90/10 mix are nearly identical curves.** That's the
direct, measured consequence of `get`/`exists` taking the *exclusive*
lock instead of a shared one (see `DESIGN.md`, "Phase 4: locking"): a
read-heavy workload gets zero benefit over a write-heavy one today,
because nothing about it is actually read-only from the lock's
perspective.

## What this suggests — not yet decided

Two different fixes address two different things, and the data points
more directly at the first one:

1. **Give `get`/`exists` the shared-lock-then-upgrade-if-expired
   path** (already sketched in `DESIGN.md`'s "Notes for later phases").
   This directly targets what the GET-only number shows: reads
   currently get no concurrency at all. Smaller change than sharding,
   and the benchmark above is exactly the tool to confirm whether it
   closes the gap.
2. **Shard the map** — spreads *both* reads and writes across N
   independently-locked partitions. Bigger change (hashing into
   shards, whole-map operations like `size()` becoming a sum across
   shards), and worth doing only if a single lock is still the
   bottleneck after trying the cheaper fix, or if writes themselves
   (which shared-locking can't help) turn out to be the binding
   constraint.

Caveat: 100 hot keys under 16 threads on a 10-core machine is a
deliberately adversarial test — real client traffic spread across more
keys, or fewer concurrent connections than this, would contend less
than this shows. The direction of the result (locking exclusively on
reads costs real throughput under contention) is real either way; the
exact multiplier isn't a promise about production load.
