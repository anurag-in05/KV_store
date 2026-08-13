#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "kvstore.hpp"

// Appends new_bytes to buffer, then extracts every complete
// '\n'-terminated line found. Any bytes after the last '\n' are left in
// buffer for the next call -- this is what lets a command arrive split
// across two recv() calls, or two commands arrive in one.
std::vector<std::string> extract_lines(std::string& buffer, std::string_view new_bytes);

// Listens on 127.0.0.1:port and serves one client at a time until the
// process is killed or a client sends QUIT. Each line is handled through
// handle_line, the same function the REPL uses, so the protocol logic
// isn't duplicated.
void run_server(KVStore& store, int port);
