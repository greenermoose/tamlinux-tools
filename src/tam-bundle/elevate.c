/* Privilege: a user's changing command on a root-owned slot runs again as
 * root through pkexec; afterwards this process, still the user, runs the
 * session steps the root run left for it in the log. */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* An absolute path, so PATH cannot choose what runs as root. */
#define TB_PKEXEC "/usr/bin/pkexec"

/* pkexec's own failures: the program never ran. */
#define PKEXEC_NOT_AUTHORIZED 126
#define PKEXEC_CANNOT_RUN 127

static const char *pkexec_path(void) {
#ifdef TAM_BUNDLE_TESTING
    const char *fake = getenv("TAM_BUNDLE_PKEXEC");
    if (fake) return fake;
#endif
    return TB_PKEXEC;
}

bool tb_test_pkexec(void) {
#ifdef TAM_BUNDLE_TESTING
    return getenv("TAM_BUNDLE_PKEXEC") && !getenv("PKEXEC_UID");
#else
    return false;
#endif
}

static long long log_size(const char *log) {
    struct stat st;
    return log && stat(log, &st) == 0 ? (long long)st.st_size : 0;
}

/* Steps logged "after <step> deferred to uid <uid>" for this user since
 * `from`, in order, each once. */
static size_t deferred_steps(const char *log, long long from, char **steps, size_t cap) {
    size_t len, n = 0;
    char *text = log ? tb_read_at(AT_FDCWD, log, &len) : NULL;
    if (!text) return 0;
    if (from < 0 || (size_t)from > len) from = 0;
    char want[64];
    snprintf(want, sizeof want, " deferred to uid %u", (unsigned)getuid());
    for (char *line = text + from; *line;) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *after = strstr(line, "Z after ");
        char *tail = after ? strstr(after, want) : NULL;
        if (tail && tail[strlen(want)] == '\0' && n < cap) {
            *tail = '\0';
            const char *step = after + 8;
            bool seen = false;
            for (size_t i = 0; i < n; i++) seen = seen || strcmp(steps[i], step) == 0;
            if (!seen) steps[n++] = strdup(step);
        }
        if (!nl) break;
        line = nl + 1;
    }
    free(text);
    return n;
}

int tb_pkexec(char **args, const char *slot, const char *state) {
    const char *pkexec = pkexec_path();
    if (access(pkexec, X_OK) != 0) {
        tb_err("slot %s belongs to root, and %s is not installed; run tam-bundle as root instead "
               "(for example with sudo), then `tam-bundle after` as yourself", slot, pkexec);
        return TB_FAIL;
    }
    char self[PATH_MAX];
    ssize_t sl = readlink("/proc/self/exe", self, sizeof self - 1);
    if (sl <= 0) {
        tb_err("cannot find this program's own path to run it through pkexec: %s", strerror(errno));
        return TB_FAIL;
    }
    self[sl] = '\0';

    size_t n = 0;
    while (args[n]) n++;
    char **argv = calloc(n + 3, sizeof *argv);
    if (!argv) return TB_FAIL;
    argv[0] = (char *)pkexec;
    argv[1] = self;
    for (size_t i = 0; i < n; i++) argv[i + 2] = args[i];

    char *state_dir = tb_state_dir(slot, state);
    char *log = state_dir ? tam_path_join(state_dir, "log") : NULL;
    long long from = log_size(log);

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        tb_err("cannot start pkexec: %s", strerror(errno));
        free(argv);
        free(log);
        free(state_dir);
        return TB_FAIL;
    }
    if (pid == 0) {
        execv(pkexec, argv);
        _exit(PKEXEC_CANNOT_RUN);
    }
    /* Like system(): a ^C reaches the root run, which is safe to stop at
     * any point; this process waits to report what it left. */
    struct sigaction ign = {.sa_handler = SIG_IGN}, old_int, old_quit;
    sigaction(SIGINT, &ign, &old_int);
    sigaction(SIGQUIT, &ign, &old_quit);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);
    free(argv);

    int r;
    if (WIFSIGNALED(status)) {
        tb_err("the root run of tam-bundle ended on signal %d; run `tam-bundle status`", WTERMSIG(status));
        r = TB_FAIL;
    } else if (WEXITSTATUS(status) == PKEXEC_NOT_AUTHORIZED) {
        tb_err("not authorized to change slot %s (refused or dismissed); nothing was changed", slot);
        r = TB_FAIL;
    } else if (WEXITSTATUS(status) == PKEXEC_CANNOT_RUN) {
        tb_err("pkexec could not run tam-bundle as root, so nothing was changed. Where no polkit "
               "agent runs, as on a text console, run the same command as root (for example with "
               "sudo), then `tam-bundle after` as yourself");
        r = TB_FAIL;
    } else {
        r = WEXITSTATUS(status);
    }

    /* Whatever the root run's result, run what it handed over. */
    char *steps[32];
    size_t ns = deferred_steps(log, from, steps, 32);
    if (ns) {
        tb_slot_t s;
        int sr = TB_FAIL;
        if (tb_slot_open(&s, slot, state, false) == 0) sr = tb_run_steps(&s, steps, ns);
        else tb_err("cannot open slot %s to run the session steps; run `tam-bundle after`", slot);
        tb_slot_close(&s);
        if (r == TB_OK && sr != TB_OK) r = TB_AFTER_FAILED;
        for (size_t i = 0; i < ns; i++) free(steps[i]);
    }
    free(log);
    free(state_dir);
    return r;
}
