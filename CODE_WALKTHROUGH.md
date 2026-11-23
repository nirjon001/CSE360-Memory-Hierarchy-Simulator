# Code Walkthrough — How the Simulator Is Built and How It Runs

This document explains **what each file does**, **how control moves from one file to
another**, and **what happens to one memory address from the moment you press Enter
until a hit/miss is printed**. It is meant to be read alongside the source code.

If you only want the commands, see [README.md](README.md). This file is the
"why and how" behind them.

---

## 1. The big picture

The program is a **trace-driven simulator**. It does not execute a real program;
instead it reads a list of addresses (a *trace*) and asks a software model of a
cache hierarchy whether each address hits or misses.

```
                        ┌──────────────────────────────────────────┐
                        │  main.c  (boss / coordinator)            │
                        │  - reads command line                    │
                        │  - builds trace + hierarchy              │
                        │  - drives the loop, prints results       │
                        └───────┬──────────────────┬───────────────┘
                                │                  │
              "give me the      │                  │  "process this address"
               next address"    │                  │
                                ▼                  ▼
                  ┌──────────────────────┐  ┌──────────────────────────────┐
                  │ trace.c / trace.h    │  │ hierarchy.c / hierarchy.h    │
                  │ - file parser        │  │ - parses "16K,64,4" specs    │
                  │ - workload generators│  │ - builds L1 → L2 → L3 chain  │
                  │ - Trace = dynamic    │  │ - sends address into L1      │
                  │   array of accesses  │  │ - computes AMAT + reports    │
                  └──────────────────────┘  └──────────────┬───────────────┘
                                                           │
                                                           │ "L1, check this address;
                                                           │  if you miss, ask me again"
                                                           ▼
                                           ┌──────────────────────────────┐
                                           │ cache.c / cache.h            │
                                           │ - ONE cache level            │
                                           │ - tag / index / offset split │
                                           │ - lookup, LRU/FIFO/Random    │
                                           │ - write-back/through         │
                                           │ - recursive call to `next`   │
                                           └──────────────┬───────────────┘
                                                          │
                                                          ▼
                                            main memory (just a latency number)
```

The key idea: **`cache.c` knows nothing about L1/L2/L3**. It is one generic cache
with a pointer `next` to the next cache. `hierarchy.c` wires several of them
together. `main.c` owns everything.

---

## 2. File-by-file responsibility

| File | Role |
| --- | --- |
| `include/cache.h` | Defines `CacheLine`, `Cache`, `CacheResult`, the policy enums, and the cache API. |
| `src/cache.c` | Implements one cache level: initialisation, address split, lookup, victim choice, writeback, recursion. |
| `include/hierarchy.h` | Defines `LevelConfig`, `Hierarchy`, and the hierarchy/AMAT API. |
| `src/hierarchy.c` | Parses cache specs, builds the L1→L2→L3 chain, forwards accesses, computes AMAT, prints reports. |
| `include/trace.h` | Defines `TraceEntry`, `Trace`, `GenKind`, and the trace API. |
| `src/trace.c` | Loads trace files or generates synthetic workloads; grows an array with `realloc`. |
| `src/main.c` | `main()`, CLI parsing, the simulation loop, the comparison demo, and final reporting. |
| `include/sim_runner.h`, `src/sim_runner.c` | Thin shared layer: build hierarchy from specs, step one access with per-level HIT/MISS, run a trace with a callback, expose read-only snapshots. |
| `gui/main.cpp` | Raylib front-end: six-tier pyramid dashboard, hit/progress bars, glowing request dot, AMAT metrics, preset dropdown, what-if sliders, reset button. |
| `gui/imgui_main.cpp` | Previous Dear ImGui + SDL3 dashboard, kept as the optional `sim_gui_imgui` target. |
| `third_party/imgui/` | Vendored Dear ImGui 1.93.0 subset + MIT licence (only needed for `sim_gui_imgui`). |
| `CMakeLists.txt` | Builds `sim_core`, `sim_cli`, `sim_gui` and optional `sim_gui_imgui`; fetches Raylib 5.5 via FetchContent. |
| `Makefile`, `ROADMAP.md` | Legacy `gcc`/`make` path, and the project progress plan. |
| `.vscode/*` | Tasks (build/run), launch configs (F5 debugging), IntelliSense include paths. |

