// sim_gui - Raylib "memory pyramid" dashboard for the CSE360 simulator.
// This file only draws and drives the UI. Every hit, miss, counter and cycle
// still comes from sim_core (cache.c / hierarchy.c / trace.c via sim_runner).

#include <raylib.h>

#include "sim_runner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// ---------------------------------------------------------------------------
// Palette / constants
// ---------------------------------------------------------------------------
const Color kBg        { 30,  31,  34, 255 };  // #1e1f22, exactly as requested
const Color kCard      { 38,  40,  46, 255 };
const Color kCardLine  { 62,  65,  74, 255 };
const Color kText      { 232, 235, 240, 255 };
const Color kMuted     { 150, 158, 170, 255 };
const Color kGreen     {  80, 220, 120, 255 };
const Color kYellow    { 255, 220,  70, 255 };

enum Tier { T_REG = 0, T_L1, T_L2, T_L3, T_RAM, T_SSD, T_COUNT };

const char *kTierName[T_COUNT] = {
    "Registers", "L1 Cache", "L2 Cache", "L3 Cache", "Main RAM", "SSD Storage"
};
const char *kTierLat[T_COUNT] = {
    "0.5 ns", "1 ns", "4 ns", "15 ns", "80 ns", "50 us"
};
const double kTierLatNs[T_COUNT] = { 0.5, 1.0, 4.0, 15.0, 80.0, 50000.0 };

Color tier_color(int t)
{
    switch (t) {
        case T_REG: return Color{ 118,  42,  42, 255 };  // dark red
        case T_L1:  return Color{ 216, 150,  34, 255 };  // amber
        case T_L2:  return Color{ 108, 132,  54, 255 };  // olive green
        case T_L3:  return Color{  32, 132, 132, 255 };  // teal
        case T_RAM: return Color{  72,  98, 148, 255 };  // slate blue
        default:    return Color{  74,  64, 145, 255 };  // indigo
    }
}

struct Preset {
    const char *name;
    GenKind     kind;
    size_t      param;
};
const Preset kPresets[3] = {
    { "Highly Localized", GEN_HOT_COLD,    4 },
    { "Sequential",       GEN_SEQUENTIAL, 512 },
    { "Random",           GEN_RANDOM,       0 }
};

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
struct Dot {
    float t      = 1.0f;
    bool  active = false;
    int   target = T_RAM;
    float flash  = 0.0f;
};

struct App {
    Hierarchy hierarchy{};
    Trace     trace{};
    bool      built = false;
    bool      trace_ready = false;

    size_t cursor  = 0;
    bool   playing = false;
    float  speed   = 6.0f;     // requests per second
    float  accum   = 0.0f;

    int  preset   = 0;
    bool dropdown = false;

    std::array<float, 3> slider   = { { 0.0f, 0.0f, 0.0f } };  // percent
    std::array<bool, 3>  follow   = { { true, true, true  } };
    int                  dragging = -1;

    SimLevelSnapshot snap[MAX_LEVELS]{};
    SimStepInfo      last{};
    Dot              dot;
};

float clampf(float x, float lo, float hi) { return std::max(lo, std::min(hi, x)); }

// ---------------------------------------------------------------------------
// Model actions - all delegate to sim_runner / sim_core
// ---------------------------------------------------------------------------
void reset_stats(App &a)
{
    if (a.built) hierarchy_reset(&a.hierarchy);
    a.cursor = 0;
    a.accum = 0.0f;
    a.playing = false;
    a.last = SimStepInfo{};
    a.dot = Dot{};
    a.follow = { { true, true, true } };
    sim_snapshot(&a.hierarchy, a.snap);
}

void build_hierarchy(App &a)
{
    if (a.built) {
        hierarchy_destroy(&a.hierarchy);
        a.built = false;
    }
    // Three levels so that all six pyramid tiers have a meaningful mapping.
    if (sim_build_hierarchy(&a.hierarchy,
                            "16K,64,4,lru", "256K,64,8,lru", "2M,64,8,lru",
                            100) == 0) {
        a.built = true;
    }
    reset_stats(a);
}

void apply_preset(App &a)
{
    trace_free(&a.trace);
    trace_init(&a.trace);
    const Preset &p = kPresets[a.preset];
    trace_generate(&a.trace, p.kind, 200000, 0x10000000, p.param);
    a.trace_ready = (a.trace.count > 0);
    reset_stats(a);
}

