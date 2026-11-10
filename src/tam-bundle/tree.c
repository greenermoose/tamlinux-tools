/* Trees: build a MANIFEST from a tree, check a tree against one, and the
 * stat cache that lets a check skip hashing unchanged files. */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool is_root_record(const tam_walk_entry_t *e) {
    return e->depth == 1 && (strcmp(e->name, "BUNDLE") == 0 || strcmp(e->name, "MANIFEST") == 0);
}

static int hash_at(int dirfd, const char *name, char out[TB_HASH_LEN + 1]) {
    int fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    uint8_t d[TAM_SHA256_LEN];
    int r = tam_sha256_fd(fd, d);
    int saved = errno;
    close(fd);
    errno = saved;
    if (r != 0) return -1;
    memcpy(out, "sha256:", 7);
    tam_sha256_hex(d, out + 7);
    return 0;
}

static char *link_at(int dirfd, const char *name) {
    char buf[4097];
    ssize_t n = readlinkat(dirfd, name, buf, sizeof buf - 1);
    if (n < 0) return NULL;
    buf[n] = '\0';
    return strdup(buf);
}

/* ---- Building a manifest ----------------------------------------------- */

typedef struct {
    tb_manifest_t *m;
    int            failed;
} build_t;

static int build_cb(const tam_walk_entry_t *e, void *user) {
    build_t *bt = user;
    tb_manifest_t *m = bt->m;
    if (is_root_record(e)) return TAM_WALK_CONTINUE;
    if (!tam_path_is_safe_rel(e->path) || strpbrk(e->path, "\t")) {
        tb_err("unsafe path in tree: %s", e->path);
        bt->failed = 1;
        return -2;
    }
    tb_entry_t ent = {0};
    mode_t mode = e->st->st_mode;
    char hash[TB_HASH_LEN + 1];
    if (S_ISDIR(mode)) {
        ent.type = 'd';
        ent.value = strdup("-");
    } else if (S_ISREG(mode)) {
        ent.type = 'f';
        if (hash_at(e->dirfd, e->name, hash) != 0) {
            tb_err("cannot read %s: %s", e->path, strerror(errno));
            bt->failed = 1;
            return -2;
        }
        ent.value = strdup(hash);
    } else if (S_ISLNK(mode)) {
        ent.type = 'l';
        ent.value = link_at(e->dirfd, e->name);
        if (!ent.value || strpbrk(ent.value, " \t")) {
            tb_err("link %s: unreadable, or its target contains a space", e->path);
            free(ent.value);
            bt->failed = 1;
            return -2;
        }
    } else {
        tb_err("not a file, directory or link: %s", e->path);
        bt->failed = 1;
        return -2;
    }
    if (ent.type != 'l') {
        if (mode & 07000) {
            tb_err("setuid, setgid or sticky bit on %s", e->path);
            free(ent.value);
            bt->failed = 1;
            return -2;
        }
        ent.mode = mode & 0777;
    }
    ent.path = strdup(e->path);
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 256;
        tb_entry_t *grown = realloc(m->v, cap * sizeof *grown);
        if (!grown) return -2;
        m->v = grown;
        m->cap = cap;
    }
    m->v[m->n++] = ent;
    return TAM_WALK_CONTINUE;
}

static int cmp_entries(const void *a, const void *b) {
    return strcmp(((const tb_entry_t *)a)->path, ((const tb_entry_t *)b)->path);
}

int tb_manifest_build(const char *root, tb_manifest_t *m) {
    memset(m, 0, sizeof *m);
    build_t bt = {m, 0};
    int r = tam_fs_walk(root, TAM_WALK_ONE_DEVICE, build_cb, &bt);
    if (r != 0) {
        if (!bt.failed) tb_err("cannot walk %s: %s", root, strerror(errno));
        tb_manifest_free(m);
        return -1;
    }
    if (m->n > 1) qsort(m->v, m->n, sizeof *m->v, cmp_entries);
    tam_str_t text = tam_str_new("format 1\n");
    for (size_t i = 0; text && i < m->n; i++) {
        tb_entry_t *e = &m->v[i];
        char type[2] = {e->type, 0}, mode[8];
        if (e->type == 'l') strcpy(mode, "-");
        else snprintf(mode, sizeof mode, "%04o", e->mode);
        const char *f[] = {type, mode, e->value, e->path};
        tam_str_t next = tam_rec_cat(text, f, 4);
        if (!next) {
            tb_err("cannot record path: %s", e->path);
            tam_str_free(text);
            text = NULL;
        } else {
            text = next;
        }
    }
    if (!text) {
        tb_manifest_free(m);
        return -1;
    }
    /* Parse what was written, so a built manifest obeys every rule a read
     * one does. */
    tb_manifest_t check;
    int ok = tb_manifest_parse(text, tam_str_len(text), "built MANIFEST", &check);
    if (ok == 0) {
        check.text = strdup(text);
        check.len = tam_str_len(text);
    }
    tam_str_free(text);
    tb_manifest_free(m);
    if (ok != 0) return -1;
    *m = check;
    return 0;
}