The build pipeline is simply:

```
src/*.c  --gcc -Iinclude -c-->  src/*.o  --gcc-->  ./memsim
```

Each `.c` includes its matching header from `include/`. No external libraries
are used — only the C standard library (`stdio.h`, `stdlib.h`, `string.h`) and
the POSIX `strcasecmp`/`strtok_r` (enabled by `-D_GNU_SOURCE` in the Makefile).

---

## 3. The important data structures

### 3.1 `TraceEntry` and `Trace` — `trace.h`

```c
typedef struct {
    uint64_t addr;       /* the byte address being accessed */
    uint8_t  is_write;   /* 0 = load (R), 1 = store (W)     */
} TraceEntry;

typedef struct {
    TraceEntry *items;   /* dynamic array, grown by realloc  */
    size_t      count;   /* how many entries are valid       */
    size_t      capacity;/* allocated slots                  */
} Trace;
```

This is a hand-rolled dynamic array — the same idea as `std::vector` in C++.
`trace_push()` doubles `capacity` when `count == capacity`.

### 3.2 `CacheLine`, `Cache`, `CacheResult` — `cache.h`

```c
typedef struct {
    uint64_t tag;        /* which block is stored here */
    uint64_t last_used;  /* clock value of last access      -> LRU  */
    uint64_t inserted;   /* clock value when filled         -> FIFO */
    uint8_t  valid;      /* 0 = empty line                  */
    uint8_t  dirty;      /* 1 = modified, must write back   */
} CacheLine;

struct Cache {
    ...
    CacheLine **sets;    /* sets[set_index][way]  */
    Cache      *next;    /* next level, NULL = main memory */
    uint64_t    clock;   /* global tick for LRU/FIFO */
    uint64_t    accesses, reads, writes, hits, read_hits, write_hits,
                misses, evictions, writebacks, cycles;
};

typedef struct {
    int      hit;            /* did THIS level hit? */
    uint64_t latency;        /* hit time + all lower-level time */
    int      wroteback;
    int      memory_access;
} CacheResult;
```

Physical layout of a 2-way cache with 4 sets:

```
sets ──► [ set 0 ] → [ way 0: CacheLine ][ way 1: CacheLine ]
         [ set 1 ] → [ way 0: CacheLine ][ way 1: CacheLine ]
         [ set 2 ] → [ way 0: CacheLine ][ way 1: CacheLine ]
         [ set 3 ] → [ way 0: CacheLine ][ way 1: CacheLine ]
```

`tag + set index + offset` selects exactly one `CacheLine`.

### 3.3 `LevelConfig` and `Hierarchy` — `hierarchy.h`

```c
typedef struct {
    size_t capacity, block_size, assoc;
    ReplacementPolicy replacement;
    WritePolicy write_policy;
    int write_allocate;
    uint32_t hit_latency;
    char text[160];        /* the original command-line string */
} LevelConfig;

typedef struct {
    Cache levels[3];       /* fixed-size array, L1..L3 */
    int   nlevels;
    uint32_t memory_latency;
    uint64_t total_accesses, total_cycles;
} Hierarchy;
```

`LevelConfig` is the *description* (from the command line). `Cache` is the
*live model* (with actual stored lines and counters). `hierarchy_init()` turns
the first into the second.

### 3.4 `Options` — `main.c`

A plain struct holding everything parsed from `argv`: three cache spec strings,
memory latency, workload kind and parameters, input/output filenames, and the
`verbose` / `demo` flags.

---

## 4. Program flow: from `main()` to the result

### Phase 0 — start and defaults (`main.c:288`)

`main()` fills `Options` with defaults:

```
L1 = "16K,64,1"   mem latency = 100   count = 200000
base = 0x10000000 param = 512         generator = sequential
```

If you run with **no arguments**, `main()` calls `run_demo()` and exits. If you
pass `-h`, it prints help and exits.

