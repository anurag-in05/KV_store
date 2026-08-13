#include "server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include "repl.hpp"

std::vector<std::string> extract_lines(std::string& buffer, std::string_view new_bytes) {
    buffer.append(new_bytes);

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = buffer.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        lines.push_back(buffer.substr(start, newline - start));
        start = newline + 1;
    }
    buffer.erase(0, start);
    return lines;
}

namespace {

// Serves one already-accepted client until it disconnects or sends QUIT.
void handle_connection(KVStore& store, int client_fd) {
    std::string buffer;
    char chunk[4096];

    while (true) {
        const ssize_t n = recv(client_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;  // 0 = peer closed the connection, < 0 = error
        }

        const std::vector<std::string> lines =
            extract_lines(buffer, std::string_view(chunk, static_cast<std::size_t>(n)));

        bool quit = false;
        for (const std::string& line : lines) {
            const std::string reply = handle_line(store, line, quit);
            const std::string out = reply + "\n";
            send(client_fd, out.data(), out.size(), 0);
            if (quit) {
                close(client_fd);
                return;
            }
        }
    }

    close(client_fd);
}

}  // namespace

void run_server(KVStore& store, int port) {
    // Without this, writing to a client that already disconnected
    // (e.g. it closed the connection right as we send a reply) raises
    // SIGPIPE, whose default action kills the whole process.
    std::signal(SIGPIPE, SIG_IGN);

    const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        std::exit(1);
    }
    const int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        std::exit(1);
    }

    if (listen(listen_fd, /*backlog=*/16) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        std::exit(1);
    }

    std::cout << "kvstore listening on 127.0.0.1:" << port << "\n";

    while (true) {
        const int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
            break;
        }
        // std::thread's constructor copies its arguments by default, so
        // passing store directly would try to copy the KVStore -- every
        // connection would get its own, defeating the point of sharing
        // one store. std::ref wraps it in a reference_wrapper instead,
        // so every thread operates on the same instance. Detached, since
        // there's no shutdown/join mechanism yet; the OS reclaims each
        // thread when the process exits.
        std::thread(handle_connection, std::ref(store), client_fd).detach();
    }

    close(listen_fd);
}