/* ---- Stat cache -------------------------------------------------------- */

void tb_stats_free(tb_stats_t *s) {
    for (size_t i = 0; i < s->n; i++) free(s->v[i].path);
    free(s->v);
    memset(s, 0, sizeof *s);
}

static int cmp_stat_path(const void *key, const void *elem) {
    return strcmp((const char *)key, ((const tb_stat_t *)elem)->path);
}

static const tb_stat_t *stats_find(const tb_stats_t *s, const char *path) {
    if (!s || !s->n) return NULL;
    return bsearch(path, s->v, s->n, sizeof *s->v, cmp_stat_path);
}

static bool stat_matches(const tb_stat_t *c, const struct stat *st) {
    return c->size == (long long)st->st_size && c->ino == (long long)st->st_ino &&
           c->mtime_s == (long long)st->st_mtim.tv_sec && c->mtime_ns == (long long)st->st_mtim.tv_nsec &&
           c->ctime_s == (long long)st->st_ctim.tv_sec && c->ctime_ns == (long long)st->st_ctim.tv_nsec;
}

static bool push_stat(tb_stats_t *s, const char *path, const struct stat *st) {
    if (s->n == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 256;
        tb_stat_t *grown = realloc(s->v, cap * sizeof *grown);
        if (!grown) return false;
        s->v = grown;
        s->cap = cap;
    }
    tb_stat_t *c = &s->v[s->n++];
    c->path = strdup(path);
    c->size = st->st_size;
    c->ino = (long long)st->st_ino;
    c->mtime_s = st->st_mtim.tv_sec;
    c->mtime_ns = st->st_mtim.tv_nsec;
    c->ctime_s = st->st_ctim.tv_sec;
    c->ctime_ns = st->st_ctim.tv_nsec;
    return c->path != NULL;
}

int tb_stats_build(const char *root, const tb_manifest_t *m, tb_stats_t *s) {
    memset(s, 0, sizeof *s);
    int rfd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (rfd < 0) return -1;
    for (size_t i = 0; i < m->n; i++) {
        if (m->v[i].type != 'f') continue;
        struct stat st;
        if (fstatat(rfd, m->v[i].path, &st, AT_SYMLINK_NOFOLLOW) != 0 || !push_stat(s, m->v[i].path, &st)) {
            close(rfd);
            tb_stats_free(s);
            return -1;
        }
    }
    close(rfd);
    return 0;
}

int tb_stats_write(const char *path, const tb_stats_t *s) {
    tam_str_t text = tam_str_new("format 1\n");
    for (size_t i = 0; text && i < s->n; i++) {
        const tb_stat_t *c = &s->v[i];
        char size[32], ino[32], mt[48], ct[48];
        snprintf(size, sizeof size, "%lld", c->size);
        snprintf(ino, sizeof ino, "%lld", c->ino);
        snprintf(mt, sizeof mt, "%lld.%09lld", c->mtime_s, c->mtime_ns);
        snprintf(ct, sizeof ct, "%lld.%09lld", c->ctime_s, c->ctime_ns);
        const char *f[] = {"s", size, ino, mt, ct, c->path};
        tam_str_t next = tam_rec_cat(text, f, 6);
        if (!next) tam_str_free(text);
        text = next;
    }
    if (!text) return -1;
    bool ok = tam_fs_write_atomic(path, text, tam_str_len(text), 0644);
    tam_str_free(text);
    return ok ? 0 : -1;
}

static bool parse_time(const char *s, long long *sec, long long *nsec) {
    char *end;
    *sec = strtoll(s, &end, 10);
    if (*end != '.') return false;
    *nsec = strtoll(end + 1, &end, 10);
    return *end == '\0';
}

static int stats_line(void *user, size_t line, int format, char **f, size_t n) {
    tb_stats_t *s = user;
    (void)line; (void)format;
    if (n != 6 || strcmp(f[0], "s") != 0) return 1;
    struct stat st = {0};
    long long sec, nsec;
    st.st_size = strtoll(f[1], NULL, 10);
    st.st_ino = (ino_t)strtoll(f[2], NULL, 10);
    if (!parse_time(f[3], &sec, &nsec)) return 1;
    st.st_mtim.tv_sec = sec;
    st.st_mtim.tv_nsec = nsec;
    if (!parse_time(f[4], &sec, &nsec)) return 1;
    st.st_ctim.tv_sec = sec;
    st.st_ctim.tv_nsec = nsec;
    if (s->n && strcmp(s->v[s->n - 1].path, f[5]) >= 0) return 1;
    return push_stat(s, f[5], &st) ? 0 : 1;
}

