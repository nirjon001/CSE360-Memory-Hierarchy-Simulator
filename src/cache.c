#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Small helpers
 * -------------------------------------------------------------------------- */
static unsigned log2_exact(size_t value)
{
    unsigned bits = 0;
    while (value > 1) {
        if (value & 1u) return (unsigned)-1;  /* not a power of two */
        value >>= 1;
        bits++;
    }
    return bits;
}

static const char *policy_name(ReplacementPolicy p)
{
    switch (p) {
        case REPLACE_LRU:    return "LRU";
        case REPLACE_FIFO:   return "FIFO";
        case REPLACE_RANDOM: return "RANDOM";
        default:             return "?";
    }
}

static const char *write_name(WritePolicy p)
{
    return p == WRITE_BACK ? "write-back" : "write-through";
}

/* Deterministic pseudo-random victim: cheap xorshift, no global rand() state. */
static size_t random_way(const Cache *c, uint64_t tag, size_t ways)
{
    uint64_t x = c->clock * 6364136223846793005ULL + tag + 1442695040888963407ULL;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return (size_t)((x * 2685821657736338717ULL) % ways);
}

/* Forward a request to the next level, or model main memory if there is none. */
static CacheResult forward(Cache *c, uint64_t addr, int is_write)
{
    CacheResult r;
    memset(&r, 0, sizeof(r));

    if (c->next) {
        return cache_access(c->next, addr, is_write);
    }
    r.latency       = c->next_latency;
    r.memory_access = 1;
    (void)addr;
    (void)is_write;
    return r;
}

/* --------------------------------------------------------------------------
 * Initialisation / teardown
 * -------------------------------------------------------------------------- */
