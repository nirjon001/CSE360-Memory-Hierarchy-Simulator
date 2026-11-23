#ifndef SIM_RUNNER_H
#define SIM_RUNNER_H

#include "hierarchy.h"
#include "trace.h"

/* --------------------------------------------------------------------------
 * sim_runner - the thin, reusable layer between the simulator core and its
 * two front-ends (sim_cli and sim_gui).
 *
 * The cache math still lives entirely in cache.c / hierarchy.c / trace.c.
 * This file only orchestrates: build a hierarchy from text specs, advance it
 * one access at a time, and expose read-only snapshots for the GUI.
 * -------------------------------------------------------------------------- */

typedef struct {
    uint8_t  reached[MAX_LEVELS];   /* 1 if this level saw the request     */
    uint8_t  hit[MAX_LEVELS];       /* 1 if this level hit                 */
    uint64_t latency;               /* cycles consumed by this access      */
    uint8_t  memory_access;         /* 1 if the last level also missed     */
} SimStepInfo;

typedef struct {
    char     name[16];
    size_t   capacity;
    size_t   block_size;
    size_t   associativity;
    size_t   num_sets;
    uint64_t accesses;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writebacks;
    double   miss_rate;
    double   read_miss_rate;
    uint32_t hit_latency;
} SimLevelSnapshot;

typedef void (*SimStepCallback)(void *user, size_t index, uint64_t addr,
                                int is_write, const SimStepInfo *info);

/* Default hit latencies used when a spec omits the last field. */
uint32_t sim_default_hit_latency(int level);

/* Parse l1/l2/l3 specs and build the chain. NULL l2/l3 means "no such level". */
int sim_build_hierarchy(Hierarchy *h, const char *l1, const char *l2,
                        const char *l3, uint32_t memory_latency);

/* Advance the hierarchy by exactly one access and report what each level did. */
SimStepInfo sim_step(Hierarchy *h, uint64_t addr, int is_write);

/* Run every entry of a trace; cb may be NULL. */
void sim_run_trace(Hierarchy *h, const Trace *t,
                   SimStepCallback cb, void *user);

/* Copy the current counters/geometry of every level into `out`. */
void sim_snapshot(const Hierarchy *h, SimLevelSnapshot out[MAX_LEVELS]);

double sim_amat_measured(const Hierarchy *h);
double sim_amat_formula(const Hierarchy *h);

#endif /* SIM_RUNNER_H */
