/* The manual index, built into the bundle. `build` runs man-db's mandb on
 * the merged tree's share/man before sealing, so index.db is hashed, sealed
 * and rolled back with the pages it describes, and a switch has no index to
 * rebuild. A tree outside man-db's system paths keeps its index inside the
 * hierarchy, which is where man, whatis and apropos look for it.
 *
 * The index stores each page's modification time, so every entry under
 * share/man is set to one time from the lock first: the same commits then
 * give the same index bytes, whatever the build directory, file system or
 * directory order.
 *
 * mandb also makes an empty cat<section> directory beside each section for
 * formatted pages; those are removed again, and anything else new is
 * refused. */
#include "tb.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* man-db's database file names, for every database kind it can use. */
static bool is_index_name(const char *name) {
    return strcmp(name, "index.db") == 0 || strcmp(name, "index.bt") == 0 ||
           strcmp(name, "index.dir") == 0 || strcmp(name, "index.pag") == 0;
}

typedef struct {
    struct timespec times[2];
    const char     *found; /* an index a component installed */
    char            path[512];
} prep_t;

static int prep_cb(const tam_walk_entry_t *e, void *user) {
    prep_t *p = user;
    if (!S_ISDIR(e->st->st_mode) && is_index_name(e->name)) {
        snprintf(p->path, sizeof p->path, "%s", e->path);
        p->found = p->path;
        return -1;
    }
    return utimensat(e->dirfd, e->name, p->times, AT_SYMLINK_NOFOLLOW) == 0 ? TAM_WALK_CONTINUE : -1;
}

/* Every path under the hierarchy, to tell what mandb added. */
typedef struct {
    char  **v;
    size_t  n;
} paths_t;

static int list_cb(const tam_walk_entry_t *e, void *user) {
    paths_t *ps = user;
    char **grown = realloc(ps->v, (ps->n + 1) * sizeof *ps->v);
    if (!grown) return -1;
    ps->v = grown;
    return (ps->v[ps->n++] = strdup(e->path)) ? TAM_WALK_CONTINUE : -1;
}

static bool listed(const paths_t *ps, const char *path) {
    for (size_t i = 0; i < ps->n; i++)
        if (strcmp(ps->v[i], path) == 0) return true;
    return false;
}

typedef struct {
    const paths_t *before;
    paths_t        empty_cats; /* new, empty cat<section> directories */
    char           odd[512];
} added_t;

static int added_cb(const tam_walk_entry_t *e, void *user) {
    added_t *a = user;
    if (listed(a->before, e->path)) return TAM_WALK_CONTINUE;
    if (S_ISREG(e->st->st_mode) && strcmp(e->name, "index.db") == 0) return TAM_WALK_CONTINUE;
    if (S_ISDIR(e->st->st_mode) && strncmp(e->name, "cat", 3) == 0 && e->st->st_nlink <= 2) {
        int fd = openat(e->dirfd, e->name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
        bool empty = d != NULL;
        struct dirent *de;
        while (d && (de = readdir(d)) != NULL)
            if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) empty = false;
        if (d) closedir(d);
        else if (fd >= 0) close(fd);
        if (empty) return list_cb(e, &a->empty_cats) == TAM_WALK_CONTINUE ? TAM_WALK_SKIP : -1;
    }
    snprintf(a->odd, sizeof a->odd, "%s", e->path);
    return -1;
}

static void paths_free(paths_t *ps) {
    for (size_t i = 0; i < ps->n; i++) free(ps->v[i]);
    free(ps->v);
}

char *tb_man_index_tool(void) {
    const char *argv[] = {"mandb", "--version", NULL};
    tam_exec_result_t r = tam_exec(argv, 10000);
    char *line = NULL;
    if (r.exit_code == 0 && r.stdout_buf) {
        char *nl = strchr(r.stdout_buf, '\n');
        if (nl) *nl = '\0';
        const char *last = strrchr(r.stdout_buf, ' ');
        const char *ver = last ? last + 1 : r.stdout_buf;
        if (ver[0] && strlen(ver) < 64 && asprintf(&line, "man-db %s", ver) < 0) line = NULL;
    }
    tam_exec_result_free(&r);
    return line;
}

int tb_man_index(const char *tree, long long epoch) {
    char *man = tam_path_join(tree, "share/man");
    if (!man) return -1;
    int result = -1;
    struct stat st;
    if (lstat(man, &st) != 0 || !S_ISDIR(st.st_mode)) {
        tb_err("the bundle file asks for a manual index (`index man`), but the bundle has no share/man directory");
        goto out;
    }
    prep_t p = {.times = {{.tv_sec = epoch}, {.tv_sec = epoch}}};
    int w = tam_fs_walk(man, TAM_WALK_ONE_DEVICE, prep_cb, &p);
    if (p.found) {
        tb_err("share/man/%s: a component installed a manual index; `build` makes it (`index man`)", p.found);
        goto out;
    }
    if (w != 0 || utimensat(AT_FDCWD, man, p.times, 0) != 0) {
        tb_err("cannot set the manual pages' times in %s: %s", man, strerror(errno));
        goto out;
    }

    paths_t before = {0};
    if (tam_fs_walk(man, TAM_WALK_ONE_DEVICE, list_cb, &before) != 0) {
        tb_err("cannot list %s: %s", man, strerror(errno));
        paths_free(&before);
        goto out;
    }

    /* A fixed environment: no ~/.manpath (HOME names nothing, -C /dev/null)
     * and the C locale. The hierarchy is an argument, not MANPATH, which
     * some builds of mandb do not read. */
    char *path_var = NULL, *sde = NULL;
    const char *path = getenv("PATH");
    if (asprintf(&path_var, "PATH=%s", path ? path : "/usr/bin:/bin") < 0) path_var = NULL;
    if (asprintf(&sde, "SOURCE_DATE_EPOCH=%lld", epoch) < 0) sde = NULL;
    if (path_var && sde) {
        const char *argv[] = {"env", "-i", path_var, sde, "HOME=/nonexistent", "LC_ALL=C", "TZ=UTC",
                              "mandb", "--quiet", "--create", "--config-file=/dev/null", "--", man, NULL};
        tam_exec_result_t r = tam_exec(argv, 600000);
        if (r.exit_code == 127) {
            tb_err("the bundle file asks for a manual index; install man-db, which provides mandb");
        } else if (r.exit_code != 0) {
            tb_err("mandb failed on %s (exit %d)%s%s", man, r.exit_code, r.stderr_len ? ":\n" : "",
                   r.stderr_len ? r.stderr_buf : "");
        } else {
            char *db = tam_path_join(man, "index.db");
            added_t a = {.before = &before};
            if (!db || access(db, F_OK) != 0) {
                tb_err("mandb made no index.db in %s", man);
            } else if (tam_fs_walk(man, TAM_WALK_ONE_DEVICE, added_cb, &a) != 0) {
                if (a.odd[0]) tb_err("mandb added share/man/%s, which a bundle cannot carry", a.odd);
                else tb_err("cannot list %s: %s", man, strerror(errno));
            } else {
                result = 0;
                for (size_t i = a.empty_cats.n; i-- > 0;) {
                    char *cat = tam_path_join(man, a.empty_cats.v[i]);
                    if (!cat || rmdir(cat) != 0) {
                        tb_err("cannot remove %s/%s: %s", man, a.empty_cats.v[i], strerror(errno));
                        result = -1;
                    }
                    free(cat);
                }
            }
            paths_free(&a.empty_cats);
            free(db);
        }
        tam_exec_result_free(&r);
    }
    free(path_var);
    free(sde);
    paths_free(&before);
out:
    free(man);
    return result;
}