int cache_init(Cache *c, const char *name, size_t capacity, size_t block_size,
               size_t associativity, ReplacementPolicy rp, WritePolicy wp,
               int write_allocate, uint32_t hit_latency, uint32_t next_latency)
{
    if (!c || !name || !capacity || !block_size || !associativity) return -1;

    unsigned ob = log2_exact(block_size);
    if (ob == (unsigned)-1) return -1;

    size_t line_size = block_size * associativity;
    if (line_size == 0 || capacity % line_size != 0) return -1;

    size_t sets = capacity / line_size;
    unsigned ib = log2_exact(sets);
    if (ib == (unsigned)-1) return -1;

    memset(c, 0, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->capacity       = capacity;
    c->block_size     = block_size;
    c->associativity  = associativity;
    c->num_sets       = sets;
    c->offset_bits    = ob;
    c->index_bits     = ib;
    c->replacement    = rp;
    c->write_policy   = wp;
    c->write_allocate = write_allocate ? 1 : 0;
    c->hit_latency    = hit_latency;
    c->next_latency   = next_latency;

    c->sets = (CacheLine **)calloc(sets, sizeof(*c->sets));
    if (!c->sets) return -1;
    for (size_t i = 0; i < sets; i++) {
        c->sets[i] = (CacheLine *)calloc(associativity, sizeof(CacheLine));
        if (!c->sets[i]) {
            for (size_t j = 0; j < i; j++) free(c->sets[j]);
            free(c->sets);
            c->sets = NULL;
            return -1;
        }
    }
    return 0;
}

void cache_destroy(Cache *c)
{
    if (!c || !c->sets) return;
    for (size_t i = 0; i < c->num_sets; i++) free(c->sets[i]);
    free(c->sets);
    c->sets = NULL;
}

void cache_reset_stats(Cache *c)
{
    if (!c) return;
    c->accesses = c->reads = c->writes = 0;
    c->hits = c->read_hits = c->write_hits = c->misses = 0;
    c->evictions = c->writebacks = 0;
    c->cycles = 0;
    c->clock = 0;
    for (size_t i = 0; i < c->num_sets; i++)
        for (size_t j = 0; j < c->associativity; j++)
            memset(&c->sets[i][j], 0, sizeof(CacheLine));
}

void cache_connect(Cache *c, Cache *next)
{
    if (c) c->next = next;
}

/* --------------------------------------------------------------------------
 * Address decomposition
 * -------------------------------------------------------------------------- */
uint64_t cache_tag(const Cache *c, uint64_t addr)
{
    return addr >> (c->index_bits + c->offset_bits);
}

uint64_t cache_set_index(const Cache *c, uint64_t addr)
{
    return (addr >> c->offset_bits) & (((uint64_t)1 << c->index_bits) - 1);
}

uint64_t cache_block_address(const Cache *c, uint64_t addr)
{
    return addr & ~(((uint64_t)1 << c->offset_bits) - 1);
}

/* --------------------------------------------------------------------------
 * The actual access
 * -------------------------------------------------------------------------- */
CacheResult cache_access(Cache *c, uint64_t addr, int is_write)
{
    CacheResult res;
    memset(&res, 0, sizeof(res));
    if (!c) return res;

    c->clock++;
    c->accesses++;
    if (is_write) c->writes++; else c->reads++;

    uint64_t idx = cache_set_index(c, addr);
    uint64_t tag = cache_tag(c, addr);
    CacheLine *set = c->sets[idx];

    /* ---- lookup ---- */
    size_t way = c->associativity;
    for (size_t i = 0; i < c->associativity; i++) {
        if (set[i].valid && set[i].tag == tag) {
            way = i;
            break;
        }
    }

    res.latency = c->hit_latency;

    if (way < c->associativity) {
        /* ================= HIT ================= */
        c->hits++;
        if (is_write) c->write_hits++; else c->read_hits++;
        res.hit = 1;
        set[way].last_used = c->clock;          /* FIFO victims ignore this   */

        if (is_write) {
            if (c->write_policy == WRITE_BACK) {
                set[way].dirty = 1;
            } else {
                set[way].dirty = 0;
                CacheResult lower = forward(c, addr, 1);
                res.latency += lower.latency;
                res.memory_access |= lower.memory_access;
            }
        }
    } else {
        /* ================= MISS ================= */
        c->misses++;

        /* pick a victim: invalid line first, else apply replacement policy */
        size_t victim = c->associativity;
        for (size_t i = 0; i < c->associativity; i++) {
            if (!set[i].valid) { victim = i; break; }
        }
        if (victim == c->associativity) {
            c->evictions++;
            switch (c->replacement) {
                case REPLACE_LRU:
                    victim = 0;
                    for (size_t i = 1; i < c->associativity; i++)
                        if (set[i].last_used < set[victim].last_used) victim = i;
                    break;
                case REPLACE_FIFO:
                    victim = 0;
                    for (size_t i = 1; i < c->associativity; i++)
                        if (set[i].inserted < set[victim].inserted) victim = i;
                    break;
                case REPLACE_RANDOM:
                default:
                    victim = random_way(c, tag, c->associativity);
                    break;
            }

            /* Write back the dirty victim. A real cache uses a write buffer,
             * so this traffic is counted in the lower level but does NOT stall
             * the requesting access (otherwise AMAT would include stores). */
            if (set[victim].dirty) {
                uint64_t shift = c->index_bits + c->offset_bits;
                uint64_t victim_addr = (set[victim].tag << shift) |
                                       (idx << c->offset_bits);
                CacheResult wb = forward(c, victim_addr, 1);
                c->writebacks++;
                res.wroteback = 1;
                (void)wb;
            }
        }

        if (!is_write || c->write_allocate) {
            /* fetch the block from the lower level (read, not a store) */
            CacheResult lower = forward(c, addr, 0);
            res.latency += lower.latency;
            res.memory_access |= lower.memory_access;

            set[victim].valid     = 1;
            set[victim].tag       = tag;
            set[victim].dirty     = (is_write && c->write_policy == WRITE_BACK);
            set[victim].last_used = c->clock;
            set[victim].inserted  = c->clock;

            if (is_write && c->write_policy == WRITE_THROUGH) {
                CacheResult wt = forward(c, addr, 1);
                res.latency += wt.latency;
                res.memory_access |= wt.memory_access;
            }
        } else {
            /* no-write-allocate: go straight to the next level */
            CacheResult lower = forward(c, addr, 1);
            res.latency += lower.latency;
            res.memory_access |= lower.memory_access;
        }
    }

    c->cycles += res.latency;   /* counted exactly once per level */
    return res;
}

/* --------------------------------------------------------------------------
 * Reporting
 * -------------------------------------------------------------------------- */
double cache_miss_rate(const Cache *c)
{
    if (!c || c->accesses == 0) return 0.0;
    return (double)c->misses / (double)c->accesses;
}

double cache_read_miss_rate(const Cache *c)
{
    if (!c || c->reads == 0) return 0.0;
    return (double)(c->reads - c->read_hits) / (double)c->reads;
}

void cache_print_config(const Cache *c)
{
    if (!c) return;
    printf("    %-4s  %6zu B  block=%3zu B  ways=%-3zu  sets=%5zu  "
           "%-6s  %-13s  wa=%d  hit=%u cyc\n",
           c->name, c->capacity, c->block_size, c->associativity, c->num_sets,
           policy_name(c->replacement), write_name(c->write_policy),
           c->write_allocate, c->hit_latency);
}

void cache_print_stats(const Cache *c)
{
    if (!c) return;
    printf("  %-4s : accesses=%-9llu hits=%-9llu misses=%-9llu "
           "evictions=%-8llu writebacks=%-8llu miss rate=%6.2f%%\n",
           c->name,
           (unsigned long long)c->accesses,
           (unsigned long long)c->hits,
           (unsigned long long)c->misses,
           (unsigned long long)c->evictions,
           (unsigned long long)c->writebacks,
           cache_miss_rate(c) * 100.0);
}
