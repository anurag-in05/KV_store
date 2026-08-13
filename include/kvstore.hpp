#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

// In-memory key-value store.
//
// Knows nothing about how commands reach it. Phase 1 drives this from a
// REPL, Phase 3 drives it from a socket, one thread per connection.
//
// Thread-safe as of Phase 4: every method takes m_mutex. Every method
// currently takes it exclusively -- see kvstore.cpp for why size() is
// the only one that doesn't.
class KVStore {
public:
    // Inserts or overwrites the value for key. ttl, if given, is how
    // long from now the key stays alive; std::nullopt means it never
    // expires. A plain set() (no ttl) replaces the whole entry, clearing
    // any TTL a previous set() on this key may have had.
    void set(const std::string& key, const std::string& value,
             std::optional<std::chrono::steady_clock::duration> ttl = std::nullopt);

    // Returns the value, or std::nullopt if the key is absent or expired.
    // std::optional rather than "" so an empty stored value stays
    // distinguishable from a missing key.
    std::optional<std::string> get(const std::string& key) const;

    // Returns true if a live key was actually removed. An expired key
    // is purged but reported as already gone, so this returns false.
    bool del(const std::string& key);

    // False for a missing or expired key.
    bool exists(const std::string& key) const;

    // May count expired entries that nothing has touched yet -- see
    // DESIGN.md's note on deferred eviction.
    std::size_t size() const;

private:
    struct Entry {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
    };

    // get()/exists() purge an expired entry the moment they notice one.
    // m_map is mutable so they can do that despite being const: the
    // erase() doesn't change what a caller observes (the key was already
    // logically gone), only internal bookkeeping does.
    //
    // Not locked itself -- only ever called from a method that's already
    // holding m_mutex. Locking again here would deadlock: shared_mutex
    // isn't recursive.
    bool is_expired(const Entry& entry) const;

    // mutable for the same reason m_map is: locking/unlocking is
    // bookkeeping a const method needs to do, not a change a caller
    // observes.
    mutable std::shared_mutex m_mutex;
    mutable std::unordered_map<std::string, Entry> m_map;
};
