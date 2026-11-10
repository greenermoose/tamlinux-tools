/* Test the live-root guard without changing any installed files. The
 * Python suite runs this fixture only inside its private PID namespace. */
#include "tb.h"

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    tb_manifest_t empty = {0};
    /* No placed files. The suite starts a compositor and a client with a
     * current-tree argument; both must survive a requested stop pass. */
    return tb_busy(argv[1], argv[2], &empty, true) > 0 ? 0 : 1;
}
