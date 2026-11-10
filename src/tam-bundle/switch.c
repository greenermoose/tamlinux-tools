/* Switch, rollback and interruption recovery: the algorithm in the design's
 * "Switch algorithm" and "Interruption recovery" sections. */
#include "tb.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool exists_at(int dirfd, const char *rel) {
    struct stat st;
    return fstatat(dirfd, rel, &st, AT_SYMLINK_NOFOLLOW) == 0;
}

static int same_device(const tb_slot_t *s, const char *rel) {
    struct stat st;
    if (fstatat(s->fd, rel, &st, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT ? 0 : -1;
    if (st.st_dev != s->dev) {
        tb_err("%s/%s is on another filesystem or Btrfs subvolume than the slot; "
               "a switch needs them on one", s->dir, rel);
        return -1;
    }
    return 0;
}

static bool sync_slot(const tb_slot_t *s) {
    return tam_fs_sync_dirat(s->fd, "kept") && tam_fs_sync_dirat(s->fd, ".");
}

/* ---- Finishing (shared by switch, back and recover) -------------------- */

/* After the exchange: move the displaced tree to kept/<from>, record the stat
 * cache and keep list, log, and clear the intent. */
static int finish(tb_slot_t *s, const tb_intent_t *it, const tb_manifest_t *live, double started) {
    char kept[300];
    if (strcmp(it->from, "-") != 0) {
        snprintf(kept, sizeof kept, "kept/%s", it->from);
        if (exists_at(s->fd, it->source)) {
            /* A kept tree of the same id is a duplicate; replace it. */
            if (exists_at(s->fd, kept) && tb_remove_tree(s, kept) != 0) {
                tb_err("cannot replace %s/%s: %s", s->dir, kept, strerror(errno));
                return -1;
            }
            if (renameat(s->fd, it->source, s->fd, kept) != 0) {
                tb_err("cannot move the previous tree to %s/%s: %s", s->dir, kept, strerror(errno));
                return -1;
            }
        }
        if (!sync_slot(s)) {
            tb_err("cannot sync %s: %s", s->dir, strerror(errno));
            return -1;
        }
    }
    tb_fault("after-keep");

    char *live_dir = tam_path_join(s->dir, "current");
    char leaf[300];
    snprintf(leaf, sizeof leaf, "stat/%s", it->to);
    char *stat_path = tb_state_path(s, leaf);
    tb_stats_t st;
    if (live_dir && stat_path && tb_stats_build(live_dir, live, &st) == 0) {
        if (tb_stats_write(stat_path, &st) != 0) tb_err("cannot write %s (checks will hash every file)", stat_path);
        tb_stats_free(&st);
    }
    free(live_dir);
    free(stat_path);

    tb_keep_t k;
    if (tb_keep_read(s, &k) != 0) return -1;
    const char *prev;
    while ((prev = tb_keep_previous(&k)) != NULL) tb_keep_drop(&k, prev);
    if (strcmp(it->from, "-") != 0) tb_keep_set(&k, it->from, "previous");
    int r = tb_keep_write(s, &k);
    tb_keep_free(&k);
    if (r != 0) return -1;

    if (started > 0)
        tb_log(s, "%s %s from %s ok %.1fs", it->op, it->to, it->from, now_s() - started);
    else
        tb_log(s, "%s %s from %s finished by recover", it->op, it->to, it->from);
    tb_fault("after-record");
    if (tb_intent_clear(s) != 0) {
        tb_err("cannot remove the switch intent in %s", s->state);
        return -1;
    }
    return 0;
}

static int run_live_after(tb_slot_t *s) {
    tb_bundle_t b;
    tb_manifest_t m;
    if (tb_tree_read(s->fd, "current", &b, &m) != 0) return TB_FAIL;
    int r = tb_run_after(s, &b);
    tb_bundle_free(&b);
    tb_manifest_free(&m);
    return r;
}

/* ---- Recovery ---------------------------------------------------------- */

static int remove_stale_staging(tb_slot_t *s, bool dry_run) {
    int dup_fd = fcntl(s->fd, F_DUPFD_CLOEXEC, 0);
    DIR *dir = dup_fd >= 0 ? fdopendir(dup_fd) : NULL;
    if (!dir) return -1;
    char **names = NULL;
    size_t n = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "staging.", 8) != 0) continue;
        char **grown = realloc(names, (n + 1) * sizeof *names);
        if (!grown) break;
        names = grown;
        names[n++] = strdup(de->d_name);
    }
    closedir(dir);
    int r = 0;
    for (size_t i = 0; i < n; i++) {
        if (dry_run) {
            tb_note("would remove a staging tree that never went live: %s/%s", s->dir, names[i]);
        } else if (tb_remove_tree(s, names[i]) == 0) {
            tb_log(s, "removed stale %s", names[i]);
            tb_note("removed a staging tree that never went live: %s/%s", s->dir, names[i]);
        } else {
            tb_err("cannot remove %s/%s: %s", s->dir, names[i], strerror(errno));
            r = -1;
        }
        free(names[i]);
    }
    free(names);
    return r;
}

