/* Git, through the git command: find, fetch and export exact commits. */
#include "tb.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GIT_QUICK_MS   60000
#define GIT_NETWORK_MS (15 * 60000)

bool tb_valid_commit(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n != 40 && n != 64) return false;
    for (const char *p = s; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    return true;
}

/* Run git; on failure print `what` and git's own message unless quiet. */
static bool git(const char *const argv[], int timeout_ms, const char *what, char **out) {
    tam_exec_result_t r = tam_exec(argv, timeout_ms);
    bool ok = r.exit_code == 0;
    if (!ok && what) {
        const char *msg = r.stderr_buf ? r.stderr_buf : "";
        size_t n = strlen(msg);
        while (n && (msg[n - 1] == '\n' || msg[n - 1] == ' ')) n--;
        if (r.exit_code == 127) tb_err("%s: git is not installed", what);
        else if (r.timed_out) tb_err("%s: git did not finish in %d s", what, timeout_ms / 1000);
        else tb_err("%s%s%.*s", what, n ? ": " : "", (int)n, msg);
    }
    if (ok && out) {
        *out = r.stdout_buf;
        r.stdout_buf = NULL;
    }
    tam_exec_result_free(&r);
    return ok;
}

bool tb_git_has(const char *repo, const char *commit) {
    char spec[80];
    snprintf(spec, sizeof spec, "%s^{commit}", commit);
    const char *argv[] = {"git", "-C", repo, "cat-file", "-e", spec, NULL};
    return access(repo, F_OK) == 0 && git(argv, GIT_QUICK_MS, NULL, NULL);
}

char *tb_git_resolve(const char *repo, const char *rev) {
    if (!rev[0] || rev[0] == '-') return NULL;
    char spec[300];
    snprintf(spec, sizeof spec, "%s^{commit}", rev);
    const char *argv[] = {"git", "-C", repo, "rev-parse", "--verify", "--quiet", "--end-of-options", spec, NULL};
    char *out = NULL;
    if (!git(argv, GIT_QUICK_MS, NULL, &out)) return NULL;
    out[strcspn(out, "\n")] = '\0';
    if (!tb_valid_commit(out)) {
        free(out);
        return NULL;
    }
    return out;
}

int tb_git_commit_time(const char *repo, const char *commit, long long *t) {
    const char *argv[] = {"git", "-C", repo, "show", "-s", "--format=%ct", commit, NULL};
    char *out = NULL;
    if (!git(argv, GIT_QUICK_MS, "cannot read the commit time", &out)) return -1;
    char *end;
    *t = strtoll(out, &end, 10);
    bool ok = end != out && *t > 0;
    free(out);
    return ok ? 0 : -1;
}

int tb_git_fetch(const char *mirror, const char *url, const char *commit) {
    if (tb_git_has(mirror, commit)) return 0;
    if (access(mirror, F_OK) != 0) {
        const char *init[] = {"git", "init", "--bare", "--quiet", mirror, NULL};
        if (!git(init, GIT_QUICK_MS, "cannot create a source mirror", NULL)) return -1;
    }
    tb_note("fetching %.12s from %s", commit, url);
    /* Most servers hand out a commit by its id; otherwise take every branch
     * and tag and look again. */
    const char *one[] = {"git", "-C", mirror, "fetch", "--quiet", "--no-tags", "--end-of-options", url, commit, NULL};
    if (git(one, GIT_NETWORK_MS, NULL, NULL) && tb_git_has(mirror, commit)) return 0;
    const char *all[] = {"git", "-C", mirror, "fetch", "--quiet", "--end-of-options", url,
                         "+refs/heads/*:refs/heads/*", "+refs/tags/*:refs/tags/*", NULL};
    char what[600];
    snprintf(what, sizeof what, "cannot fetch from %s", url);
    if (!git(all, GIT_NETWORK_MS, what, NULL)) return -1;
    if (tb_git_has(mirror, commit)) return 0;
    tb_err("%s has no commit %s", url, commit);
    return -1;
}

int tb_git_export(const char *repo, const char *commit, const char *dest) {
    size_t n = strlen(dest) + 8;
    char *tar = malloc(n);
    if (!tar) return -1;
    snprintf(tar, n, "%s.tar", dest);
    const char *archive[] = {"git", "-C", repo, "archive", "--format=tar", "-o", tar, commit, NULL};
    const char *extract[] = {"tar", "-x", "--no-same-owner", "-f", tar, "-C", dest, NULL};
    char what[200];
    snprintf(what, sizeof what, "cannot export commit %.12s", commit);
    bool ok = git(archive, GIT_NETWORK_MS, what, NULL) && git(extract, GIT_NETWORK_MS, what, NULL);
    unlink(tar);
    free(tar);
    return ok ? 0 : -1;
}
