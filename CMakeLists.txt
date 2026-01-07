cmake_minimum_required(VERSION 3.16)
project(libuthread VERSION 1.0.0 LANGUAGES C)

# C11 standard required
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Platform check
if(NOT UNIX)
    message(WARNING "LibUThread is designed for Linux x86-64. Some features may not work on other platforms.")
endif()

# Library source files
set(LIBUTHREAD_SOURCES
    src/uthread.c
    src/context.c
    src/scheduler.c
    src/sched_rr.c
    src/sched_priority.c
    src/sched_cfs.c
    src/timer.c
    src/mutex.c
    src/condvar.c
    src/semaphore.c
    src/rwlock.c
)

# Create shared library
add_library(uthread SHARED ${LIBUTHREAD_SOURCES})
target_include_directories(uthread PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(uthread PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Create static library
add_library(uthread_static STATIC ${LIBUTHREAD_SOURCES})
target_include_directories(uthread_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(uthread_static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(uthread_static PROPERTIES OUTPUT_NAME uthread)

# Link with pthread for atomic operations (if needed)
if(UNIX)
    target_link_libraries(uthread PRIVATE pthread)
    target_link_libraries(uthread_static PRIVATE pthread)
endif()

# Enable testing
enable_testing()

# Test executables
add_executable(test_basic tests/test_basic.c)
target_link_libraries(test_basic uthread_static)
add_test(NAME test_basic COMMAND test_basic)

add_executable(test_sync tests/test_sync.c)
target_link_libraries(test_sync uthread_static)
add_test(NAME test_sync COMMAND test_sync)

add_executable(test_scheduler tests/test_scheduler.c)
target_link_libraries(test_scheduler uthread_static)
add_test(NAME test_scheduler COMMAND test_scheduler)

add_executable(test_stress tests/test_stress.c)
target_link_libraries(test_stress uthread_static)
add_test(NAME test_stress COMMAND test_stress)

# Classic concurrency problem tests
add_executable(producer_consumer tests/classic/producer_consumer.c)
target_link_libraries(producer_consumer uthread_static)

add_executable(dining_philosophers tests/classic/dining_philosophers.c)
target_link_libraries(dining_philosophers uthread_static)

add_executable(readers_writers tests/classic/readers_writers.c)
target_link_libraries(readers_writers uthread_static)

# Example applications
add_executable(example_parallel_sum examples/parallel_sum.c)
target_link_libraries(example_parallel_sum uthread_static)

# Benchmarks
add_executable(bench_context_switch benchmarks/context_switch.c)
target_link_libraries(bench_context_switch uthread_static)

add_executable(bench_creation benchmarks/creation.c)
target_link_libraries(bench_creation uthread_static)

add_executable(bench_mutex benchmarks/mutex.c)
target_link_libraries(bench_mutex uthread_static)

# Installation
install(TARGETS uthread uthread_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES include/uthread.h DESTINATION include)

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/uthreadConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Summary
message(STATUS "LibUThread ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
