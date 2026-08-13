# kvstore — design

A small in-memory key-value store in C++17. Built in phases so it is
demoable at the end of every phase.

## Core principle

The storage engine knows nothing about networking. `KVStore` is a plain
class with `set/get/del`. Phase 1 drives it from a REPL, Phase 3 drives
it from a socket. Neither one changes the engine.

```
          +-----------+
REPL  --> |           |
          |  KVStore  |   <- all logic lives here
socket--> |           |
          +-----------+
```

If adding the network layer later requires editing `kvstore.cpp`, the
boundary was drawn wrong.

## Phases

| Phase | What                                   | Status                        |
|-------|----------------------------------------|--------------------------------|
| 1     | KVStore + REPL, single-threaded        | done                           |
| 2     | Tests, key expiry (TTL)                | done                            |
| 3     | TCP server, thread per connection      | done                           |
| 4     | `shared_mutex`                         | done                            |
| 5     | Benchmark harness                      | done                            |

Do not start a phase before the previous one builds and runs.

## Command reference (Phases 1-2)

Commands, typed at a prompt:

```
SET <key> <value> [ttl_seconds]   -> OK | ERR invalid TTL
GET <key>                         -> <value> | (nil)
DEL <key>                         -> 1 | 0
EXISTS <key>                      -> 1 | 0
PING                              -> PONG
QUIT                              -> exits
```

- Keys and values are `std::string`.
- Values are single tokens for now (no spaces). Quoting comes later.
- Storage is `std::unordered_map<std::string, Entry>`, where `Entry` holds
  a value and an optional expiry (`std::chrono::steady_clock::time_point`).
- `ttl_seconds` must be a positive integer. A plain `SET` (no ttl) always
  replaces the whole entry, clearing any TTL the key had before.
- `GET`/`EXISTS`/`DEL` treat an expired key as absent. `GET`/`EXISTS`
  physically purge it the moment they notice (see "Deliberately
  deferred" below for what this does and doesn't guarantee).
- No persistence.

## Phase 3: server

`kvstore` (no arguments, or `kvstore <port>`) listens on `127.0.0.1`,
port 7070 by default. Each connection is framed the same way: bytes from
`recv()` accumulate in a per-connection buffer, `extract_lines()` pulls
out every complete `\n`-terminated line (leaving a partial trailing line
buffered for the next `recv()`), and each line goes through the same
`handle_line()` the REPL used — the protocol logic isn't duplicated,
only how bytes reach it changed. The REPL (`run_repl`) still exists and
is still tested, it's just not what `main` calls anymore.

Each accepted connection is handled on its own detached `std::thread`,
all sharing one `KVStore` instance. As of Phase 4, `KVStore` locks
itself internally (see "Phase 4: locking" below) — `server.cpp` and
`repl.cpp` don't know or care that the store is shared across threads.

## Phase 4: locking

`KVStore` holds a `mutable std::shared_mutex m_mutex`, taken by every
public method. `set`/`del`/`get`/`exists` all take it exclusively —
`get`/`exists` can mutate too (purging an expired entry), so they don't
get the concurrent-read path yet. `size()` never mutates, so it's the
one method that takes the lock in shared mode; it's also the only
reason this is a `shared_mutex` rather than a plain `std::mutex` right
now. `is_expired()` (private) takes no lock itself — it's only ever
called from inside a method that's already holding one, and
`shared_mutex` isn't recursive.

Proof this actually fixes something: `docs/race-condition-demo.md`
reruns the exact stress test that lost 2 of 1000 keys pre-Phase-4
against the locked build — 0 missing. `tests/tests.cpp` also has a
permanent regression test, `test_concurrent_set_no_lost_keys`, spawning
real threads against `KVStore` directly.

## Phase 5: benchmark

`bench/bench.cpp` → `kvstore_bench`, a third build target (not wired
into `ctest` — it produces numbers to read, not a pass/fail result).
Measures `KVStore` directly, no sockets, across 3 workloads (SET-only,
GET-only, 90/10 mixed) and a thread-count sweep, all threads contending
on the same 100-key pool on purpose. Build it with
`-DCMAKE_BUILD_TYPE=Release` — an unoptimized build measures debug
overhead, not the lock.

Result, in full in `docs/benchmark-results.md`: throughput collapses as
threads increase (16 threads moved *less* total work per second than 1
did), and GET-only performs almost identically to the 90/10 mix — the
direct, measured consequence of `get`/`exists` taking the exclusive
lock instead of a shared one. Whether the next move is giving reads a
shared-lock path or sharding the map is an open, informed decision, not
yet made — see "Notes for later phases."

## Layout

```
include/kvstore.hpp   engine interface
src/kvstore.cpp       engine implementation
src/repl.cpp          line parsing + command dispatch (used by REPL and server)
src/server.cpp        TCP accept loop, byte-stream framing, thread-per-connection
src/main.cpp          entry point
tests/tests.cpp       hand-rolled test harness (no external framework)
bench/bench.cpp       throughput benchmark (no external framework)
```

`repl.cpp` owns all string parsing. `kvstore.cpp` never sees a raw input
line — it only receives already-parsed arguments.

## Deliberately deferred

Recorded so they are choices, not oversights:

- Concurrent read scaling. `get`/`exists` take the exclusive lock, not
  the shared one, so reads from different connections still serialize
  against each other even though neither mutates in the common case.
  Fixing this needs the shared-lock-then-upgrade-if-expired pattern,
  deliberately not built in Phase 4 — see "Notes for later phases."
- Values containing spaces or newlines.
- Persistence. Everything is lost on exit.
- Proactive eviction. Expired keys are only purged lazily, when `GET` or
  `EXISTS` happens to touch them — a key that expires and is never
  touched again sits in the map until overwritten. No background sweep.
- Binary-safe protocol.

## Notes for later phases

- Measured, not guessed, per `docs/benchmark-results.md`: 16 threads
  hammering the store moves *less* total work per second than 1 thread
  does, and GET-only scales identically to a 90/10 mixed workload — the
  exclusive lock on reads is a confirmed, not hypothetical, cost.
- Two candidate fixes, not yet chosen between: give `get`/`exists` a
  shared-lock-then-upgrade-if-expired path (cheaper, targets exactly
  what the GET-only number shows; re-check expiry after reacquiring
  exclusively, since another thread could act on the entry in the gap
  between releasing the shared lock and taking the exclusive one), or
  shard the map (bigger change, helps writes too, but `size()` becomes
  a sum across shards). Whichever is picked, rerun `kvstore_bench`
  after to confirm it actually moved the number.