int tb_recover(tb_slot_t *s, bool dry_run, bool *steps_due) {
    if (steps_due) *steps_due = false;
    tb_intent_t it;
    int have = tb_intent_read(s, &it);
    if (have < 0) return TB_INTERRUPTED;
    if (have == 0) return remove_stale_staging(s, dry_run) == 0 ? TB_OK : TB_FAIL;

    char cur[256];
    if (tb_current_id(s, cur) != 0) {
        tb_err("an interrupted %s to %s needs attention, and the live tree cannot be read", it.op, it.to);
        return TB_INTERRUPTED;
    }
    if (strcmp(cur, it.from) == 0) {
        if (dry_run) {
            tb_note("would undo the interrupted %s to %s: it stopped before the exchange, so %s is still live",
                    it.op, it.to, it.from);
            return TB_OK;
        }
        if (strcmp(it.op, "switch") == 0 && exists_at(s->fd, it.source) &&
            tb_remove_tree(s, it.source) != 0) {
            tb_err("cannot remove %s/%s: %s", s->dir, it.source, strerror(errno));
            return TB_INTERRUPTED;
        }
        if (tb_intent_clear(s) != 0) return TB_INTERRUPTED;
        tb_log(s, "%s %s from %s undone by recover", it.op, it.to, it.from);
        tb_note("undid the interrupted %s to %s; %s is live and untouched", it.op, it.to, it.from);
        return remove_stale_staging(s, false) == 0 ? TB_OK : TB_FAIL;
    }
    if (strcmp(cur, it.to) == 0) {
        if (dry_run) {
            tb_note("would finish the interrupted %s to %s: the exchange happened, %s is live",
                    it.op, it.to, it.to);
            return TB_OK;
        }
        tb_bundle_t b;
        tb_manifest_t m;
        if (tb_tree_read(s->fd, "current", &b, &m) != 0) return TB_INTERRUPTED;
        int r = finish(s, &it, &m, 0);
        tb_bundle_free(&b);
        tb_manifest_free(&m);
        if (r != 0) return TB_INTERRUPTED;
        tb_note("finished the interrupted %s to %s; running its post-install steps", it.op, it.to);
        if (steps_due) *steps_due = true;
        int ar = run_live_after(s);
        if (remove_stale_staging(s, false) != 0) return TB_FAIL;
        return ar;
    }
    tb_err("an interrupted %s from %s to %s needs attention, but the live tree is %s. "
           "Nothing was changed; inspect %s and %s/switch by hand.",
           it.op, it.from, it.to, cur, s->dir, s->state);
    return TB_INTERRUPTED;
}

/* ---- Checks before a switch -------------------------------------------- */

static bool listed(char **paths, size_t n, const char *path) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(paths[i], path) == 0) return true;
    return false;
}

