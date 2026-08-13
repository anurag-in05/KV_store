# Race condition demo — why Phase 4 exists

`DESIGN.md` documents that Phase 3's `KVStore` is shared, unlocked, and
accessed from a `std::thread` per connection. This is the empirical
demonstration of what that actually causes, run against a real build.

## Setup

- Built `kvstore` fresh, started it as its own process on `127.0.0.1:7072`.
- 25 concurrent `nc` clients, launched together via a backgrounded shell
  loop so their connections and writes overlap in real time.
- Each client ran 40 iterations of:
  ```
  SET counter client<N>      # 25 clients racing the SAME key
  SET k<N>_<i> v             # each client's own, otherwise-unique key
  ```
  1000 unique-key `SET`s and 1000 same-key `SET`s, fully interleaved
  across threads, no synchronization anywhere in `KVStore`.

## Result 1 — the shared key is non-deterministic

Final value of `GET counter` after all 25 clients finished: `client20`.
Not the last client launched, not the first — whichever thread's write
happened to land last is unrecoverable information; the outcome isn't
reproducible run to run.

## Result 2 — two of the 1000 unique keys were silently lost

Checked all 1000 expected keys with `EXISTS` after the run:

```
expected keys: 1000
found (EXISTS=1): 998
missing (EXISTS=0): 2      -> k13_33, k24_38
```

The damning part: **the server replied `OK` to every single `SET`
in every client's log, including the two whose keys vanished.** No
`ERR`, no crash, no indication anything went wrong. A client that wrote
`k13_33` has no way to discover that write didn't survive.

## Why this happens

`std::unordered_map::operator[]` (what `KVStore::set` uses) is a plain
read-modify-write against shared bucket/node state. When two threads
call it concurrently — especially when an insert forces the table to
rehash — there's no atomicity guarantee at all. The C++ standard simply
calls concurrent non-const access to the same container undefined
behavior; "two keys quietly disappear with no error" is one legal
outcome. A crash would have been another.

## What Phase 4 fixes

A single lock around `KVStore`'s map (`std::shared_mutex`, taken
exclusively by `set`/`del`/`get`/`exists` — `get`/`exists` can mutate too,
to purge an expired entry, so they don't get the shared/concurrent-read
path yet; `size()` alone does) removes the race by serializing the
internal mutation, without changing the public API or requiring
`repl.cpp`/`server.cpp` to know anything changed. Measuring the
throughput cost of locking every read, too, and whether sharding the map
earns it back, is Phase 5's job, not Phase 4's.

## Fixed in Phase 4 — same demo, rerun against the locked build

Identical script, identical 25 clients × 40 iterations, against the
rebuilt server with `shared_mutex` in place:

```
expected OK replies: 2000, actual: 2000
expected keys: 1000
found (EXISTS=1): 1000
missing (EXISTS=0): 0
```

Zero missing, up from 2. `GET counter` still ended up nondeterministic
(`client24` this run, `client20` before) — and that's fine. The lock
guarantees each `set()` call is atomic, not that concurrent clients'
writes land in a predictable order. "Last write wins" is ordinary
key-value store behavior; silently losing a write, which is what
happened before, never was.

A permanent regression test for this — `test_concurrent_set_no_lost_keys`
in `tests/tests.cpp` — spawns real `std::thread`s directly against a
`KVStore`, no sockets required, and asserts all keys survive. It runs as
part of the normal test suite now, so this bug can't silently come back.