### Phase 1 — command-line parsing (`main.c:304`)

A simple `for` loop over `argv`. Each option writes one field of `Options`.
Notice that `-l2` / `-l3` being present is what *creates* extra levels — the
number of levels is computed later as:

```c
int nlevels = 1 + (o->l2 != NULL) + (o->l3 != NULL);
```

`parse_gen()` maps friendly names (`seq`, `row`, `hot`, …) to `GenKind` enum
values. `parse_u64()` uses `strtoull(..., 0)`, which is why `-b 0x1000` works.

### Phase 2 — build the address trace (`run_single`, `main.c:143`)

Two branches:

```
                    -t FILE given?
                   /              \
                yes                no
                 |                  |
       trace_load_file()     trace_generate()
                 |                  |
       parse each line        compute addr for i = 0..count-1
       "0x1000 R"             using the chosen workload formula
                 |                  |
                 └────────┬─────────┘
                          ▼
                  Trace.items[] = [ {addr,R/W}, {addr,R/W}, ... ]
```

`trace_load_file()` accepts both `0x1000 W` and `W 0x1000`, skips blank lines
and `#` comments. `trace_generate()` implements seven formulas (see §8).

If `-o FILE` was given, `save_trace()` dumps the array back out as hex — handy
for saving a random workload so every experiment uses exactly the same trace.

### Phase 3 — build the hierarchy (`build_hierarchy`, `main.c:84`)

```
main.c  build_hierarchy()
   │
   ├─ for each spec string (e.g. "16K,64,1"):
   │     hierarchy_parse_level()          [hierarchy.c:46]
   │       strtok_r on ','  → cap, block, ways, repl, wb/wt, wa/nwa, hitcyc
   │       parse_size()     → "16K" becomes 16384
   │       parse_assoc()    → "full" becomes 0 (meaning "not resolved yet")
   │
   └─ hierarchy_init()                    [hierarchy.c:100]
         for each level i:
           if assoc == 0  → assoc = capacity / block_size  (fully associative)
           next_latency = (i == last) ? memory_latency : next level's hit latency
           cache_init(...)       → allocates sets[][] with calloc
           cache_connect(L(i-1), L(i))   → links the chain
```

Result in memory (example with `-l1 16K,64,1 -l2 256K,64,8`):

```
Hierarchy
 ├─ nlevels = 2
 ├─ memory_latency = 100
 ├─ levels[0] = L1: sets=256, ways=1, hit=1, next_latency=10, next ──┐
 ├─ levels[1] = L2: sets=512, ways=8, hit=10, next_latency=100, next=NULL
 └─ total_cycles = 0, total_accesses = 0
                                                                  │
                                                                  └──► levels[1]
```

### Phase 4 — the simulation loop (`run_trace`, `main.c:101`)

This is the heart of the "from where to where" story:

```c
for (size_t i = 0; i < t->count; i++) {
    hierarchy_access(h, t->items[i].addr, t->items[i].is_write);
}
```

`hierarchy_access()` (`hierarchy.c:151`) does only three things:

1. `total_accesses++`
2. `r = cache_access(&h->levels[0], addr, is_write)` — enter L1
3. `total_cycles += r.latency` — the returned latency already includes L2/L3/memory

With `-v`, `run_trace()` snapshots each level's `accesses`/`hits` counters before
the call and compares them afterwards. That is how it knows whether a level was
`HIT`, `MISS`, or not reached (`---`) without changing the simulator API.

### Phase 5 — reporting (`hierarchy_report`, `hierarchy.c:208`)

Prints the configuration, per-level statistics, and both AMAT numbers:

* **AMAT (measured)** = `hierarchy_amat()` = `total_cycles / total_accesses`.
* **AMAT (formula)** = `hierarchy_theoretical_amat()`, a recursion over the
  configured hit times and observed miss rates.

### Phase 6 — teardown

`hierarchy_destroy()` frees every `sets[][]`; `trace_free()` frees the access
array. Both are called at the end of `run_single()`.

---

## 5. The complete call sequence (one-liner view)