static int save_file(const tb_slot_t *s, const char *id, const char *rel) {
    char leaf[4400];
    snprintf(leaf, sizeof leaf, "saved/%s/%s", id, rel);
    char *dst = tb_state_path(s, leaf);
    char *src = tam_path_join(s->dir, "current");
    char *src_file = src ? tam_path_join(src, rel) : NULL;
    int r = -1;
    if (dst && src_file) {
        char *slash = strrchr(dst, '/');
        *slash = '\0';
        bool dir_ok = tam_fs_mkdir_p(dst, 0700);
        *slash = '/';
        int in = open(src_file, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        int out = dir_ok ? open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600) : -1;
        if (in >= 0 && out >= 0 && tam_fs_clone_or_copy(in, out, NULL) == 0 && fsync(out) == 0) r = 0;
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        if (r == 0) tb_note("saved %s to %s", rel, dst);
        else tb_err("cannot save %s to %s: %s", rel, dst, strerror(errno));
    }
    free(dst);
    free(src);
    free(src_file);
    return r;
}

/* Check the live tree for edits. Returns 0 to go on (with `edits` filled),
 * -1 to stop. */
static int check_live(tb_slot_t *s, const char *cur, const tb_manifest_t *cm, bool full,
                      bool save_edits, tb_check_t *edits) {
    char leaf[300];
    snprintf(leaf, sizeof leaf, "stat/%s", cur);
    char *stat_path = tb_state_path(s, leaf);
    char *live = tam_path_join(s->dir, "current");
    tb_stats_t st;
    tb_stats_read(stat_path, &st);
    memset(edits, 0, sizeof *edits);
    int r = tb_tree_check(live, cm, &st, full, edits);
    tb_stats_free(&st);
    free(stat_path);
    free(live);
    if (r != 0) return -1;
    size_t problems = edits->edited + edits->missing + edits->extra;
    if (problems == 0) return 0;
    if (!save_edits) {
        tb_err("the live tree %s/current has %zu change%s not made by tam-bundle (listed above). "
               "Run again with --save-edits to save the changed files to %s/saved/%s/ and continue.",
               s->dir, problems, problems == 1 ? "" : "s", s->state, cur);
        return -1;
    }
    for (size_t i = 0; i < edits->npaths; i++)
        if (save_file(s, cur, edits->paths[i]) != 0) return -1;
    tb_log(s, "saved %zu changed file%s from %s", edits->npaths, edits->npaths == 1 ? "" : "s", cur);
    return 0;
}

static bool host_ok(const tb_bundle_t *b) {
    if (strcmp(b->host, "any") == 0) return true;
    char host[256] = "";
    gethostname(host, sizeof host - 1);
    if (strcmp(host, b->host) == 0) return true;
    tb_err("%s was built for host %s, and this is %s", b->id, b->host, host);
    return false;
}

/* ---- Staging ----------------------------------------------------------- */

static int write_at(int dirfd, const char *name, const char *text, size_t len) {
    int fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    bool ok = write(fd, text, len) == (ssize_t)len;
    return close(fd) == 0 && ok ? 0 : -1;
}

typedef struct {
    size_t reused, copied, cloned;
} stage_counts_t;

