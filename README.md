<div align="center">

<br>

<img src="https://img.shields.io/badge/🧠-CSE360%20Computer%20Architecture-6f42c1?style=for-the-badge" alt="Course">

# 💾 Memory Hierarchy Simulator

### _A trace-driven cache explorer — L1 → L2 → L3 → main memory, one address at a time._

<p>
<img src="https://img.shields.io/badge/Language-C11-00599C?style=for-the-badge&logo=c&logoColor=white" alt="C11">
<img src="https://img.shields.io/badge/Compiler-GCC%2015.2-orange?style=for-the-badge" alt="GCC">
<img src="https://img.shields.io/badge/Editor-VS%20Code-007ACC?style=for-the-badge&logo=visualstudiocode&logoColor=white" alt="VS Code">
<img src="https://img.shields.io/badge/Debugger-GDB-green?style=for-the-badge" alt="GDB">
<img src="https://img.shields.io/badge/Build-passing-2ea44f?style=for-the-badge" alt="Build passing">
<img src="https://img.shields.io/badge/Platform-Linux-blue?style=for-the-badge" alt="Linux">
</p>

<p>
<img src="https://img.shields.io/github/commit-activity/m/nirjon001/CSE360-Memory-Hierarchy-Simulator?style=for-the-badge&color=9b59b6" alt="Commits">
<img src="https://img.shields.io/github/last-commit/nirjon001/CSE360-Memory-Hierarchy-Simulator?style=for-the-badge&color=e67e22" alt="Last commit">
<img src="https://img.shields.io/github/stars/nirjon001/CSE360-Memory-Hierarchy-Simulator?style=for-the-badge&color=f1c40f" alt="Stars">
</p>

<br>

🟦 🟩 🟨 🟧 🟥 🟪 🟦 🟩 🟨 🟧 🟥 🟪

<br>