```
main
 ├─ parse args
 ├─ run_single
 │   ├─ trace_init
 │   ├─ trace_load_file  OR  trace_generate ──► trace_push (realloc)
 │   ├─ build_hierarchy
 │   │   ├─ hierarchy_parse_level × N
 │   │   └─ hierarchy_init ──► cache_init × N ──► cache_connect × (N-1)
 │   ├─ run_trace ── for each access ──► hierarchy_access
 │   │                                      └─► cache_access(L1)
 │   │                                             └─► forward → cache_access(L2)
 │   │                                                    └─► forward → cache_access(L3)
 │   │                                                           └─► forward → "memory" (latency only)
 │   ├─ hierarchy_report
 │   ├─ hierarchy_destroy ──► cache_destroy × N
 │   └─ trace_free
 └─ return
```

Demo mode replaces `run_single` with `run_demo` → for each workload × config:
`trace_generate` → `run_config` → `build_hierarchy` → `run_trace` → `print_row`
→ `hierarchy_destroy`. See §9.

---

## 6. Deep dive: `cache_access()` (`cache.c:156`)

This single function is the whole cache model. Here is what happens, in order.

### Step 1 — bookkeeping

```c
c->clock++;  c->accesses++;
if (is_write) c->writes++; else c->reads++;
```

`clock` ticks on every access *received by this level*. It is the timestamp
source for LRU and FIFO.

### Step 2 — decode the address

```c
idx = cache_set_index(c, addr);   /* (addr >> offset_bits) & (num_sets - 1) */
tag = cache_tag(c, addr);         /*  addr >> (index_bits + offset_bits)    */
set = c->sets[idx];
```

`offset_bits = log2(block_size)` and `index_bits = log2(num_sets)` were computed
in `cache_init()`. Because both are powers of two, masking replaces division.

### Step 3 — search the set for the tag

```c
for (i = 0; i < associativity; i++)
    if (set[i].valid && set[i].tag == tag) { way = i; break; }
```

`way` stays equal to `associativity` if no line matched → miss.

### Step 4a — HIT path

```
hits++ (and read_hits++/write_hits++)
last_used = clock                    ← refreshes LRU position
if store:
    write-back  → dirty = 1          (memory not touched now)
    write-through → dirty = 0 and forward a write to next level
```

FIFO deliberately does **not** update `inserted` on a hit — a FIFO line keeps
its original arrival time.

### Step 4b — MISS path

```
misses++
1. Find an invalid way. If found, use it (no eviction).
2. Otherwise:
      evictions++
      LRU    → smallest last_used
      FIFO   → smallest inserted
      Random → xorshift(clock, tag) % ways
3. If the victim is dirty → write it back through a write buffer:
      reconstruct victim_addr = (tag << (index+offset)) | (set << offset)
      forward(victim_addr, WRITE)     ← lower level sees this traffic
      writebacks++ ; wroteback = 1
      NOTE: its latency is discarded: writeback does not stall the CPU.
4. Fetch the missing block:
      if (read OR write-allocate) → forward(addr, READ)  then install the line
      else (store, no-write-allocate) → forward(addr, WRITE), install nothing
5. If write-through with write-allocate → also forward(addr, WRITE)
```

When a line is installed, all four fields are set:

```c
valid=1; tag=tag;
dirty = (is_write && write_policy == WRITE_BACK);
last_used = clock;   inserted = clock;
```

### Step 5 — latency accounting

```c
c->cycles += res.latency;
return res;
```

`res.latency` starts at this level's hit time and every `forward()` result is
added to it. Because `forward()` recursively calls `cache_access(next, ...)`,
the latency of L3/memory bubbles all the way back up to L1. Each level also
adds the same total to its own `cycles` counter exactly once.

---

## 7. Worked example: following one address all the way through

Configuration: **L1 = 16 KiB, 64-byte blocks, 1-way, write-back**.

Derived values:

```
block_size  = 64   → offset_bits = 6   (bits 0..5)
line_size   = 64×1 = 64
num_sets    = 16384 / 64 = 256         → index_bits = 8 (bits 6..13)
tag         = remaining upper bits     (bit 14 and above)

address layout:
  [ tag ............ ][ index (8 bits) ][ offset (6 bits) ]
```

