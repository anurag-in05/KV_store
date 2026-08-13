# kvstore

An in-memory key-value store in C++17, built in phases. Currently a
single-threaded engine with a REPL front end.

## Build and run

```sh
cmake -S . -B build
cmake --build build
./build/kvstore
```

```
kvstore> SET name ada
OK
kvstore> GET name
ada
kvstore> EXISTS nope
0
kvstore> QUIT
BYE
```

## Design

See `DESIGN.md` for the layering and the phase plan.

## Current limitations

- Single-threaded, no locking.
- Values cannot contain spaces.
- Nothing is persisted; data is lost on exit.
- The map grows without bound — no eviction, no TTL.
