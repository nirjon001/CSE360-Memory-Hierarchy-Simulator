#include "cache.h"
#include "hierarchy.h"
#include "trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_COUNT  200000
#define DEFAULT_BASE   0x10000000ULL
#define DEFAULT_MEM    100u
#define VERBOSE_LIMIT  12

static const uint32_t default_hits[MAX_LEVELS] = { 1, 10, 30 };

typedef struct {
    const char *l1;
    const char *l2;
    const char *l3;
    uint32_t    mem_latency;
    size_t      count;
    uint64_t    base;
    size_t      param;
    GenKind     gen;
    const char *trace_file;
    const char *out_file;
    int         verbose;
    int         demo;
} Options;

static void print_usage(const char *prog)
{
    printf("Memory Hierarchy Simulator - CSE360 Computer Architecture\n\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("  -l1 SPEC     L1 cache specification   (default: 16K,64,1)\n");
    printf("  -l2 SPEC     L2 cache specification   (adds a second level)\n");
    printf("  -l3 SPEC     L3 cache specification   (adds a third level)\n");
    printf("  -m N         main-memory latency in cycles (default: %u)\n", DEFAULT_MEM);
    printf("  -t FILE      load an address trace file\n");
    printf("  -g KIND      generate a workload: seq|stride|random|row|col|ws|hot\n");
    printf("  -p N         generator parameter (stride / matrix size / lines)\n");
    printf("  -n N         number of generated accesses (default: %d)\n", DEFAULT_COUNT);
    printf("  -b ADDR      base address, decimal or 0x hex (default: 0x10000000)\n");
    printf("  -o FILE      save the generated/loaded trace to FILE\n");
    printf("  -v           verbose: show first %d accesses and per-level hits\n", VERBOSE_LIMIT);
    printf("  -d           run the built-in comparison demo\n");
    printf("  -h           show this help\n\n");
    printf("SPEC format: CAP,BLOCK,WAYS[,REPL][,WB|WT][,WA|NWA][,HITCYC]\n");
    printf("  CAP/BLOCK : integer with optional K/M/G suffix\n");
    printf("  WAYS      : 1|direct, 2way, 4way, 8way, full or any integer\n");
    printf("  REPL      : lru (default), fifo, rand\n");
    printf("  WB|WT     : write-back (default), write-through\n");
    printf("  WA|NWA    : write-allocate (default), no-write-allocate\n\n");
    printf("Examples:\n");
    printf("  %s -l1 32K,64,8,lru -g row -p 512 -n 100000\n", prog);
    printf("  %s -l1 16K,64,1 -l2 256K,64,8,lru -g col -p 512\n", prog);
    printf("  %s -t traces/sample.trace -l1 8K,32,4,fifo\n", prog);
    printf("  %s -d\n\n", prog);
}

static GenKind parse_gen(const char *s)
{
    if (!strcasecmp(s, "seq") || !strcasecmp(s, "sequential")) return GEN_SEQUENTIAL;
    if (!strcasecmp(s, "stride") || !strcasecmp(s, "strided")) return GEN_STRIDED;
    if (!strcasecmp(s, "random")) return GEN_RANDOM;
    if (!strcasecmp(s, "row")) return GEN_MATRIX_ROW;
    if (!strcasecmp(s, "col")) return GEN_MATRIX_COL;
    if (!strcasecmp(s, "ws") || !strcasecmp(s, "working")) return GEN_WORKING_SET;
    if (!strcasecmp(s, "hot") || !strcasecmp(s, "hotcold")) return GEN_HOT_COLD;
    return (GenKind)-1;
}

static int parse_u64(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    if (end == s) return -1;
    *out = (uint64_t)v;
    return 0;
}

static int build_hierarchy(Hierarchy *h, const char *const *specs,
                           int nlevels, uint32_t mem_latency)
{
    LevelConfig cfgs[MAX_LEVELS];
    for (int i = 0; i < nlevels; i++) {
        if (hierarchy_parse_level(specs[i], default_hits[i], &cfgs[i]) != 0) {
            fprintf(stderr, "Invalid cache specification: %s\n", specs[i]);
            return -1;
        }
    }
    if (hierarchy_init(h, cfgs, nlevels, mem_latency) != 0) {
        fprintf(stderr, "Failed to initialise the memory hierarchy.\n");
        return -1;
    }
    return 0;
}

static void run_trace(Hierarchy *h, const Trace *t, int verbose)
{
    for (size_t i = 0; i < t->count; i++) {
        uint64_t prev_acc[MAX_LEVELS], prev_hit[MAX_LEVELS];
        if (verbose && i < VERBOSE_LIMIT) {
            for (int l = 0; l < h->nlevels; l++) {
                prev_acc[l] = h->levels[l].accesses;
                prev_hit[l] = h->levels[l].hits;
            }
        }

        hierarchy_access(h, t->items[i].addr, t->items[i].is_write);

        if (verbose && i < VERBOSE_LIMIT) {
            printf("  %2zu: 0x%08" PRIx64 " %c  -> ", i, t->items[i].addr,
                   t->items[i].is_write ? 'W' : 'R');
            for (int l = 0; l < h->nlevels; l++) {
                int reached = (h->levels[l].accesses > prev_acc[l]);
                printf("%s:%s  ", h->levels[l].name,
                       !reached ? "---" :
                       (h->levels[l].hits > prev_hit[l] ? "HIT" : "MISS"));
            }
            printf("\n");
        }
    }
}

static void save_trace(const Trace *t, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror("fopen"); return; }
    fprintf(f, "# generated address trace\n");
    for (size_t i = 0; i < t->count; i++)
        fprintf(f, "0x%" PRIx64 " %c\n", t->items[i].addr,
                t->items[i].is_write ? 'W' : 'R');
    fclose(f);
    printf("  Trace saved to %s (%zu accesses)\n", path, t->count);
}