| 🚀 [Quick start](#quick-start) | 🧱 [CMake + GUI](#cmake) | 🖥️ [VS Code setup](#vscode) | 🎮 [Run commands](#cli) | 🧮 [Theory](#theory) | 🔬 [Experiments](#experiments) | 🗺️ [Roadmap](ROADMAP.md) |
|:---:|:---:|:---:|:---:|:---:|

</div>

---

<a id="overview"></a>

## 🎯 What this project does

<div style="background-color:#E3F2FD; padding:18px;">
<b>This is a <i>trace-driven simulator</i>.</b> It never runs a real program — instead it feeds a list of
memory addresses through a software model of a cache hierarchy and reports what hit, what missed,
and how many cycles each access cost. Change the cache geometry, re-run the <i>same</i> trace,
and you can see the performance difference immediately.
</div>

<br>

<table>
<tr>
<td width="33%" valign="top" style="background-color:#FFF3E0; padding:16px;">
<b>🪙 Multi-level hierarchy</b><br><br>
Chain up to <code>L1 → L2 → L3</code> and then main memory. A miss at one level recursively
asks the next one, just like real hardware.
</td>
<td width="33%" valign="top" style="background-color:#E8F5E9; padding:16px;">
<b>🎛️ Fully configurable</b><br><br>
Capacity, block size, associativity, replacement policy, write policy, write-allocate
and hit latency — all from one comma-separated string.
</td>
<td width="33%" valign="top" style="background-color:#F3E5F5; padding:16px;">
<b>📊 Real analysis</b><br><br>
Hits, misses, evictions, writebacks, hit rate and <b>AMAT</b> computed two independent ways
so you can trust the numbers.
</td>
</tr>
<tr>
<td valign="top" style="background-color:#E0F7FA; padding:16px;">
<b>🧪 Built-in workloads</b><br><br>
Seven generators: sequential, strided, random, row-major, column-major, working-set
and hot/cold — plus your own trace files.
</td>
<td valign="top" style="background-color:#FFFDE7; padding:16px;">
<b>🐞 Debug-friendly</b><br><br>
<code>-v</code> prints the first 12 accesses with per-level HIT / MISS, and VS Code
launch configs let you step straight into <code>cache_access()</code>.
</td>
<td valign="top" style="background-color:#FCE4EC; padding:16px;">
<b>📚 Course-ready</b><br><br>
A comparison demo, a full code walkthrough, suggested experiments and a list of
deliberate simplifications for your report.
</td>
</tr>
</table>

<br>

> 📖 **New here?** Start with [CODE_WALKTHROUGH.md](CODE_WALKTHROUGH.md) — it explains every
> function and follows one address all the way from `main()` to main memory.
> 🗺️ See [ROADMAP.md](ROADMAP.md) for where the project came from and what is next.

---

<a id="quick-start"></a>

## 🚀 Quick start

```bash
cd ~/Projects/CSE360_project
make                 # compile with gcc
./memsim --demo      # see the comparison table
./memsim -h          # full help
```

<div style="background-color:#E8F5E9; padding:14px;">
✅ <b>You should see</b> a table of workloads versus cache configurations, with
<code>L1 miss%</code>, <code>L2 miss%</code>, <b>AMAT</b> and a <b>Measured</b> column.
Those last two numbers match when the model is correct.
</div>

### 🧱 The hierarchy at a glance

```
   ┌─────────────────────────────────────────────────────────┐
   │  your program's addresses  (a trace file or generator)  │
   └───────────────────────────┬─────────────────────────────┘
                               ▼
                        ┌─────────────┐
                        │     L1      │  1 cycle   ── hit here? done ✅
                        └──────┬──────┘
                               │ miss
                               ▼
                        ┌─────────────┐
                        │     L2      │  10 cycles ── hit here? done ✅
                        └──────┬──────┘
                               │ miss
                               ▼
                        ┌─────────────┐
                        │     L3      │  30 cycles ── hit here? done ✅
                        └──────┬──────┘
                               │ miss
                               ▼
                      ┌──────────────────┐
                      │  MAIN MEMORY     │  100 cycles ⏳
                      └──────────────────┘
```

---

<a id="cmake"></a>

## 🧱 Three executables, one core (CMake)

`sim_cli` is the original terminal program. `sim_gui` is the new **Raylib**
pyramid dashboard. The earlier ImGui dashboard is preserved as the optional
`sim_gui_imgui`. All three link the **same** `sim_core` library — `cache.c`,
`hierarchy.c`, `trace.c` and the thin `sim_runner.c` orchestration layer. The
cache math and every terminal string are unchanged.

```bash
cmake -S . -B build -G Ninja    # fetches Raylib 5.5 on the first configure
cmake --build build
./build/bin/sim_cli --demo      # byte-identical to the original ./memsim --demo
./build/bin/sim_gui             # Raylib pyramid dashboard
./build/bin/sim_gui_imgui       # only with -DBUILD_IMGUI_GUI=ON
```

Raylib is pulled in with **CMake FetchContent** when it is not installed
system-wide, so no package manager is needed. The first configure needs network
access; later builds reuse `build/_deps`. On Wayland-only machines CMake selects
GLFW's Wayland backend automatically. Use `-DBUILD_GUI=OFF` to skip Raylib or
`-DBUILD_IMGUI_GUI=ON` to bring back the legacy dashboard. The old
`make` → `./memsim` path still works too.

### What the Raylib dashboard shows

- **Top:** a six-tier pyramid of coloured trapezoids — Registers (dark red,
  0.5 ns) → L1 (amber, 1 ns) → L2 (olive, 4 ns) → L3 (teal, 15 ns) → Main RAM
  (slate blue, 80 ns) → SSD (indigo, 50 us). Each tier shows its live hit % with
  a mini progress bar. A glowing yellow dot travels down the centre to the tier
  that hit, and a circular **Play/Pause** button sits in the top right.
- **Middle:** three centered metrics — **Avg Latency (AMAT, ns)**,
  **Total Requests**, and **Cache Efficiency %** in green.
- **Bottom:** a dark card with a **Workload Pattern Preset** dropdown
  (Highly Localized / Sequential / Random), three L1/L2/L3 hit-rate sliders
  and a **Reset Statistics** button in the bottom right.

The sliders follow the measured hit rates until you drag one; that detaches it
into a "what-if" mode and updates the AMAT immediately. **Reset Statistics**
re-attaches them and zeroes all counters. `Space` steps, `P` toggles play,
`→` steps once, `F2` saves a screenshot.

| Pyramid tier | Live value comes from |
|:---|:---|
| Registers | 0% — a trace contains memory references, so all of them missed registers |
| L1 / L2 / L3 | `sim_snapshot()` hit rates (or the what-if sliders) |
| Main RAM | share of requests that missed every cache and terminate at memory |
| SSD | 0% — paging is not modelled |

AMAT is computed from these six displayed latencies and hit probabilities, so
the number on screen always agrees with the pyramid. The cycle-based AMAT from
the core is still printed by `sim_cli`.

> The older ImGui + SDL3 dashboard lives in `gui/imgui_main.cpp` with ImGui
> 1.93.0 vendored in `third_party/imgui` (MIT licence). It is opt-in.

---

<a id="vscode"></a>

## 🖥️ Setting up VS Code — the process

<div style="background-color:#E3F2FD; padding:18px;">
<b>Already done on this machine:</b> GCC 15.2.0, GDB, VS Code 1.139.1 and the
<code>ms-vscode.cpptools</code> extension. The <code>.vscode/</code> folder holds the build,
run and debug recipes, so the editor "just works" when you open the project folder.
</div>

<br>

### 1️⃣ Open the *folder*

```bash
cd ~/Projects/CSE360_project
code .
```

> ⚠️ Open the **folder**, not a single file — VS Code only reads `.vscode/tasks.json`
> and `.vscode/launch.json` from the workspace root.

### 2️⃣ Install the C/C++ extension (if needed)

```bash
code --install-extension ms-vscode.cpptools --force
code --list-extensions | grep cpptools
```

### 3️⃣ Build — <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>B</kbd>

Runs `make`. Compiler errors appear in the **Problems** panel and you can click them
to jump to the exact source line.

### 4️⃣ Run — three options

<table>
<tr>
<td width="33%" valign="top" style="background-color:#E8F5E9; padding:14px;">
<b>💻 Integrated terminal</b><br><br>
Press <code>Ctrl + `</code>, then type<br><code>./memsim --demo</code>
</td>
<td width="33%" valign="top" style="background-color:#FFF3E0; padding:14px;">
<b>⚙️ A task</b><br><br>
<code>Ctrl+Shift+P</code> → <i>Tasks: Run Task</i><br>→ <b>Run: demo comparison</b>
</td>
<td width="33%" valign="top" style="background-color:#F3E5F5; padding:14px;">
<b>▶️ Code Runner</b><br><br>
Open a <code>.c</code> file and click ▶<br>(or <code>Ctrl+Alt+N</code>)
</td>
</tr>
</table>

### 5️⃣ Debug — <kbd>F5</kbd>

Two launch configurations are ready:

- 🐛 **Debug memsim (demo)** — runs `./memsim --demo`
- 🐛 **Debug memsim (custom args)** — a 4-way cache on a column-major trace

Put a breakpoint inside `cache_access()` in `src/cache.c` and watch the tag decode
and victim selection happen live. Shortcuts: **F9** breakpoint · **F10** step over ·
**F11** step into · **Shift+F5** stop.

### 6️⃣ Check the toolchain with a tiny program

Open `examples/hello.c` → `Ctrl+Shift+P` → *Tasks: Run Task* → **Run active C file**.

### 🧠 IntelliSense

`c_cpp_properties.json` points the extension at `include/` with the C11 standard —
autocomplete, go-to-definition and hover all work across `src/` and `include/`.

---

<a id="cli"></a>

## 🎮 Run it from a terminal

```bash
make                                                     # build
./memsim --demo                                          # comparison table
./memsim -l1 32K,64,8,lru -g row -p 512 -n 100000        # single run
./memsim -l1 16K,64,1 -l2 256K,64,8 -g col -p 512        # two levels
./memsim -l1 8K,32,4 -t traces/sample.trace -v           # verbose, real trace
./memsim -g random -n 50000 -o traces/random.trace       # save a trace
make test                                                # smoke tests
make clean                                               # remove binaries
```

### 🎛️ Command-line options

| Flag | What it does |
|:---|:---|
| `-l1` `-l2` `-l3` `SPEC` | Cache configuration. Giving `-l2` / `-l3` adds that level. |
| `-m N` | Main-memory latency in cycles (default `100`) |
| `-t FILE` | Read an address trace file |
| `-g KIND` | Workload: `seq` · `stride` · `random` · `row` · `col` · `ws` · `hot` |
| `-p N` | Generator parameter (stride bytes / matrix size / number of lines) |
| `-n N` | Number of generated accesses (default `200000`) |
| `-b ADDR` | Base address, decimal or `0x` hex |
| `-o FILE` | Save the generated trace |
| `-v` | Verbose — first 12 accesses with per-level HIT/MISS |
| `-d` | Run the comparison demo |
| `-h` | Show help |

### 🧾 Cache specification grammar

```
CAP , BLOCK , WAYS [, REPL] [, WB|WT] [, WA|NWA] [, HITCYC]
```

<table>
<tr>
<td style="background-color:#E3F2FD; padding:12px;"><code>CAP</code><br><code>8K</code> · <code>256K</code> · <code>2M</code></td>
<td style="background-color:#E8F5E9; padding:12px;"><code>BLOCK</code><br><code>32</code> · <code>64</code> bytes</td>
<td style="background-color:#FFF3E0; padding:12px;"><code>WAYS</code><br><code>direct</code> · <code>2way</code> · <code>full</code></td>
</tr>
<tr>
<td style="background-color:#F3E5F5; padding:12px;"><code>REPL</code><br><code>lru</code> · <code>fifo</code> · <code>rand</code></td>
<td style="background-color:#FFFDE7; padding:12px;"><code>WB|WT</code><br>write-back · write-through</td>
<td style="background-color:#FCE4EC; padding:12px;"><code>WA|NWA</code><br>allocate · no-allocate</td>
</tr>
</table>

**Example:** `16K,64,4,fifo,wt,nwa,3` &nbsp;→&nbsp; 16 KiB, 64 B blocks, 4-way, FIFO,
write-through, no-write-allocate, 3-cycle hit.

Default hit latencies: **L1 = 1**, **L2 = 10**, **L3 = 30** cycles.

### 📄 Trace file format

```
# one access per line:  <address> [R|W]      (# = comment)
0x10000000 R
0x10000004 W
268435464 R
```

Both `0x1000 W` and `W 0x1000` are accepted. See [`traces/sample.trace`](traces/sample.trace).

---

<a id="demo"></a>

## 📈 A taste of the demo output

Same 200 000-access traces, different cache geometries:

| Workload | Configuration | L1 miss% | AMAT | Verdict |
|:---|:---|---:|---:|:---|
| sequential | direct-mapped 16K | 12.50 | 13.500 | ⚪ compulsory misses only |
| matrix-row | direct-mapped 16K | 12.50 | 13.500 | 🟢 ideal scan |
| matrix-col | direct-mapped 16K | 100.00 | 101.000 | 🔴 conflict misses |
| working-set | direct-mapped 16K | 100.00 | 101.000 | 🔴 thrashing |
| working-set | fully associative 16K | 0.02 | 1.016 | 🟢 associativity fixes it |

<div style="background-color:#FFFDE7; padding:14px;">
💡 <b>Reading the table:</b> smaller AMAT is better. Compare rows <i>vertically</i>
for each workload to isolate the effect of one design choice.
</div>

---

<a id="theory"></a>

## 🧮 The theory behind the numbers

### Address split

```
┌──────────────────────┬────────────────┬─────────────────┐
│        TAG           │  SET INDEX     │  BLOCK OFFSET   │
└──────────────────────┴────────────────┴─────────────────┘
        upper bits        log2(num_sets)     log2(block_size)
```

- `offset_bits = log2(block_size)`
- `index_bits  = log2(capacity / (block_size × ways))`
- The tag is whatever is left on top.

### On a hit

Update the LRU timestamp → a store sets `dirty = 1` (write-back) or is forwarded
immediately (write-through).

### On a miss

Pick an invalid way, otherwise apply **LRU / FIFO / Random** → write back a dirty
victim through a buffered write buffer → fetch the whole block from the next level
→ install it with `valid = 1`.

### AMAT

```
AMAT = hit_time_L1 + miss_rate_L1 × miss_penalty_L1

miss_penalty_Li   = hit_time_L(i+1) + miss_rate_L(i+1) × miss_penalty_L(i+1)
miss_penalty_Llast = main_memory_latency
```

Two values are printed:

| | Meaning |
|:---|:---|
| 🟢 **AMAT (formula)** | Recursive model using observed miss rates. Deeper levels use the *read* miss rate so buffered writebacks don't distort the fill path. |
| 🔵 **AMAT (measured)** | Simply `total_cycles / total_accesses` from the simulation. |

When they agree, the bookkeeping is right. Example: `1 + 0.125 × (10 + 1.00 × 100) = 14.75`.

---

<a id="experiments"></a>

## 🔬 Suggested experiments for the assignment

| # | Experiment | Command hint | What to look for |
|:--|:---|:---|:---|
| 1️⃣ | **Associativity** | `-l1 16K,64,1\|2\|4\|full -g ws` | Miss rate drops as ways increase |
| 2️⃣ | **Block size** | `-l1 16K,16\|32\|64\|128,1` | Big blocks help streams, hurt random |
| 3️⃣ | **Multi-level trade-off** | big L1 *vs* small L1 + large L2 | Compare **AMAT**, not raw hit rate |
| 4️⃣ | **Replacement policy** | `-g hot -l1 8K,32,4,lru\|fifo\|rand` | LRU exploits the hot/cold locality |
| 5️⃣ | **Write policy** | use `,wb,wa` vs `,wt,nwa` on `seq`/`ws` | Watch `writebacks` in the report |
| 6️⃣ | **The 3C model** | compare direct-mapped vs fully associative | Classify compulsory / capacity / conflict |

---

<a id="structure"></a>

## 📁 Project structure

```
.
├── .vscode/                 ⚙️  build · debug · IntelliSense
├── include/
│   ├── cache.h              📦 one cache level: structs + API
│   ├── hierarchy.h          🔗 L1→L2→L3 chain + AMAT API
│   └── trace.h              🧵 trace loading + generators
├── src/
│   ├── cache.c              🧠 decode, lookup, victims, writeback
│   ├── hierarchy.c          🏗️  parse, connect, simulate, report
│   ├── trace.c              🧪 file parser + 7 workloads
│   └── main.c               🎬 CLI, loop, demo
├── examples/hello.c         👋 check the toolchain
├── traces/sample.trace      📄 hand-written trace
├── CODE_WALKTHROUGH.md      📖 full code explanation
├── Makefile                 🔨 build targets
└── README.md                📍 you are here
```

---

<a id="troubleshoot"></a>

## 🆘 Troubleshooting

| Symptom | Fix |
|:---|:---|
| 🔴 `make: command not found` | `sudo dnf install make gcc gdb` |
| 🔴 Red squiggles / no autocomplete | Open the **folder**, then *C/C++: Reset IntelliSense Database* |
| 🔴 F5 says "program not found" | Run `Ctrl+Shift+B` once, or check `preLaunchTask` in `launch.json` |
| 🟠 "Unknown option" | Run `./memsim -h` for exact syntax |
| 🟠 `cache_init` fails | Capacity must be a multiple of `block_size × ways`; both must be powers of two |

---

<div align="center">

<br>

🟪 🟥 🟧 🟨 🟩 🟦 🟪 🟥 🟧 🟨 🟩 🟦

**Built for CSE360 · Computer Architecture**

`C11` &nbsp;•&nbsp; `GCC` &nbsp;•&nbsp; `GDB` &nbsp;•&nbsp; `VS Code`

<a href="#overview">⬆️ Back to top</a> &nbsp;·&nbsp;
<a href="CODE_WALKTHROUGH.md">📖 Code walkthrough</a>

<br>

</div>
