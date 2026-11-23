#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t addr;
    uint8_t  is_write;
} TraceEntry;

typedef struct {
    TraceEntry *items;
    size_t      count;
    size_t      capacity;
} Trace;

typedef enum {
    GEN_SEQUENTIAL = 0,  /* stream through memory, 8 bytes at a time     */
    GEN_STRIDED,         /* fixed stride (param = stride in bytes)       */
    GEN_RANDOM,          /* pseudo-random addresses within a 1 MiB region */
    GEN_MATRIX_ROW,      /* row-major traversal of a param x param matrix */
    GEN_MATRIX_COL,      /* column-major traversal (cache-unfriendly)    */
    GEN_WORKING_SET,     /* repeated reuse of param distinct cache lines  */
    GEN_HOT_COLD         /* 4 hot lines + 2 cold lines in one set (policy) */
} GenKind;

int  trace_init(Trace *t);
void trace_free(Trace *t);
int  trace_push(Trace *t, uint64_t addr, int is_write);

/* Read a trace file. Each non-comment line: "<addr> [R|W]" (addr may be hex). */
int  trace_load_file(Trace *t, const char *path);

/* Generate a synthetic workload of `count` accesses. */
int  trace_generate(Trace *t, GenKind kind, size_t count,
                    uint64_t base, size_t param);

const char *gen_name(GenKind kind);
const char *gen_description(GenKind kind);

#endif /* TRACE_H */
