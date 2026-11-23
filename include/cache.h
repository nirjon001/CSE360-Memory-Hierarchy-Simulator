#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stddef.h>

/* --------------------------------------------------------------------------
 * Replacement / write policies
 * -------------------------------------------------------------------------- */
typedef enum {
    REPLACE_LRU = 0,   /* least recently used (exact, using a global clock) */
    REPLACE_FIFO,      /* first in, first out */
    REPLACE_RANDOM     /* pseudo-random victim                              */
} ReplacementPolicy;

typedef enum {
    WRITE_BACK = 0,    /* write only the cache; memory updated on eviction  */
    WRITE_THROUGH      /* write cache and next level on every store         */
} WritePolicy;

/* --------------------------------------------------------------------------
 * A single cache level
 * -------------------------------------------------------------------------- */
typedef struct Cache Cache;

typedef struct {
    uint64_t tag;
    uint64_t last_used;   /* timestamp of last access  -> LRU   */
    uint64_t inserted;    /* timestamp when installed  -> FIFO  */
    uint8_t  valid;
    uint8_t  dirty;
} CacheLine;

struct Cache {
    char             name[16];
    size_t           capacity;        /* total capacity in bytes            */
    size_t           block_size;      /* block (cache line) size in bytes   */
    size_t           associativity;   /* ways per set                       */
    size_t           num_sets;
    unsigned         offset_bits;
    unsigned         index_bits;
    ReplacementPolicy replacement;
    WritePolicy       write_policy;
    int               write_allocate; /* 1 = allocate on write miss          */
    uint32_t          hit_latency;    /* cycles to service a hit             */
    uint32_t          next_latency;   /* cycles of the next level/mem (miss) */

    CacheLine      **sets;            /* [num_sets][associativity]           */
    Cache           *next;            /* next level, NULL = main memory      */
    uint64_t         clock;           /* ticking counter for LRU/FIFO        */

    /* ----- statistics ----- */
    uint64_t accesses;
    uint64_t reads;
    uint64_t writes;
    uint64_t hits;
    uint64_t read_hits;       /* hit breakdown, used for read-only AMAT    */
    uint64_t write_hits;
    uint64_t misses;
    uint64_t evictions;      /* valid lines replaced                      */
    uint64_t writebacks;     /* dirty lines sent to next level            */
    uint64_t cycles;         /* cycles consumed by requests at this level */
};

typedef struct {
    int      hit;                 /* 1 if this level hit                      */
    uint64_t latency;             /* cycles: hit time + lower-level service   */
    int      wroteback;           /* 1 if a dirty line was evicted            */
    int      memory_access;       /* 1 if request reached main memory         */
} CacheResult;

/* Create/initialise a cache. Returns 0 on success, -1 on invalid parameters. */
int  cache_init(Cache *c, const char *name, size_t capacity, size_t block_size,
                size_t associativity, ReplacementPolicy rp, WritePolicy wp,
                int write_allocate, uint32_t hit_latency, uint32_t next_latency);

void cache_destroy(Cache *c);
void cache_reset_stats(Cache *c);
void cache_connect(Cache *c, Cache *next);

/* Access one address. Recurses into c->next on a miss (or write-through). */
CacheResult cache_access(Cache *c, uint64_t addr, int is_write);

/* Address decomposition helpers (exported for teaching / debugging). */
uint64_t cache_tag(const Cache *c, uint64_t addr);
uint64_t cache_set_index(const Cache *c, uint64_t addr);
uint64_t cache_block_address(const Cache *c, uint64_t addr);

/* Reporting */
double cache_miss_rate(const Cache *c);
double cache_read_miss_rate(const Cache *c);   /* ignores writeback traffic */
void   cache_print_config(const Cache *c);
void   cache_print_stats(const Cache *c);

#endif /* CACHE_H */
