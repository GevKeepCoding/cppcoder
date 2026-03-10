// ptr_publish.cpp
// On ARM: reader can see ptr != nullptr but payload still 0
// On x86: virtually never observed (TSO orders the two stores)
#include <thread>
#include <atomic>
#include <cstdio>

struct Data {
    int payload;
};

Data* ptr = nullptr;   // non-atomic

std::atomic<bool> go{false};

void producer() {
    while (!go.load(std::memory_order_acquire)) {}
    Data* tmp = new Data;
    tmp->payload = 42;   // store A: write payload
    ptr = tmp;            // store B: publish pointer — ARM can reorder B before A
}

void consumer() {
    while (!go.load(std::memory_order_acquire)) {}
    Data* local;
    while ((local = ptr) == nullptr) {}  // spin
    // On ARM: local != nullptr but local->payload may be 0 (uninitialized)
    if (local->payload != 42) {
        printf("BUG: ptr set but payload=%d (half-constructed!)\n", local->payload);
    }
    delete local;
}

int main() {
    for (int i = 0; i < 10'000'000; ++i) {
        ptr = nullptr;
        go.store(false);
        std::thread t1(producer), t2(consumer);
        go.store(true, std::memory_order_release);
        t1.join(); t2.join();
    }
    printf("Done.\n");
}