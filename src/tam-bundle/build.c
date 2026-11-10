/* build: make a bundle from recipes and a lock file.
 *
 * Each component's exact commit is exported with `git archive` into a clean
 * directory and built with make, with SOURCE_DATE_EPOCH set to the commit
 * time, LC_ALL=C and TZ=UTC. Finished components are cached under
 * $XDG_CACHE_HOME/tamlinux/tam-bundle/build/, keyed by everything that goes
 * into them: the recipe, the commit, the prefix, the tool versions and the
 * keys of the components built before them. The outputs are then merged into
 * one tree and sealed. */
#define _GNU_SOURCE /* execvpe, asprintf, renameat2 */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define KEY_SHOWN 20 /* hex digits of the key in a cache entry's name */

typedef struct {
    tb_recipe_t           *rc;
    const tb_lock_entry_t *lock;
    char                  *repo;   /* a repository holding the commit */
    char                   key[TAM_SHA256_HEX_LEN + 1];
    char                  *entry;  /* cache entry directory */
    char                  *out;    /* entry/out: the installed files */
    bool                   cached;
} comp_t;

typedef struct {
    const tb_build_opts_t *o;
    tb_recipes_t           r;
    tb_lock_t              lock;
    comp_t                *c;
    size_t                 n;
    char                  *cache;  /* $XDG_CACHE_HOME/tamlinux/tam-bundle */
    char                  *packaging, *packaging_commit;
    char                  *tools[4];
    size_t                 ntools;
} build_t;

static comp_t *comp_named(build_t *b, const char *name) {
    for (size_t i = 0; i < b->n; i++)
        if (strcmp(b->c[i].rc->name, name) == 0) return &b->c[i];
    return NULL;
}

static char *join3(const char *a, const char *b, const char *c) {
    char *ab = tam_path_join(a, b);
    char *abc = ab ? tam_path_join(ab, c) : NULL;
    free(ab);
    return abc;
}

/* ---- Tools ------------------------------------------------------------- */

/* "<name> <version>" from the last word of the first line `argv` prints. */
static char *tool_line(const char *name, const char *const argv[]) {
    tam_exec_result_t r = tam_exec(argv, 10000);
    char *line = NULL;
    if (r.exit_code == 0 && r.stdout_buf) {
        char *nl = strchr(r.stdout_buf, '\n');
        if (nl) *nl = '\0';
        char *last = strrchr(r.stdout_buf, ' ');
        const char *ver = last ? last + 1 : r.stdout_buf;
        if (ver[0] && strlen(ver) < 64) {
            size_t n = strlen(name) + strlen(ver) + 2;
            line = malloc(n);
            if (line) snprintf(line, n, "%s %s", name, ver);
        }
    }
    tam_exec_result_free(&r);
    return line;
}

static void find_tools(build_t *b) {
    const char *gcc[] = {"gcc", "-dumpfullversion", NULL};
    const char *make[] = {"make", "--version", NULL};
    char *t;
    if ((t = tool_line("gcc", gcc))) b->tools[b->ntools++] = t;
    if ((t = tool_line("make", make))) b->tools[b->ntools++] = t;
}

/* ---- Cache keys -------------------------------------------------------- */

static void make_key(build_t *b, comp_t *c) {
    tam_sha256_t h;
    tam_sha256_init(&h);
    tam_str_t s = tam_str_new("tam-bundle build 2\nroot " TB_BUILD_ROOT "\n");
    s = tam_str_cat_printf(s, "component %s\ncommit %s\noutput %s\n", c->rc->name, c->lock->commit,
                           c->rc->build_only ? "build-only" : "bundle");
    if (!c->rc->build_only) s = tam_str_cat_printf(s, "prefix %s\n", b->o->prefix);
    for (size_t i = 0; i < b->ntools; i++) s = tam_str_cat_printf(s, "tool %s\n", b->tools[i]);
    for (size_t i = 0; i < c->rc->ndeps; i++)
        s = tam_str_cat_printf(s, "dep %s %s\n", c->rc->deps[i], comp_named(b, c->rc->deps[i])->key);
    s = tam_str_cat(s, "recipe\n");
    tam_sha256_update(&h, s, tam_str_len(s));
    tam_sha256_update(&h, c->rc->text, c->rc->len);
    tam_str_free(s);
    uint8_t d[TAM_SHA256_LEN];
    tam_sha256_final(&h, d);
    tam_sha256_hex(d, c->key);
}

