#ifndef HOLOGRAM_TEST_HARNESS_H
#define HOLOGRAM_TEST_HARNESS_H

/* The counting and reporting shared by the host tests.
 *
 * Deliberately this small. A test that fails should say which check failed and
 * what it wanted, and the run should exit non-zero -- everything past that is a
 * framework nobody asked for, and a dependency the whole point of these tests is
 * not to have.
 *
 * Each test is its own binary, so these can live in the header. They are
 * `inline` rather than plain `static` so that a test which happens not to need
 * one of them still compiles warning-clean under -Wall -Wextra.
 */

#include <stdio.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

static inline void check(int cond, const char *what) {
    checks++;
    if (!cond) {
        printf("  FAIL: %s\n", what);
        failures++;
    }
}

static inline void check_int(int got, int want, const char *what) {
    checks++;
    if (got != want) {
        printf("  FAIL: %s (got %d, want %d)\n", what, got, want);
        failures++;
    }
}

static inline void check_str(const char *got, const char *want, const char *what) {
    checks++;
    if (!got || !want || strcmp(got, want) != 0) {
        printf("  FAIL: %s (got \"%s\", want \"%s\")\n",
               what, got ? got : "(null)", want ? want : "(null)");
        failures++;
    }
}

static inline int report(void) {
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

#endif
