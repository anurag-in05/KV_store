#include "kvstore.hpp"
#include "repl.hpp"
#include "server.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

// display() turns a value into the text a failure message prints. The
// standard library has no generic "print any T", so each type we
// actually compare gets its own overload.
std::string display(const std::string& s) { return "\"" + s + "\""; }
std::string display(bool b) { return b ? "true" : "false"; }
std::string display(std::size_t n) { return std::to_string(n); }
std::string display(std::nullopt_t) { return "nullopt"; }
std::string display(const std::optional<std::string>& v) {
    return v ? display(*v) : display(std::nullopt);
}
std::string display(const std::vector<std::string>& v) {
    std::string out = "{";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ", ";
        out += display(v[i]);
    }
    return out + "}";
}

// Two independent type parameters, not one shared T, so that comparing a
// std::optional<std::string> against std::nullopt works: std::nullopt's
// type (std::nullopt_t) is never the same type as the optional itself,
// so a single-T template couldn't deduce both sides at once.
template <typename Actual, typename Expected>
void expect_eq(const char* file, int line, const char* expr,
               const Actual& actual, const Expected& expected) {
    ++g_checks;
    if (!(actual == expected)) {
        std::cout << file << ":" << line << ": FAIL " << expr
                  << " -- got " << display(actual)
                  << ", want " << display(expected) << "\n";
        ++g_failures;
    }
}

}  // namespace

// __FILE__/__LINE__ only work as macros -- a normal function has no way
// to see where it was called from. #actual stringifies the expression
// itself so a failure message shows what was checked, not just its value.
#define EXPECT_EQ(actual, expected) \
    expect_eq(__FILE__, __LINE__, #actual, (actual), (expected))

// ---- KVStore ------------------------------------------------------

void test_set_then_get() {
    KVStore store;
    store.set("name", "ada");
    EXPECT_EQ(store.get("name"), std::string("ada"));
}

void test_get_missing_is_nullopt() {
    KVStore store;
    EXPECT_EQ(store.get("missing"), std::nullopt);
}

void test_empty_value_is_not_missing() {
    // The whole reason get() returns optional instead of "": a key
    // storing "" must stay distinguishable from a key never set.
    KVStore store;
    store.set("k", "");
    EXPECT_EQ(store.get("k"), std::string(""));
}

void test_del_then_get_is_nullopt() {
    KVStore store;
    store.set("k", "v");
    store.del("k");
    EXPECT_EQ(store.get("k"), std::nullopt);
}

void test_del_existing_returns_true() {
    KVStore store;
    store.set("k", "v");
    EXPECT_EQ(store.del("k"), true);
}

void test_del_missing_returns_false() {
    KVStore store;
    EXPECT_EQ(store.del("missing"), false);
}

void test_overwrite_does_not_change_size() {
    KVStore store;
    store.set("k", "v1");
    EXPECT_EQ(store.size(), std::size_t{1});
    store.set("k", "v2");  // same key, new value
    EXPECT_EQ(store.size(), std::size_t{1});
    EXPECT_EQ(store.get("k"), std::string("v2"));
}

void test_exists_reflects_presence() {
    KVStore store;
    EXPECT_EQ(store.exists("k"), false);
    store.set("k", "v");
    EXPECT_EQ(store.exists("k"), true);
    store.del("k");
    EXPECT_EQ(store.exists("k"), false);
}

// A duration short enough to keep the suite fast, long enough not to be
// flaky on a loaded machine. See DESIGN.md: real sleep_for rather than
// clock injection, revisited only if this ever proves flaky or slow.
constexpr auto kTestTtl = std::chrono::milliseconds(50);
constexpr auto kPastTestTtl = std::chrono::milliseconds(80);

void test_get_returns_value_before_ttl_expires() {
    KVStore store;
    store.set("k", "v", kTestTtl);
    EXPECT_EQ(store.get("k"), std::string("v"));
}

void test_get_returns_nullopt_after_ttl_expires() {
    KVStore store;
    store.set("k", "v", kTestTtl);
    std::this_thread::sleep_for(kPastTestTtl);
    EXPECT_EQ(store.get("k"), std::nullopt);
}

void test_expired_entry_is_purged_from_size_on_get() {
    // Proves the *mutable* choice specifically: size() still counts the
    // dead entry until something actually touches it, then drops.
    KVStore store;
    store.set("k", "v", kTestTtl);
    std::this_thread::sleep_for(kPastTestTtl);
    EXPECT_EQ(store.size(), std::size_t{1});
    store.get("k");  // triggers the mutable erase inside get()
    EXPECT_EQ(store.size(), std::size_t{0});
}

void test_del_on_expired_key_returns_false_and_purges() {
    KVStore store;
    store.set("k", "v", kTestTtl);
    std::this_thread::sleep_for(kPastTestTtl);
    EXPECT_EQ(store.del("k"), false);
    EXPECT_EQ(store.size(), std::size_t{0});
}

void test_exists_false_after_expiry_and_purges() {
    KVStore store;
    store.set("k", "v", kTestTtl);
    std::this_thread::sleep_for(kPastTestTtl);
    EXPECT_EQ(store.exists("k"), false);
    EXPECT_EQ(store.size(), std::size_t{0});
}

void test_plain_set_clears_existing_ttl() {
    KVStore store;
    store.set("k", "v1", kTestTtl);
    store.set("k", "v2");  // no ttl argument -- should clear the old one
    std::this_thread::sleep_for(kPastTestTtl);
    EXPECT_EQ(store.get("k"), std::string("v2"));
}

void test_concurrent_set_no_lost_keys() {
    // Regression test for the Phase 3 race: before Phase 4's shared_mutex,
    // this exact pattern -- many threads inserting distinct keys into one
    // unlocked KVStore -- lost entries under real concurrent load (see
    // docs/race-condition-demo.md). No sockets needed to prove the fix;
    // std::thread against the KVStore API directly is faster and just as
    // real a test of the lock.
    constexpr int kThreads = 20;
    constexpr int kKeysPerThread = 50;

    KVStore store;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&store, t]() {
            for (int i = 0; i < kKeysPerThread; ++i) {
                store.set("k" + std::to_string(t) + "_" + std::to_string(i), "v");
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(store.size(), std::size_t{kThreads * kKeysPerThread});
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kKeysPerThread; ++i) {
            EXPECT_EQ(store.exists("k" + std::to_string(t) + "_" + std::to_string(i)), true);
        }
    }
}