/* ---- Running make ------------------------------------------------------ */

static void log_argv(int fd, const char *const argv[]) {
    dprintf(fd, "\n$");
    for (size_t i = 0; argv[i]; i++) dprintf(fd, " %s", argv[i]);
    dprintf(fd, "\n");
}

/* Run argv at the fixed build paths `binds`, with exactly `envp`, standard
 * input from /dev/null and both output streams appended to `logfd`. The
 * exit status, or -1. */
static int run_logged(const char *const argv[], char *const envp[], int logfd,
                      const tb_bind_t *binds, size_t nbinds) {
    log_argv(logfd, argv);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) dup2(null, 0);
        dup2(logfd, 1);
        dup2(logfd, 2);
        umask(022);
        const char *why = NULL;
        if (tb_isolate(binds, nbinds, &why) != 0) {
            dprintf(2, "tam-bundle: cannot set up the fixed build paths: %s: %s\n", why, strerror(errno));
            _exit(126);
        }
        execvpe(argv[0], (char *const *)argv, envp);
        dprintf(logfd, "tam-bundle: cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static void show_log_tail(const char *log) {
    size_t len;
    char *text = tb_read_at(AT_FDCWD, log, &len);
    if (!text) return;
    const char *p = text + len;
    for (int lines = 0; p > text && lines <= 20; p--)
        if (p[-1] == '\n') lines++;
    fprintf(stderr, "%s", p == text ? p : p + 1);
    free(text);
}

/* Where a build-only component's private prefix appears during builds. */
static char *fixed_dep(const comp_t *c) {
    char *p = NULL;
    return asprintf(&p, "%s/deps/%s", TB_BUILD_ROOT, c->rc->name) < 0 ? NULL : p;
}

/* The build-only components `c` builds against: those it names in
 * after-build and, through them, theirs. */
static void collect_deps(build_t *b, const comp_t *c, comp_t **list, size_t *n) {
    for (size_t i = 0; i < c->rc->ndeps; i++) {
        comp_t *d = comp_named(b, c->rc->deps[i]);
        bool seen = false;
        for (size_t k = 0; k < *n; k++) seen |= list[k] == d;
        if (seen) continue;
        if (d->rc->build_only && *n < TB_MAX_BINDS - 2) list[(*n)++] = d;
        collect_deps(b, d, list, n);
    }
}

/* "@dep@" in a make variable becomes that component's private prefix. */
static char *expand_var(build_t *b, const char *var) {
    tam_str_t s = tam_str_new(NULL);
    for (const char *p = var; *p;) {
        const char *at = strchr(p, '@');
        if (!at) {
            s = tam_str_cat(s, p);
            break;
        }
        s = tam_str_cat_len(s, p, (size_t)(at - p));
        const char *end = strchr(at + 1, '@'); /* checked when the recipe was read */
        char name[128];
        snprintf(name, sizeof name, "%.*s", (int)(end - at - 1), at + 1);
        char *fixed = fixed_dep(comp_named(b, name));
        s = fixed ? tam_str_cat(s, fixed) : NULL;
        free(fixed);
        if (!s) return NULL;
        p = end + 1;
    }
    char *out = s ? strdup(s) : NULL;
    tam_str_free(s);
    return out;
}

/* Everything installed under DESTDIR must lie inside the prefix; only the
 * directories leading to it may sit outside. */
typedef struct {
    const char *prefix_rel;
    size_t      plen;
    const char *component;
    int         bad;
} outside_t;

static int outside_cb(const tam_walk_entry_t *e, void *user) {
    outside_t *ot = user;
    size_t n = strlen(e->path);
    bool under = n > ot->plen && strncmp(e->path, ot->prefix_rel, ot->plen) == 0 && e->path[ot->plen] == '/';
    bool is_prefix = n == ot->plen && strcmp(e->path, ot->prefix_rel) == 0;
    bool leading = n < ot->plen && strncmp(e->path, ot->prefix_rel, n) == 0 && ot->prefix_rel[n] == '/';
    if (under || is_prefix) return TAM_WALK_SKIP;
    if (leading && S_ISDIR(e->st->st_mode)) return TAM_WALK_CONTINUE;
    tb_err("%s installs /%s, outside the prefix /%s", ot->component, e->path, ot->prefix_rel);
    ot->bad++;
    return TAM_WALK_SKIP;
}

