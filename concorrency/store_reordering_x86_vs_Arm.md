## Here are 3 examples that expose store ordering differences between x86 (TSO) and ARM (weak):
TSO - means Total Store Order: stores from one core become visible to all cores in the order they were issued. ARM allows more reordering, so stores can become visible in a different order than issued.

---

### Example 1: Store-Store Reordering (Message Passing)

```cpp
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
```

**Why it differs:** x86 TSO guarantees stores from one core become visible in program order. ARM does not — the CPU may commit `flag = 1` to cache before `data = 42` propagates.

---

### Example 2: Store-Load Reordering (Dekker Pattern)

```cpp
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
    x = 1;       // store
    r1 = y;      // load  — can x86 see y==0 while thread2 already stored y=1?
}

void thread2() {
    while (ready.load(std::memory_order_acquire) != 1) {}
    y = 1;       // store
    r2 = x;      // load
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
```

**Why it differs:** Even x86 allows store-load reordering (stores sit in the store buffer and loads can bypass them). ARM allows this even more freely. This is the one reordering x86 does NOT prevent — making it the classic litmus test.

---

### Example 3: Pointer Publish (Half-Constructed Object)

```cpp
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
```

**Why it differs:** This is the DCLP problem in miniature. On ARM, the store to `ptr` can become visible to the consumer before the store to `payload`. The consumer sees a non-null pointer to an object with `payload == 0`. On x86/TSO, the two stores are seen in order, masking the bug.

---

### Summary Table

| Example | Reordering Type | Triggers on x86? | Triggers on ARM? |
|---|---|---|---|
| 1 — Message Passing | Store → Store | **No** (TSO prevents) | **Yes** |
| 2 — Dekker Pattern | Store → Load | **Yes** (store buffer) | **Yes** (more often) |
| 3 — Pointer Publish | Store → Store | **No** (TSO prevents) | **Yes** |

All three are **undefined behavior** in C++ regardless of architecture. Use `std::atomic` with `memory_order_acquire`/`memory_order_release` to fix examples 1 and 3, and `memory_order_seq_cst` to fix example 2.