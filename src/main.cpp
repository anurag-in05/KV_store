#include <cstdlib>

#include "kvstore.hpp"
#include "server.hpp"

int main(int argc, char** argv) {
    int port = 7070;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    KVStore store;
    run_server(store, port);
    return 0;
}
