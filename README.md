# Memory Hierarchy Simulator — CSE360 Computer Architecture

A configurable cache/memory-hierarchy simulator written in C11. It models up to
three cache levels (L1 → L2 → L3 → main memory), several replacement policies,
write policies, and reports hits, misses, evictions, writebacks, and **AMAT**
(Average Memory Access Time).

> **New:** [CODE_WALKTHROUGH.md](CODE_WALKTHROUGH.md) explains every file and
> function, traces one address from `main()` to main memory, and shows how AMAT
> is computed. Read it if you need to explain or extend the code.

---

## 1. What was configured on this machine

| Item | Result |
| --- | --- |
| Compiler | GCC 15.2.0 (`/bin/gcc`, `/bin/g++`) |
| Debugger | GDB (`/usr/bin/gdb`) |
| Editor | VS Code 1.139.1 (`/app/bin/code`) |
| C/C++ extension | `ms-vscode.cpptools` v1.35.6 — **installed** |
| Workspace config | `.vscode/tasks.json`, `launch.json`, `c_cpp_properties.json`, `settings.json`, `extensions.json` |

Verify any time in a terminal:

```bash
gcc --version
g++ --version
gdb --version
code --list-extensions | grep cpptools
```

If the C/C++ extension is ever missing, reinstall it with:

```bash
code --install-extension ms-vscode.cpptools --force
```

---

## 2. Running C/C++ in VS Code — the process

### A. Open the project folder

```bash
cd ~/Projects/CSE360_project
code .
```

Or: **File → Open Folder… → select `CSE360_project`**.
Opening the *folder* (not a single file) is important — VS Code only reads
`.vscode/tasks.json` and `launch.json` from the workspace root.

### B. Build

- Press **Ctrl+Shift+B** → runs `make` (the default build task).
- Or **Terminal → Run Build Task… → `C/C++: build (make)`**.
- Output: executable `./memsim`. Problems (compiler errors/warnings) appear in
  the **Problems** panel, and you can click them to jump to the source line.

Other build tasks:
- `C/C++: debug build` — no optimisation, full debug symbols (`make debug`).
- `Clean build artefacts` — `make clean`.

### C. Run

Three ways:

1. **Integrated terminal** (most common):
   - **Ctrl+`** to open it, then type `./memsim --demo`.
2. **Task**: **Ctrl+Shift+P → Tasks: Run Task → `Run: demo comparison`**
   (or `Run: current configuration`).
3. **Code Runner** (the `formulahendry.code-runner` extension already installed):
   open a `.c` file and click the ▶ play button, or **Ctrl+Alt+N**.
   For a single stand-alone file use the task **`Run active C file`**:
   open `examples/hello.c`, then **Ctrl+Shift+P → Tasks: Run Task → Run active C file**.

### D. Debug

- Press **F5**. Two launch configurations are provided:
  - `Debug memsim (demo)` — runs `./memsim --demo`.
  - `Debug memsim (custom args)` — a 4-way cache on a column-major trace.
- The build runs automatically before debugging (`preLaunchTask`).
- Useful shortcuts: **F9** toggle breakpoint, **F10** step over, **F11** step
  into, **Shift+F5** stop. Put a breakpoint inside `cache_access()` in
  `src/cache.c` to watch tag/index decoding and victim selection happen.

### E. IntelliSense

`c_cpp_properties.json` points the extension at `include/` and uses the C11
standard with GCC, so autocomplete, go-to-definition and hover all work across
`src/` and `include/`.

---

## 3. Running the simulator from a terminal

```bash
make                       # build
./memsim --demo            # comparison table (also: ./memsim with no args)
./memsim -h                # full help
make test                  # quick smoke tests
```

Basic single simulation:

```bash
./memsim -l1 32K,64,8,lru -g row -p 512 -n 100000
```

Two-level hierarchy on a cache-unfriendly matrix traversal:

```bash
./memsim -l1 16K,64,1 -l2 256K,64,8 -g col -p 512 -n 200000
```

Use the supplied hand-written trace, with verbose access-by-access output:

```bash
./memsim -l1 8K,32,4 -t traces/sample.trace -v
```

Generate and save a trace for later use:

```bash
./memsim -g random -n 50000 -o traces/random.trace
./memsim -t traces/random.trace -l1 16K,64,4
```

### Command-line options

| Option | Meaning |
| --- | --- |
| `-l1/-l2/-l3 SPEC` | Cache configuration for that level (add a level by giving the option) |
| `-m N` | Main-memory latency in cycles (default 100) |
| `-t FILE` | Read an address trace file |
| `-g KIND` | Generate a workload: `seq`, `stride`, `random`, `row`, `col`, `ws`, `hot` |
| `-p N` | Generator parameter: stride bytes / matrix dimension / number of lines |
| `-n N` | Number of generated accesses (default 200000) |
| `-b ADDR` | Base address, decimal or `0x` hex |
| `-o FILE` | Save the trace to a file |
| `-v` | Show the first 12 accesses with L1/L2/L3 hit/miss |
| `-d` | Run the comparison demo |

### Cache specification format

```
CAP,BLOCK,WAYS[,REPL][,WB|WT][,WA|NWA][,HITCYC]
```

| Field | Examples | Default |
| --- | --- | --- |
| `CAP` | `8K`, `256K`, `2M` | required |
| `BLOCK` | `32`, `64` | required |
| `WAYS` | `1`/`direct`, `2way`, `4way`, `full` | required |
| `REPL` | `lru`, `fifo`, `rand` | `lru` |
| Write policy | `wb`, `wt` | `wb` |
| Allocate | `wa`, `nwa` | `wa` |
| `HITCYC` | `1`, `10`, `30` | 1 / 10 / 30 by level |

Example: `16K,64,4,fifo,wt,nwa,3`.

Default hit latencies are L1 = 1, L2 = 10, L3 = 30 cycles.

### Trace file format

One access per line: `<address> [R|W]`. Blank lines and `#` comments are
ignored. Addresses may be decimal or hexadecimal.

