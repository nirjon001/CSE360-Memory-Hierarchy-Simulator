// sim_gui - Dear ImGui + SDL3 front-end for the CSE360 memory hierarchy core.
// It never implements cache behaviour itself: every hit/miss/cycle comes from
// sim_runner -> hierarchy.c -> cache.c, the same code used by sim_cli.

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"

#include <SDL3/SDL.h>
#include <GL/gl.h>

#include "sim_runner.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
struct AccessRow {
    size_t  index;
    uint64_t addr;
    bool    is_write;
    uint64_t latency;
    std::array<bool, MAX_LEVELS> reached{};
    std::array<bool, MAX_LEVELS> hit{};
    bool    memory_access;
};

struct AppState {
    // controls
    char   l1[128]     = "16K,64,1";
    char   l2[128]     = "";
    char   l3[128]     = "";
    int    mem_latency = 100;
    int    workload    = 0;
    int    count       = 200000;
    int    param       = 512;
    char   base_text[64]     = "0x10000000";
    char   trace_path[260]   = "traces/sample.trace";
    bool   use_trace_file    = false;

    // model
    Hierarchy hierarchy{};
    Trace     trace{};
    bool      hierarchy_built = false;
    bool      trace_ready     = false;
    size_t    cursor          = 0;

    // playback
    bool       playing     = false;
    float      speed       = 40.0f;   // accesses per second
    float      accumulator = 0.0f;

    // history
    std::vector<AccessRow> log;
    std::vector<float>     latency_history;
    static constexpr size_t kMaxLog = 200;

    SimStepInfo last{};
    uint64_t    last_addr = 0;
    bool        has_last  = false;

    int   heat_level = 0;
    char  status[256] = "Ready. Build the hierarchy, then generate or load a trace.";
};

static const GenKind kGens[] = {
    GEN_SEQUENTIAL, GEN_STRIDED, GEN_RANDOM, GEN_MATRIX_ROW,
    GEN_MATRIX_COL, GEN_WORKING_SET, GEN_HOT_COLD
};
static const char *kGenNames[] = {
    "Sequential", "Strided", "Random", "Matrix row-major",
    "Matrix column-major", "Working set", "Hot / cold lines"
};

