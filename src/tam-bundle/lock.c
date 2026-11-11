/* Lock files: one exact commit per component.
 *
 *   format 1
 *   component <name> <url> <commit>
 *
 * Nothing changes a lock file except `tam-bundle lock`, which rewrites one
 * line and leaves the rest of the file, comments included, as it was. */
#include "tb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    tb_lock_t  *l;
    const char *what;
} lparse_t;

static int lock_line(void *user, size_t line, int format, char **f, size_t n) {
    lparse_t *lp = user;
    (void)format;
    if (n != 4 || strcmp(f[0], "component") != 0) {
        tb_err("%s line %zu: expected: component <name> <url> <commit>", lp->what, line);
        return 1;
    }
    if (!tb_valid_name(f[1]) || tb_lock_find(lp->l, f[1])) {
        tb_err("%s line %zu: invalid or repeated component name %s", lp->what, line, f[1]);
        return 1;
    }
    if (!tb_valid_commit(f[3])) {
        tb_err("%s line %zu: the commit must be a full commit id (40 or 64 hex digits)", lp->what, line);
        return 1;
    }
    tb_lock_entry_t *grown = realloc(lp->l->v, (lp->l->n + 1) * sizeof *grown);
    if (!grown) return 1;
    lp->l->v = grown;
    tb_lock_entry_t *e = &grown[lp->l->n++];
    e->name = strdup(f[1]);
    e->url = strdup(f[2]);
    e->commit = strdup(f[3]);
    return e->name && e->url && e->commit ? 0 : 1;
}

static int lock_parse(const char *text, size_t len, const char *what, tb_lock_t *l) {
    memset(l, 0, sizeof *l);
    lparse_t lp = {l, what};
    tam_rec_error_t err;
    int r = tam_rec_parse(text, len, 4, TB_FORMAT, lock_line, &lp, &err);
    if (r == -1) {
        if (err.format > TB_FORMAT)
            tb_err("%s is format %d; this tam-bundle %s reads up to format %d. Use a newer tam-bundle.",
                   what, err.format, TB_VERSION, TB_FORMAT);
        else if (err.line) tb_err("%s line %zu: %s", what, err.line, err.reason);
        else tb_err("%s: %s", what, err.reason);
    }
    if (r != 0) {
        tb_lock_free(l);
        return -1;
    }
    return 0;
}

int tb_lock_read(const char *path, tb_lock_t *l) {
    memset(l, 0, sizeof *l);
    size_t len;
    char *text = tb_read_limit(AT_FDCWD, path, &len,1024U*1024U);
    if (!text) {
        tb_err("cannot read %s: %s", path, strerror(errno));
        return -1;
    }
    int r = lock_parse(text, len, path, l);
    if(r)free(text); else {l->text=text; l->len=len;}
    return r;
}

void tb_lock_free(tb_lock_t *l) {
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].name);
        free(l->v[i].url);
        free(l->v[i].commit);
    }
    free(l->v);
    free(l->text);
    memset(l, 0, sizeof *l);
}

const tb_lock_entry_t *tb_lock_find(const tb_lock_t *l, const char *name) {
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i].name, name) == 0) return &l->v[i];
    return NULL;
}

/* True when `line` (without its newline) is the record for `name`. */
static bool is_line_for(const char *line, size_t n, const char *name) {
    const char *p = line, *end = line + n;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if ((size_t)(end - p) < 10 || strncmp(p, "component", 9) != 0 || (p[9] != ' ' && p[9] != '\t'))
        return false;
    p += 9;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    size_t k = strlen(name);
    return (size_t)(end - p) > k && strncmp(p, name, k) == 0 && (p[k] == ' ' || p[k] == '\t');
}

int tb_lock_set(const char *component, const char *rev, const tb_lock_opts_t *o) {
    if (!tb_valid_name(component)) {
        tb_err("invalid component name %s", component);
        return TB_USAGE;
    }
    if (o->url && (!o->url[0] || strpbrk(o->url, " \t\n"))) {
        tb_err("a URL cannot be empty or contain spaces");
        return TB_USAGE;
    }
    size_t len = 0;
    char *text = tb_read_at(AT_FDCWD, o->file, &len);
    if (!text && errno == ENOENT && o->url) {
        text = strdup("format 1\n");
        len = text ? strlen(text) : 0;
    }
    if (!text) {
        tb_err("cannot read %s: %s%s", o->file, strerror(errno),
               errno == ENOENT ? " (give --url to start a new lock file)" : "");
        return TB_FAIL;
    }
    tb_lock_t l;
    if (lock_parse(text, len, o->file, &l) != 0) {
        free(text);
        return TB_FAIL;
    }
    const tb_lock_entry_t *old = tb_lock_find(&l, component);
    int result = TB_FAIL;
    char *commit = NULL;
    tam_str_t out = NULL;

    if (!old && !o->url) {
        tb_err("%s has no component %s; give --url to add it", o->file, component);
        goto done;
    }
    /* A full commit is taken as given; anything shorter is resolved in a
     * local clone, never on a server. */
    if (tb_valid_commit(rev)) {
        commit = strdup(rev);
    } else if (o->repos) {
        char *repo = tam_path_join(o->repos, component);
        commit = repo ? tb_git_resolve(repo, rev) : NULL;
        if (!commit) tb_err("%s does not name a commit in %s", rev, repo ? repo : o->repos);
        free(repo);
        if (!commit) goto done;
    } else {
        tb_err("%s is not a full commit id; give --repos DIR to resolve it in DIR/%s", rev, component);
        result = TB_USAGE;
        goto done;
    }
    const char *url = o->url ? o->url : old->url;
    if (old && strcmp(old->commit, commit) == 0 && strcmp(old->url, url) == 0) {
        tb_note("%s: %s stays at %.12s", o->file, component, commit);
        result = TB_OK;
        goto done;
    }
    const char *fields[] = {"component", component, url, commit};
    tam_str_t record = tam_rec_cat(tam_str_new(NULL), fields, 4);
    out = tam_str_new(NULL);
    bool replaced = false;
    for (const char *p = text, *end = text + len; p < end;) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t n = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (!replaced && is_line_for(p, n, component)) {
            out = tam_str_cat(out, record);
            replaced = true;
        } else {
            out = tam_str_cat_len(out, p, n + (nl ? 1 : 0));
        }
        p += n + (nl ? 1 : 0);
    }
    if (!replaced) out = tam_str_cat(out, record);
    tam_str_free(record);
    /* What is written must read back. */
    tb_lock_t check;
    if (!out || lock_parse(out, tam_str_len(out), "the new lock file", &check) != 0) goto done;
    tb_lock_free(&check);

    if (o->dry_run) {
        tb_note("would set %s to %.12s in %s", component, commit, o->file);
        result = TB_OK;
        goto done;
    }
    struct stat st;
    mode_t mode = stat(o->file, &st) == 0 ? (st.st_mode & 0777) : 0644;
    if (!tam_fs_write_atomic(o->file, out, tam_str_len(out), mode)) {
        tb_err("cannot write %s: %s", o->file, strerror(errno));
        goto done;
    }
    if (old) tb_note("%s: %s %.12s -> %.12s", o->file, component, old->commit, commit);
    else tb_note("%s: added %s at %.12s", o->file, component, commit);
    result = TB_OK;
done:
    tam_str_free(out);
    free(commit);
    tb_lock_free(&l);
    free(text);
    return result;
}
