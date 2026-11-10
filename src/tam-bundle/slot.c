/* Slots and their state: the lock, the intent file, the keep list, the log. */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

char *tb_state_path(const tb_slot_t *s, const char *leaf) {
    return tam_path_join(s->state, leaf);
}

/* True when the slot, or its nearest existing parent, belongs to root: such
 * a slot keeps its state in the system state directory whoever reads it. */
bool tb_owned_by_root(const char *dir) {
    char *p = strdup(dir);
    struct stat st;
    bool root = true;
    while (p) {
        if (stat(p, &st) == 0) {
            root = st.st_uid == 0;
            break;
        }
        char *slash = strrchr(p, '/');
        if (!slash || slash == p) break;
        *slash = '\0';
    }
    free(p);
    return root;
}

char *tb_state_dir(const char *dir, const char *state) {
    if (state) return strdup(state);
    char *d = strdup(dir);
    size_t len = d ? strlen(d) : 0;
    while (len > 1 && d[len - 1] == '/') d[--len] = '\0';
    const char *base = d ? strrchr(d, '/') : NULL;
    char *out = NULL;
    if (base && tb_valid_name(base + 1)) {
        if (tb_owned_by_root(d)) {
            out = tam_path_join(TB_SYSTEM_STATE, base + 1);
        } else {
            char *xdg = tam_xdg_path(TAM_XDG_STATE, "tam-bundle", NULL, false);
            out = xdg ? tam_path_join(xdg, base + 1) : NULL;
            free(xdg);
        }
    }
    free(d);
    return out;
}

