#include "kvstore.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kKeyPoolSize = 100;
constexpr int kOpsPerThread = 200000;

// All threads cycle through the same kKeyPoolSize keys -- deliberately
// worst-case for a shared lock, since it's the same handful of keys
// every thread is contending on, not unique keys per thread.
std::string key_for(int i) {
    return "key" + std::to_string(i % kKeyPoolSize);
}

void seed(KVStore& store) {
    for (int i = 0; i < kKeyPoolSize; ++i) {
        store.set(key_for(i), "v");
    }
}

enum class Workload { kSetOnly, kGetOnly, kMixed90Get10Set };

const char* workload_name(Workload workload) {
    switch (workload) {
        case Workload::kSetOnly: return "SET only";
        case Workload::kGetOnly: return "GET only";
        case Workload::kMixed90Get10Set: return "90% GET / 10% SET";
    }
    return "?";
}

void run_worker(KVStore& store, Workload workload, int ops) {
    for (int i = 0; i < ops; ++i) {
        const std::string key = key_for(i);
        switch (workload) {
            case Workload::kSetOnly:
                store.set(key, "v");
                break;
            case Workload::kGetOnly:
                store.get(key);
                break;
            case Workload::kMixed90Get10Set:
                if (i % 10 == 0) {
                    store.set(key, "v");
                } else {
                    store.get(key);
                }
                break;
        }
    }
}

void run_one(Workload workload, int num_threads) {
    KVStore store;
    seed(store);

    std::vector<std::thread> threads;
    const auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(run_worker, std::ref(store), workload, kOpsPerThread);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(end - start).count();
    const long long total_ops = static_cast<long long>(num_threads) * kOpsPerThread;
    const double ops_per_sec = total_ops / elapsed_seconds;

    std::cout << std::left << std::setw(20) << workload_name(workload)
              << std::right << std::setw(8) << num_threads
              << std::setw(14) << total_ops
              << std::setw(12) << std::fixed << std::setprecision(1)
              << (elapsed_seconds * 1000.0) << " ms"
              << std::setw(17) << std::fixed << std::setprecision(0)
              << ops_per_sec << " ops/s"
              << "\n";
}

}  // namespace

int main() {
    const std::vector<int> thread_counts = {1, 2, 4, 8, 16};
    const std::vector<Workload> workloads = {
        Workload::kSetOnly, Workload::kGetOnly, Workload::kMixed90Get10Set};

    std::cout << std::left << std::setw(20) << "workload"
              << std::right << std::setw(8) << "threads"
              << std::setw(14) << "total ops"
              << std::setw(15) << "elapsed"
              << std::setw(17) << "throughput"
              << "\n";
    std::cout << std::string(74, '-') << "\n";

    for (Workload workload : workloads) {
        for (int threads : thread_counts) {
            run_one(workload, threads);
        }
        std::cout << "\n";
    }

    return 0;
}
