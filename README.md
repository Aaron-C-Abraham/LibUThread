# LibUThread

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![C Standard](https://img.shields.io/badge/C-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Platform](https://img.shields.io/badge/Platform-Linux%20x86--64-lightgrey.svg)](https://www.linux.org/)

**A userspace threading library with pluggable schedulers**

LibUThread is a complete M:N threading implementation featuring context switching, synchronization primitives, and multiple scheduling algorithms. Built for educational purposes to understand operating system internals.

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Installation](#installation)
- [Usage](#usage)
- [API Reference](#api-reference)
- [Scheduling Policies](#scheduling-policies)
- [Project Structure](#project-structure)
- [Testing](#testing)
- [Benchmarks](#benchmarks)
- [Contributing](#contributing)
- [License](#license)

---

## Features

### Thread Management
- POSIX-like thread API (`create`, `join`, `detach`, `yield`, `exit`)
- Configurable stack sizes with guard pages for overflow detection
- Thread naming for debugging
- Thread-local cleanup handlers

### Scheduling Algorithms
| Scheduler | Description | Use Case |
|-----------|-------------|----------|
| **Round-Robin** | Fair, time-sliced scheduling | General purpose, CPU-bound workloads |
| **Priority** | 32 priority levels (0-31) | Real-time applications, task prioritization |
| **CFS** | Completely Fair Scheduler with nice values | Interactive workloads, proportional fairness |

### Synchronization Primitives
- **Mutex** — Normal, recursive, and error-checking variants
- **Condition Variables** — With signal, broadcast, and timed wait
- **Semaphores** — Counting semaphores with try and timed operations
- **Read-Write Locks** — Multiple readers, single writer

### Additional Features
- Preemptive scheduling via `SIGALRM`
- Runtime statistics and debugging support
- Memory-safe stack allocation with mmap

---

## Quick Start

```c
#include <stdio.h>
#include "uthread.h"

void *worker(void *arg) {
    int id = (int)(intptr_t)arg;
    printf("Hello from thread %d\n", id);
    return NULL;
}

int main() {
    uthread_init(SCHED_ROUND_ROBIN);

    uthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        uthread_create(&threads[i], NULL, worker, (void*)(intptr_t)i);
    }

    for (int i = 0; i < 4; i++) {
        uthread_join(threads[i], NULL);
    }

    uthread_shutdown();
    return 0;
}
```

---

## Installation

### Prerequisites

| Requirement | Version |
|-------------|---------|
| Operating System | Linux x86-64 |
| Compiler | GCC 11+ or Clang 14+ |
| Build System | CMake 3.16+ |
| C Standard | C11 |

### Build from Source

```bash
# Clone the repository
git clone https://github.com/yourusername/libuthread.git
cd libuthread

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# Run tests
ctest --output-on-failure

# Install (optional)
sudo make install
```

### Build Options

```bash
# Debug build with symbols
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release build with optimizations
cmake -DCMAKE_BUILD_TYPE=Release ..
```

---

## Usage

### Basic Thread Operations

```c
#include "uthread.h"

// Initialize with desired scheduler
uthread_init(SCHED_ROUND_ROBIN);  // or SCHED_PRIORITY, SCHED_CFS

// Create a thread
uthread_t thread;
uthread_create(&thread, NULL, my_function, arg);

// Wait for completion
void *result;
uthread_join(thread, &result);

// Cleanup
uthread_shutdown();
```

### Using Mutex

```c
uthread_mutex_t mutex;
uthread_mutex_init(&mutex, NULL);

uthread_mutex_lock(&mutex);
// Critical section
uthread_mutex_unlock(&mutex);

uthread_mutex_destroy(&mutex);
```

### Using Condition Variables

```c
uthread_mutex_t mutex;
uthread_cond_t cond;
int ready = 0;

// Waiter thread
uthread_mutex_lock(&mutex);
while (!ready) {
    uthread_cond_wait(&cond, &mutex);
}
uthread_mutex_unlock(&mutex);

// Signaler thread
uthread_mutex_lock(&mutex);
ready = 1;
uthread_cond_signal(&cond);
uthread_mutex_unlock(&mutex);
```

### Thread Attributes

```c
uthread_attr_t attr;
uthread_attr_init(&attr);
uthread_attr_setstacksize(&attr, 128 * 1024);  // 128KB stack
uthread_attr_setpriority(&attr, 20);            // Priority 20
uthread_attr_setdetachstate(&attr, UTHREAD_CREATE_DETACHED);

uthread_t thread;
uthread_create(&thread, &attr, worker, NULL);
uthread_attr_destroy(&attr);
```

---

## API Reference

### Thread Management

| Function | Description |
|----------|-------------|
| `uthread_init(policy)` | Initialize library with scheduling policy |
| `uthread_shutdown()` | Shutdown library and cleanup |
| `uthread_create()` | Create a new thread |
| `uthread_join()` | Wait for thread termination |
| `uthread_detach()` | Detach a thread |
| `uthread_yield()` | Voluntarily yield CPU |
| `uthread_exit()` | Terminate calling thread |
| `uthread_self()` | Get current thread handle |
| `uthread_equal()` | Compare thread handles |
| `uthread_sleep()` | Sleep for milliseconds |

### Synchronization

| Function | Description |
|----------|-------------|
| `uthread_mutex_init/destroy()` | Initialize/destroy mutex |
| `uthread_mutex_lock/unlock()` | Lock/unlock mutex |
| `uthread_mutex_trylock()` | Non-blocking lock attempt |
| `uthread_cond_init/destroy()` | Initialize/destroy condition variable |
| `uthread_cond_wait()` | Wait on condition |
| `uthread_cond_signal/broadcast()` | Wake one/all waiters |
| `uthread_sem_init/destroy()` | Initialize/destroy semaphore |
| `uthread_sem_wait/post()` | Decrement/increment semaphore |
| `uthread_rwlock_rdlock/wrlock()` | Acquire read/write lock |
| `uthread_rwlock_unlock()` | Release read-write lock |

### Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `UTHREAD_SUCCESS` | 0 | Operation successful |
| `UTHREAD_EINVAL` | 22 | Invalid argument |
| `UTHREAD_ENOMEM` | 12 | Out of memory |
| `UTHREAD_EBUSY` | 16 | Resource busy |
| `UTHREAD_EDEADLK` | 35 | Deadlock detected |
| `UTHREAD_ETIMEDOUT` | 110 | Operation timed out |

---

## Scheduling Policies

### Round-Robin (`SCHED_ROUND_ROBIN`)

- **Algorithm**: FIFO queue with time-slicing
- **Timeslice**: 10ms (configurable)
- **Fairness**: Equal CPU time for all threads
- **Best for**: General-purpose workloads

### Priority (`SCHED_PRIORITY`)

- **Algorithm**: Multi-level priority queues
- **Levels**: 32 (0 = lowest, 31 = highest)
- **Preemption**: Higher priority always preempts lower
- **Best for**: Real-time applications, task prioritization

```c
uthread_attr_t attr;
uthread_attr_init(&attr);
uthread_attr_setpriority(&attr, 25);  // High priority
```

### CFS (`SCHED_CFS`)

- **Algorithm**: Red-black tree sorted by virtual runtime
- **Nice values**: -20 (highest) to +19 (lowest)
- **Fairness**: Proportional to weight (derived from nice)
- **Best for**: Interactive workloads, fair CPU distribution

```c
uthread_attr_t attr;
uthread_attr_init(&attr);
uthread_attr_setnice(&attr, -10);  // Higher priority (more CPU time)
```

---

## Project Structure

```
libuthread/
├── include/
│   └── uthread.h              # Public API header
├── src/
│   ├── internal.h             # Internal structures and declarations
│   ├── uthread.c              # Core thread management
│   ├── context.c              # Context switching (ucontext)
│   ├── scheduler.c            # Scheduler framework
│   ├── sched_rr.c             # Round-Robin implementation
│   ├── sched_priority.c       # Priority scheduler implementation
│   ├── sched_cfs.c            # CFS implementation (RB-tree)
│   ├── timer.c                # Preemption timer (SIGALRM)
│   ├── mutex.c                # Mutex implementation
│   ├── condvar.c              # Condition variables
│   ├── semaphore.c            # Semaphores
│   └── rwlock.c               # Read-write locks
├── tests/
│   ├── test_basic.c           # Basic thread tests
│   ├── test_sync.c            # Synchronization tests
│   ├── test_scheduler.c       # Scheduler tests
│   ├── test_stress.c          # Stress tests
│   └── classic/
│       ├── producer_consumer.c
│       ├── dining_philosophers.c
│       └── readers_writers.c
├── examples/
│   └── parallel_sum.c         # Parallel computation example
├── benchmarks/
│   ├── context_switch.c       # Context switch latency
│   ├── creation.c             # Thread creation rate
│   └── mutex.c                # Mutex performance
├── CMakeLists.txt
├── README.md
├── LICENSE
├── CONTRIBUTING.md
└── .gitignore
```

---

## Testing

### Run All Tests

```bash
cd build
ctest --output-on-failure
```

### Run Specific Tests

```bash
./test_basic       # Thread creation, join, yield
./test_sync        # Mutex, condvar, semaphore, rwlock
./test_scheduler   # All scheduling algorithms
./test_stress      # High-load stress tests
```

### Classic Concurrency Problems

```bash
./producer_consumer     # Bounded buffer
./dining_philosophers   # Deadlock avoidance
./readers_writers       # RWLock demonstration
```

### Memory Checking

```bash
valgrind --leak-check=full --show-leak-kinds=all ./test_basic
```

---

## Benchmarks

Run performance benchmarks:

```bash
./bench_context_switch   # Context switch latency
./bench_creation         # Thread creation/join rate
./bench_mutex            # Mutex lock/unlock throughput
```

### Sample Results (Reference Only)

| Metric | Round-Robin | Priority | CFS |
|--------|-------------|----------|-----|
| Context Switch | ~500 ns | ~600 ns | ~800 ns |
| Thread Create | ~2 μs | ~2 μs | ~2.5 μs |
| Mutex Lock/Unlock | ~100 ns | ~100 ns | ~100 ns |

*Results vary based on system configuration.*

---

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

### Areas for Contribution

- Bug fixes and improvements
- Additional scheduling algorithms
- Platform support (ARM, macOS)
- Documentation and examples
- Performance optimizations

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## Acknowledgments

- Operating Systems: Three Easy Pieces (Remzi H. Arpaci-Dusseau)
- Linux Kernel CFS Scheduler
- POSIX Threads Specification

---

## Author
Aaron C. Abraham - [aaronabraham06@gmail.com](mailto:aaronabraham06@gmail.com)

Created as an educational project to learn operating systems concepts including:
- Context switching mechanics
- CPU scheduling algorithms
- Synchronization primitives
- Signal handling in userspace