static void set_status(AppState &st, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(st.status, sizeof(st.status), fmt, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Actions shared by buttons, keyboard and auto-play
// ---------------------------------------------------------------------------
static void build_trace(AppState &st)
{
    GenKind kind = GEN_SEQUENTIAL;
    trace_free(&st.trace);
    trace_init(&st.trace);

    if (st.use_trace_file) {
        if (trace_load_file(&st.trace, st.trace_path) != 0 || st.trace.count == 0) {
            set_status(st, "Could not load trace '%s'.", st.trace_path);
            st.trace_ready = false;
            return;
        }
    } else {
        uint64_t base = strtoull(st.base_text, nullptr, 0);
        if (st.count <= 0 || st.param <= 0) {
            set_status(st, "Count and parameter must be positive.");
            st.trace_ready = false;
            return;
        }
        kind = kGens[std::min(std::max(st.workload, 0),
                              (int)(sizeof(kGens) / sizeof(kGens[0])) - 1)];
        if (trace_generate(&st.trace, kind, (size_t)st.count, base,
                           (size_t)st.param) != 0) {
            set_status(st, "Could not generate workload.");
            st.trace_ready = false;
            return;
        }
    }

    st.cursor = 0;
    st.log.clear();
    st.latency_history.clear();
    st.has_last = false;
    st.trace_ready = true;
    set_status(st, "Trace ready: %zu accesses (%s).", st.trace.count,
               st.use_trace_file ? "file" : gen_name(kind));
}

static void rebuild_system(AppState &st)
{
    if (st.hierarchy_built) {
        hierarchy_destroy(&st.hierarchy);
        st.hierarchy_built = false;
    }

    const char *l2 = (st.l2[0] == '\0') ? nullptr : st.l2;
    const char *l3 = (st.l3[0] == '\0') ? nullptr : st.l3;
    if (sim_build_hierarchy(&st.hierarchy, st.l1, l2, l3,
                            (uint32_t)std::max(st.mem_latency, 1)) != 0) {
        set_status(st, "Invalid cache specification.");
        return;
    }

    st.hierarchy_built = true;
    st.cursor = 0;
    st.log.clear();
    st.latency_history.clear();
    st.has_last = false;
    st.playing = false;
    st.heat_level = std::min(st.heat_level, st.hierarchy.nlevels - 1);
    set_status(st, "Hierarchy built with %d level(s).", st.hierarchy.nlevels);
}

static void step_once(AppState &st)
{
    if (!st.hierarchy_built || !st.trace_ready) return;
    if (st.cursor >= st.trace.count) {
        st.playing = false;
        return;
    }

    const TraceEntry &entry = st.trace.items[st.cursor];
    SimStepInfo info = sim_step(&st.hierarchy, entry.addr, entry.is_write);

    AccessRow row;
    row.index = st.cursor;
    row.addr = entry.addr;
    row.is_write = entry.is_write != 0;
    row.latency = info.latency;
    row.memory_access = info.memory_access != 0;
    for (int i = 0; i < MAX_LEVELS; i++) {
        row.reached[i] = info.reached[i] != 0;
        row.hit[i]     = info.hit[i] != 0;
    }

    st.log.push_back(row);
    if (st.log.size() > st.kMaxLog)
        st.log.erase(st.log.begin());
    st.latency_history.push_back((float)info.latency);
    if (st.latency_history.size() > 400)
        st.latency_history.erase(st.latency_history.begin());

    st.last = info;
    st.last_addr = entry.addr;
    st.has_last = true;
    st.cursor++;

    if (st.cursor == st.trace.count)
        st.playing = false;
}

static void run_all(AppState &st)
{
    if (!st.hierarchy_built || !st.trace_ready) return;
    while (st.cursor < st.trace.count) step_once(st);
    set_status(st, "Completed %zu accesses.", st.cursor);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void draw_level_box(AppState &st, int level, const char *label,
                           bool active, bool hit)
{
    ImVec4 bg = active
        ? (hit ? ImVec4(0.09f, 0.42f, 0.22f, 1.0f) : ImVec4(0.48f, 0.13f, 0.11f, 1.0f))
        : ImVec4(0.12f, 0.14f, 0.20f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
    ImGui::BeginChild(label, ImVec2(0, 76), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    char hits[64] = "", stats[160] = "";
    if (level < st.hierarchy.nlevels) {
        const Cache *c = &st.hierarchy.levels[level];
        snprintf(hits, sizeof(hits), "%llu hits / %llu misses",
                 (unsigned long long)c->hits, (unsigned long long)c->misses);
        snprintf(stats, sizeof(stats), "%zu B  |  %zu B blocks  |  %zu-way  |  %u cyc",
                 c->capacity, c->block_size, c->associativity, c->hit_latency);
    } else {
        snprintf(hits, sizeof(hits), "%llu full memory accesses",
                 (unsigned long long)(st.has_last && st.last.memory_access ? 1u : 0u));
        snprintf(stats, sizeof(stats), "%u cycle latency  |  backing store",
                 st.hierarchy.memory_latency);
    }

    ImGui::TextColored(active ? (hit ? ImVec4(0.55f, 1.0f, 0.65f, 1) : ImVec4(1.0f, 0.55f, 0.5f, 1))
                              : ImVec4(0.85f, 0.88f, 0.95f, 1),
                       "%s   %s", label,
                       active ? (hit ? "[ HIT ]" : "[ MISS ]") : "");
    ImGui::TextDisabled("%s", stats);
    ImGui::Text("%s", hits);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

static void draw_hierarchy_panel(AppState &st)
{
    ImGui::TextUnformatted("Memory hierarchy");
    ImGui::Separator();

    for (int i = 0; i < st.hierarchy.nlevels; i++) {
        bool active = st.has_last && st.last.reached[i];
        draw_level_box(st, i, st.hierarchy.levels[i].name, active,
                       active && st.last.hit[i]);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
        ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1), "  |");
        ImGui::TextDisabled("        miss travels down");
    }

    bool mem_active = st.has_last && st.last.memory_access;
    draw_level_box(st, st.hierarchy.nlevels, "MEM", mem_active, false);
}

static void draw_heatmap(AppState &st)
{
    ImGui::TextUnformatted("Set / way map");
    ImGui::Separator();
    if (!st.hierarchy_built || st.hierarchy.nlevels == 0) {
        ImGui::TextDisabled("Build a hierarchy first.");
        return;
    }

    const char *names[MAX_LEVELS] = { "L1", "L2", "L3" };
    ImGui::Combo("Level", &st.heat_level, names, st.hierarchy.nlevels);
    const Cache *c = &st.hierarchy.levels[st.heat_level];

    size_t highlight_set = SIZE_MAX;
    if (st.has_last)
        highlight_set = cache_set_index(c, st.last_addr);

    const float cell = 7.0f;
    const float gap  = 2.0f;
    const int   per_row = 64;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float row_h = (float)c->associativity * (cell + gap) + 6.0f;
    const int   rows = (int)((c->num_sets + per_row - 1) / per_row);
    ImGui::Dummy(ImVec2(per_row * (cell + gap), rows * row_h));

    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (size_t s = 0; s < c->num_sets; s++) {
        float x = origin.x + (float)(s % per_row) * (cell + gap);
        float y = origin.y + (float)(s / per_row) * row_h;
        for (size_t w = 0; w < c->associativity; w++) {
            const CacheLine &line = c->sets[s][w];
            ImU32 col = !line.valid ? IM_COL32(45, 50, 62, 255)
                      : line.dirty  ? IM_COL32(240, 160, 40, 255)
                                    : IM_COL32(60, 150, 220, 255);
            dl->AddRectFilled(ImVec2(x, y + (float)w * (cell + gap)),
                              ImVec2(x + cell, y + (float)w * (cell + gap) + cell),
                              col, 1.0f);
        }
        if (s == highlight_set) {
            dl->AddRect(ImVec2(x - 2, y - 2),
                        ImVec2(x + cell + 2, y + (float)c->associativity * (cell + gap) + 2),
                        IM_COL32(255, 225, 80, 255), 2.0f, 0, 2.0f);
        }
    }
    ImGui::TextDisabled("blue = valid   orange = dirty   grey = empty   yellow = last access");
}

static void draw_stats_panel(AppState &st)
{
    ImGui::TextUnformatted("Live statistics");
    ImGui::Separator();

    SimLevelSnapshot snap[MAX_LEVELS];
    sim_snapshot(&st.hierarchy, snap);

    if (ImGui::BeginTable("stats", 8,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        const char *cols[] = { "Lvl", "Access", "Hits", "Miss", "Evict", "WB", "Miss%", "Hit%" };
        for (const char *c : cols) ImGui::TableSetupColumn(c);
        ImGui::TableHeadersRow();

        for (int i = 0; i < st.hierarchy.nlevels; i++) {
            const SimLevelSnapshot &s = snap[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(s.name);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%llu", (unsigned long long)s.accesses);
            ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "%llu", (unsigned long long)s.hits);
            ImGui::TableSetColumnIndex(3); ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%llu", (unsigned long long)s.misses);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%llu", (unsigned long long)s.evictions);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%llu", (unsigned long long)s.writebacks);
            ImGui::TableSetColumnIndex(6); ImGui::Text("%.2f", s.miss_rate * 100.0);
            ImGui::TableSetColumnIndex(7); ImGui::Text("%.2f", (1.0 - s.miss_rate) * 100.0);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();

    if (st.hierarchy_built) {
        float amat_m = (float)sim_amat_measured(&st.hierarchy);
        float amat_f = (float)sim_amat_formula(&st.hierarchy);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.25f, 0.42f, 1));
        ImGui::BeginChild("amat_measured", ImVec2(0, 70), true);
        ImGui::TextDisabled("AMAT  (measured = cycles / access)");
        ImGui::SetWindowFontScale(1.9f);
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1), "%.3f cyc", amat_m);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.32f, 0.12f, 0.38f, 1));
        ImGui::BeginChild("amat_formula", ImVec2(0, 70), true);
        ImGui::TextDisabled("AMAT  (formula = hit + miss rate x penalty)");
        ImGui::SetWindowFontScale(1.9f);
        ImGui::TextColored(ImVec4(0.95f, 0.6f, 1.0f, 1), "%.3f cyc", amat_f);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Latency per access");
    if (!st.latency_history.empty()) {
        float max_lat = *std::max_element(st.latency_history.begin(),
                                          st.latency_history.end());
        ImGui::PlotLines("##latency", st.latency_history.data(),
                         (int)st.latency_history.size(), 0, nullptr,
                         0.0f, max_lat * 1.15f, ImVec2(0, 110));
    } else {
        ImGui::TextDisabled("No accesses yet.");
    }
}

static void draw_log_panel(AppState &st)
{
    ImGui::TextUnformatted("Recent accesses");
    ImGui::Separator();

    if (ImGui::BeginTable("log", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY)) {
        const char *cols[] = { "#", "Address", "R/W", "L1", "L2", "cyc" };
        for (const char *c : cols) ImGui::TableSetupColumn(c);
        ImGui::TableHeadersRow();

        for (auto it = st.log.rbegin(); it != st.log.rend(); ++it) {
            const AccessRow &r = *it;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%zu", r.index);
            ImGui::TableSetColumnIndex(1); ImGui::Text("0x%08llx", (unsigned long long)r.addr);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(r.is_write ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.6f, 0.8f, 1, 1),
                               "%c", r.is_write ? 'W' : 'R');
            for (int l = 0; l < 2; l++) {
                ImGui::TableSetColumnIndex(3 + l);
                if (!r.reached[l]) ImGui::TextDisabled("---");
                else ImGui::TextColored(r.hit[l] ? ImVec4(0.4f, 1, 0.5f, 1)
                                                 : ImVec4(1, 0.45f, 0.4f, 1),
                                        r.hit[l] ? "HIT" : "MISS");
            }
            ImGui::TableSetColumnIndex(5); ImGui::Text("%llu", (unsigned long long)r.latency);
        }
        ImGui::EndTable();
    }
}

