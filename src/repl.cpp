#include "repl.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

// Splits on whitespace. Phase 1 accepts no spaces inside values.
std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

}  // namespace

std::string handle_line(KVStore& store, const std::string& line, bool& quit) {
    quit = false;

    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty()) {
        return "";
    }

    const std::string command = to_upper(tokens[0]);

    if (command == "PING") {
        return "PONG";
    }

    if (command == "QUIT") {
        quit = true;
        return "BYE";
    }

    if (command == "SET") {
        if (tokens.size() != 3 && tokens.size() != 4) {
            return "ERR wrong number of arguments for SET";
        }
        if (tokens.size() == 3) {
            store.set(tokens[1], tokens[2]);
            return "OK";
        }

        // Fourth token is a TTL in seconds. from_chars (not stoi) because
        // it never throws and reports exactly how much of the string it
        // consumed, so trailing garbage like "10x" is caught below.
        const std::string& ttl_token = tokens[3];
        int ttl_seconds = 0;
        const auto [ptr, ec] = std::from_chars(
            ttl_token.data(), ttl_token.data() + ttl_token.size(), ttl_seconds);
        const bool fully_consumed = ptr == ttl_token.data() + ttl_token.size();
        if (ec != std::errc() || !fully_consumed || ttl_seconds <= 0) {
            return "ERR invalid TTL";
        }

        store.set(tokens[1], tokens[2], std::chrono::seconds(ttl_seconds));
        return "OK";
    }

    if (command == "GET") {
        if (tokens.size() != 2) {
            return "ERR wrong number of arguments for GET";
        }
        const std::optional<std::string> value = store.get(tokens[1]);
        return value ? *value : "(nil)";
    }

    if (command == "DEL") {
        if (tokens.size() != 2) {
            return "ERR wrong number of arguments for DEL";
        }
        return store.del(tokens[1]) ? "1" : "0";
    }

    if (command == "EXISTS") {
        if (tokens.size() != 2) {
            return "ERR wrong number of arguments for EXISTS";
        }
        return store.exists(tokens[1]) ? "1" : "0";
    }

    return "ERR unknown command '" + tokens[0] + "'";
}

void run_repl(KVStore& store) {
    std::string line;
    bool quit = false;

    while (!quit) {
        std::cout << "kvstore> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;  // EOF, e.g. Ctrl-D
        }

        const std::string reply = handle_line(store, line, quit);
        if (!reply.empty()) {
            std::cout << reply << "\n";
        }
    }
}
