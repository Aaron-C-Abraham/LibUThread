# Contributing to LibUThread

Thank you for your interest in contributing to LibUThread! This document provides guidelines and information for contributors.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [How to Contribute](#how-to-contribute)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment. Please:

- Be respectful and considerate in all interactions
- Welcome newcomers and help them get started
- Focus on constructive feedback
- Accept responsibility for mistakes and learn from them

## Getting Started

1. Fork the repository on GitHub
2. Clone your fork locally
3. Set up the development environment (see below)
4. Create a branch for your changes
5. Make your changes and test them
6. Submit a pull request

## How to Contribute

### Reporting Bugs

- Check if the bug has already been reported in Issues
- Use the bug report template if available
- Include:
  - Clear description of the bug
  - Steps to reproduce
  - Expected vs actual behavior
  - System information (OS, compiler version)
  - Relevant code snippets or test cases

### Suggesting Features

- Open an issue with the "enhancement" label
- Describe the feature and its use case
- Explain why it would be valuable

### Code Contributions

Areas where contributions are welcome:

- Bug fixes
- Performance optimizations
- Additional scheduling algorithms
- New synchronization primitives
- Documentation improvements
- Test coverage improvements
- Platform support (e.g., ARM, macOS)

## Development Setup

### Prerequisites

- Linux x86-64 (Ubuntu 22.04+ or Fedora 38+ recommended)
- GCC 11+ or Clang 14+
- CMake 3.16+
- Valgrind (for memory checking)
- GDB (for debugging)

### Building

```bash
# Clone your fork
git clone https://github.com/YOUR_USERNAME/libuthread.git
cd libuthread

# Create build directory
mkdir build && cd build

# Configure
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Build
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Debug Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-DDEBUG" ..
```

## Coding Standards

### Style Guide

- **Language**: C11 standard
- **Indentation**: 4 spaces (no tabs)
- **Line length**: Maximum 100 characters
- **Braces**: Opening brace on same line (K&R style)
- **Naming**:
  - Functions: `snake_case` (e.g., `uthread_create`)
  - Macros: `UPPER_SNAKE_CASE` (e.g., `UTHREAD_MAX_THREADS`)
  - Types: `snake_case_t` (e.g., `uthread_t`)
  - Internal functions: prefix with module name (e.g., `scheduler_yield`)

### Example

```c
/**
 * Brief description of the function.
 *
 * @param param1 Description of param1
 * @param param2 Description of param2
 * @return Description of return value
 */
int uthread_example_function(int param1, void *param2)
{
    if (param1 < 0) {
        return UTHREAD_EINVAL;
    }

    preemption_disable();

    /* Implementation */
    int result = do_something(param1, param2);

    preemption_enable();

    return result;
}
```

### Documentation

- All public functions must have Doxygen-style comments
- Complex algorithms should have inline explanations
- Update README.md if adding new features

### Error Handling

- Return error codes (not -1)
- Use defined constants (e.g., `UTHREAD_EINVAL`)
- Check all pointer parameters for NULL
- Clean up resources on error paths

## Testing

### Running Tests

```bash
# All tests
ctest --output-on-failure

# Specific test
./test_basic
./test_sync

# With verbose output
ctest -V
```

### Memory Checking

```bash
valgrind --leak-check=full --show-leak-kinds=all ./test_basic
```

### Writing Tests

- Add tests for any new functionality
- Tests should be deterministic (no flaky tests)
- Test both success and failure cases
- Use the existing test framework pattern

## Submitting Changes

### Pull Request Process

1. **Create a branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make commits**
   - Use clear, descriptive commit messages
   - Keep commits focused and atomic
   - Format: `component: brief description`
   - Example: `scheduler: add priority inheritance for mutexes`

3. **Test your changes**
   ```bash
   make && ctest --output-on-failure
   valgrind --leak-check=full ./test_basic
   ```

4. **Push and create PR**
   ```bash
   git push origin feature/your-feature-name
   ```
   Then create a Pull Request on GitHub

### PR Requirements

- [ ] Code compiles without warnings (`-Wall -Wextra -Wpedantic`)
- [ ] All existing tests pass
- [ ] New tests added for new functionality
- [ ] No memory leaks (Valgrind clean)
- [ ] Documentation updated if needed
- [ ] Follows coding standards

### Review Process

- PRs require at least one approval
- Address review feedback promptly
- Keep PRs focused (one feature/fix per PR)
- Rebase on main if needed

## Questions?

Feel free to open an issue for any questions about contributing!