void step_once(App &a)
{
    if (!a.built || !a.trace_ready || a.cursor >= a.trace.count) {
        a.playing = false;
        return;
    }

    const TraceEntry &e = a.trace.items[a.cursor];
    a.last = sim_step(&a.hierarchy, e.addr, e.is_write);
    a.cursor++;

    // First modelled level that hit: L1->1, L2->2, L3->3. Otherwise RAM->4.
    int target = T_RAM;
    for (int i = 0; i < a.hierarchy.nlevels; i++) {
        if (a.last.hit[i]) { target = i + 1; break; }
    }
    a.dot.target = target;
    a.dot.t = 0.0f;
    a.dot.active = true;
    a.dot.flash = 1.0f;

    if (a.cursor >= a.trace.count) a.playing = false;
}

// Effective hit rate of a modelled cache level (0..1): measured, unless the
// corresponding what-if slider has been detached by the user.
double level_hit(const App &a, int level)
{
    if (!a.built || level >= a.hierarchy.nlevels) return 0.0;
    return a.follow[level]
        ? (1.0 - a.snap[level].miss_rate)
        : (a.slider[level] / 100.0);
}

// Probability that a request reaches main memory (i.e. missed every cache).
double ram_reach(const App &a)
{
    double p = 1.0;
    for (int i = 0; i < a.hierarchy.nlevels; i++) p *= (1.0 - level_hit(a, i));
    return p;
}

// Value shown in each trapezoid (percent).
double tier_percent(const App &a, int tier)
{
    switch (tier) {
        case T_REG: return 0.0;                       // trace = memory requests
        case T_L1: case T_L2: case T_L3:
            return level_hit(a, tier - 1) * 100.0;
        case T_RAM: return ram_reach(a) * 100.0;      // requests ending in RAM
        default:    return 0.0;                       // no paging modelled
    }
}

