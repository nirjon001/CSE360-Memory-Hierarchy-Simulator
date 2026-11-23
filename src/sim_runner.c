#include "sim_runner.h"

#include <stdio.h>
#include <string.h>

static const uint32_t default_hits[MAX_LEVELS] = { 1, 10, 30 };

uint32_t sim_default_hit_latency(int level)
{
    return (level >= 0 && level < MAX_LEVELS) ? default_hits[level] : 0;
}

int sim_build_hierarchy(Hierarchy *h, const char *l1, const char *l2,
                        const char *l3, uint32_t memory_latency)
{
    LevelConfig cfgs[MAX_LEVELS];
    const char *specs[MAX_LEVELS] = { l1, l2, l3 };
    int nlevels = 1 + (l2 != NULL) + (l3 != NULL);

    for (int i = 0; i < nlevels; i++) {
        if (hierarchy_parse_level(specs[i], default_hits[i], &cfgs[i]) != 0) {
            fprintf(stderr, "Invalid cache specification: %s\n", specs[i]);
            return -1;
        }
    }
    if (hierarchy_init(h, cfgs, nlevels, memory_latency) != 0) {
        fprintf(stderr, "Failed to initialise the memory hierarchy.\n");
        return -1;
    }
    return 0;
}

SimStepInfo sim_step(Hierarchy *h, uint64_t addr, int is_write)
{
    SimStepInfo info;
    uint64_t before_acc[MAX_LEVELS] = { 0 };
    uint64_t before_hit[MAX_LEVELS] = { 0 };
    uint64_t cycles_before = h ? h->total_cycles : 0;
    int n = h ? h->nlevels : 0;

    memset(&info, 0, sizeof(info));
    for (int i = 0; i < n; i++) {
        before_acc[i] = h->levels[i].accesses;
        before_hit[i] = h->levels[i].hits;
    }

    hierarchy_access(h, addr, is_write);

    for (int i = 0; i < n; i++) {
        info.reached[i] = (h->levels[i].accesses > before_acc[i]);
        info.hit[i] = (uint8_t)(info.reached[i] &&
                                (h->levels[i].hits > before_hit[i]));
    }
    info.latency = (h ? h->total_cycles : 0) - cycles_before;
    if (n > 0)
        info.memory_access = (uint8_t)(info.reached[n - 1] && !info.hit[n - 1]);

    return info;
}

void sim_run_trace(Hierarchy *h, const Trace *t,
                   SimStepCallback cb, void *user)
{
    if (!t) return;
    for (size_t i = 0; i < t->count; i++) {
        SimStepInfo info = sim_step(h, t->items[i].addr, t->items[i].is_write);
        if (cb) cb(user, i, t->items[i].addr, t->items[i].is_write, &info);
    }
}

void sim_snapshot(const Hierarchy *h, SimLevelSnapshot out[MAX_LEVELS])
{
    if (!out) return;
    memset(out, 0, MAX_LEVELS * sizeof(*out));
    if (!h) return;

    for (int i = 0; i < h->nlevels; i++) {
        const Cache *c = &h->levels[i];
        SimLevelSnapshot *s = &out[i];
        snprintf(s->name, sizeof(s->name), "%s", c->name);
        s->capacity      = c->capacity;
        s->block_size    = c->block_size;
        s->associativity = c->associativity;
        s->num_sets      = c->num_sets;
        s->accesses      = c->accesses;
        s->hits          = c->hits;
        s->misses        = c->misses;
        s->evictions     = c->evictions;
        s->writebacks    = c->writebacks;
        s->miss_rate     = cache_miss_rate(c);
        s->read_miss_rate = cache_read_miss_rate(c);
        s->hit_latency   = c->hit_latency;
    }
}

double sim_amat_measured(const Hierarchy *h)
{
    return hierarchy_amat(h);
}

double sim_amat_formula(const Hierarchy *h)
{
    return hierarchy_theoretical_amat(h);
}