Trace (from `traces/sample.trace`):

| # | Address | R/W | Set | Tag | Result |
|---|---|---|---|---|---|
| 0 | `0x10000000` | R | 0 | `0x4000` | miss → set 0 empty, fetch block from memory, install |
| 1 | `0x10000004` | R | 0 | `0x4000` | **hit** — same 64-byte block, offset differs |
| 2 | `0x10000008` | R | 0 | `0x4000` | **hit** |
| 8 | `0x1000000c` | W | 0 | `0x4000` | **hit**, line becomes `dirty = 1` |
| 10 | `0x10000800` | R | 32 | `0x4000` | miss → different set, fetch new block |
| 14 | `0x10000000` | R | 0 | `0x4000` | hit — original line still in set 0 |

Math for the first address:

```
0x10000000 >> 6          = 0x400000
set = 0x400000 & 0xFF    = 0x00
tag = 0x10000000 >> 14   = 0x4000
```

Math for `0x10000800`:

```
0x10000800 >> 6          = 0x400020
set = 0x400020 & 0xFF    = 0x20  (32)
tag = 0x10000800 >> 14   = 0x4000   ← same tag, different set!
```

That is a **conflict miss** in a direct-mapped cache: the tags match but the
set indices differ, so the lines cannot coexist. This is exactly why the
`working-set` generator uses a 2 KiB stride — it forces addresses into a few
sets so you can see associativity help.

Now add an L2 (`256K,64,8`). On access #0:

```
L1 miss → forward(0x10000000, READ)
   L2 miss → forward(..., READ) → "memory" returns 100 cycles
   L2 result latency = 10 + 100 = 110, L2 installs the block
L1 result latency = 1 + 110 = 111
hierarchy_access adds 111 to total_cycles
```

On access #1, L1 hits immediately: latency = 1, L2 is never touched. With
`-v` the line would read `L1:HIT  L2:---`.

---

## 8. The workload generators (`trace.c:104`)

Each generator computes an address from the loop index `i`. `param` (`-p`) is
reused differently by each one:

| Kind (`-g`) | Address formula | What it demonstrates |
| --- | --- | --- |
| `seq` | `base + i*8` | compulsory misses only; ~1 miss per 8 accesses with 64 B blocks |
| `stride` | `base + (i*param) % 1MiB` | stride/conflict behaviour; `param` = stride bytes |
| `random` | xorshift inside 1 MiB, 8-byte aligned | almost no locality; `param` unused |
| `row` | `base + (i % N²)*8` | ideal row-major scan; `param` = matrix dimension N |
| `col` | `base + (row*N + col)*8` with `row = i%N`, `col = i/N` | column-major scan of row-major data → stride `N*8` |
| `ws` | `base + (i % param)*2048` | `param` hot lines in very few sets → associativity stress |
| `hot` | 4 hot lines + 2 cold lines, all same set | LRU/FIFO/Random comparison |

Note the `col` formula: the outer loop index `col = i/N` changes slowly, the
inner `row = i%N` changes fast. Multiplying `row` by `N` gives the large stride
that causes conflict misses. The `hot` generator is deterministic, so every
policy sees exactly the same addresses.

`xorshift()` is a tiny pseudo-random number generator. It is local (`state` is
a variable inside `trace_generate`) so runs are reproducible.

---

## 9. Demo mode (`main.c:227`)

`run_demo()` is a nested experiment runner:

```
for each of 5 workloads:            (sequential, row, col, random, ws)
    trace_generate(...)             ← one shared buffer
    for each of 6 configurations:   (1-way, 2-way, 4-way, full, 2-level, 3-level)
        run_config(...)
            build_hierarchy(...)
            run_trace(...)          ← same trace every time → fair comparison
            print_row(...)          ← L1 miss%, L2 miss%, AMAT formula, AMAT measured
            hierarchy_destroy(...)
    trace.count = 0                 ← keep the allocated array, reuse it for next workload
trace_free(...)

then: 3 extra runs of the `hot` workload with LRU / FIFO / Random
```