```
0x10000000 R
0x10000004 W
268435464 R
```

---

## 4. What the simulator models

**Address split** (for each level):

```
|        tag        |    set index    |  block offset  |
```

- `offset_bits = log2(block_size)`
- `index_bits  = log2(capacity / (block_size × associativity))`

**On a hit:** update LRU timestamp; store updates dirty bit (write-back) or is
forwarded immediately (write-through).

**On a miss:** choose an invalid way, otherwise apply LRU / FIFO / Random;
write back a dirty victim through a buffered write buffer (counted as traffic,
not added to the critical path); fetch the block from the next level; install
it in the chosen way.

**AMAT** (computed two ways and printed together):

```
AMAT = hit_time_L1 + miss_rate_L1 × miss_penalty_L1
miss_penalty_Li = hit_time_L(i+1) + miss_rate_L(i+1) × miss_penalty_L(i+1)
miss_penalty_Llast = main_memory_latency
```

- **AMAT (formula)** uses the miss rates above. Deeper levels use the *read*
  miss rate so buffered writeback hits do not distort the fill path.
- **AMAT (measured)** is simply total simulated cycles / total accesses.
  When the model is consistent, these two numbers match.

---

## 5. Suggested experiments for the assignment

1. **Associativity:** run `seq`, `row`, `col`, `random`, `ws` with 1, 2, 4 and
   fully-associative caches of the same capacity. Plot miss rate vs. ways.
2. **Block size:** keep capacity fixed and sweep 16/32/64/128-byte blocks.
   Large blocks help streaming but hurt random access (more conflict misses).
3. **Multi-level trade-off:** compare a big L1 against a small fast L1 plus a
   large L2. Use `AMAT`, not raw hit rate, to decide which is better.
4. **Replacement policy:** try `-g hot -l1 8K,32,4,lru|fifo|rand` and explain
   why LRU beats the others when a small set of hot lines is reused.
5. **Write policy:** compare `wb`/`wt` and `wa`/`nwa` on write-heavy traces
   (the `seq` and `ws` generators include stores). Look at `writebacks`.
6. **The 3C model:** classify observed misses as compulsory, capacity or
   conflict by comparing direct-mapped vs. fully-associative results.

---

## 6. Project structure

```
.
├── .vscode/                 # editor build / debug / IntelliSense config
├── include/
│   ├── cache.h              # one cache level: data structures + API
│   ├── hierarchy.h          # L1→L2→L3 chain + AMAT API
│   └── trace.h              # trace loading and workload generators
├── src/
│   ├── cache.c              # tag/index decode, lookup, victims, writeback
│   ├── hierarchy.c          # chain setup, parsing, statistics, AMAT
│   ├── trace.c              # file parsing + 7 synthetic workloads
│   └── main.c               # CLI, single runs, comparison demo
├── examples/hello.c         # check that the VS Code toolchain works
├── traces/sample.trace      # small hand-written trace
├── Makefile                 # build, debug, run, demo, test, clean
└── README.md
```

---

## 7. Troubleshooting

- **`make: command not found`** → install build tools:
  `sudo dnf install make gcc gdb`.
- **Red squiggles / no autocomplete in VS Code** → make sure you opened the
  *folder*, then **Ctrl+Shift+P → C/C++: Reset IntelliSense Database**.
- **F5 says "program not found"** → run the build task once (Ctrl+Shift+B), or
  check that `preLaunchTask` is present in `.vscode/launch.json`.
- **"Unknown option"** → run `./memsim -h` to see the exact syntax.
- **`cache_init` fails** → capacity must be an exact multiple of
  `block_size × ways`, and both capacity and block size must be powers of two.
