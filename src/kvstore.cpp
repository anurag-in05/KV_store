#include "kvstore.hpp"

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <utility>

void KVStore::set(const std::string& key, const std::string& value,
                   std::optional<std::chrono::steady_clock::duration> ttl) {
    const std::lock_guard<std::shared_mutex> lock(m_mutex);

    Entry entry;
    entry.value = value;
    if (ttl) {
        entry.expires_at = std::chrono::steady_clock::now() + *ttl;
    }
    m_map[key] = std::move(entry);
}

bool KVStore::is_expired(const Entry& entry) const {
    return entry.expires_at && std::chrono::steady_clock::now() >= *entry.expires_at;
}

std::optional<std::string> KVStore::get(const std::string& key) const {
    const std::lock_guard<std::shared_mutex> lock(m_mutex);

    // find() rather than operator[] because operator[] is non-const and
    // would insert an empty entry for a missing key.
    auto it = m_map.find(key);
    if (it == m_map.end()) {
        return std::nullopt;
    }
    if (is_expired(it->second)) {
        m_map.erase(it);
        return std::nullopt;
    }
    return it->second.value;
}

bool KVStore::del(const std::string& key) {
    const std::lock_guard<std::shared_mutex> lock(m_mutex);

    auto it = m_map.find(key);
    if (it == m_map.end()) {
        return false;
    }
    const bool was_live = !is_expired(it->second);
    m_map.erase(it);
    return was_live;
}

bool KVStore::exists(const std::string& key) const {
    const std::lock_guard<std::shared_mutex> lock(m_mutex);

    auto it = m_map.find(key);
    if (it == m_map.end()) {
        return false;
    }
    if (is_expired(it->second)) {
        m_map.erase(it);
        return false;
    }
    return true;
}

std::size_t KVStore::size() const {
    // The one method that never mutates, so it's the only one that can
    // safely share the lock with other readers instead of blocking them.
    const std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_map.size();
}