`trace.count = 0` is safe because `trace_push()` only allocates when
`count == capacity`; the existing capacity is reused. This avoids reallocating
a 200000-entry array five times.

`print_row()` deliberately prints **both** AMAT columns. In a correct model
they agree; seeing them diverge means a bookkeeping bug (this constraint was
used while developing the simulator to catch a double-counted cycle bug).

---

## 10. How AMAT is computed (`hierarchy.c:166`)

Two independent calculations:

**1. Measured** — count real cycles:

```
AMAT_measured = total_cycles / total_accesses
```

`total_cycles` is incremented once per program access by the latency returned
from L1, which already contains L2/L3/memory time through recursion.

**2. Formula** — recursive model:

```
AMAT(L1)  = hit1 + MR1 × penalty1
penalty1  = hit2 + MR2 × penalty2
penalty2  = hit3 + MR3 × memory_latency
```

`level_amat()` implements this:

```c
double mr = (i == 0) ? cache_miss_rate(c)        /* L1 sees all accesses */
                     : cache_read_miss_rate(c);  /* deeper levels: read path only */
```

Why the distinction? Deeper levels also receive **buffered writebacks** from
the level above. Those writes normally hit, which would artificially lower the
reported miss rate. `cache_read_miss_rate()` counts only read requests:

```c
(reads - read_hits) / reads
```

Worked example (2-level, `sequential`, from the demo):

```
L1 miss rate  = 12.5%    (one fill per 8-byte-group of 64-byte block)
L2 read miss rate = 100%          (streaming: every block is new to L2)
hit1=1, hit2=10, memory=100

AMAT = 1 + 0.125 × (10 + 1.00 × 100) = 1 + 13.75 = 14.75
```

The demo prints `14.750` in both columns.

---

## 11. Build and editor pipeline

### Terminal

```
make
  → gcc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -O2 -g -Iinclude -c src/cache.c -o src/cache.o
  → ... same for hierarchy.c, trace.c, main.c ...
  → gcc ... src/*.o -o memsim
```

`-D_GNU_SOURCE` exposes `strtok_r` and `strcasecmp`. `-Iinclude` lets the `.c`
files write `#include "cache.h"` instead of relative paths.

### VS Code mapping

| Shortcut | VS Code task | Actual command |
| --- | --- | --- |
| Ctrl+Shift+B | `C/C++: build (make)` | `make` |
| F5 | `Debug memsim (demo)` | build, then `gdb --args ./memsim --demo` |
| Ctrl+Shift+P → Run Task | `Run: demo comparison` | `make` then `./memsim --demo` |
| Run Task | `Run active C file` | `gcc ... '${file}' -o ... && ./...` |

`c_cpp_properties.json` gives IntelliSense the same `include/` path and the
C11 standard, so go-to-definition works across files.

---

## 12. Ownership and memory lifecycle

| Allocation | Allocated in | Freed in |
| --- | --- | --- |
| `Trace.items` | `trace_push()` via `realloc` | `trace_free()` |
| `Cache.sets` (outer array) | `cache_init()` via `calloc` | `cache_destroy()` |
| `Cache.sets[i]` (each set) | `cache_init()` via `calloc` | `cache_destroy()` |
| `Hierarchy` | stack in `run_single`/`run_config` | `hierarchy_destroy()` frees its caches |

`hierarchy_reset()` also exists: it zeroes counters **and** every `CacheLine`,
which lets you re-run a trace on the exact same warm/cold start.

---

## 13. Where to make changes for your assignment

| You want to… | Edit this |
| --- | --- |
| Add a replacement policy | enum in `include/cache.h`, victim `switch` in `cache.c:209`, parser in `hierarchy.c:71`, name in `policy_name()` |
| Add a workload | `GenKind` in `trace.h`, `switch` in `trace.c:115`, `gen_name()`, `parse_gen()` in `main.c:63` |
| Change latencies | `default_hits[]` in `main.c:16` or add the 7th field to a cache spec |
| Add an L4 level | `MAX_LEVELS` in `hierarchy.h`, name table in `hierarchy.c:132`, arrays in `main.c` |
| Change write policy behaviour | hit path `cache.c:188` and miss path `cache.c:240` |
| Add a statistic | field in `struct Cache`, increment in `cache_access()`, print in `cache_print_stats()` |
| Include writeback stalls in AMAT | stop discarding `wb.latency` at `cache.c:233` (then explain the measured/formula gap) |