static void draw_controls(AppState &st)
{
    ImGui::BeginChild("controls", ImVec2(0, 196), true);

    ImGui::Columns(3, "ctrl_cols", false);
    ImGui::SetColumnWidth(0, 360.0f);
    ImGui::SetColumnWidth(1, 360.0f);

    ImGui::TextColored(ImVec4(0.45f, 0.8f, 1, 1), "Cache specifications");
    ImGui::InputText("L1", st.l1, IM_ARRAYSIZE(st.l1));
    ImGui::InputText("L2 (blank = none)", st.l2, IM_ARRAYSIZE(st.l2));
    ImGui::InputText("L3 (blank = none)", st.l3, IM_ARRAYSIZE(st.l3));
    ImGui::InputInt("Memory latency (cyc)", &st.mem_latency, 1, 10);

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.55f, 1, 0.65f, 1), "Workload");
    ImGui::Checkbox("Use trace file", &st.use_trace_file);
    if (st.use_trace_file) {
        ImGui::InputText("Trace file", st.trace_path, IM_ARRAYSIZE(st.trace_path));
    } else {
        ImGui::Combo("Generator", &st.workload, kGenNames,
                     (int)(sizeof(kGenNames) / sizeof(kGenNames[0])));
        ImGui::InputInt("Accesses", &st.count, 1000, 10000);
        ImGui::InputInt("Param (stride / N / lines)", &st.param, 1, 10);
        ImGui::InputText("Base address", st.base_text, IM_ARRAYSIZE(st.base_text));
    }

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "Simulation");
    if (ImGui::Button("Build / Reset", ImVec2(150, 30))) rebuild_system(st);
    ImGui::SameLine();
    if (ImGui::Button("Load trace", ImVec2(150, 30))) build_trace(st);
    if (ImGui::Button("Run all", ImVec2(95, 28))) run_all(st);
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(80, 28))) step_once(st);
    ImGui::SameLine();
    if (ImGui::Button(st.playing ? "Pause" : "Play", ImVec2(80, 28)))
        st.playing = !st.playing;
    ImGui::SliderFloat("Speed (acc/s)", &st.speed, 1.0f, 500.0f, "%.0f");

    size_t total = st.trace_ready ? st.trace.count : 0;
    ImGui::ProgressBar(total ? (float)st.cursor / (float)total : 0.0f,
                       ImVec2(-1, 14),
                       total ? "" : "no trace loaded");
    ImGui::Text("%zu / %zu accesses", st.cursor, total);

    ImGui::Columns(1);
    ImGui::EndChild();
}