static int stage(tb_slot_t *s, const char *bundle_dir, const tb_bundle_t *b, const tb_manifest_t *m,
                 const char *staging, const tb_manifest_t *cm, const tb_check_t *edits,
                 stage_counts_t *counts) {
    memset(counts, 0, sizeof *counts);
    if (exists_at(s->fd, staging) && tb_remove_tree(s, staging) != 0) return -1;
    if (mkdirat(s->fd, staging, 0700) != 0) {
        tb_err("cannot create %s/%s: %s", s->dir, staging, strerror(errno));
        return -1;
    }
    int sfd = openat(s->fd, staging, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int bfd = open(bundle_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int cfd = cm ? openat(s->fd, "current", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    int r = (sfd < 0 || bfd < 0 || (cm && cfd < 0)) ? -1 : 0;
    if (r) tb_err("cannot open the trees to stage %s: %s", b->id, strerror(errno));

    for (size_t i = 0; r == 0 && i < m->n; i++) {
        const tb_entry_t *e = &m->v[i];
        if (e->type == 'd') {
            if (mkdirat(sfd, e->path, 0700) != 0) r = -1;
        } else if (e->type == 'l') {
            if (symlinkat(e->value, sfd, e->path) != 0) r = -1;
        } else {
            const tb_entry_t *ce = cm ? tb_manifest_find(cm, e->path) : NULL;
            bool reuse = ce && ce->type == 'f' && strcmp(ce->value, e->value) == 0 &&
                         !listed(edits->paths, edits->npaths, e->path);
            int src = tb_open_beneath(reuse ? cfd : bfd, e->path);
            int dst = openat(sfd, e->path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            bool cloned = false;
            if (src < 0 || dst < 0 || tam_fs_clone_or_copy(src, dst, &cloned) != 0 ||
                fchmod(dst, e->mode) != 0) {
                r = -1;
            } else if (!reuse) {
                /* A file from the bundle is checked as it lands. */
                uint8_t d[TAM_SHA256_LEN];
                char hash[TB_HASH_LEN + 1] = "sha256:";
                if (lseek(dst, 0, SEEK_SET) != 0 || tam_sha256_fd(dst, d) != 0) {
                    r = -1;
                } else {
                    tam_sha256_hex(d, hash + 7);
                    if (strcmp(hash, e->value) != 0) {
                        tb_err("%s/%s does not match its MANIFEST hash", bundle_dir, e->path);
                        errno = 0;
                        r = -2;
                    }
                }
            }
            if (r == 0) {
                if (reuse) counts->reused++;
                else counts->copied++;
                if (cloned) counts->cloned++;
            }
            if (src >= 0) close(src);
            if (dst >= 0) close(dst);
        }
        if (r == -1) tb_err("cannot stage %s: %s", e->path, strerror(errno));
        tb_fault_file(i + 1);
    }
    if (r == 0 && (write_at(sfd, "MANIFEST", m->text, m->len) != 0 ||
                   write_at(sfd, "BUNDLE", b->text, b->len) != 0)) {
        tb_err("cannot write the records into %s/%s: %s", s->dir, staging, strerror(errno));
        r = -1;
    }
    /* Directory modes last, deepest first, so read-only directories do not
     * block their own contents. */
    for (size_t i = m->n; r == 0 && i-- > 0;) {
        const tb_entry_t *e = &m->v[i];
        if (e->type == 'd' && fchmodat(sfd, e->path, e->mode, 0) != 0) r = -1;
    }
    if (r == 0 && (fchmod(sfd, 0755) != 0 || syncfs(sfd) != 0)) {
        tb_err("cannot finish %s/%s: %s", s->dir, staging, strerror(errno));
        r = -1;
    }
    if (sfd >= 0) close(sfd);
    if (bfd >= 0) close(bfd);
    if (cfd >= 0) close(cfd);
    return r == 0 ? 0 : -1;
}

/* ---- Switch and back --------------------------------------------------- */

static int exchange_into_current(tb_slot_t *s, const tb_intent_t *it) {
    int r;
    if (strcmp(it->from, "-") == 0) r = renameat(s->fd, it->source, s->fd, "current");
    else r = tam_fs_exchange(s->fd, it->source, s->fd, "current");
    if (r != 0) {
        tb_err("cannot make %s live: %s%s", it->to, strerror(errno),
               errno == EINVAL || errno == ENOSYS ? " (this filesystem cannot exchange directories)" : "");
        return -1;
    }
    if (!sync_slot(s)) {
        tb_err("cannot sync %s: %s", s->dir, strerror(errno));
        return -1;
    }
    tb_fault("after-exchange");
    return 0;
}

/* Every command that changes the slot runs this under the lock first: an
 * interrupted switch can leave kept trees misnamed and the keep list stale
 * (after an interrupted back, the tree that was live sits under the
 * target's name in kept/), so nothing acts on them until it is recovered.
 * A dry run changes nothing: it says what recovery would do and stops. */
int tb_recover_first(tb_slot_t *s, bool dry_run) {
    if (dry_run) {
        tb_intent_t it;
        int have = tb_intent_read(s, &it);
        if (have == 0) return TB_OK;
        if (have > 0) tb_recover(s, true, NULL);
        tb_err("an interrupted switch must be recovered first; run `tam-bundle recover`");
        return TB_INTERRUPTED;
    }
    int r = tb_recover(s, false, NULL);
    return r == TB_AFTER_FAILED ? TB_OK : r;
}

static int prepare(tb_slot_t *s, char cur[256], bool dry_run) {
    if (tb_slot_lock(s) != 0) return TB_FAIL;
    int r = tb_recover_first(s, dry_run);
    if (r != TB_OK) return r;
    if (same_device(s, "current") != 0 || same_device(s, "kept") != 0) return TB_FAIL;
    if (tb_current_id(s, cur) != 0) return TB_FAIL;
    return TB_OK;
}

/* Read the live tree and check it; fills cb/cm when there is one. */
static int live_checked(tb_slot_t *s, const char *cur, const tb_switch_opts_t *o,
                        tb_bundle_t *cb, tb_manifest_t *cm, bool *have, tb_check_t *edits) {
    *have = false;
    memset(edits, 0, sizeof *edits);
    if (strcmp(cur, "-") == 0) return 0;
    if (tb_tree_read(s->fd, "current", cb, cm) != 0) return -1;
    *have = true;
    if (check_live(s, cur, cm, o->full, o->save_edits, edits) != 0) return -1;
    /* A kept copy of the live bundle is a stale duplicate. */
    char stale[300];
    snprintf(stale, sizeof stale, "kept/%s", cur);
    if (!o->dry_run && exists_at(s->fd, stale) && tb_remove_tree(s, stale) == 0)
        tb_log(s, "removed a stale kept copy of the live %s", cur);
    return 0;
}

static int do_back(tb_slot_t *s, const char *cur, const char *target, const tb_switch_opts_t *o,
                   double started);

int tb_switch(tb_slot_t *s, const char *bundle_dir, const tb_switch_opts_t *o) {
    double started = now_s();
    char cur[256];
    int r = prepare(s, cur, o->dry_run);
    if (r != TB_OK) return r;

    tb_bundle_t b;
    tb_manifest_t m;
    if (tb_tree_read(AT_FDCWD, bundle_dir, &b, &m) != 0) return TB_FAIL;
    int result = TB_FAIL;
    tb_bundle_t cb = {0};
    tb_manifest_t cm = {0};
    bool have_live = false;
    tb_check_t edits = {0};

    if (strcmp(cur, b.id) == 0) {
        tb_note("%s is already live", b.id);
        result = TB_OK;
        goto out;
    }
    if (!host_ok(&b) || tb_needs_check(&b, true) > 0) goto out;

    char kept[300];
    snprintf(kept, sizeof kept, "kept/%s", b.id);
    if (exists_at(s->fd, kept)) {
        tb_note("%s is kept in the slot; switching to it without copying", b.id);
        result = do_back(s, cur, b.id, o, started);
        goto out;
    }
    if (live_checked(s, cur, o, &cb, &cm, &have_live, &edits) != 0) goto out;
    if (o->dry_run) {
        tb_note("would switch %s from %s to %s (%zu entries)", s->dir, cur, b.id, m.n);
        result = TB_OK;
        goto out;
    }

    tb_intent_t it = {0};
    snprintf(it.op, sizeof it.op, "switch");
    snprintf(it.from, sizeof it.from, "%s", cur);
    snprintf(it.to, sizeof it.to, "%s", b.id);
    snprintf(it.source, sizeof it.source, "staging.%s", b.id);
    if (tb_intent_write(s, &it) != 0) goto out;
    tb_fault("after-intent");

    stage_counts_t counts;
    if (stage(s, bundle_dir, &b, &m, it.source, have_live ? &cm : NULL, &edits, &counts) != 0) {
        /* Nothing is live yet: undo at once. */
        if (tb_remove_tree(s, it.source) == 0 && tb_intent_clear(s) == 0)
            tb_log(s, "switch %s from %s failed while staging; undone", it.to, it.from);
        goto out;
    }
    tb_fault("after-stage");
    if (exchange_into_current(s, &it) != 0) goto out;
    if (finish(s, &it, &m, started) != 0) {
        result = TB_INTERRUPTED;
        goto out;
    }
    tb_note("switched %s to %s (from %s): %zu files reused, %zu copied%s", s->dir, b.id, cur,
            counts.reused, counts.copied, counts.cloned ? ", unchanged ones cloned" : "");
    result = tb_run_after(s, &b);
out:
    tb_check_free(&edits);
    if (have_live) {
        tb_bundle_free(&cb);
        tb_manifest_free(&cm);
    }
    tb_bundle_free(&b);
    tb_manifest_free(&m);
    return result;
}

static int do_back(tb_slot_t *s, const char *cur, const char *target, const tb_switch_opts_t *o,
                   double started) {
    char kept[300];
    snprintf(kept, sizeof kept, "kept/%s", target);
    tb_bundle_t kb, cb = {0};
    tb_manifest_t km, cm = {0};
    if (tb_tree_read(s->fd, kept, &kb, &km) != 0) return TB_FAIL;
    int result = TB_FAIL;
    bool have_live = false;
    tb_check_t edits = {0}, kc = {0};

    if (strcmp(kb.id, target) != 0) {
        tb_err("%s/%s holds %s, not %s", s->dir, kept, kb.id, target);
        goto out;
    }
    char leaf[300];
    snprintf(leaf, sizeof leaf, "stat/%s", target);
    char *stat_path = tb_state_path(s, leaf);
    char *kept_dir = tam_path_join(s->dir, kept);
    tb_stats_t st;
    tb_stats_read(stat_path, &st);
    int cr = tb_tree_check(kept_dir, &km, &st, o->full, &kc);
    tb_stats_free(&st);
    free(stat_path);
    free(kept_dir);
    if (cr != 0) goto out;
    if (kc.edited + kc.missing + kc.extra > 0) {
        tb_err("the kept tree %s/%s differs from its MANIFEST (listed above); not switching to it",
               s->dir, kept);
        goto out;
    }
    if (tb_needs_check(&kb, true) > 0) goto out;
    if (live_checked(s, cur, o, &cb, &cm, &have_live, &edits) != 0) goto out;
    if (o->dry_run) {
        tb_note("would switch %s back from %s to the kept %s", s->dir, cur, target);
        result = TB_OK;
        goto out;
    }

    tb_intent_t it = {0};
    snprintf(it.op, sizeof it.op, "back");
    snprintf(it.from, sizeof it.from, "%s", cur);
    snprintf(it.to, sizeof it.to, "%s", target);
    snprintf(it.source, sizeof it.source, "%s", kept);
    if (tb_intent_write(s, &it) != 0) goto out;
    tb_fault("after-intent");
    if (exchange_into_current(s, &it) != 0) goto out;
    if (finish(s, &it, &km, started) != 0) {
        result = TB_INTERRUPTED;
        goto out;
    }
    tb_note("switched %s back to %s (from %s)", s->dir, target, cur);
    result = tb_run_after(s, &kb);
out:
    tb_check_free(&edits);
    tb_check_free(&kc);
    if (have_live) {
        tb_bundle_free(&cb);
        tb_manifest_free(&cm);
    }
    tb_bundle_free(&kb);
    tb_manifest_free(&km);
    return result;
}

int tb_back(tb_slot_t *s, const char *id, const tb_switch_opts_t *o) {
    double started = now_s();
    char cur[256];
    int r = prepare(s, cur, o->dry_run);
    if (r != TB_OK) return r;
    tb_keep_t k;
    if (tb_keep_read(s, &k) != 0) return TB_FAIL;
    char target[256] = "";
    const char *prev = id ? id : tb_keep_previous(&k);
    if (prev) snprintf(target, sizeof target, "%s", prev);
    tb_keep_free(&k);
    if (!target[0]) {
        tb_err("no previous bundle is recorded for %s; name one from `tam-bundle status`", s->dir);
        return TB_FAIL;
    }
    if (strcmp(target, cur) == 0) {
        tb_note("%s is already live", target);
        return TB_OK;
    }
    char kept[300];
    snprintf(kept, sizeof kept, "kept/%s", target);
    if (!tb_valid_name(target) || !exists_at(s->fd, kept)) {
        tb_err("%s is not kept in %s/kept", target, s->dir);
        return TB_FAIL;
    }
    return do_back(s, cur, target, o, started);
}