/* --------------------------------------------------------------------------
 * Single simulation from the command line
 * -------------------------------------------------------------------------- */
static int run_single(const Options *o)
{
    Trace trace;
    trace_init(&trace);

    if (o->trace_file) {
        if (trace_load_file(&trace, o->trace_file) != 0) {
            trace_free(&trace);
            return 1;
        }
    } else {
        if (trace_generate(&trace, o->gen, o->count, o->base, o->param) != 0) {
            fprintf(stderr, "Failed to generate trace.\n");
            trace_free(&trace);
            return 1;
        }
    }
    if (trace.count == 0) {
        fprintf(stderr, "Trace is empty.\n");
        trace_free(&trace);
        return 1;
    }
    if (o->out_file) save_trace(&trace, o->out_file);

    const char *specs[MAX_LEVELS] = { o->l1, o->l2, o->l3 };
    int nlevels = 1 + (o->l2 != NULL) + (o->l3 != NULL);
    Hierarchy h;

    if (build_hierarchy(&h, specs, nlevels, o->mem_latency) != 0) {
        trace_free(&trace);
        return 1;
    }

    printf("\n  Workload: %s (%zu accesses)",
           o->trace_file ? o->trace_file : gen_name(o->gen), trace.count);
    if (!o->trace_file) printf("  param=%zu  base=0x%" PRIx64, o->param, o->base);
    printf("\n");
    if (o->verbose) printf("\n  First accesses:\n");

    run_trace(&h, &trace, o->verbose);

    char title[160];
    snprintf(title, sizeof(title), "%s on %s",
             o->trace_file ? "Trace run" : gen_name(o->gen),
             o->trace_file ? o->trace_file : "generated workload");
    hierarchy_report(&h, title);

    hierarchy_destroy(&h);
    trace_free(&trace);
    return 0;
}

/* --------------------------------------------------------------------------
 * Demo: compare cache geometries and policies on identical workloads
 * -------------------------------------------------------------------------- */
typedef struct {
    const char *label;
    const char *l1;
    const char *l2;
    const char *l3;
} Config;

static void print_row(const char *workload, const char *label, const Hierarchy *h)
{
    double l1mr = h->nlevels > 0 ? cache_miss_rate(&h->levels[0]) * 100.0 : 0.0;
    double l2mr = h->nlevels > 1 ? cache_miss_rate(&h->levels[1]) * 100.0 : 0.0;

    printf("  %-12s %-30s %8.2f %8.2f %8.3f %10.3f\n",
           workload, label, l1mr, l2mr,
           hierarchy_theoretical_amat(h), hierarchy_amat(h));
}

static void run_config(const Trace *t, const Config *cfg, uint32_t mem,
                       const char *workload)
{
    const char *specs[MAX_LEVELS] = { cfg->l1, cfg->l2, cfg->l3 };
    int nlevels = 1 + (cfg->l2 != NULL) + (cfg->l3 != NULL);
    Hierarchy h;
    if (build_hierarchy(&h, specs, nlevels, mem) != 0) return;
    run_trace(&h, t, 0);
    print_row(workload, cfg->label, &h);
    hierarchy_destroy(&h);
}