// ---- handle_line ----------------------------------------------------

void test_ping() {
    KVStore store;
    bool quit = true;  // start true to prove handle_line resets it
    EXPECT_EQ(handle_line(store, "PING", quit), std::string("PONG"));
    EXPECT_EQ(quit, false);
}

void test_quit_sets_flag() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "QUIT", quit), std::string("BYE"));
    EXPECT_EQ(quit, true);
}

void test_set_get_roundtrip_through_handle_line() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "SET name ada", quit), std::string("OK"));
    EXPECT_EQ(handle_line(store, "GET name", quit), std::string("ada"));
}

void test_command_name_is_case_insensitive() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "set name ada", quit), std::string("OK"));
    EXPECT_EQ(handle_line(store, "get name", quit), std::string("ada"));
}

void test_get_wrong_arity() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "GET", quit),
              std::string("ERR wrong number of arguments for GET"));
}

void test_set_with_four_non_numeric_tokens_is_invalid_ttl() {
    // This test used to pin "a 4th token is rejected outright" -- and it
    // did its job: TTL support deliberately makes the 4th token mean
    // something (a ttl in seconds), so "v2" is now parsed as an invalid
    // TTL rather than rejected for wrong arity. Updated on purpose, not
    // silently -- this is exactly the decision the old comment asked for.
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "SET k v1 v2", quit),
              std::string("ERR invalid TTL"));
}

void test_set_with_valid_ttl_through_handle_line() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "SET k v 5", quit), std::string("OK"));
    EXPECT_EQ(handle_line(store, "GET k", quit), std::string("v"));
}

void test_set_with_zero_ttl_is_invalid() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "SET k v 0", quit),
              std::string("ERR invalid TTL"));
}

void test_set_with_negative_ttl_is_invalid() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "SET k v -5", quit),
              std::string("ERR invalid TTL"));
}

void test_set_with_non_numeric_ttl_is_invalid() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "SET k v abc", quit),
              std::string("ERR invalid TTL"));
}

void test_set_with_trailing_garbage_ttl_is_invalid() {
    // from_chars parses "10" and stops before "x" -- ptr doesn't reach
    // the end of the token, so this is rejected as garbage, not read as 10.
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "SET k v 10x", quit),
              std::string("ERR invalid TTL"));
}

