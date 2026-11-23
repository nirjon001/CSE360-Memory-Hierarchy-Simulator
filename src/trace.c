#include "trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define REGION (1u << 20)   /* 1 MiB address region used by random workloads */

int trace_init(Trace *t)
{
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    return 0;
}

void trace_free(Trace *t)
{
    if (!t) return;
    free(t->items);
    t->items = NULL;
    t->count = t->capacity = 0;
}

int trace_push(Trace *t, uint64_t addr, int is_write)
{
    if (!t) return -1;
    if (t->count == t->capacity) {
        size_t new_cap = t->capacity ? t->capacity * 2 : 1024;
        TraceEntry *p = (TraceEntry *)realloc(t->items, new_cap * sizeof(*p));
        if (!p) return -1;
        t->items = p;
        t->capacity = new_cap;
    }
    t->items[t->count].addr = addr;
    t->items[t->count].is_write = (uint8_t)(is_write ? 1 : 0);
    t->count++;
    return 0;
}

/* --------------------------------------------------------------------------
 * Trace file loading
 * -------------------------------------------------------------------------- */
static uint64_t parse_addr(const char *s)
{
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    if (end == s) return 0;
    return (uint64_t)v;
}

int trace_load_file(Trace *t, const char *path)
{
    if (!t || !path) return -1;
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen");
        return -1;
    }

    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        /* accept both  "0x1000 W"  and  "W 0x1000" */
        int is_write = 0;
        if (*p == 'R' || *p == 'r' || *p == 'W' || *p == 'w') {
            is_write = (*p == 'W' || *p == 'w');
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            uint64_t a = parse_addr(p);
            if (trace_push(t, a, is_write) != 0) { fclose(f); return -1; }
        } else {
            uint64_t a = parse_addr(p);
            while (*p && !isspace((unsigned char)*p)) p++;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == 'W' || *p == 'w') is_write = 1;
            if (trace_push(t, a, is_write) != 0) { fclose(f); return -1; }
        }
        if (feof(f)) break;
        (void)lineno;
    }
    fclose(f);
    return 0;
}

/* --------------------------------------------------------------------------
 * Synthetic workloads
 * -------------------------------------------------------------------------- */
static uint64_t xorshift(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

int trace_generate(Trace *t, GenKind kind, size_t count,
                   uint64_t base, size_t param)
{
    if (!t || count == 0) return -1;
    uint64_t state = 0x9E3779B97F4A7C15ULL;
    size_t N = param ? param : 512;   /* matrix dimension / working-set size */

    for (size_t i = 0; i < count; i++) {
        uint64_t addr = base;
        int write = 0;

        switch (kind) {
            case GEN_SEQUENTIAL:
                addr = base + (uint64_t)i * 8;
                write = (i % 8 == 7);
                break;

            case GEN_STRIDED:
                addr = base + ((uint64_t)i * (uint64_t)N) % REGION;
                write = (i % 8 == 7);
                break;

            case GEN_RANDOM:
                addr = base + (xorshift(&state) % (REGION / 8)) * 8;
                write = (i % 5 == 4);
                break;

            case GEN_MATRIX_ROW:
                addr = base + ((uint64_t)i % ((uint64_t)N * N)) * 8;
                break;

            case GEN_MATRIX_COL: {
                /* Row-major storage, column-major traversal: stride N*8. */
                size_t k = i % (N * N);
                size_t row = k % N;
                size_t col = k / N;
                addr = base + ((uint64_t)row * N + col) * 8;
                break;
            }

            case GEN_WORKING_SET:
                /* N distinct lines with a 2 KiB stride: they map to very few
                 * sets, so associativity and replacement policy matter. */
                addr = base + ((uint64_t)i % N) * 2048;
                write = (i % 16 == 15);
                break;

            case GEN_HOT_COLD:
                /* 4 hot lines + 2 cold lines that all map to the same set. */
                if (i % 10 == 0)
                    addr = base + ((uint64_t)((i / 10) % 2) + 4) * 2048;
                else
                    addr = base + (uint64_t)((i % 10) - 1) * 2048;
                break;

            default:
                return -1;
        }

        if (trace_push(t, addr, write) != 0) return -1;
    }
    return 0;
}

const char *gen_name(GenKind kind)
{
    switch (kind) {
        case GEN_SEQUENTIAL:  return "sequential";
        case GEN_STRIDED:     return "strided";
        case GEN_RANDOM:      return "random";
        case GEN_MATRIX_ROW:  return "matrix-row";
        case GEN_MATRIX_COL:  return "matrix-col";
        case GEN_WORKING_SET: return "working-set";
        case GEN_HOT_COLD:    return "hot-cold";
        default:              return "?";
    }
}

const char *gen_description(GenKind kind)
{
    switch (kind) {
        case GEN_SEQUENTIAL:  return "streaming 8-byte accesses";
        case GEN_STRIDED:     return "fixed stride (param = stride bytes)";
        case GEN_RANDOM:      return "random hits inside a 1 MiB region";
        case GEN_MATRIX_ROW:  return "row-major matrix scan";
        case GEN_MATRIX_COL:  return "column-major matrix scan (strided)";
        case GEN_WORKING_SET: return "reuse of param distinct cache lines";
        case GEN_HOT_COLD:    return "4 hot + 2 cold lines in one set";
        default:              return "";
    }
}
