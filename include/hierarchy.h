#ifndef HIERARCHY_H
#define HIERARCHY_H

#include "cache.h"

#define MAX_LEVELS 3

typedef struct {
    size_t            capacity;
    size_t            block_size;
    size_t            assoc;
    ReplacementPolicy replacement;
    WritePolicy       write_policy;
    int               write_allocate;
    uint32_t          hit_latency;
    char              text[160];      /* original spec, for reporting */
} LevelConfig;

typedef struct {
    Cache     levels[MAX_LEVELS];
    LevelConfig configs[MAX_LEVELS];
    int       nlevels;
    uint32_t  memory_latency;
    uint64_t  total_accesses;
    uint64_t  total_cycles;
} Hierarchy;

const char *hierarchy_level_name(int index);

/* Parse "CAP,BLOCK,WAYS[,REPL][,WB|WT][,WA|NWA][,HITCYC]".
 * CAP/BLOCK accept K/M/G suffixes. WAYS also accepts direct/2way/4way/full. */
int hierarchy_parse_level(const char *spec, uint32_t default_hit, LevelConfig *cfg);

/* Build the chain L1 -> L2 -> L3 -> main memory. Returns 0 on success. */
int  hierarchy_init(Hierarchy *h, const LevelConfig *cfgs, int nlevels,
                    uint32_t memory_latency);
void hierarchy_destroy(Hierarchy *h);
void hierarchy_reset(Hierarchy *h);

void hierarchy_access(Hierarchy *h, uint64_t addr, int is_write);

/* Reports */
double hierarchy_amat(const Hierarchy *h);         /* measured: cycles/access */
double hierarchy_theoretical_amat(const Hierarchy *h);
void   hierarchy_report(const Hierarchy *h, const char *title);
void   hierarchy_print_config(const Hierarchy *h);

#endif /* HIERARCHY_H */