int tb_slot_open(tb_slot_t *s, const char *dir, const char *state, bool create, bool plan_missing) {
    memset(s, 0, sizeof *s);
    s->fd = s->lock_fd = -1;
    if (!dir || dir[0] != '/') {
        tb_err("the slot must be an absolute path: %s", dir ? dir : "(none)");
        return -1;
    }
    s->dir = strdup(dir);
    size_t len = strlen(s->dir);
    while (len > 1 && s->dir[len - 1] == '/') s->dir[--len] = '\0';
    const char *base = strrchr(s->dir, '/') + 1;
    if (!tb_valid_name(base)) {
        tb_err("the slot directory needs a plain name: %s", s->dir);
        return -1;
    }
    s->name = strdup(base);

    s->state = tb_state_dir(s->dir, state);
    if (!s->state) {
        tb_err("no state directory for slot %s", s->dir);
        return -1;
    }
    if (create) {
        if (!tam_fs_mkdir_p(s->dir, 0755)) {
            tb_err("cannot create slot %s: %s%s", s->dir, strerror(errno),
                   errno == EACCES ? " (run with pkexec or sudo)" : "");
            return -1;
        }
        /* Readable by everyone: `status`, `check` and `where` need no
         * privilege, and a user who ran a switch through pkexec reads the
         * log to find the steps left for their session. Nothing here is
         * secret; the slot's files are world-readable too. */
        char *stat_dir = tam_path_join(s->state, "stat");
        bool ok = stat_dir && tam_fs_mkdir_p(stat_dir, 0755);
        free(stat_dir);
        if (!ok) {
            tb_err("cannot create state directory %s: %s", s->state, strerror(errno));
            return -1;
        }
    }
    s->fd = open(s->dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (s->fd < 0) {
        if (plan_missing && !create && errno == ENOENT) return 0;
        tb_err("cannot open slot %s: %s", s->dir, strerror(errno));
        return -1;
    }
    if (create && mkdirat(s->fd, "kept", 0755) != 0 && errno != EEXIST) {
        tb_err("cannot create %s/kept: %s", s->dir, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(s->fd, &st) != 0) return -1;
    s->dev = st.st_dev;
    return 0;
}

void tb_slot_close(tb_slot_t *s) {
    if (s->lock_fd >= 0) tam_fs_unlock(s->lock_fd);
    if (s->fd >= 0) close(s->fd);
    free(s->dir);
    free(s->name);
    free(s->state);
    memset(s, 0, sizeof *s);
    s->fd = s->lock_fd = -1;
}

int tb_slot_lock(tb_slot_t *s) {
    char *path = tb_state_path(s, "lock");
    s->lock_fd = path ? tam_fs_lock(path, 5000) : -1;
    int saved = errno;
    free(path);
    if (s->lock_fd < 0) {
        if (saved == EWOULDBLOCK)
            tb_err("another tam-bundle is working on slot %s; try again when it finishes", s->dir);
        else
            tb_err("cannot lock slot %s: %s", s->dir, strerror(saved));
        return -1;
    }
    /* libtam creates locks privately by default. Slot state is public,
     * including the lock used by an unprivileged read-only plan. This also
     * repairs older 0600 locks on the next privileged changing command. */
    if (fchmod(s->lock_fd, 0644) != 0) {
        tb_err("cannot make the slot lock readable: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int tb_slot_plan_lock(tb_slot_t *s) {
    char *path = tb_state_path(s, "lock");
    s->lock_fd = path ? open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    int saved = errno;
    free(path);
    if (s->lock_fd < 0 && saved == ENOENT) return 0;
    if (s->lock_fd >= 0 && flock(s->lock_fd, LOCK_SH | LOCK_NB) == 0) return 0;
    tb_err("cannot read-lock slot %s: %s", s->dir, strerror(s->lock_fd < 0 ? saved : errno));
    return -1;
}

/* ---- Intent ------------------------------------------------------------ */

static int intent_line(void *user, size_t line, int format, char **f, size_t n) {
    tb_intent_t *it = user;
    (void)line; (void)format;
    if (n != 2) return 1;
    char *dst = strcmp(f[0], "op") == 0 ? it->op
              : strcmp(f[0], "from") == 0 ? it->from
              : strcmp(f[0], "to") == 0 ? it->to
              : strcmp(f[0], "source") == 0 ? it->source : NULL;
    size_t cap = dst == it->op ? sizeof it->op : dst == it->source ? sizeof it->source : sizeof it->from;
    if (!dst || strlen(f[1]) >= cap) return 1;
    strcpy(dst, f[1]);
    return 0;
}

int tb_intent_read(const tb_slot_t *s, tb_intent_t *it) {
    memset(it, 0, sizeof *it);
    char *path = tb_state_path(s, "switch");
    size_t len;
    char *text = path ? tb_read_at(AT_FDCWD, path, &len) : NULL;
    if (!text) {
        int missing = errno == ENOENT;
        if (!missing) tb_err("cannot read %s: %s", path ? path : "switch", strerror(errno));
        free(path);
        return missing ? 0 : -1;
    }
    tam_rec_error_t err;
    int r = tam_rec_parse(text, len, 2, TB_FORMAT, intent_line, it, &err);
    free(text);
    if (r != 0 || !it->op[0] || !it->from[0] || !it->to[0] || !it->source[0] ||
        !tam_path_is_safe_rel(it->source)) {
        tb_err("%s is damaged; inspect it and the slot by hand", path);
        free(path);
        return -1;
    }
    free(path);
    return 1;
}

int tb_intent_write(const tb_slot_t *s, const tb_intent_t *it) {
    char text[1200];
    snprintf(text, sizeof text, "format 1\nop %s\nfrom %s\nto %s\nsource %s\n",
             it->op, it->from, it->to, it->source);
    char *path = tb_state_path(s, "switch");
    bool ok = path && tam_fs_write_atomic(path, text, strlen(text), 0644);
    if (!ok) tb_err("cannot write %s: %s", path ? path : "switch", strerror(errno));
    free(path);
    return ok ? 0 : -1;
}

int tb_intent_clear(const tb_slot_t *s) {
    char *path = tb_state_path(s, "switch");
    int r = path && (unlink(path) == 0 || errno == ENOENT) ? 0 : -1;
    free(path);
    if (r == 0 && !tam_fs_sync_dir(s->state)) r = -1;
    return r;
}

/* ---- Keep list --------------------------------------------------------- */

static int keep_line(void *user, size_t line, int format, char **f, size_t n) {
    (void)line; (void)format;
    if (n != 3 || strcmp(f[0], "keep") != 0) return 1;
    tb_keep_set(user, f[1], f[2]);
    return 0;
}

int tb_keep_read(const tb_slot_t *s, tb_keep_t *k) {
    memset(k, 0, sizeof *k);
    char *path = tb_state_path(s, "keep");
    size_t len;
    char *text = path ? tb_read_at(AT_FDCWD, path, &len) : NULL;
    if (!text) {
        int missing = errno == ENOENT;
        if (!missing) tb_err("cannot read %s: %s", path, strerror(errno));
        free(path);
        return missing ? 0 : -1;
    }
    tam_rec_error_t err;
    int r = tam_rec_parse(text, len, 3, TB_FORMAT, keep_line, k, &err);
    free(text);
    if (r != 0) tb_err("%s is damaged", path);
    free(path);
    return r == 0 ? 0 : -1;
}

int tb_keep_write(const tb_slot_t *s, const tb_keep_t *k) {
    tam_str_t text = tam_str_new("format 1\n");
    for (size_t i = 0; text && i < k->n; i++) {
        const char *f[] = {"keep", k->ids[i], k->reasons[i]};
        tam_str_t next = tam_rec_cat(text, f, 3);
        if (!next) tam_str_free(text);
        text = next;
    }
    char *path = tb_state_path(s, "keep");
    bool ok = text && path && tam_fs_write_atomic(path, text, tam_str_len(text), 0644);
    if (!ok) tb_err("cannot write the keep list: %s", strerror(errno));
    tam_str_free(text);
    free(path);
    return ok ? 0 : -1;
}

void tb_keep_set(tb_keep_t *k, const char *id, const char *reason) {
    for (size_t i = 0; i < k->n; i++) {
        if (strcmp(k->ids[i], id) == 0) {
            free(k->reasons[i]);
            k->reasons[i] = strdup(reason);
            return;
        }
    }
    char **ids = realloc(k->ids, (k->n + 1) * sizeof *ids);
    if (!ids) return;
    k->ids = ids;
    char **reasons = realloc(k->reasons, (k->n + 1) * sizeof *reasons);
    if (!reasons) return;
    k->reasons = reasons;
    k->ids[k->n] = strdup(id);
    k->reasons[k->n] = strdup(reason);
    k->n++;
}

void tb_keep_drop(tb_keep_t *k, const char *id) {
    for (size_t i = 0; i < k->n; i++) {
        if (strcmp(k->ids[i], id) != 0) continue;
        free(k->ids[i]);
        free(k->reasons[i]);
        memmove(k->ids + i, k->ids + i + 1, (k->n - i - 1) * sizeof *k->ids);
        memmove(k->reasons + i, k->reasons + i + 1, (k->n - i - 1) * sizeof *k->reasons);
        k->n--;
        return;
    }
}

const char *tb_keep_reason(const tb_keep_t *k, const char *id) {
    for (size_t i = 0; i < k->n; i++)
        if (strcmp(k->ids[i], id) == 0) return k->reasons[i];
    return NULL;
}

const char *tb_keep_previous(const tb_keep_t *k) {
    for (size_t i = 0; i < k->n; i++)
        if (strcmp(k->reasons[i], "previous") == 0) return k->ids[i];
    return NULL;
}

void tb_keep_free(tb_keep_t *k) {
    for (size_t i = 0; i < k->n; i++) {
        free(k->ids[i]);
        free(k->reasons[i]);
    }
    free(k->ids);
    free(k->reasons);
    memset(k, 0, sizeof *k);
}

/* ---- Log --------------------------------------------------------------- */

/* One timestamped line, control characters made spaces. */
static int format_line(char *line, size_t cap, const char *fmt, va_list ap) {
    char msg[1024];
    vsnprintf(msg, sizeof msg, fmt, ap);
    for (char *p = msg; *p; p++)
        if ((unsigned char)*p < 0x20) *p = ' ';
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    int n = (int)strftime(line, 32, "%Y-%m-%dT%H:%M:%SZ ", &tm);
    n += snprintf(line + n, cap - (size_t)n, "%s\n", msg);
    return n < (int)cap ? n : (int)cap - 1;
}

static int append_line(int fd, const char *line, int n) {
    if (fd < 0) return -1;
    bool ok = write(fd, line, (size_t)n) == n && fsync(fd) == 0;
    close(fd);
    return ok ? 0 : -1;
}

int tb_log(const tb_slot_t *s, const char *fmt, ...) {
    char line[1100];
    va_list ap;
    va_start(ap, fmt);
    int n = format_line(line, sizeof line, fmt, ap);
    va_end(ap);
    char *path = tb_state_path(s, "log");
    int fd = path ? open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644) : -1;
    free(path);
    return append_line(fd, line, n);
}

static char *session_path(const tb_slot_t *s, uid_t uid) {
    char leaf[40];
    snprintf(leaf, sizeof leaf, "session.%u", (unsigned)uid);
    return tb_state_path(s, leaf);
}

int tb_session_prepare(const tb_slot_t *s, uid_t uid) {
    char *path = session_path(s, uid);
    int fd = path ? open(path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644) : -1;
    struct stat st;
    bool ok = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
              (st.st_uid == uid || fchown(fd, uid, (gid_t)-1) == 0) && fchmod(fd, 0644) == 0;
    if (!ok) tb_err("cannot prepare %s for the session steps' results: %s", path ? path : "session file",
                    strerror(errno));
    if (fd >= 0) close(fd);
    free(path);
    return ok ? 0 : -1;
}

int tb_log_session(const tb_slot_t *s, const char *fmt, ...) {
    char line[1100];
    va_list ap;
    va_start(ap, fmt);
    int n = format_line(line, sizeof line, fmt, ap);
    va_end(ap);
    char *path = session_path(s, getuid());
    /* Never created here: only the root run makes it, owned by this user. */
    int fd = path ? open(path, O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC) : -1;
    int r = append_line(fd, line, n);
    if (r != 0) tb_note("could not record this step's result in %s: %s", path ? path : "the state directory",
                        strerror(errno));
    free(path);
    return r;
}

int tb_current_id(const tb_slot_t *s, char out[256]) {
    struct stat st;
    if (fstatat(s->fd, "current", &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT) return -1;
        strcpy(out, "-");
        return 0;
    }
    size_t len;
    char *text = tb_read_at(s->fd, "current/BUNDLE", &len);
    if (!text) {
        tb_err("%s/current has no readable BUNDLE: %s", s->dir, strerror(errno));
        return -1;
    }
    tb_bundle_t b;
    int r = tb_bundle_parse(text, len, "current/BUNDLE", &b);
    free(text);
    if (r != 0) return -1;
    snprintf(out, 256, "%s", b.id);
    tb_bundle_free(&b);
    return 0;
}

/* ---- Removing trees ---------------------------------------------------- */

static int writable_cb(const tam_walk_entry_t *e, void *user) {
    (void)user;
    if (S_ISDIR(e->st->st_mode) && (e->st->st_mode & 0700) != 0700)
        fchmodat(e->dirfd, e->name, (e->st->st_mode & 07777) | 0700, 0);
    return TAM_WALK_CONTINUE;
}

int tb_remove_at(int basefd, const char *base, const char *rel) {
    struct stat st;
    if (fstatat(basefd, rel, &st, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT ? 0 : -1;
    const char *slash = strrchr(rel, '/');
    char parent[4200] = ".";
    if (slash) snprintf(parent, sizeof parent, "%.*s", (int)(slash - rel), rel);
    const char *name = slash ? slash + 1 : rel;
    int pfd = openat(basefd, parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (pfd < 0) return -1;
    /* A tree may hold read-only directories; an owner who is not root cannot
     * empty them until they are writable again. */
    if (S_ISDIR(st.st_mode)) {
        if ((st.st_mode & 0700) != 0700) fchmodat(pfd, name, (st.st_mode & 07777) | 0700, 0);
        char *abs = tam_path_join(base, rel);
        if (abs) tam_fs_walk(abs, TAM_WALK_ONE_DEVICE, writable_cb, NULL);
        free(abs);
    }
    int r = tam_fs_remove_tree(pfd, name);
    int saved = errno;
    close(pfd);
    errno = saved;
    return r;
}

int tb_remove_tree(const tb_slot_t *s, const char *rel) {
    return tb_remove_at(s->fd, s->dir, rel);
}
