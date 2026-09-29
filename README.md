# Concurrent Task Scheduler in C++20

A multithreaded task scheduler built in modern C++20 with a configurable worker pool, thread-safe priority scheduling, futures, delayed execution, exception propagation, and graceful shutdown.

## Features

- Configurable worker thread pool
- Thread-safe priority queue with `High`, `Normal`, and `Low` priorities
- FIFO execution for tasks with the same priority
- Generic task submission using templates and `std::future`
- Return-value and exception propagation through `std::packaged_task`
- Delayed task submission
- Condition-variable-based worker dispatch
- Graceful shutdown that drains queued work and joins worker threads
- Atomic tracking of active tasks
- GoogleTest coverage for correctness, concurrency, scheduling, lifecycle behavior, and stress cases

## Benchmark

The benchmark compares three execution strategies using the same deterministic CPU-bound workload:

- **2,000 tasks**
- **50,000 square-root iterations per task**
- **8 hardware threads reported by `std::thread::hardware_concurrency()`**
- Release build with `-O2`

Three benchmark runs were performed. Representative median results:

| Execution model | Time | Throughput | Speedup vs. sequential |
|---|---:|---:|---:|
| Sequential | 125.99 ms | 15,874 tasks/sec | 1.0x |
| Thread per task | 38.00 ms | 52,631 tasks/sec | 3.3x |
| Thread pool | **17.45 ms** | **114,621 tasks/sec** | **7.2x** |

The thread pool was approximately **2.2x faster than creating one thread per task** in the median run.

All benchmark strategies produced the same checksum (`14907343041.40`), confirming that each strategy completed the same workload.

> Benchmark results are machine- and workload-dependent. Results above were measured locally and may vary across systems and runs.

## How It Works

Tasks are wrapped in `std::packaged_task` objects and inserted into a synchronized `std::priority_queue`. Worker threads wait on a `std::condition_variable` and pull work as it becomes available.

Each queued task stores:

- its priority,
- a monotonically increasing sequence number, and
- the callable to execute.

Higher-priority tasks are selected first. The sequence number acts as a tie-breaker so tasks with the same priority execute in FIFO order.

The default `submit()` overload assigns `Normal` priority, while callers can explicitly choose `Low`, `Normal`, or `High`.

## Example

```cpp
#include "thread_pool.hpp"

#include <iostream>

int main() {
    ThreadPool pool(4);

    auto normal = pool.submit([] {
        return 21 * 2;
    });

    auto high = pool.submit(TaskPriority::High, [] {
        return 100;
    });

    std::cout << normal.get() << "\n";
    std::cout << high.get() << "\n";

    pool.shutdown();
}
```

Tasks can also be scheduled after a delay:

```cpp
auto future = pool.submit_delayed(
    std::chrono::milliseconds(100),
    TaskPriority::Normal,
    [] { return 42; }
);

std::cout << future.get() << "\n";
```

Delayed submissions use a lightweight detached waiting thread for the delay period and enqueue the task into the scheduler once the delay has elapsed.

## Tests

The GoogleTest suite covers:

- task return values and argument forwarding,
- `void` tasks,
- exactly-once execution under concurrency,
- independent result correctness,
- shared-state contention,
- priority ordering,
- FIFO ordering within equal priority,
- exception propagation through futures,
- continued pool operation after a task throws,
- rejection of submissions after shutdown,
- delayed-task timing and completion,
- queue draining during shutdown,
- repeated shutdown calls,
- destructor cleanup, and
- a 10,000-task stress workload.

## Build with CMake

### Requirements

- C++20-compatible compiler
- CMake 3.14+
- pthreads

GoogleTest is discovered automatically. If it is not installed locally, CMake fetches GoogleTest v1.14.0.

```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

Run the benchmark:

```bash
./benchmark
```

Run the test suite:

```bash
ctest --output-on-failure
```

You can also run the test executable directly:

```bash
./run_tests
```

## Build with Make

If GoogleTest is already installed on your system:

```bash
make
```

Run the benchmark:

```bash
./benchmark
```

Run the tests:

```bash
make test
```

Clean generated executables:

```bash
make clean
```

## Project Structure

```text
.
├── thread_pool.hpp        # Thread pool and task scheduler implementation
├── benchmark.cpp          # Sequential vs. thread-per-task vs. pooled benchmark
├── test_thread_pool.cpp   # GoogleTest test suite
├── CMakeLists.txt         # CMake build and test configuration
├── Makefile               # Alternative build commands
├── README.md
└── .gitignore
```

## Technical Concepts Demonstrated

- Multithreading and concurrency
- Thread synchronization
- Mutexes and condition variables
- Atomic operations
- Priority queues
- Futures and packaged tasks
- Generic programming with templates
- Exception propagation across asynchronous tasks
- Resource lifecycle management
- Performance benchmarking
- Unit and stress testing
