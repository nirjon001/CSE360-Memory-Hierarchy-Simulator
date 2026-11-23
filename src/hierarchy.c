#include "hierarchy.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */

/* --------------------------------------------------------------------------
 * Parsing helpers
 * -------------------------------------------------------------------------- */
static int parse_size(const char *s, size_t *out)
{
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    if (end == s || v == 0) return -1;

    while (*end && isspace((unsigned char)*end)) end++;
    char suffix = (char)tolower((unsigned char)*end);
    if (suffix == 'k')      v *= 1024ULL;
    else if (suffix == 'm') v *= 1024ULL * 1024ULL;
    else if (suffix == 'g') v *= 1024ULL * 1024ULL * 1024ULL;
    else if (suffix != '\0' && suffix != 'b') return -1;

    *out = (size_t)v;
    return 0;
}

static int parse_assoc(const char *s, size_t *out)
{
    if (!strcasecmp(s, "direct") || !strcmp(s, "1")) { *out = 1; return 0; }
    if (!strcasecmp(s, "2way")  || !strcmp(s, "2")) { *out = 2; return 0; }
    if (!strcasecmp(s, "4way")  || !strcmp(s, "4")) { *out = 4; return 0; }
    if (!strcasecmp(s, "8way")  || !strcmp(s, "8")) { *out = 8; return 0; }
    if (!strcasecmp(s, "full") || !strcasecmp(s, "fully")) {
        *out = 0;   /* resolved once capacity/block are known */
        return 0;
    }
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || *end != '\0' || v == 0) return -1;
    *out = (size_t)v;
    return 0;
}