static void draw_ui(AppState &st, ImGuiIO &io)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("sim_gui", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

    draw_controls(st);
    ImGui::Separator();

    float avail_y = ImGui::GetContentRegionAvail().y;
    float avail_x = ImGui::GetContentRegionAvail().x;
    float left_w = 340.0f;
    float right_w = 300.0f;
    float mid_w = avail_x - left_w - right_w -
                  2.0f * ImGui::GetStyle().ItemSpacing.x;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.16f, 1));
    ImGui::BeginChild("left_col", ImVec2(left_w, avail_y), true);
    draw_hierarchy_panel(st);
    ImGui::Spacing();
    draw_heatmap(st);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::BeginChild("mid_col", ImVec2(mid_w, avail_y), true);
    draw_stats_panel(st);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("right_col", ImVec2(right_w, avail_y), true);
    draw_log_panel(st);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Status: %s", st.status);
    ImGui::TextDisabled("Space = step   |   P = play/pause   |   R = run all   |   Esc = quit");

    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
int main(int, char **)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *window = SDL_CreateWindow(
        "CSE360 - Memory Hierarchy Simulator", 1400, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12, 10);
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.28f, 0.42f, 1.0f);

    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 130");

    AppState st;
    build_trace(st);      // start with a ready-made workload
    rebuild_system(st);   // and the default hierarchy

    bool done = false;
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) done = true;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                e.window.windowID == SDL_GetWindowID(window)) done = true;
        }

        if (!done && io.DeltaTime > 0.0f) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_ESCAPE]) done = true;
            if (keys[SDL_SCANCODE_SPACE]) step_once(st);
            if (keys[SDL_SCANCODE_P]) st.playing = !st.playing;
            if (keys[SDL_SCANCODE_R]) run_all(st);

            if (st.playing) {
                st.accumulator += io.DeltaTime * st.speed;
                while (st.accumulator >= 1.0f && st.cursor < st.trace.count) {
                    step_once(st);
                    st.accumulator -= 1.0f;
                }
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw_ui(st, io);
        ImGui::Render();

        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    hierarchy_destroy(&st.hierarchy);
    trace_free(&st.trace);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
