/* Fault points for the interruption test. Only the test build
 * (TAM_BUNDLE_TESTING) acts on them. */
#include "tb.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void tb_fault(const char *point) {
#ifdef TAM_BUNDLE_TESTING
    static unsigned long sequence;
    sequence++;
    const char *trace = getenv("TAM_BUNDLE_TRACE");
    if (trace) {int fd=open(trace,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);if(fd>=0){dprintf(fd,"%lu %s\n",sequence,point);close(fd);}}
    const char *at = getenv("TAM_BUNDLE_FAULT_AT");
    if (at && strtoul(at,NULL,10)==sequence) _exit(99);
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
