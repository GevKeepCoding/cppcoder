// store_load.cpp
// On x86: CAN also fail (x86 allows store-load reorder!)
// On ARM: fails more frequently
// This shows x86 is NOT sequentially consistent either.
#include <thread>
#include <atomic>
#include <cstdio>

int x = 0, y = 0;   // non-atomic
int r1 = 0, r2 = 0;

std::atomic<int> ready{0};

void thread1() {
    while (ready.load(std::memory_order_acquire) != 1) {}
    x = 1;        // store
    r1 = y;       // load  — can x86 see y==0 while thread2 already stored y=1?
}

void thread2() {
    while (ready.load(std::memory_order_acquire) != 1) {}
    y = 1;        // store
    r2 = x;       // load
}

int main() {
    long both_zero = 0;
    for (int i = 0; i < 10'000'000; ++i) {
        x = y = 0;
        r1 = r2 = -1;
        ready.store(0);
        std::thread t1(thread1), t2(thread2);
        ready.store(1, std::memory_order_release);
        t1.join(); t2.join();
        if (r1 == 0 && r2 == 0) {
            ++both_zero;  // Both threads stored before loading, yet both read 0!
        }
    }
    printf("Both-zero (store-load reorder): %ld times\n", both_zero);
    // x86:  rare but nonzero (store buffers cause this)
    // ARM:  much more frequent
}