void test_unknown_command() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "NOPE", quit),
              std::string("ERR unknown command 'NOPE'"));
}

void test_empty_line_is_silently_ignored() {
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "", quit), std::string(""));
    EXPECT_EQ(quit, false);
}

void test_whitespace_only_line_is_silently_ignored() {
    // tokenize() splits on whitespace via istringstream, so a line of
    // only spaces produces zero tokens -- same path as an empty line.
    KVStore store;
    bool quit = false;
    EXPECT_EQ(handle_line(store, "   ", quit), std::string(""));
    EXPECT_EQ(quit, false);
}

// ---- extract_lines ---------------------------------------------------

void test_extract_lines_single_complete_line() {
    std::string buffer;
    const std::vector<std::string> lines = extract_lines(buffer, "PING\n");
    EXPECT_EQ(lines, std::vector<std::string>{"PING"});
    EXPECT_EQ(buffer, std::string(""));
}

void test_extract_lines_multiple_lines_in_one_chunk() {
    // Two commands that arrived in a single recv() call.
    std::string buffer;
    const std::vector<std::string> lines = extract_lines(buffer, "SET a b\nGET a\n");
    // Extra parens: a bare comma inside the macro argument would be
    // parsed as two separate EXPECT_EQ arguments instead of one vector.
    EXPECT_EQ(lines, (std::vector<std::string>{"SET a b", "GET a"}));
    EXPECT_EQ(buffer, std::string(""));
}

void test_extract_lines_partial_line_then_completion() {
    // Simulates one command arriving split across two recv() calls.
    std::string buffer;
    EXPECT_EQ(extract_lines(buffer, "SET a"), std::vector<std::string>{});
    EXPECT_EQ(buffer, std::string("SET a"));

    const std::vector<std::string> lines = extract_lines(buffer, " b\n");
    EXPECT_EQ(lines, std::vector<std::string>{"SET a b"});
    EXPECT_EQ(buffer, std::string(""));
}

void test_extract_lines_no_trailing_newline_leaves_buffer() {
    std::string buffer;
    const std::vector<std::string> lines = extract_lines(buffer, "PING");
    EXPECT_EQ(lines, std::vector<std::string>{});
    EXPECT_EQ(buffer, std::string("PING"));
}

void test_extract_lines_two_commands_plus_a_partial_third() {
    std::string buffer;
    const std::vector<std::string> lines = extract_lines(buffer, "PING\nPING\nGET");
    EXPECT_EQ(lines, (std::vector<std::string>{"PING", "PING"}));
    EXPECT_EQ(buffer, std::string("GET"));
}

int main() {
    test_set_then_get();
    test_get_missing_is_nullopt();
    test_empty_value_is_not_missing();
    test_del_then_get_is_nullopt();
    test_del_existing_returns_true();
    test_del_missing_returns_false();
    test_overwrite_does_not_change_size();
    test_exists_reflects_presence();

    test_get_returns_value_before_ttl_expires();
    test_get_returns_nullopt_after_ttl_expires();
    test_expired_entry_is_purged_from_size_on_get();
    test_del_on_expired_key_returns_false_and_purges();
    test_exists_false_after_expiry_and_purges();
    test_plain_set_clears_existing_ttl();
    test_concurrent_set_no_lost_keys();

    test_ping();
    test_quit_sets_flag();
    test_set_get_roundtrip_through_handle_line();
    test_command_name_is_case_insensitive();
    test_get_wrong_arity();
    test_set_with_four_non_numeric_tokens_is_invalid_ttl();
    test_set_with_valid_ttl_through_handle_line();
    test_set_with_zero_ttl_is_invalid();
    test_set_with_negative_ttl_is_invalid();
    test_set_with_non_numeric_ttl_is_invalid();
    test_set_with_trailing_garbage_ttl_is_invalid();
    test_unknown_command();
    test_empty_line_is_silently_ignored();
    test_whitespace_only_line_is_silently_ignored();

    test_extract_lines_single_complete_line();
    test_extract_lines_multiple_lines_in_one_chunk();
    test_extract_lines_partial_line_then_completion();
    test_extract_lines_no_trailing_newline_leaves_buffer();
    test_extract_lines_two_commands_plus_a_partial_third();

    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
