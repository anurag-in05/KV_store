#pragma once

#include <string>

#include "kvstore.hpp"

// Turns one line of text into a command against the store and returns the
// reply. Phase 3 will call this same function with lines read off a
// socket, so it must not read from stdin or write to stdout itself.
//
// Sets quit to true when the line was QUIT.
std::string handle_line(KVStore& store, const std::string& line, bool& quit);

// Reads lines from stdin until EOF or QUIT.
void run_repl(KVStore& store);
