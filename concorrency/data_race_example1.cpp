// store_store.cpp
// On ARM: reader can see data==0 && flag==1 (stores reordered)
// On x86: practically never happens (TSO prevents store-store reorder)
#include <thread>
#include <atomic>
#include <cassert>
#include <cstdio>

int data = 0;       // non-atomic
int flag = 0;       // non-atomic

std::atomic<bool> go{false};

void writer() {
    while (!go.load(std::memory_order_acquire)) {}
    data = 42;   // store 1
    flag = 1;    // store 2 — ARM may reorder this BEFORE store 1
}

void reader() {
    while (!go.load(std::memory_order_acquire)) {}
    while (flag == 0) {}  // spin until flag is set
    // On ARM: flag==1 but data may still be 0
    if (data != 42) {
        printf("BUG: flag=%d but data=%d (store-store reorder observed)\n", flag, data);
    }
}

int main() {
    long bugs = 0;
    for (int i = 0; i < 10'000'000; ++i) {
        data = 0;
        flag = 0;
        go.store(false);
        std::thread t1(writer), t2(reader);
        go.store(true, std::memory_order_release);
        t1.join(); t2.join();
    }
    printf("Done. bugs=%ld\n", bugs);
}