int hierarchy_parse_level(const char *spec, uint32_t default_hit, LevelConfig *cfg)
{
    if (!spec || !cfg) return -1;
    char buf[256], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", spec);
    snprintf(cfg->text, sizeof(cfg->text), "%s", spec);

    char *cap  = strtok_r(buf, ",", &save);
    char *blk  = strtok_r(NULL, ",", &save);
    char *ways = strtok_r(NULL, ",", &save);
    char *repl = strtok_r(NULL, ",", &save);
    char *wr   = strtok_r(NULL, ",", &save);
    char *al   = strtok_r(NULL, ",", &save);
    char *cyc  = strtok_r(NULL, ",", &save);

    if (!cap || !blk || !ways) return -1;
    if (parse_size(cap, &cfg->capacity) != 0) return -1;
    if (parse_size(blk, &cfg->block_size) != 0) return -1;
    if (parse_assoc(ways, &cfg->assoc) != 0) return -1;

    cfg->replacement   = REPLACE_LRU;
    cfg->write_policy  = WRITE_BACK;
    cfg->write_allocate = 1;
    cfg->hit_latency   = default_hit;

    if (repl) {
        if      (!strcasecmp(repl, "lru"))  cfg->replacement = REPLACE_LRU;
        else if (!strcasecmp(repl, "fifo")) cfg->replacement = REPLACE_FIFO;
        else if (!strcasecmp(repl, "rand") || !strcasecmp(repl, "random"))
            cfg->replacement = REPLACE_RANDOM;
        else return -1;
    }
    if (wr) {
        if      (!strcasecmp(wr, "wb")) cfg->write_policy = WRITE_BACK;
        else if (!strcasecmp(wr, "wt")) cfg->write_policy = WRITE_THROUGH;
        else return -1;
    }
    if (al) {
        if      (!strcasecmp(al, "wa"))  cfg->write_allocate = 1;
        else if (!strcasecmp(al, "nwa")) cfg->write_allocate = 0;
        else return -1;
    }
    if (cyc) {
        char *end = NULL;
        unsigned long v = strtoul(cyc, &end, 10);
        if (end == cyc || *end != '\0') return -1;
        cfg->hit_latency = (uint32_t)v;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Hierarchy lifetime
 * -------------------------------------------------------------------------- */
int hierarchy_init(Hierarchy *h, const LevelConfig *cfgs, int nlevels,
                   uint32_t memory_latency)
{
    if (!h || !cfgs || nlevels < 0 || nlevels > MAX_LEVELS) return -1;

    memset(h, 0, sizeof(*h));
    h->nlevels        = nlevels;
    h->memory_latency = memory_latency;

    for (int i = 0; i < nlevels; i++) {
        LevelConfig cfg = cfgs[i];
        if (cfg.assoc == 0)  /* fully associative -> one set */
            cfg.assoc = cfg.capacity / cfg.block_size;
        if (cfg.assoc == 0) return -1;

        /* the last cache's miss goes to main memory */
        uint32_t next_latency = (i == nlevels - 1)
                              ? memory_latency : cfgs[i + 1].hit_latency;

        if (cache_init(&h->levels[i], hierarchy_level_name(i), cfg.capacity,
                       cfg.block_size, cfg.assoc, cfg.replacement,
                       cfg.write_policy, cfg.write_allocate,
                       cfg.hit_latency, next_latency) != 0)
            return -1;

        h->configs[i] = cfg;
        if (i > 0) cache_connect(&h->levels[i - 1], &h->levels[i]);
    }
    return 0;
}

/* helper used above (declared here as a static-like external for clarity) */
const char *hierarchy_level_name(int index)
{
    static const char *names[MAX_LEVELS] = { "L1", "L2", "L3" };
    return (index >= 0 && index < MAX_LEVELS) ? names[index] : "L?";
}

void hierarchy_destroy(Hierarchy *h)
{
    if (!h) return;
    for (int i = 0; i < h->nlevels; i++) cache_destroy(&h->levels[i]);
}

void hierarchy_reset(Hierarchy *h)
{
    if (!h) return;
    h->total_accesses = h->total_cycles = 0;
    for (int i = 0; i < h->nlevels; i++) cache_reset_stats(&h->levels[i]);
}

void hierarchy_access(Hierarchy *h, uint64_t addr, int is_write)
{
    if (!h) return;
    h->total_accesses++;
    if (h->nlevels == 0) {
        h->total_cycles += h->memory_latency;
        return;
    }
    CacheResult r = cache_access(&h->levels[0], addr, is_write);
    h->total_cycles += r.latency;
}

/* --------------------------------------------------------------------------
 * AMAT analysis
 * -------------------------------------------------------------------------- */
double hierarchy_amat(const Hierarchy *h)
{
    if (!h || h->total_accesses == 0) return 0.0;
    return (double)h->total_cycles / (double)h->total_accesses;
}

/* Recursively: penalty_i = hit_{i+1} + missrate_{i+1} * penalty_{i+1} */
static double level_amat(const Hierarchy *h, int i)
{
    const Cache *c = &h->levels[i];
    /* L1 sees every program access, so its overall miss rate is correct.
     * Deeper levels also see buffered writebacks: counting those hits would
     * artificially lower the miss rate on the critical (fill) path, so use
     * their read miss rate instead. */
    double mr = (i == 0) ? cache_miss_rate(c) : cache_read_miss_rate(c);

    if (i == h->nlevels - 1) {
        return c->hit_latency + mr * (double)c->next_latency;
    }
    return c->hit_latency + mr * level_amat(h, i + 1);
}

double hierarchy_theoretical_amat(const Hierarchy *h)
{
    if (!h) return 0.0;
    if (h->nlevels == 0) return (double)h->memory_latency;
    return level_amat(h, 0);
}

/* --------------------------------------------------------------------------
 * Reporting
 * -------------------------------------------------------------------------- */
void hierarchy_print_config(const Hierarchy *h)
{
    if (!h) return;
    printf("  Hierarchy configuration (%d level%s):\n", h->nlevels,
           h->nlevels == 1 ? "" : "s");
    for (int i = 0; i < h->nlevels; i++)
        cache_print_config(&h->levels[i]);
    printf("    MEM    %u cycle main-memory latency\n", h->memory_latency);
}

void hierarchy_report(const Hierarchy *h, const char *title)
{
    if (!h) return;
    printf("\n");
    printf("==========================================================================\n");
    printf(" %s\n", title ? title : "Simulation results");
    printf("==========================================================================\n");
    hierarchy_print_config(h);
    printf("\n  Per-level statistics (each level sees only requests that miss above):\n");
    for (int i = 0; i < h->nlevels; i++)
        cache_print_stats(&h->levels[i]);

    printf("\n  Accesses       : %llu\n",
           (unsigned long long)h->total_accesses);
    printf("  Total cycles   : %llu\n",
           (unsigned long long)h->total_cycles);
    printf("  AMAT (measured): %.4f cycles/access\n", hierarchy_amat(h));
    printf("  AMAT (formula) : %.4f cycles/access\n", hierarchy_theoretical_amat(h));
    printf("  Global hit rate: %.2f%%\n",
           h->total_accesses
               ? 100.0 * (1.0 - (double)h->levels[0].misses /
                                 (double)h->total_accesses)
               : 0.0);
    printf("--------------------------------------------------------------------------\n");
}
