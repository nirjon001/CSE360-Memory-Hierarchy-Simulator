# 🗺️ Project Roadmap

How this project got from "empty folder" to a two-executable simulator, and what
comes next. Each phase lists what was delivered and the commands/artifacts that
prove it.

**Legend:** ✅ done &nbsp;·&nbsp; 🔄 in progress &nbsp;·&nbsp; ⬜ planned

---

## ✅ Phase 0 — Environment & toolchain

**Goal:** make the machine able to compile and debug C/C++.

- Verified `gcc` / `g++` 15.2.0, `gdb`, CMake 4.4.4, Ninja, Make.
- Installed the VS Code C/C++ extension (`ms-vscode.cpptools`).
- Added `.vscode/` build, run, debug and IntelliSense configuration.

**Proof:** `gcc --version`, `make`, <kbd>Ctrl+Shift+B</kbd>, <kbd>F5</kbd>.

---

## ✅ Phase 1 — Core simulator

**Goal:** implement the memory hierarchy model itself.

- `include/cache.h` + `src/cache.c` — one cache level: tag/index/offset split,
  LRU / FIFO / Random victims, write-back / write-through, write-allocate,
  buffered writebacks, recursive access into the next level.
- `include/hierarchy.h` + `src/hierarchy.c` — L1→L2→L3 chain, spec parser,
  statistics, AMAT (measured + formula).
- `include/trace.h` + `src/trace.c` — trace-file parsing and 7 workload generators.
- `src/main.c` — CLI, simulation loop, verbose mode and comparison demo.
- `Makefile` and `memsim` executable.

**Proof:** `./memsim --demo` prints the comparison table; measured AMAT matches
formula AMAT.

---

## ✅ Phase 2 — Deep documentation

**Goal:** make the code explainable for CSE360.

- `README.md` — setup process, commands, spec grammar, theory, experiments.
- `CODE_WALKTHROUGH.md` — 16 sections, file-by-file and function-by-function
  explanation, worked address example, call sequence, extension points.

---

## ✅ Phase 3 — Publish to GitHub

**Goal:** put the project under version control and online.

- `git init -b main`, `.gitignore`, initial commit `cedeb31`.
- Created public repo
  [CSE360-Memory-Hierarchy-Simulator](https://github.com/nirjon001/CSE360-Memory-Hierarchy-Simulator).
- Pushed `main` tracking `origin/main`.

---

## ✅ Phase 4 — Colourful landing-page README

**Goal:** turn the README into a proper intro page.

- Centred hero, live badges, colour-coded feature cards, navigation anchors,
  hierarchy diagram, result table with verdicts, experiment table, footer.
- Commit `997a4cf`.

---

## ✅ Phase 5 — Inspect & plan the graphical interface

**Goal:** decide the GUI stack before touching code.

Findings:

| Item | Result |
|:---|:---|
| Source language | C11 (compiles cleanly as C++17 with zero changes) |
| `g++` / CMake | 15.2.0 / 4.4.4 |
| Raylib, SFML, GLFW | not installed |
| SDL3 | ✅ 3.2.30 installed, Wayland-capable |
| OpenGL | ✅ 4.5 + headers |
| Runtime | Flatpak Freedesktop SDK 25.08, no package manager, `WAYLAND_DISPLAY=wayland-0` |
| Decision | **Dear ImGui + SDL3**, vendored ImGui v1.93.0 |

Decisions:

- Keep `cache.*`, `hierarchy.*`, `trace.*` byte-for-byte unchanged.
- Add a thin reusable `sim_runner` layer; CLI output must stay byte-identical.
- CMake produces `sim_cli` and `sim_gui` from the same `sim_core` library.
- Baseline CLI outputs saved before refactoring for later `diff`.

---

## ✅ Phase 6 — Shared core + CMake build

**Goal:** one core library, two executables.

- [x] Add `include/sim_runner.h` + `src/sim_runner.c` (build hierarchy, single
      access step with per-level hit/miss, run-trace callback, snapshots for GUI).
- [x] Point `src/main.c` internal helpers at `sim_runner` without changing any
      printed string or flag.
- [x] Add root `CMakeLists.txt`: `sim_core` (STATIC), `sim_cli`, `sim_gui`,
      `BUILD_GUI` option, binaries in `build/bin/`.
- [x] Keep the existing `Makefile` working — added `src/sim_runner.c` to `SRCS`.
- [x] Verify `sim_cli` output matches the saved baseline exactly.

**Proof:** clean `cmake -S . -B build -G Ninja && cmake --build build` → 0 warnings.
`diff` against pre-refactor baselines: `--demo`, `trace -v`, and 2-level runs all
**IDENTICAL** for both `sim_cli` and legacy `memsim`.

---

## ✅ Phase 7 — Dear ImGui graphical simulator

**Goal:** a visual front-end driven by the exact same simulation core.

- [x] Vendor the ImGui v1.93.0 subset + LICENSE into `third_party/imgui`.
- [x] Implement `gui/main.cpp` with SDL3 + OpenGL3 backend.
- [x] Controls: L1/L2/L3 specs, memory latency, workload, trace file, Run all /
      Step / Play-Pause / Reset (Build + Load trace), speed.
- [x] Animated L1 → L2 → L3 → MEM boxes with green hit / red miss feedback.
- [x] Live per-level stats, AMAT cards, latency plot, colour-coded access log.
- [x] Set/way heatmap for the selected cache level (blue valid / orange dirty /
      yellow last access).
- [x] Confirm a window opens on Wayland and stepping works — the process stayed
      alive for the full smoke-test window with no errors on `stderr`.

---

## ✅ Phase 8 — Verify, document, publish

- [x] Build from a clean `build/` directory with CMake and Ninja.
- [x] Re-run the byte-for-byte CLI comparison (3 scenarios, both executables).
- [x] Update `README.md` and `CODE_WALKTHROUGH.md` for CMake + GUI usage.
- [x] Update `.gitignore` for `build/`, `cmake-build-*/`, `imgui.ini`.
- [x] Commit and push.

---

## ⬜ Phase 9 — Nice-to-have follow-ups (not committed yet)

- Docking / layout persistence in the GUI.
- Click a cache line in the heatmap to show its tag, dirty bit and timestamps.
- Import real difficulty-like traces with instruction addresses.
- Export simulation results to CSV for lab reports.
- Optional web/headless renderer for CI screenshots.

---

**Current status:** Phases 0–8 complete. `sim_cli` and `sim_gui` both build from
the same `sim_core`; the terminal output is unchanged. Phase 9 is a backlog of
optional enhancements — pick one when you want to extend the project further.
