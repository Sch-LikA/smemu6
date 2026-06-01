#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include("generated_sm6_st_symbols.h")
#    include "generated_sm6_st_symbols.h"
#  elif __has_include("../build/generated-st/generated_sm6_st_symbols.h")
#    include "../build/generated-st/generated_sm6_st_symbols.h"
#  else
#    error "generated_sm6_st_symbols.h is required; build smaky6_st_exports first"
#  endif
#else
#  include "generated_sm6_st_symbols.h"
#endif

/* Look up one generated symbol entry by either its decoded or preferred name. */
static const struct GeneratedSmaky6StEntry *find_symbol(const char *name)
{
    for (size_t i = 0; i < smaky6_sm6_symbol_count; ++i) {
        const struct GeneratedSmaky6StEntry *entry = &smaky6_sm6_symbols[i];

        if (strcmp(entry->name, name) == 0 || strcmp(entry->best_name, name) == 0)
            return entry;
    }

    return NULL;
}

/* Smoke-test the generated ST export header by resolving a few known symbols. */
int main(void)
{
    const char *names[] = {"MAXMEM", "OUTCAR", "ALPHA", "LF"};

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const struct GeneratedSmaky6StEntry *entry = find_symbol(names[i]);

        if (!entry) {
            fprintf(stderr, "missing generated symbol: %s\n", names[i]);
            return 1;
        }

        printf("%s -> 0x%04X (decoded=%s best=%s flags=0x%02X)\n",
               names[i],
               entry->value,
               entry->name,
               entry->best_name,
               entry->high_bit_mask);
    }

    return 0;
}