/*
 * Test Framework Implementation
 */

#include "test_framework.h"
#include "lib/common/log.h"
#include "lib/common/memory.h"

void test_init(void)
{
    log_init("test", LOG_DEST_STDOUT);
    log_set_level(LOG_LEVEL_WARNING);  /* Reduce noise during tests */
}

void test_cleanup(void)
{
    mem_stats_dump();
    log_close();
}
