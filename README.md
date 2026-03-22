# tickproc

High-performance C++20 command-line tool for processing 2 GB+ CSV files of financial tick data.

**This project strictly adheres to RAII principles to ensure resource safety and exception-guaranteed cleanup.** Every system resource — memory-mapped file regions, file descriptors, OS handles, mutex locks, and thread lifetimes — is acquired in a constructor and released in a destructor, with no manual cleanup calls required. Move semantics are used throughout to transfer ownership without copying.

[![CMake Build & Test](https://github.com/KyleH777/C--/actions/workflows/cmake.yml/badge.svg)](https://github.com/KyleH777/C--/actions/workflows/cmake.yml)

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              Disk (CSV)                                │
│                         2 GB+ tick data file                           │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                          open() / fstat()
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│                     MappedFile (RAII)                                   │
│                                                                         │
│  Linux/macOS: mmap(PROT_READ, MAP_PRIVATE) + madvise(MADV_SEQUENTIAL)  │
│  Windows:     CreateFileMapping(PAGE_READONLY) + MapViewOfFile          │
│                                                                         │
│  ► Zero-copy: the OS maps file pages directly into virtual memory.     │
│    No read() syscalls, no user-space buffers, no memcpy.               │
│  ► Kernel read-ahead pre-fetches pages before we touch them.           │
│  ► RAII: destructor calls munmap()/UnmapViewOfFile() automatically.    │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                        const char* data()
                        (pointer into mapped region)
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│                      ChunkPartitioner                                   │
│                                                                         │
│  Divides the mapped region into N line-aligned spans (one per producer │
│  thread).  Each span ends on a '\n' boundary so no CSV row is ever     │
│  split across chunks.                                                  │
│                                                                         │
│  ► N = hardware_concurrency() / 2 (auto-scaled)                        │
└──────┬────────────┬────────────┬────────────┬───────────────────────────┘
       │            │            │            │
   chunk[0]     chunk[1]     chunk[2]      chunk[N-1]
       │            │            │            │
┌──────▼──────┬─────▼──────┬─────▼──────┬─────▼──────┐
│ Producer 0  │ Producer 1 │ Producer 2 │ Producer N │   ← std::jthread
│             │            │            │            │
│ CsvParser::for_each_row()                          │
│   • memchr() for comma scanning (SIMD in libc)     │
│   • string_view fields → zero allocation           │
│   • fast_float::from_chars() for doubles           │
│   • std::from_chars() for integers                 │
│                                                     │
│ Batches of 256 TickRows pushed to queue             │
└──────┬──────┴─────┬──────┴─────┬──────┴─────┬──────┘
       │            │            │            │
       ▼            ▼            ▼            ▼
┌─────────────────────────────────────────────────────┐
│              ThreadSafeQueue<TickRow>                │
│                  (bounded MPMC)                      │
│                                                     │
│  • std::mutex + dual std::condition_variable        │
│  • Bounded capacity → backpressure when consumers   │
│    lag, capping memory regardless of file size       │
│  • RAII lock_guard/unique_lock in every operation   │
│  • Spurious-wakeup safe (predicated wait)           │
│  • shutdown() drains remaining items before exit    │
└──────┬──────┬─────┬──────┬──────────────────────────┘
       │      │     │      │
       ▼      ▼     ▼      ▼
┌──────────────────────────────────────────────────────┐
│  Consumer 0  │  Consumer 1  │  ...  │  Consumer M   │  ← std::jthread
│                                                      │
│  DataProcessor::process(TickRow)                     │
│    • mutex-guarded per-symbol aggregation            │
│    • std::unique_ptr<ProcessedResult> lifecycle      │
│    • VWAP, turnover, volume, tick count              │
└──────────────────────┬───────────────────────────────┘
                       │
                  finalise()
                       │
                       ▼
              ┌─────────────────┐
              │  Console Output  │
              │  (results table) │
              └─────────────────┘
```

### RAII Resource Map

| Resource | RAII Owner | Acquisition | Release |
|---|---|---|---|
| Memory-mapped region | `MappedFile` | `mmap()` / `MapViewOfFile()` | `munmap()` / `UnmapViewOfFile()` in destructor |
| File descriptor / HANDLE | `MappedFile` | `open()` / `CreateFile()` | `close()` / `CloseHandle()` in destructor |
| Mutex locks | `std::lock_guard` / `std::unique_lock` | Constructor | Destructor (even on exception) |
| Worker threads | `std::jthread` / `WorkerPool` | `start()` / emplace | `join()` in destructor |
| Heap-allocated results | `std::unique_ptr<ProcessedResult>` | `std::make_unique()` | Destructor (automatic) |

## Project Structure

```
tickproc/
├── CMakeLists.txt              # C++20, -O3, -flto, FetchContent(fast_float)
├── include/
│   ├── mapped_file.h           # RAII mmap wrapper (POSIX + Win32)
│   ├── thread_safe_queue.h     # Bounded MPMC queue with backpressure
│   ├── tick_row.h              # TickRowView (zero-copy) + TickRow (owning)
│   ├── csv_parser.h            # memchr-based parser + chunk partitioner
│   ├── pipeline.h              # Producer-Consumer orchestrator
│   ├── worker_pool.h           # Dynamic thread pool (hardware_concurrency)
│   └── data_processor.h        # Mutex-guarded aggregator with unique_ptr
├── src/
│   ├── main.cpp                # CLI entry point
│   ├── mapped_file.cpp         # Platform-specific mmap/CreateFileMapping
│   └── csv_parser.cpp          # Parser implementation (uses fast_float)
├── bench/
│   └── benchmark.cpp           # Single-threaded vs multi-threaded comparison
├── scripts/
│   └── generate_csv.py         # Generate dummy tick data (configurable size)
└── tests/
    ├── test_parser.cpp          # Line parsing, CRLF, partitioning
    ├── test_queue.cpp           # Single-thread, MPMC, backpressure
    └── test_data_processor.cpp  # Aggregation, thread safety, ownership
```

## Build

```bash
cd tickproc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run

```bash
# Generate test data
python3 scripts/generate_csv.py --size-gb 1.0

# Process
./build/tickproc tick_data.csv

# Benchmark (single-threaded vs multi-threaded comparison)
./build/tickproc_bench tick_data.csv
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

## CI/CD

Every push and pull request triggers the [CMake Build & Test](https://github.com/KyleH777/C--/actions/workflows/cmake.yml) workflow, which builds on Ubuntu and macOS in both Debug and Release modes, runs all unit tests, and performs a smoke test with a 10 MB generated CSV.

## Key Design Decisions

| Decision | Rationale |
|---|---|
| `madvise(MADV_SEQUENTIAL)` | Triggers aggressive kernel read-ahead for sequential 2 GB+ scans |
| `fast_float` for double parsing | 4-10x faster than `std::stod` / `strtod`, IEEE-754 compliant |
| `string_view` fields | Point directly into the mmap region -- zero heap allocation per row |
| Bounded queue (backpressure) | Caps memory to `capacity * sizeof(TickRow)` regardless of file size |
| Per-thread batch push (256 rows) | Reduces mutex acquisitions by ~256x on the hot path |
| `MAP_PRIVATE` + read-only | Pages shared with the kernel page cache, no COW overhead |