---

## 14. Deliberate simplifications

Useful to mention in a report:

* **No TLB**, virtual-to-physical translation, or page faults — addresses are
  already physical.
* **Unified caches** — no separate instruction/data L1, no instruction trace.
* **No prefetching**, no write-combining beyond the simple buffered writeback,
  no out-of-order execution; latencies are added serially.
* **Single core**, no coherence or shared-cache effects.
* **Latencies are parameters**, not measured hardware timings; the simulator
  reports cycles, not wall-clock seconds.
* **Block-fetch granularity** is one block; the lower level is asked for the
  whole block, not individual bytes.

---

## 15. Quick function index

| Function | File | Line (approx.) | Job |
| --- | --- | --- | --- |
| `main` | `src/main.c` | 288 | entry point, arg loop, dispatch |
| `run_single` | `src/main.c` | 143 | one simulation from CLI options |
| `run_demo` | `src/main.c` | 227 | comparison table driver |
| `run_trace` | `src/main.c` | 101 | access loop + verbose output |
| `build_hierarchy` | `src/main.c` | 84 | specs → Hierarchy |
| `trace_generate` | `src/trace.c` | 104 | synthetic address formulas |
| `trace_load_file` | `src/trace.c` | 52 | parse a trace file |
| `trace_push` | `src/trace.c` | 25 | grow and append to the array |
| `hierarchy_init` | `src/hierarchy.c` | 100 | create and link cache levels |
| `hierarchy_parse_level` | `src/hierarchy.c` | 46 | parse `16K,64,4,lru,...` |
| `hierarchy_access` | `src/hierarchy.c` | 151 | send one access into L1 |
| `level_amat` | `src/hierarchy.c` | 173 | recursive formula |
| `hierarchy_report` | `src/hierarchy.c` | 208 | final printed report |
| `cache_init` | `src/cache.c` | 65 | allocate and configure one cache |
| `cache_access` | `src/cache.c` | 156 | lookup, hit/miss, victims, recursion |
| `cache_tag` / `cache_set_index` | `src/cache.c` | 138 / 143 | address decomposition |
| `cache_read_miss_rate` | `src/cache.c` | 278 | read-only miss rate for AMAT |

Line numbers are from the version this document was written against; they may
drift slightly as the code changes.

---

## 16. Suggested reading order

1. `include/trace.h` and `include/cache.h` — learn the vocabulary.
2. `src/main.c` → `main()` and `run_single()` — see the whole story at a glance.
3. `src/trace.c` → `trace_generate()` — understand where addresses come from.
4. `src/hierarchy.c` → `hierarchy_init()` and `hierarchy_access()` — see the wiring.
5. `src/cache.c` → `cache_access()` — the actual hardware model.
6. `src/hierarchy.c` → `level_amat()` — the theory that matches the measurements.
7. `include/sim_runner.h` + `gui/main.cpp` — how a second front-end reuses the
   exact same core without changing a single cache equation (see §17).

---

## 17. The multi-front-end architecture (CMake + `sim_runner` + two GUIs)

### Why a runner layer was needed

`cache.c`, `hierarchy.c` and `trace.c` were already clean header/source pairs,
so they were **not rewritten**. What was missing was a front-end-neutral way to:

1. turn the three `-l1/-l2/-l3` strings into a `Hierarchy`, and
2. advance exactly one access and report what each level did.

Those two jobs used to live inside `src/main.c`. They are now in
`sim_runner.h/.c`, so the terminal and graphical programs share them.

### The new dependency graph

