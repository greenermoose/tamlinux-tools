/* Fault points for the interruption test. Only the test build
 * (TAM_BUNDLE_TESTING) acts on them. */
#include "tb.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void tb_fault(const char *point) {
#ifdef TAM_BUNDLE_TESTING
    const char *want = getenv("TAM_BUNDLE_FAULT");
    if (want && strcmp(want, point) == 0) {
        fprintf(stderr, "tam-bundle: test fault at %s\n", point);
        _exit(99);
    }
#else
    (void)point;
#endif
}

void tb_fault_file(size_t index) {
#ifdef TAM_BUNDLE_TESTING
    const char *slow = getenv("TAM_BUNDLE_SLOW_US");
    if (slow) usleep((useconds_t)atoi(slow));
    if (index > 0 && index % 100 == 0) {
        char point[64];
        snprintf(point, sizeof point, "stage-%zu", index);
        tb_fault(point);
    }
#else
    (void)index;
#endif
}
