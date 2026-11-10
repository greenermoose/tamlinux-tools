/* Post-install steps by name, and the check that native needs are present. */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A command from the live tree's bin/, else from PATH. Caller frees. */
__attribute__((unused)) static char *find_command(const tb_slot_t *s, const char *name) {
    char rel[300];
    snprintf(rel, sizeof rel, "current/bin/%s", name);
    char *own = tam_path_join(s->dir, rel);
    if (own && access(own, X_OK) == 0) return own;
    free(own);
    const char *path = getenv("PATH");
    if (!path) return NULL;
    char *copy = strdup(path);
    for (char *save = NULL, *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        char *full = dir[0] ? tam_path_join(dir, name) : NULL;
        if (full && access(full, X_OK) == 0) {
            free(copy);
            return full;
        }
        free(full);
    }
    free(copy);
    return NULL;
}

bool tb_step_is_session(const char *step) {
    return strcmp(step, "user-units-reload") == 0 || strcmp(step, "qml-cache-purge") == 0 ||
           strcmp(step, "shell-restart") == 0;
}

/* Root started by pkexec for a user: that user's own tam-bundle runs the
 * session steps when this process ends. The test build has no root, so
 * there PKEXEC_UID alone marks the run its stand-in for pkexec makes. */
static const char *pkexec_uid(void) {
    const char *uid = getenv("PKEXEC_UID");
#ifdef TAM_BUNDLE_TESTING
    return uid;
#else
    return geteuid() == 0 ? uid : NULL;
#endif
}

enum { STEP_RAN = 0, STEP_SKIPPED = 1, STEP_DEFERRED = 2, STEP_FAILED = -1 };

/* Run one step, or say why not. */
static int run_step(const tb_slot_t *s, const char *step) {
    bool session = tb_step_is_session(step);
    if (session && pkexec_uid()) return STEP_DEFERRED;
    if (session && geteuid() == 0) {
        tb_note("post-install step %s: run `tam-bundle after` as the desktop user", step);
        return STEP_SKIPPED;
    }
#ifdef TAM_BUNDLE_TESTING
    (void)s;
    tb_note("post-install step %s: skipped in the test build", step);
    return STEP_SKIPPED;
#else
    const char *argv[4] = {NULL};
    char *cmd = NULL;
    if (strcmp(step, "user-units-reload") == 0) {
        argv[0] = "systemctl"; argv[1] = "--user"; argv[2] = "daemon-reload";
    } else if (strcmp(step, "qml-cache-purge") == 0 || strcmp(step, "shell-restart") == 0) {
        const char *name = strcmp(step, "qml-cache-purge") == 0 ? "tam-qmlcache-purge" : "tam-restart-shell";
        cmd = find_command(s, name);
        if (!cmd) {
            tb_note("post-install step %s: %s is not installed; skipped", step, name);
            return STEP_SKIPPED;
        }
        argv[0] = cmd;
    } else {
        /* man-index: the index location for a tree outside the standard
         * manual path is not settled yet (see the design's open items). */
        tb_note("post-install step %s: not available in tam-bundle %s; skipped", step, TB_VERSION);
        return STEP_SKIPPED;
    }
    tam_exec_result_t res = tam_exec(argv, 60000);
    int ok = res.exit_code == 0;
    if (!ok) tb_err("post-install step %s failed (exit %d)%s%s", step, res.exit_code,
                    res.stderr_len ? ": " : "", res.stderr_len ? res.stderr_buf : "");
    tam_exec_result_free(&res);
    free(cmd);
    return ok ? STEP_RAN : STEP_FAILED;
#endif
}

static int run_list(const tb_slot_t *s, char *const *steps, size_t n) {
    int result = TB_OK;
    for (size_t i = 0; i < n; i++) {
        int r = run_step(s, steps[i]);
        if (r == STEP_DEFERRED)
            tb_log(s, "after %s deferred to uid %s", steps[i], pkexec_uid());
        else
            tb_log(s, "after %s %s", steps[i], r == STEP_RAN ? "ok" : r == STEP_SKIPPED ? "skipped" : "failed");
        if (r == STEP_FAILED) result = TB_AFTER_FAILED;
    }
    return result;
}

int tb_run_after(const tb_slot_t *s, const tb_bundle_t *b) {
    return run_list(s, b->after, b->nafter);
}

int tb_run_steps(const tb_slot_t *s, char *const *steps, size_t n) {
    return run_list(s, steps, n);
}

/* The distribution's ID from os-release, or "" when unknown. */
static void distro_id(char out[64]) {
    out[0] = '\0';
    size_t len;
    char *text = tb_read_at(AT_FDCWD, "/etc/os-release", &len);
    if (!text) text = tb_read_at(AT_FDCWD, "/usr/lib/os-release", &len);
    if (!text) return;
    for (char *line = text; line && *line;) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, "ID=", 3) == 0) {
            const char *v = line + 3;
            if (*v == '"') v++;
            snprintf(out, 64, "%s", v);
            char *q = strchr(out, '"');
            if (q) *q = '\0';
            break;
        }
        line = nl ? nl + 1 : NULL;
    }
    free(text);
}

static bool command_present(const char *name) {
    const char *path = getenv("PATH");
    if (!path || strchr(name, '/')) return false;
    char *copy = strdup(path);
    bool found = false;
    for (char *save = NULL, *dir = strtok_r(copy, ":", &save); dir && !found; dir = strtok_r(NULL, ":", &save)) {
        char *full = tam_path_join(dir, name);
        found = full && access(full, X_OK) == 0;
        free(full);
    }
    free(copy);
    return found;
}

size_t tb_needs_check(const tb_bundle_t *b, bool print) {
    size_t missing = 0;
    char id[64];
    distro_id(id);
    for (size_t i = 0; i < b->nneeds; i++) {
        const tb_need_t *n = &b->needs[i];
        bool ok = strcmp(n->kind, "command") == 0 ? command_present(n->target) : access(n->target, F_OK) == 0;
        if (ok) continue;
        missing++;
        if (!print) continue;
        /* Name this distribution's package when the bundle gives one. */
        char want[80], pkg[128] = "";
        snprintf(want, sizeof want, "%s=", id);
        const char *hit = id[0] ? strstr(n->pkgs, want) : NULL;
        if (hit && (hit == n->pkgs || hit[-1] == ' ')) {
            snprintf(pkg, sizeof pkg, "%s", hit + strlen(want));
            char *sp = strchr(pkg, ' ');
            if (sp) *sp = '\0';
        }
        if (pkg[0])
            tb_err("%s needs %s %s: install the %s package", b->id, n->kind, n->target, pkg);
        else if (n->pkgs[0])
            tb_err("%s needs %s %s (packages: %s)", b->id, n->kind, n->target, n->pkgs);
        else
            tb_err("%s needs %s %s", b->id, n->kind, n->target);
    }
    return missing;
}
