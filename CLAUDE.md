# Context for Claude Code

Read `DESIGN.md` first. It defines the phases and the current scope.

## About this project

A learning project. The author is comfortable with C++ basics and has
written socket code in Rust, but is new to modern C++ idioms and to
concurrency. Explanations matter more than finished code.

## Working rules

- **Stay inside the current phase.** `DESIGN.md` marks it. Do not add
  threading, sockets, or persistence early, even if trivial.
- **Do not write large blocks of code unprompted.** Suggest the change,
  explain the reasoning, wait to be asked.
- **Explain the C++-specific choice** whenever one comes up: why a
  reference over a pointer, why `const`, why `std::optional` over a
  sentinel value, why pass by `const&`. Short — one or two sentences.
- **Prefer the standard library** over hand-rolled utilities.
- **No premature abstraction.** No interfaces with one implementation,
  no templates until something needs to be generic.
- If a change would blur the KVStore / transport boundary, say so
  instead of doing it.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/kvstore
```

## Style

- C++17. Standard library only for now.
- 4-space indent, `snake_case` for functions and variables,
  `PascalCase` for types, `m_` prefix on private members.
- Headers declare, `.cpp` files define. No logic in headers yet.
