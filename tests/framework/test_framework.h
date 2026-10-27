/*
 * Test Framework
 *
 * Simple test framework for the assessment.
 */

#ifndef _TEST_FRAMEWORK_H
#define _TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdbool.h>

/* Test result structure */
typedef struct {
    int total;
    int passed;
    int failed;
    int skipped;
} test_results_t;

/* Test function type */
typedef int (*test_func_t)(void);

/* Test case structure */
typedef struct {
    const char *name;
    test_func_t func;
    bool skip;
} test_case_t;

/* Assertion macros */
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("    ASSERT FAILED: %s\n", msg); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
        return -1; \
    } \
} while (0)

#define TEST_ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("    ASSERT FAILED: %s\n", msg); \
        printf("    expected: %ld, got: %ld\n", (long)(b), (long)(a)); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
        return -1; \
    } \
} while (0)

#define TEST_ASSERT_STR_EQ(a, b, msg) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("    ASSERT FAILED: %s\n", msg); \
        printf("    expected: \"%s\", got: \"%s\"\n", (b), (a)); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
        return -1; \
    } \
} while (0)

#define TEST_ASSERT_NULL(p, msg) TEST_ASSERT((p) == NULL, msg)
#define TEST_ASSERT_NOT_NULL(p, msg) TEST_ASSERT((p) != NULL, msg)

/* Test suite macros */
#define TEST_SUITE_START(name) \
    printf("\n========================================\n"); \
    printf(" %s\n", name); \
    printf("========================================\n\n"); \
    test_results_t __results = {0, 0, 0, 0}

#define TEST_RUN(tc) do { \
    __results.total++; \
    printf("  [%d] %s ... ", __results.total, (tc).name); \
    fflush(stdout); \
    if ((tc).skip) { \
        printf("SKIP\n"); \
        __results.skipped++; \
    } else if ((tc).func() == 0) { \
        printf("PASS\n"); \
        __results.passed++; \
    } else { \
        printf("FAIL\n"); \
        __results.failed++; \
    } \
} while (0)

#define TEST_SUITE_END() \
    printf("\n----------------------------------------\n"); \
    printf("Results: %d/%d passed", __results.passed, __results.total); \
    if (__results.skipped > 0) \
        printf(", %d skipped", __results.skipped); \
    if (__results.failed > 0) \
        printf(", %d FAILED", __results.failed); \
    printf("\n----------------------------------------\n\n"); \
    return __results.failed

/* Global test initialization */
void test_init(void);
void test_cleanup(void);

#endif /* _TEST_FRAMEWORK_H */