// AMAT in ns from the six displayed tier latencies and their hit rates.
double amat_ns(const App &a)
{
    double reach = 1.0;
    double amat  = 0.0;

    amat += reach * kTierLatNs[T_REG];   // registers: hit rate 0 for this trace
    reach *= (1.0 - 0.0);

    for (int i = 0; i < a.hierarchy.nlevels; i++) {
        amat += reach * kTierLatNs[i + 1];
        reach *= (1.0 - level_hit(a, i));
    }

    amat += reach * kTierLatNs[T_RAM];   // every request reaching RAM ends here
    return amat;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
Color lighten(Color c, float amount)
{
    auto mix = [&](unsigned char v) {
        return static_cast<unsigned char>(v + (255 - v) * amount);
    };
    return Color{ mix(c.r), mix(c.g), mix(c.b), 255 };
}

Rectangle pyramid_bounds()
{
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    float top = 92.0f;
    float bottom = std::max(top + 240.0f, h * 0.56f);
    return Rectangle{ (float)(w / 2) - 380.0f, top, 760.0f, bottom - top };
}

void draw_pyramid(const App &a, Rectangle area)
{
    const int cx = GetScreenWidth() / 2;
    const float tier_h = area.height / T_COUNT;
    const float w_top = 140.0f;
    const float w_bot = 760.0f;

    for (int i = 0; i < T_COUNT; i++) {
        const float y0 = area.y + i * tier_h + 2.0f;
        const float y1 = area.y + (i + 1) * tier_h - 2.0f;
        const float w0 = w_top + (w_bot - w_top) * (float)i / T_COUNT;
        const float w1 = w_top + (w_bot - w_top) * (float)(i + 1) / T_COUNT;

        Vector2 p[4] = {
            { cx - w0 / 2, y0 }, { cx + w0 / 2, y0 },
            { cx + w1 / 2, y1 }, { cx - w1 / 2, y1 }
        };

        const bool flash = (a.dot.target == i && a.dot.flash > 0.0f);
        const Color base = tier_color(i);
        const Color fill = flash ? lighten(base, 0.35f * a.dot.flash) : base;

        // Draw the trapezoid as a centre rectangle plus two side triangles.
        DrawRectangle((int)p[0].x, (int)y0, (int)(p[1].x - p[0].x),
                      (int)(y1 - y0), fill);
        DrawTriangle(p[0], p[3], Vector2{ p[0].x, y1 }, fill);
        DrawTriangle(p[1], Vector2{ p[1].x, y1 }, p[2], fill);

        for (int k = 0; k < 4; k++)
            DrawLineV(p[k], p[(k + 1) % 4], Color{ 20, 20, 24, 255 });

        const float w_mid = (w0 + w1) / 2;
        const int font = (i == T_REG) ? 12 : 14;
        const int ty = (int)(y0 + tier_h / 2 - font / 2 - 4);

        const double pct = tier_percent(a, i);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.1f%%", pct);

        DrawText(kTierName[i], (int)(cx - w_mid / 2 + 10), ty, font, kText);
        DrawText(buf, cx - MeasureText(buf, font) / 2, ty, font, kText);
        const int lat_w = MeasureText(kTierLat[i], font);
        DrawText(kTierLat[i], (int)(cx + w_mid / 2 - 10 - lat_w), ty, font, kText);

        // mini progress bar under the percentage
        const float bar_w = 74.0f;
        const float bar_h = 7.0f;
        const float bx = cx - bar_w / 2;
        const float by = ty + font + 6;
        DrawRectangle((int)bx, (int)by, (int)bar_w, (int)bar_h, Color{ 24, 26, 30, 255 });
        DrawRectangle((int)bx, (int)by, (int)(bar_w * clampf((float)pct / 100.0f, 0, 1)),
                      (int)bar_h, flash ? kYellow : lighten(base, 0.45f));
    }
}

void draw_dot(const App &a, Rectangle area)
{
    if (!a.dot.active) return;
    const int cx = GetScreenWidth() / 2;
    const float tier_h = area.height / T_COUNT;
    const float dest_y = area.y + (a.dot.target + 0.5f) * tier_h;
    const float e = 1.0f - std::pow(1.0f - clampf(a.dot.t, 0, 1), 3.0f);
    const float y = area.y + (dest_y - area.y) * e;

    DrawCircle(cx, (int)y, 15, Color{ 255, 220,  70,  45 });
    DrawCircle(cx, (int)y, 10, Color{ 255, 220,  70, 110 });
    DrawCircle(cx, (int)y,  5, Color{ 255, 245, 150, 255 });
}

Vector2 play_center()
{
    return Vector2{ (float)GetScreenWidth() - 52.0f, 42.0f };
}

void draw_play_button(const App &a)
{
    const Vector2 c = play_center();
    const float r = 23.0f;
    DrawCircle((int)c.x, (int)c.y, (int)r + 3, Color{ 20, 21, 24, 255 });
    DrawCircle((int)c.x, (int)c.y, (int)r, a.playing ? Color{ 60, 110, 80, 255 }
                                                    : Color{ 65, 68, 76, 255 });
    if (a.playing) {
        DrawRectangle((int)c.x - 8, (int)c.y - 9, 5, 18, kGreen);
        DrawRectangle((int)c.x + 3, (int)c.y - 9, 5, 18, kGreen);
    } else {
        DrawTriangle(Vector2{ c.x - 6, c.y - 10 }, Vector2{ c.x - 6, c.y + 10 },
                     Vector2{ c.x + 10, c.y }, kYellow);
    }
    DrawText("PLAY / PAUSE", (int)c.x - 44, (int)c.y + 30, 11, kMuted);
}

void draw_metric(float cx, float top, const char *label, const char *value, Color c)
{
    DrawText(label, (int)(cx - MeasureText(label, 15) / 2), (int)top, 15, kMuted);
    DrawText(value, (int)(cx - MeasureText(value, 27) / 2), (int)top + 22, 27, c);
}

void draw_metrics(const App &a, float top)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f ns", amat_ns(a));
    const double reach = ram_reach(a);
    const float col = (float)GetScreenWidth() / 3.0f;
    draw_metric(col * 0.5f, top, "AVG LATENCY (AMAT)", buf, kYellow);

    char req[64];
    std::snprintf(req, sizeof(req), "%zu", a.cursor);
    draw_metric(col * 1.5f, top, "TOTAL REQUESTS", req, Color{ 130, 190, 255, 255 });

    char eff[64];
    std::snprintf(eff, sizeof(eff), "%.1f%%", (1.0 - reach) * 100.0);
    draw_metric(col * 2.5f, top, "CACHE EFFICIENCY", eff, kGreen);
}

Rectangle card_rect()
{
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    return Rectangle{ 30.0f, (float)h - 238.0f, (float)w - 60.0f, 214.0f };
}