static int build_one(build_t *b, comp_t *c) {
    const tb_recipe_t *rc = c->rc;
    char *buildroot = tam_path_join(b->cache, "build");
    int bfd = open(buildroot, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const char *leaf = strrchr(c->entry, '/') + 1;
    char *src = tam_path_join(c->entry, "src"), *home = tam_path_join(c->entry, "home");
    char *tmp = tam_path_join(c->entry, "tmp"), *dest = tam_path_join(c->entry, "dest");
    char *log = tam_path_join(c->entry, "log"), *done = tam_path_join(c->entry, "done");
    char **vars = calloc(rc->nvars + 1, sizeof *vars);
    char *env[10] = {0}, *prefix_var = NULL, *dest_var = NULL, jobs[32];
    char *fixed_self = NULL, *fixed_deps[TB_MAX_BINDS] = {0};
    comp_t *deps[TB_MAX_BINDS];
    size_t ndeps = 0;
    tb_bind_t binds[TB_MAX_BINDS];
    size_t nbinds = 0;
    /* The work tree as the build sees it. */
    const char *w_src = TB_BUILD_ROOT "/work/src", *w_home = TB_BUILD_ROOT "/work/home";
    const char *w_tmp = TB_BUILD_ROOT "/work/tmp", *w_dest = TB_BUILD_ROOT "/work/dest";
    int logfd = -1, result = TB_FAIL;
    long long when = 0;

    if (bfd < 0 || !src || !home || !tmp || !dest || !log || !done || !vars) {
        tb_err("cannot build %s: %s", rc->name, strerror(errno));
        goto out;
    }
    if (tb_remove_at(bfd, buildroot, leaf) != 0 || mkdir(c->entry, 0755) != 0 || mkdir(src, 0755) != 0 ||
        mkdir(home, 0700) != 0 || mkdir(tmp, 0700) != 0) {
        tb_err("cannot prepare %s: %s", c->entry, strerror(errno));
        goto out;
    }
    tb_note("building %s at %.12s (log: %s)", rc->name, c->lock->commit, log);
    if (tb_git_export(c->repo, c->lock->commit, src) != 0) goto out;
    if (tb_git_commit_time(c->repo, c->lock->commit, &when) != 0) goto out;
    logfd = open(log, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
    if (logfd < 0) {
        tb_err("cannot write %s: %s", log, strerror(errno));
        goto out;
    }
    /* dup2 in the child clears close-on-exec on the copies it makes. */

    /* The fixed paths: the work tree, this component's own prefix if it is
     * build-only, and its build-only dependencies, read-only. */
    binds[nbinds++] = (tb_bind_t){c->entry, TB_BUILD_ROOT "/work", false};
    if (rc->build_only) {
        if ((mkdir(c->out, 0755) != 0 && errno != EEXIST) || !(fixed_self = fixed_dep(c))) goto out;
        binds[nbinds++] = (tb_bind_t){c->out, fixed_self, false};
    }
    collect_deps(b, c, deps, &ndeps);
    for (size_t i = 0; i < ndeps; i++) {
        if (!(fixed_deps[i] = fixed_dep(deps[i]))) goto out;
        binds[nbinds++] = (tb_bind_t){deps[i]->out, fixed_deps[i], true};
    }

    /* A small, fixed environment: only PATH comes from the caller. */
    const char *path = getenv("PATH");
    tam_str_t pc = tam_str_new(NULL);
    for (size_t i = 0; i < ndeps; i++)
        pc = tam_str_cat_printf(pc, "%s%s/lib/pkgconfig:%s/share/pkgconfig", tam_str_len(pc) ? ":" : "",
                                fixed_deps[i], fixed_deps[i]);
    size_t ne = 0;
    if (asprintf(&env[ne++], "PATH=%s", path && path[0] ? path : "/usr/bin:/bin") < 0 ||
        asprintf(&env[ne++], "HOME=%s", w_home) < 0 || asprintf(&env[ne++], "TMPDIR=%s", w_tmp) < 0 ||
        asprintf(&env[ne++], "SOURCE_DATE_EPOCH=%lld", when) < 0 ||
        asprintf(&env[ne++], "PKG_CONFIG_PATH=%s", pc ? pc : "") < 0 || !(env[ne++] = strdup("LC_ALL=C")) ||
        !(env[ne++] = strdup("TZ=UTC"))) {
        tam_str_free(pc);
        goto out;
    }
    tam_str_free(pc);

    for (size_t i = 0; i < rc->nvars; i++)
        if (!(vars[i] = expand_var(b, rc->vars[i]))) goto out;
    if (asprintf(&prefix_var, "PREFIX=%s", rc->build_only ? fixed_self : b->o->prefix) < 0 ||
        asprintf(&dest_var, "DESTDIR=%s", w_dest) < 0)
        goto out;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    snprintf(jobs, sizeof jobs, "-j%ld", cpus > 0 ? cpus : 1);

    for (int pass = 0; pass < 2; pass++) {
        size_t ntargets = pass ? rc->ninstalls : rc->ntargets;
        char **targets = pass ? rc->installs : rc->targets;
        const char **argv = calloc(rc->nvars + ntargets + 9, sizeof *argv);
        if (!argv) goto out;
        size_t a = 0;
        argv[a++] = "make";
        argv[a++] = "-C";
        argv[a++] = w_src;
        if (pass == 0) argv[a++] = jobs;
        if (ntargets) for (size_t i = 0; i < ntargets; i++) argv[a++] = targets[i];
        else if (pass) argv[a++] = "install";
        for (size_t i = 0; i < rc->nvars; i++) argv[a++] = vars[i];
        argv[a++] = prefix_var;
        if (pass == 1 && !rc->build_only) argv[a++] = dest_var;
        argv[a] = NULL;
        int st = run_logged(argv, env, logfd, binds, nbinds);
        free(argv);
        if (st != 0) {
            tb_err("%s: make%s failed (exit %d); the end of %s:", rc->name, pass ? " install" : "", st, log);
            show_log_tail(log);
            goto out;
        }
    }

    if (rc->build_only) {
        if (mkdir(c->out, 0755) != 0 && errno != EEXIST) goto out;
    } else {
        const char *prefix_rel = b->o->prefix + 1;
        outside_t ot = {prefix_rel, strlen(prefix_rel), rc->name, 0};
        struct stat st;
        if (stat(dest, &st) != 0 && mkdir(dest, 0755) != 0) goto out;
        if (tam_fs_walk(dest, TAM_WALK_ONE_DEVICE, outside_cb, &ot) != 0 || ot.bad) goto out;
        char *installed = tam_path_join(dest, prefix_rel);
        bool moved = installed && (rename(installed, c->out) == 0 ||
                                   (errno == ENOENT && mkdir(c->out, 0755) == 0));
        free(installed);
        if (!moved) {
            tb_err("cannot collect %s's files: %s", rc->name, strerror(errno));
            goto out;
        }
    }
    /* Keep the installed files and the log; drop the work trees. */
    int efd = open(c->entry, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (efd < 0) goto out;
    tb_remove_at(efd, c->entry, "src");
    tb_remove_at(efd, c->entry, "dest");
    tb_remove_at(efd, c->entry, "home");
    tb_remove_at(efd, c->entry, "tmp");
    close(efd);
    /* The stamp goes last: an entry without it is rebuilt. */
    if (!tam_fs_write_atomic(done, c->key, strlen(c->key), 0644)) goto out;
    result = TB_OK;
out:
    if (logfd >= 0) close(logfd);
    if (bfd >= 0) close(bfd);
    for (size_t i = 0; vars && i < rc->nvars; i++) free(vars[i]);
    for (size_t i = 0; i < sizeof env / sizeof *env; i++) free(env[i]);
    free(vars); free(prefix_var); free(dest_var); free(fixed_self);
    for (size_t i = 0; i < ndeps; i++) free(fixed_deps[i]);
    free(buildroot); free(src); free(home); free(tmp); free(dest); free(log); free(done);
    return result;
}

/* ---- Merging the outputs ----------------------------------------------- */

typedef struct {
    char    *path;
    unsigned mode;
    size_t   owner; /* component index */
} placed_t;

typedef struct {
    build_t  *b;
    int       dstfd;
    size_t    comp;
    placed_t *v;
    size_t    n, cap;
    int       failed;
} merge_t;

static const char *owner_of(merge_t *m, const char *path) {
    for (size_t i = 0; i < m->n; i++)
        if (strcmp(m->v[i].path, path) == 0) return m->b->c[m->v[i].owner].rc->name;
    return "an earlier component";
}

static int remember(merge_t *m, const char *path, unsigned mode) {
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 512;
        placed_t *grown = realloc(m->v, cap * sizeof *grown);
        if (!grown) return -1;
        m->v = grown;
        m->cap = cap;
    }
    m->v[m->n].path = strdup(path);
    m->v[m->n].mode = mode;
    m->v[m->n].owner = m->comp;
    return m->v[m->n++].path ? 0 : -1;
}

static int conflict(merge_t *m, const char *path) {
    tb_err("%s and %s both install %s", owner_of(m, path), m->b->c[m->comp].rc->name, path);
    m->failed = 1;
    return -2;
}

static int merge_cb(const tam_walk_entry_t *e, void *user) {
    merge_t *m = user;
    mode_t mode = e->st->st_mode;
    struct stat have;
    if (S_ISDIR(mode)) {
        if (mkdirat(m->dstfd, e->path, 0700) == 0) return remember(m, e->path, mode & 07777) ? -2 : 0;
        if (errno != EEXIST || fstatat(m->dstfd, e->path, &have, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(have.st_mode))
            return conflict(m, e->path);
        for (size_t i = 0; i < m->n; i++)
            if (strcmp(m->v[i].path, e->path) == 0 && m->v[i].mode != (mode & 07777)) {
                tb_err("%s makes directory %s mode %04o, %s mode %04o", owner_of(m, e->path), e->path,
                       m->v[i].mode, m->b->c[m->comp].rc->name, (unsigned)(mode & 07777));
                m->failed = 1;
                return -2;
            }
        return TAM_WALK_CONTINUE;
    }
    if (S_ISLNK(mode)) {
        char target[4097];
        ssize_t n = readlinkat(e->dirfd, e->name, target, sizeof target - 1);
        if (n < 0) return -2;
        target[n] = '\0';
        if (symlinkat(target, m->dstfd, e->path) != 0) return errno == EEXIST ? conflict(m, e->path) : -2;
        return remember(m, e->path, 0) ? -2 : 0;
    }
    if (!S_ISREG(mode)) {
        tb_err("%s installs %s, which is not a file, directory or link", m->b->c[m->comp].rc->name, e->path);
        m->failed = 1;
        return -2;
    }
    int in = openat(e->dirfd, e->name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return -2;
    int outfd = openat(m->dstfd, e->path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (outfd < 0) {
        close(in);
        return errno == EEXIST ? conflict(m, e->path) : -2;
    }
    /* Setuid and similar bits are copied so that sealing refuses them. */
    int r = tam_fs_clone_or_copy(in, outfd, NULL) == 0 && fchmod(outfd, mode & 07777) == 0 ? 0 : -2;
    close(in);
    close(outfd);
    return r == 0 && remember(m, e->path, mode & 07777) == 0 ? 0 : -2;
}

static int cmp_placed_desc(const void *a, const void *b) {
    return strcmp(((const placed_t *)b)->path, ((const placed_t *)a)->path);
}

static int merge(build_t *b, const char *tree) {
    merge_t m = {.b = b, .dstfd = open(tree, O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    int result = m.dstfd >= 0 ? 0 : -1;
    for (size_t i = 0; i < b->n && result == 0; i++) {
        if (b->c[i].rc->build_only) continue;
        m.comp = i;
        if (tam_fs_walk(b->c[i].out, TAM_WALK_ONE_DEVICE, merge_cb, &m) != 0) {
            if (!m.failed) tb_err("cannot merge %s's files: %s", b->c[i].rc->name, strerror(errno));
            result = -1;
        }
    }
    /* Directory modes last, deepest first, so read-only ones can be filled. */
    if (m.n > 1) qsort(m.v, m.n, sizeof *m.v, cmp_placed_desc);
    for (size_t i = 0; i < m.n; i++) {
        struct stat st;
        if (result == 0 && fstatat(m.dstfd, m.v[i].path, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode))
            fchmodat(m.dstfd, m.v[i].path, m.v[i].mode, 0);
        free(m.v[i].path);
    }
    free(m.v);
    if (m.dstfd >= 0) close(m.dstfd);
    return result;
}

/* ---- tb_build ---------------------------------------------------------- */

/* Packaging inputs in a Git work tree must match one commit. The commit
 * supplies extra files too, so dirty or untracked files cannot enter a
 * bundle whose identity claims otherwise. Standalone scratch recipes are
 * still useful, but cannot use `file` without a packaging commit. */
static int packaging_input(build_t *b, const char *path) {
    char *abs = realpath(path, NULL);
    size_t rootlen = strlen(b->packaging);
    if (!abs || strncmp(abs, b->packaging, rootlen) != 0 || abs[rootlen] != '/') {
        tb_err("%s must be inside the packaging repository %s", path, b->packaging);
        free(abs);
        return -1;
    }
    size_t committed_len = 0, working_len = 0;
    char *committed = tb_git_file(b->packaging, b->packaging_commit, abs + rootlen + 1, &committed_len);
    char *working = tb_read_at(AT_FDCWD, abs, &working_len);
    bool same = committed && working && committed_len == working_len && memcmp(committed, working, working_len) == 0;
    free(abs); free(committed); free(working);
    if (!same) tb_err("%s must be committed at packaging %.12s before building", path, b->packaging_commit);
    return same ? 0 : -1;
}

static int packaging_prepare(build_t *b) {
    b->packaging = tb_git_root(b->o->recipes);
    if (!b->packaging) {
        if (!b->r.nfiles) return 0;
        tb_err("bundle file entries need recipes and lock in a committed packaging repository");
        return -1;
    }
    b->packaging_commit = tb_git_resolve(b->packaging, "HEAD");
    if (!b->packaging_commit || packaging_input(b, b->o->lock) != 0) return -1;
    char *path = tam_path_join(b->o->recipes, "bundle");
    int result = path ? packaging_input(b, path) : -1;
    free(path);
    for (size_t i = 0; i < b->r.n && result == 0; i++) {
        char *leaf = NULL;
        if (asprintf(&leaf, "%s.recipe", b->r.v[i].name) < 0) return -1;
        path = tam_path_join(b->o->recipes, leaf);
        free(leaf);
        result = path ? packaging_input(b, path) : -1;
        free(path);
        if (strcmp(b->r.v[i].name, "packaging") == 0) {
            tb_err("packaging is reserved for the packaging source commit");
            return -1;
        }
    }
    return result;
}

/* Descend destination parents without following links, including links
 * installed by components. Extra files never replace component output. */
static int packaging_files(build_t *b, const char *tree) {
    for (size_t i = 0; i < b->r.nfiles; i++) {
        tb_recipe_file_t *file = &b->r.files[i];
        size_t len = 0;
        char *data = tb_git_file(b->packaging, b->packaging_commit, file->source, &len);
        if (!data) return -1;
        char *path = strdup(file->path), *save = NULL;
        int fd = open(tree, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int result = path && fd >= 0 ? 0 : -1;
        char *part = path ? strtok_r(path, "/", &save) : NULL;
        while (result == 0 && part) {
            char *next = strtok_r(NULL, "/", &save);
            if (!next) {
                int out = openat(fd, part, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
                if (out < 0) result = -1;
                size_t off = 0;
                while (out >= 0 && off < len) {
                    ssize_t n = write(out, data + off, len - off);
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) { result = -1; break; }
                    off += (size_t)n;
                }
                if (out >= 0 && close(out) != 0) result = -1;
            } else {
                if (mkdirat(fd, part, 0755) != 0 && errno != EEXIST) { result = -1; break; }
                int child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (child < 0) { result = -1; break; }
                close(fd);
                fd = child;
            }
            part = next;
        }
        if (result) tb_err("cannot add packaging file %s as %s: %s", file->source, file->path, strerror(errno));
        if (fd >= 0) close(fd);
        free(path); free(data);
        if (result) return -1;
    }
    return 0;
}

static int check_prefix(const char *p) {
    if (!p || p[0] != '/' || !tam_path_is_safe_rel(p + 1)) {
        tb_err("the prefix must be an absolute path without . or .. components: %s", p ? p : "(none)");
        return -1;
    }
    return 0;
}

int tb_build(const tb_build_opts_t *o) {
    if (!o->version || !tb_valid_name(o->version)) {
        tb_err("build needs --version with a valid version");
        return TB_USAGE;
    }
    if (check_prefix(o->prefix) != 0) return TB_USAGE;
    struct stat st;
    if (lstat(o->output, &st) == 0) {
        tb_err("%s already exists; name a new directory with -o", o->output);
        return TB_FAIL;
    }
    umask(022);

    build_t b = {.o = o};
    int result = TB_FAIL, lockfd = -1, pfd = -1;
    char *partial = NULL, *parent = NULL;
    const char *leaf = NULL;
    if (tb_recipes_read(o->recipes, &b.r) != 0 || tb_lock_read(o->lock, &b.lock) != 0) goto out;
    if (packaging_prepare(&b) != 0) goto out;

    b.c = calloc(b.r.n, sizeof *b.c);
    if (!b.c) goto out;
    for (size_t i = 0; i < b.r.n; i++) {
        comp_t *c = &b.c[b.n++];
        c->rc = &b.r.v[i];
        if (!(c->lock = tb_lock_find(&b.lock, c->rc->name))) {
            tb_err("%s has no line for %s; add it with `tam-bundle lock %s <commit> --url <url>`", o->lock,
                   c->rc->name, c->rc->name);
            goto out;
        }
    }
    for (size_t i = 0; i < b.lock.n; i++)
        if (!comp_named(&b, b.lock.v[i].name)) {
            tb_err("%s names %s, which has no recipe in %s", o->lock, b.lock.v[i].name, o->recipes);
            goto out;
        }

    b.cache = tam_xdg_path(TAM_XDG_CACHE, "tam-bundle", NULL, true);
    char *gitdir = b.cache ? tam_path_join(b.cache, "git") : NULL;
    char *builddir = b.cache ? tam_path_join(b.cache, "build") : NULL;
    bool dirs = gitdir && builddir && tam_fs_mkdir_p(gitdir, 0700) && tam_fs_mkdir_p(builddir, 0700);
    free(gitdir);
    free(builddir);
    if (!dirs) {
        tb_err("cannot make the build cache under $XDG_CACHE_HOME: %s", strerror(errno));
        goto out;
    }
    char *lockpath = tam_path_join(b.cache, "lock");
    lockfd = lockpath ? tam_fs_lock(lockpath, 0) : -1;
    free(lockpath);
    if (lockfd < 0) {
        tb_err("another tam-bundle build is using %s; try again when it finishes", b.cache);
        goto out;
    }
    find_tools(&b);

    size_t built = 0;
    long long newest = 0; /* the newest locked commit: the manual index's time */
    if (b.r.index_man && b.packaging_commit &&
        tb_git_commit_time(b.packaging, b.packaging_commit, &newest) != 0) goto out;
    for (size_t i = 0; i < b.n; i++) {
        comp_t *c = &b.c[i];
        const char *name = c->rc->name, *commit = c->lock->commit;
        char *local = o->repos ? tam_path_join(o->repos, name) : NULL;
        if (local && tb_git_has(local, commit)) {
            c->repo = local;
        } else {
            free(local);
            char git_leaf[200];
            snprintf(git_leaf, sizeof git_leaf, "git/%s.git", name);
            c->repo = tam_path_join(b.cache, git_leaf);
            if (!c->repo || tb_git_fetch(c->repo, c->lock->url, commit) != 0) goto out;
        }
        long long when = 0;
        if (b.r.index_man && tb_git_commit_time(c->repo, commit, &when) != 0) goto out;
        if (when > newest) newest = when;
        make_key(&b, c);
        char entry_leaf[200];
        snprintf(entry_leaf, sizeof entry_leaf, "%s-%.*s", name, KEY_SHOWN, c->key);
        c->entry = join3(b.cache, "build", entry_leaf);
        c->out = c->entry ? tam_path_join(c->entry, "out") : NULL;
        char *done = c->entry ? tam_path_join(c->entry, "done") : NULL;
        if (!c->out || !done) goto out;
        size_t dlen;
        char *stamp = tb_read_at(AT_FDCWD, done, &dlen);
        free(done);
        c->cached = stamp && strcmp(stamp, c->key) == 0;
        free(stamp);
        if (c->cached) {
            tb_note("%s at %.12s: from the build cache", name, commit);
            continue;
        }
        if (build_one(&b, c) != TB_OK) goto out;
        built++;
    }

    /* Merge into <output>.partial, seal, then give it its name. */
    if (asprintf(&partial, "%s.partial", o->output) < 0) goto out;
    parent = strdup(partial);
    char *slash = parent ? strrchr(parent, '/') : NULL;
    if (!parent) goto out;
    if (slash) {
        *slash = '\0';
        leaf = slash + 1;
        if (!parent[0]) strcpy(parent, "/");
    } else {
        leaf = partial;
        strcpy(parent, ".");
    }
    pfd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (pfd < 0 || tb_remove_at(pfd, parent, leaf) != 0 || mkdirat(pfd, leaf, 0755) != 0) {
        tb_err("cannot make %s: %s", partial, strerror(errno));
        goto out;
    }
    if (merge(&b, partial) != 0) goto out;
    if (packaging_files(&b, partial) != 0) goto out;
    /* After the cache keys, so a man-db update rebuilds no component. */
    if (b.r.index_man) {
        char *tool = tb_man_index_tool();
        if (!tool) {
            tb_err("the bundle file asks for a manual index; install man-db, which provides mandb");
            goto out;
        }
        b.tools[b.ntools++] = tool;
        if (tb_man_index(partial, newest) != 0) goto out;
    }

    size_t nsources = b.n + (b.packaging_commit ? 1 : 0);
    const char **sources = calloc(nsources, sizeof *sources);
    char **source_text = calloc(nsources, sizeof *source_text);
    bool ok = sources && source_text;
    for (size_t i = 0; ok && i < b.n; i++) {
        ok = asprintf(&source_text[i], "%s %s", b.c[i].rc->name, b.c[i].lock->commit) >= 0;
        if (ok) sources[i] = source_text[i];
    }
    if (ok && b.packaging_commit) {
        ok = asprintf(&source_text[b.n], "packaging %s", b.packaging_commit) >= 0;
        if (ok) sources[b.n] = source_text[b.n];
    }
    tb_seal_t so = {
        .name = b.r.name, .version = o->version, .host = o->host ? o->host : b.r.host, .built = o->built,
        .sources = sources, .nsources = nsources,
        .tools = (const char *const *)b.tools, .ntools = b.ntools,
        .needs = (const char *const *)b.r.needs, .nneeds = b.r.nneeds,
        .after = (const char *const *)b.r.after, .nafter = b.r.nafter,
    };
    char id[256] = "";
    bool sealed = ok && tb_seal(partial, &so, id) == TB_OK;
    for (size_t i = 0; source_text && i < nsources; i++) free(source_text[i]);
    free(source_text);
    free(sources);
    if (!sealed) goto out;
    if (renameat2(AT_FDCWD, partial, AT_FDCWD, o->output, RENAME_NOREPLACE) != 0 || !tam_fs_sync_dirat(pfd, ".")) {
        tb_err("cannot rename %s to %s: %s", partial, o->output, strerror(errno));
        goto out;
    }
    tb_note("built %s in %s: %zu components, %zu built, %zu from the cache", id, o->output, b.n, built, b.n - built);
    result = TB_OK;
out:
    if (result != TB_OK && pfd >= 0 && leaf) tb_remove_at(pfd, parent, leaf);
    if (pfd >= 0) close(pfd);
    if (lockfd >= 0) tam_fs_unlock(lockfd);
    for (size_t i = 0; i < b.n; i++) {
        free(b.c[i].repo);
        free(b.c[i].entry);
        free(b.c[i].out);
    }
    free(b.c);
    for (size_t i = 0; i < b.ntools; i++) free(b.tools[i]);
    free(b.cache);
    free(b.packaging); free(b.packaging_commit);
    free(partial);
    free(parent);
    tb_lock_free(&b.lock);
    tb_recipes_free(&b.r);
    return result;
}
