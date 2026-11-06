#ifndef VS_TEST_HARNESS_H
#define VS_TEST_HARNESS_H

#include <math.h>
#include <stdio.h>

/* Minimal in-repo assert harness; include from exactly one translation unit per test executable. */

static int th_failures;
static int th_checks;
static const char *th_current;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        th_checks++;                                                                       \
        if (!(cond)) {                                                                     \
            th_failures++;                                                                 \
            printf("  FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, th_current, #cond);      \
        }                                                                                  \
    } while (0)

#define CHECK_EQ(actual, expected)                                                         \
    do {                                                                                   \
        const long long th_a = (long long)(actual);                                        \
        const long long th_e = (long long)(expected);                                      \
        th_checks++;                                                                       \
        if (th_a != th_e) {                                                                \
            th_failures++;                                                                 \
            printf("  FAIL %s:%d [%s] %s == %lld, expected %s == %lld\n", __FILE__,       \
                   __LINE__, th_current, #actual, th_a, #expected, th_e);                  \
        }                                                                                  \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                                  \
    do {                                                                                   \
        const double th_a = (double)(actual);                                              \
        const double th_e = (double)(expected);                                            \
        th_checks++;                                                                       \
        if (fabs(th_a - th_e) > (double)(tol)) {                                           \
            th_failures++;                                                                 \
            printf("  FAIL %s:%d [%s] %s == %.4f, expected %.4f +/- %.4f\n", __FILE__,    \
                   __LINE__, th_current, #actual, th_a, th_e, (double)(tol));             \
        }                                                                                  \
    } while (0)

#define RUN_TEST(fn)                                                                       \
    do {                                                                                   \
        const int th_before = th_failures;                                                 \
        th_current = #fn;                                                                  \
        fn();                                                                              \
        printf("%s %s\n", th_failures == th_before ? "PASS" : "FAIL", #fn);               \
    } while (0)

static inline int th_finish(void)
{
    printf("%d checks, %d failures\n", th_checks, th_failures);
    return th_failures == 0 ? 0 : 1;
}

#endif