Rectangle dropdown_rect(const Rectangle &card)
{
    return Rectangle{ card.x + 24, card.y + 48, 300, 34 };
}

Rectangle reset_rect(const Rectangle &card)
{
    return Rectangle{ card.x + card.width - 204, card.y + card.height - 56, 180, 38 };
}

void draw_slider(const App &a, int i, const Rectangle &track, const char *label)
{
    const float v = clampf(a.slider[i], 0.0f, 100.0f);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.0f%%", v);

    DrawText(label, (int)track.x, (int)track.y - 24, 14, kText);
    const int vw = MeasureText(buf, 14);
    DrawText(buf, (int)(track.x + track.width - vw), (int)track.y - 24, 14,
             a.follow[i] ? kMuted : kYellow);

    DrawRectangle((int)track.x, (int)track.y, (int)track.width, 6, Color{ 22, 24, 28, 255 });
    DrawRectangle((int)track.x, (int)track.y,
                  (int)(track.width * v / 100.0f), 6, Color{ 90, 150, 220, 255 });
    DrawCircle((int)(track.x + track.width * v / 100.0f),
               (int)(track.y + 3), 9, Color{ 220, 232, 245, 255 });

    if (!a.follow[i]) {
        DrawText("what-if", (int)(track.x + track.width + 12), (int)track.y - 10,
                 11, kYellow);
    }
}

