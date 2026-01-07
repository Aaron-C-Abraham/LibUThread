# Changelog

All notable changes to LibUThread will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2025-01-06

### Added
- Initial release of LibUThread
- **Thread Management**
  - `uthread_create()` - Create new threads
  - `uthread_join()` - Wait for thread termination
  - `uthread_detach()` - Detach threads
  - `uthread_yield()` - Voluntary CPU yield
  - `uthread_exit()` - Thread termination
  - `uthread_self()` - Get current thread handle
  - `uthread_sleep()` - Sleep for milliseconds
  - Thread attributes (stack size, priority, detach state)

- **Scheduling Algorithms**
  - Round-Robin scheduler with configurable timeslice
  - Priority scheduler with 32 priority levels
  - CFS (Completely Fair Scheduler) with nice values
  - Runtime scheduler selection
  - Preemptive scheduling via SIGALRM

- **Synchronization Primitives**
  - Mutex (normal, recursive, error-checking)
  - Condition variables (wait, signal, broadcast, timed wait)
  - Counting semaphores (wait, post, trywait, timed wait)
  - Read-write locks (rdlock, wrlock, trylock)

- **Additional Features**
  - Guard pages for stack overflow detection
  - Thread naming for debugging
  - Runtime statistics collection
  - Debug dump functionality

- **Tests**
  - Basic thread tests
  - Synchronization tests
  - Scheduler tests
  - Stress tests
  - Classic concurrency problems (Producer-Consumer, Dining Philosophers, Readers-Writers)

- **Benchmarks**
  - Context switch latency benchmark
  - Thread creation benchmark
  - Mutex performance benchmark

- **Documentation**
  - Comprehensive README with examples
  - API documentation
  - Contributing guidelines

### Technical Details
- Uses `ucontext` API for context switching
- Stack allocation via `mmap` with guard pages
- Red-black tree for CFS vruntime tracking
- FIFO queues for Round-Robin
- Multi-level queues with bitmap for Priority scheduler

---

## Version History

| Version | Date | Description |
|---------|------|-------------|
| 1.0.0 | 2025-01-06 | Initial release |

---

[Unreleased]: https://github.com/yourusername/libuthread/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/yourusername/libuthread/releases/tag/v1.0.0