```
                       ┌────────────────────────────┐
                       │         sim_core           │
                       │  cache.c hierarchy.c      │
                       │  trace.c  sim_runner.c     │
                       └──────┬─────────────┬───────┘
                              │             │
                   ┌──────────▼──────┐  ┌───▼────────────────┐
                   │     sim_cli      │  │      sim_gui       │
                   │ src/main.c      │  │ gui/main.cpp       │
                   │ original output │  │ Raylib pyramid     │
                   └─────────────────┘  └────────────────────┘
                                           (optional: sim_gui_imgui
                                            = gui/imgui_main.cpp)
```

Key detail: the three original `.c` files are compiled with the **C++ compiler**
via CMake's `LANGUAGE CXX` source property. They were verified to compile under
C++17 with zero changes, so no `extern "C"` wrappers or source edits were needed.

### `sim_step()` — how the GUI gets per-level hit/miss

`hierarchy_access()` deliberately returns `void`, so `sim_step()` snapshots every
level's `accesses` and `hits` counters immediately before and after the call:

```c
before_acc[i] = h->levels[i].accesses;
before_hit[i] = h->levels[i].hits;
hierarchy_access(h, addr, is_write);
info.reached[i] = (h->levels[i].accesses > before_acc[i]);
info.hit[i]     = info.reached[i] && (h->levels[i].hits > before_hit[i]);
info.latency    = h->total_cycles - cycles_before;
```

This gives the GUI the same information the `-v` flag uses in the CLI, without
changing the core API. `sim_cli` now calls `sim_step()` too, so both front-ends
walk the identical path. The demo output was verified byte-for-byte against the
pre-refactor `./memsim --demo`.

### `sim_snapshot()` — how the GUI reads statistics

Instead of parsing printed text, the GUI calls `sim_snapshot()` which copies
geometry and counters from each `Cache` into a plain `SimLevelSnapshot` struct.
The heatmap reads `c->sets[s][w]` directly (valid / dirty / timestamps), which is
why `CacheLine` is exposed in `cache.h`.

### Raylib GUI event flow

```
SDL/Wayland event → raylib PollInputEvents() (inside WindowShouldClose)
                  → GetMousePosition() + IsMouseButtonPressed/KeyDown
                  → play/pause, preset choice, slider drag, reset
                  → build_hierarchy() / apply_preset() / step_once()
                  → sim_runner → sim_core (same math as CLI)
                  → raylib draws pyramid, dot, metrics, card
                  → BeginDrawing() / EndDrawing() swap the framebuffer
```

`Play` calls `step_once()` at `speed` requests per second using `GetFrameTime()`
as the clock. Each step sets `dot.target` to the first modelled level that hit
(L1→1, L2→2, L3→3) or to Main RAM (4) when every cache missed. The dot's `t`
is eased with `1-(1-t)^3` over 0.45 s, and the target trapezoid is lightened
while `flash` decays.

The six tiers are a *presentation* of the model: hit rates for L1–L3 come from
`sim_snapshot()`; Registers and SSD are 0% because the trace contains memory
references and paging is not modelled; Main RAM shows the fraction of requests
that missed every cache. `AMAT(ns)` is computed from the six displayed latencies
and reach probabilities. Dragging a slider sets `follow[i] = false`, which
switches that tier from measured to what-if data; **Reset Statistics** calls
`hierarchy_reset()` and re-attaches all three sliders.

### Build targets

| Target | Sources | What it is |
| --- | --- | --- |
| `sim_core` | `src/cache.c`, `hierarchy.c`, `trace.c`, `sim_runner.c` | STATIC library, shared core |
| `sim_cli` | `src/main.c` + `sim_core` | original terminal simulator |
| `sim_gui` | `gui/main.cpp` + `sim_core` + Raylib 5.5 (FetchContent) | pyramid dashboard |
| `sim_gui_imgui` | `gui/imgui_main.cpp` + vendored ImGui + SDL3/OpenGL | previous dashboard, opt-in |

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/bin/sim_cli --demo
./build/bin/sim_gui
cmake -S . -B build -DBUILD_IMGUI_GUI=ON   # adds sim_gui_imgui
```

`BUILD_GUI=OFF` skips the Raylib target. On Wayland-only systems CMake forces
GLFW's Wayland backend; on X11 it leaves Raylib's defaults alone. The first
configure needs network access to fetch Raylib unless it is already installed.
