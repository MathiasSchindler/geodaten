# Nolibc Threading Layer

The Linux threading layer is intentionally small and syscall-backed. It is meant for bounded worker pipelines such as OSM PBF fileblock inflation and parsing.

## API

The public API is declared in `src/shared/platform.h`:

- `platform_thread_start(PlatformThread *thread, PlatformThreadMain entry, void *arg, size_t stack_size)` starts one Linux thread. A `stack_size` of `0` uses the default 64 KiB stack.
- `platform_thread_join(PlatformThread *thread, int *result_out)` waits for completion and returns the worker result.
- `platform_mutex_init`, `platform_mutex_lock`, and `platform_mutex_unlock` provide a futex-backed mutex.

## Linux Implementation

`src/platform/linux/thread.c` uses:

- `mmap` for per-thread stacks.
- `clone` with `CLONE_VM`, `CLONE_FS`, `CLONE_FILES`, `CLONE_SIGHAND`, `CLONE_THREAD`, `CLONE_SYSVSEM`, `CLONE_PARENT_SETTID`, and `CLONE_CHILD_CLEARTID`.
- `futex` for joins and mutex waiting.
- `munmap` after a joined thread no longer needs its stack.

The x86_64 clone entry shim is in `src/arch/x86_64/linux/syscall_stubs.S`; it starts the C thread entry on the newly supplied stack and exits the calling thread with the `exit` syscall when the entry returns.

## Test

Build and run:

```sh
make test-thread
./build/freestanding-linux-x86_64/test-thread
./build/freestanding-linux-x86_64/test-thread 16
./build/freestanding-linux-x86_64/test-thread 24
```

The test starts worker threads, uses the mutex to update a shared counter, joins each thread, validates each return value, and checks the final counter. Optional arguments are `workers` and `iterations`; the defaults are 4 workers and 25,000 iterations per worker. The worker limit is 256.

`pbf-info` can use the same threading layer for parallel PBF fileblock inflation and parsing:

```sh
./build/freestanding-linux-x86_64/pbf-info --threads 16 data/germany-060524.osm.pbf
```

The reader stays serial and feeds a bounded queue. Worker threads decode and parse independent fileblocks, then merge per-worker summaries.

Measured on the current Linux x86_64 host:

| Dataset | 1 thread | 4 threads | 8 threads | 16 threads |
| --- | ---: | ---: | ---: | ---: |
| Hamburg | 0.61s | 0.16s | 0.12s | 0.16s |
| Brandenburg/Berlin | 3.43s | 0.86s | 0.52s | 0.79s |
| Germany | 54.02s | 14.65s | 7.78s | 11.11s |

After replacing per-block zlib Huffman table heap allocation with stack-backed tables, the best repeated Germany run was `--threads 9` at 7.64s. `--threads 8` remains the conservative recommendation because it is almost as fast and uses less memory.

Negative experiments:

- Keeping decompression output buffers attached to queue slots increased memory use and did not improve Germany throughput.
- Increasing the queue depth from two slots per worker to four slots per worker did not improve Germany throughput.

Useful experiments:

- Stack-backed zlib Huffman tables reduced allocator traffic and improved Germany `--threads 8` from about 8.35s to about 7.78s.
- One slot per worker reduced memory use, but did not beat two slots per worker on Germany.

`pbf-to-rpack` also uses the threaded PBF fileblock stream for selected entity phases:

```sh
./build/freestanding-linux-x86_64/pbf-to-rpack --tile-zoom 10 --threads 8 data/brandenburg-260524.osm.pbf build/brandenburg.rpack
```

`--threads` controls the node-coordinate collection pass. That pass scales well because workers decode dense nodes independently, probe a shared read-only node hash, and merge only source-node counters. On Brandenburg at tile zoom 10, `collect_nodes` dropped from 6419 ms with `--threads 1` to 943 ms with `--threads 8`, and full conversion dropped from 17.97s to 12.38s.

The builder also has `--way-threads N` for the worker-local way collector. It is correct and preserves the same pack counts, but it is currently a negative performance experiment on the benchmark host: `--threads 8 --way-threads 8` raised full Brandenburg conversion to 21.86s because generic way/tag parsing and allocation overhead dominate. Keep `--way-threads 1` unless that path is optimized further.

## Current Scope

This is currently implemented for Linux x86_64, matching the active freestanding build target. macOS, Windows, and aarch64 can keep the same public API with platform-specific implementations later.