static void run_demo(void)
{
    static const Config configs[] = {
        { "direct-mapped 16K,64,1",       "16K,64,1",               NULL, NULL },
        { "2-way 16K,64,2",               "16K,64,2",               NULL, NULL },
        { "4-way 16K,64,4",               "16K,64,4",               NULL, NULL },
        { "fully associative 16K,64,full","16K,64,full",            NULL, NULL },
        { "2-level 16K,64,1 + 256K,64,8", "16K,64,1", "256K,64,8",   NULL },
        { "3-level 32K,64,8 + 256K,64,8 + 2M,64,16",
                                          "32K,64,8", "256K,64,8", "2M,64,16" },
    };
    static const struct { GenKind kind; size_t param; } workloads[] = {
        { GEN_SEQUENTIAL,  0 },
        { GEN_MATRIX_ROW, 512 },
        { GEN_MATRIX_COL, 512 },
        { GEN_RANDOM,      0 },
        { GEN_WORKING_SET, 32 },
    };
    const size_t n = sizeof(configs) / sizeof(configs[0]);
    const size_t w = sizeof(workloads) / sizeof(workloads[0]);
    uint32_t mem = DEFAULT_MEM;

    printf("\n==========================================================================\n");
    printf(" MEMORY HIERARCHY DEMO  -  identical traces, different configurations\n");
    printf("==========================================================================\n");
    printf("  L1 hit=1cyc, L2 hit=10cyc, L3 hit=30cyc, memory=%u cyc\n", mem);
    printf("  Trace length: %d accesses per workload\n\n", DEFAULT_COUNT);
    printf("  %-12s %-30s %8s %8s %8s %10s\n",
           "Workload", "Configuration", "L1 miss%", "L2 miss%", "AMAT", "Measured");
    printf("  ------------------------------------------------------------------------\n");

    Trace trace;
    trace_init(&trace);
    for (size_t j = 0; j < w; j++) {
        trace_generate(&trace, workloads[j].kind, DEFAULT_COUNT,
                       DEFAULT_BASE, workloads[j].param);
        for (size_t i = 0; i < n; i++)
            run_config(&trace, &configs[i], mem, gen_name(workloads[j].kind));
        printf("  ------------------------------------------------------------------------\n");
        trace.count = 0;   /* reuse the allocation for the next workload */
    }
    trace_free(&trace);

    printf("\n  LRU vs FIFO vs Random on a 4-way set with 4 hot + 2 cold lines:\n\n");
    static const Config policies[] = {
        { "LRU",   "8K,32,4,lru",   NULL, NULL },
        { "FIFO",  "8K,32,4,fifo",  NULL, NULL },
        { "Random","8K,32,4,rand",  NULL, NULL },
    };
    trace_generate(&trace, GEN_HOT_COLD, DEFAULT_COUNT, DEFAULT_BASE, 6);
    for (size_t i = 0; i < sizeof(policies)/sizeof(policies[0]); i++)
        run_config(&trace, &policies[i], mem, "working-set");
    trace_free(&trace);

    printf("\n  Reading the table: AMAT = hit time + miss rate x miss penalty.\n");
    printf("  Smaller AMAT is better. Compare rows vertically for each workload.\n\n");
}

/* --------------------------------------------------------------------------
 * Entry point
 * -------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    Options o;
    memset(&o, 0, sizeof(o));
    o.l1 = "16K,64,1";
    o.mem_latency = DEFAULT_MEM;
    o.count = DEFAULT_COUNT;
    o.base = DEFAULT_BASE;
    o.param = 512;
    o.gen = GEN_SEQUENTIAL;

    if (argc == 1) {
        run_demo();
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "-l1") && i + 1 < argc) o.l1 = argv[++i];
        else if (!strcmp(argv[i], "-l2") && i + 1 < argc) o.l2 = argv[++i];
        else if (!strcmp(argv[i], "-l3") && i + 1 < argc) o.l3 = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            uint64_t v; if (parse_u64(argv[++i], &v) || v > 1000000) return 1;
            o.mem_latency = (uint32_t)v;
        }
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            uint64_t v; if (parse_u64(argv[++i], &v) || v == 0) return 1;
            o.count = (size_t)v;
        }
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            uint64_t v; if (parse_u64(argv[++i], &v) || v == 0) return 1;
            o.param = (size_t)v;
        }
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            if (parse_u64(argv[++i], &o.base)) return 1;
        }
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) o.trace_file = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) o.out_file = argv[++i];
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) {
            o.gen = parse_gen(argv[++i]);
            if ((int)o.gen < 0) {
                fprintf(stderr, "Unknown workload. Use seq|stride|random|row|col|ws\n");
                return 1;
            }
        }
        else if (!strcmp(argv[i], "-v")) o.verbose = 1;
        else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--demo")) o.demo = 1;
        else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (o.demo) { run_demo(); return 0; }
    return run_single(&o);
}