int tb_stats_read(const char *path, tb_stats_t *s) {
    memset(s, 0, sizeof *s);
    size_t len;
    char *text = tb_read_at(AT_FDCWD, path, &len);
    if (!text) return errno == ENOENT ? 0 : -1;
    int r = tam_rec_parse(text, len, 6, 1, stats_line, s, NULL);
    free(text);
    if (r != 0) {
        tb_stats_free(s); /* a damaged cache only means hashing everything */
        return 0;
    }
    return 0;
}

/* ---- Checking a tree --------------------------------------------------- */

typedef struct {
    const tb_manifest_t *m;
    const tb_stats_t    *stats;
    bool                 full;
    bool                *seen;
    tb_check_t          *out;
    int                  error;
} check_t;

static void note_path(tb_check_t *c, const char *path) {
    char **grown = realloc(c->paths, (c->npaths + 1) * sizeof *grown);
    if (!grown) return;
    c->paths = grown;
    c->paths[c->npaths++] = strdup(path);
}

static void problem(check_t *ck, const char *kind, const char *path, const char *detail) {
    if (!ck->out->quiet) printf("%s: %s%s%s\n", kind, path, detail ? " " : "", detail ? detail : "");
}

static int check_cb(const tam_walk_entry_t *e, void *user) {
    check_t *ck = user;
    if (is_root_record(e)) return TAM_WALK_CONTINUE;
    const tb_entry_t *me = tb_manifest_find(ck->m, e->path);
    mode_t mode = e->st->st_mode;
    if (!me) {
        ck->out->extra++;
        problem(ck, "extra", e->path, S_ISDIR(mode) ? "(directory)" : NULL);
        if (S_ISREG(mode)) note_path(ck->out, e->path);
        return S_ISDIR(mode) ? TAM_WALK_SKIP : TAM_WALK_CONTINUE;
    }
    ck->seen[me - ck->m->v] = true;
    char type = S_ISDIR(mode) ? 'd' : S_ISREG(mode) ? 'f' : S_ISLNK(mode) ? 'l' : '?';
    if (type != me->type) {
        ck->out->edited++;
        problem(ck, "edited", e->path, "(type changed)");
        return S_ISDIR(mode) ? TAM_WALK_SKIP : TAM_WALK_CONTINUE;
    }
    if (type != 'l' && (unsigned)(mode & 07777) != me->mode) {
        ck->out->edited++;
        problem(ck, "edited", e->path, "(mode changed)");
        if (type == 'f') note_path(ck->out, e->path);
        return TAM_WALK_CONTINUE;
    }
    if (type == 'f') {
        const tb_stat_t *c = ck->full ? NULL : stats_find(ck->stats, e->path);
        if (c && stat_matches(c, e->st)) return TAM_WALK_CONTINUE;
        char hash[TB_HASH_LEN + 1];
        if (hash_at(e->dirfd, e->name, hash) != 0) {
            tb_err("cannot read %s: %s", e->path, strerror(errno));
            ck->error = 1;
            return -2;
        }
        if (strcmp(hash, me->value) != 0) {
            ck->out->edited++;
            problem(ck, "edited", e->path, NULL);
            note_path(ck->out, e->path);
        }
    } else if (type == 'l') {
        char *target = link_at(e->dirfd, e->name);
        if (!target || strcmp(target, me->value) != 0) {
            ck->out->edited++;
            problem(ck, "edited", e->path, "(link target changed)");
        }
        free(target);
    }
    return TAM_WALK_CONTINUE;
}

int tb_tree_check(const char *root, const tb_manifest_t *m, const tb_stats_t *stats,
                  bool full, tb_check_t *out) {
    bool quiet = out->quiet;
    memset(out, 0, sizeof *out);
    out->quiet = quiet;
    check_t ck = {m, stats, full, calloc(m->n ? m->n : 1, sizeof(bool)), out, 0};
    if (!ck.seen) return -1;
    int r = tam_fs_walk(root, TAM_WALK_ONE_DEVICE, check_cb, &ck);
    if (r != 0) {
        if (!ck.error) tb_err("cannot walk %s: %s", root, strerror(errno));
        free(ck.seen);
        return -1;
    }
    for (size_t i = 0; i < m->n; i++) {
        if (ck.seen[i]) continue;
        /* Entries under a directory already reported as changed in type
         * were not visited; count each missing entry once. */
        out->missing++;
        problem(&ck, "missing", m->v[i].path, NULL);
    }
    free(ck.seen);
    return 0;
}

void tb_check_free(tb_check_t *c) {
    for (size_t i = 0; i < c->npaths; i++) free(c->paths[i]);
    free(c->paths);
    c->paths = NULL;
    c->npaths = 0;
}

/* ---- Size -------------------------------------------------------------- */

static int bytes_cb(const tam_walk_entry_t *e, void *user) {
    *(long long *)user += (long long)e->st->st_blocks * 512;
    return TAM_WALK_CONTINUE;
}

long long tb_tree_bytes(const char *root) {
    long long total = 0;
    if (tam_fs_walk(root, 0, bytes_cb, &total) != 0) return -1;
    return total;
}