void draw_bottom_card(const App &a)
{
    const Rectangle card = card_rect();
    DrawRectangleRounded(card, 0.03f, 8, kCard);
    DrawRectangleRoundedLines(card, 0.03f, 8, kCardLine);

    DrawText("WORKLOAD PATTERN PRESET", (int)card.x + 24, (int)card.y + 22, 13, kMuted);
    const Rectangle dd = dropdown_rect(card);
    DrawRectangleRounded(dd, 0.10f, 6, Color{ 26, 28, 33, 255 });
    DrawRectangleRoundedLines(dd, 0.10f, 6, kCardLine);
    DrawText(kPresets[a.preset].name, (int)dd.x + 12, (int)dd.y + 9, 15, kText);
    DrawTriangle(Vector2{ dd.x + dd.width - 24, dd.y + 12 },
                 Vector2{ dd.x + dd.width - 10, dd.y + 12 },
                 Vector2{ dd.x + dd.width - 17, dd.y + 22 }, kMuted);

    if (a.dropdown) {
        for (int i = 0; i < 3; i++) {
            const Rectangle opt{ dd.x, dd.y + 34 + i * 32, dd.width, 30 };
            const bool hover = CheckCollisionPointRec(GetMousePosition(), opt);
            DrawRectangle((int)opt.x, (int)opt.y, (int)opt.width, (int)opt.height,
                          hover ? Color{ 60, 70, 88, 255 } : Color{ 32, 35, 41, 255 });
            DrawText(kPresets[i].name, (int)opt.x + 12, (int)opt.y + 7, 14,
                     i == a.preset ? kYellow : kText);
        }
    }

    // Sliders occupy the middle of the card.
    const float sx = card.x + 360.0f;
    const float sw = std::max(180.0f, std::min(520.0f, card.width - 600.0f));
    const char *labels[3] = { "L1 HIT RATE", "L2 HIT RATE", "L3 HIT RATE" };
    for (int i = 0; i < 3; i++)
        draw_slider(a, i, Rectangle{ sx, card.y + 58 + i * 52, sw, 6 }, labels[i]);

    // Reset button, bottom right.
    const Rectangle rb = reset_rect(card);
    const bool hover_r = CheckCollisionPointRec(GetMousePosition(), rb);
    DrawRectangleRounded(rb, 0.15f, 6, hover_r ? Color{ 74, 52, 52, 255 }
                                               : Color{ 56, 40, 40, 255 });
    DrawRectangleRoundedLines(rb, 0.15f, 6, Color{ 130, 80, 80, 255 });
    const char *rst = "Reset Statistics";
    DrawText(rst, (int)(rb.x + (rb.width - MeasureText(rst, 15)) / 2),
             (int)rb.y + 11, 15, Color{ 255, 190, 190, 255 });
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------
void handle_input(App &a)
{
    const Vector2 m = GetMousePosition();

    if (IsKeyPressed(KEY_SPACE)) a.playing = !a.playing;
    if (IsKeyPressed(KEY_RIGHT)) step_once(a);
    if (IsKeyPressed(KEY_F2)) TakeScreenshot("/tmp/sim_gui_raylib.png");

    // Refresh live rates before the sliders are drawn / manipulated.
    sim_snapshot(&a.hierarchy, a.snap);
    for (int i = 0; i < 3; i++) {
        if (a.follow[i]) a.slider[i] = (float)(level_hit(a, i) * 100.0);
    }

    const Rectangle card = card_rect();
    const Rectangle dd = dropdown_rect(card);
    const Rectangle rb = reset_rect(card);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointCircle(m, play_center(), 26.0f)) {
            a.playing = !a.playing;
        } else if (CheckCollisionPointRec(m, rb)) {
            a.dropdown = false;
            reset_stats(a);
        } else if (a.dropdown) {
            bool chosen = false;
            for (int i = 0; i < 3; i++) {
                const Rectangle opt{ dd.x, dd.y + 34 + i * 32, dd.width, 30 };
                if (CheckCollisionPointRec(m, opt)) {
                    a.preset = i;
                    a.dropdown = false;
                    apply_preset(a);
                    chosen = true;
                    break;
                }
            }
            if (!chosen) a.dropdown = false;
        } else if (CheckCollisionPointRec(m, dd)) {
            a.dropdown = true;
        }

        // Slider tracks.
        const float sx = card.x + 360.0f;
        const float sw = std::max(180.0f, std::min(520.0f, card.width - 600.0f));
        for (int i = 0; i < 3; i++) {
            const Rectangle track{ sx, card.y + 58 + i * 52, sw, 6 };
            const Rectangle grab{ track.x - 10, track.y - 12, track.width + 20, 30 };
            if (CheckCollisionPointRec(m, grab)) {
                a.dragging = i;
                a.follow[i] = false;
                a.slider[i] = 100.0f * clampf((m.x - track.x) / sw, 0.0f, 1.0f);
            }
        }
    }

    if (a.dragging >= 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        const float sx = card.x + 360.0f;
        const float sw = std::max(180.0f, std::min(520.0f, card.width - 600.0f));
        a.slider[a.dragging] = 100.0f * clampf((m.x - sx) / sw, 0.0f, 1.0f);
    }
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) a.dragging = -1;

    // Animation / playback.
    const float dt = std::min(GetFrameTime(), 0.1f);
    if (a.dot.active) {
        a.dot.t += dt / 0.45f;
        a.dot.flash = std::max(0.0f, a.dot.flash - dt * 2.0f);
        if (a.dot.t >= 1.0f && a.dot.flash <= 0.0f) a.dot.active = false;
    }
    if (a.playing) {
        a.accum += dt * a.speed;
        while (a.accum >= 1.0f && a.cursor < a.trace.count) {
            step_once(a);
            a.accum -= 1.0f;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
int main()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 800, "CSE360 - Memory Hierarchy Simulator (Raylib)");
    if (!IsWindowReady()) {
        std::fprintf(stderr, "Could not create a Raylib window.\n");
        return 1;
    }
    SetTargetFPS(60);

    App app;
    build_hierarchy(app);
    apply_preset(app);

    // Optional self-check hook: SIM_GUI_SHOT=1 auto-plays briefly and saves a
    // screenshot. Normal interactive use is unaffected.
    const bool self_shot = std::getenv("SIM_GUI_SHOT") != nullptr;
    if (self_shot) app.playing = true;
    int frames = 0;

    while (!WindowShouldClose()) {
        handle_input(app);
        if (self_shot && ++frames == 100) {
            TakeScreenshot("/tmp/sim_gui_raylib.png");
            app.playing = false;
        }

        BeginDrawing();
        ClearBackground(kBg);

        const Rectangle area = pyramid_bounds();
        DrawText("MEMORY HIERARCHY", 30, 28, 22, kText);
        DrawText("CSE360  /  RAYLIB DASHBOARD", 30, 56, 12, kMuted);
        char prog[64];
        std::snprintf(prog, sizeof(prog), "%zu / %zu requests", app.cursor, app.trace.count);
        DrawText(prog, 30, (int)area.y + 4, 13, kMuted);

        draw_pyramid(app, area);
        draw_dot(app, area);
        draw_play_button(app);
        draw_metrics(app, area.y + area.height + 22);
        draw_bottom_card(app);

        EndDrawing();
    }

    trace_free(&app.trace);
    if (app.built) hierarchy_destroy(&app.hierarchy);
    CloseWindow();
    return 0;
}
