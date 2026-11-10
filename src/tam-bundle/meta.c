/* BUNDLE and MANIFEST: parsing, validation, the bundle hash and id. */
#include "tb.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The manual index is not a step: `build` makes it into the bundle
 * (`index man` in the bundle file). */
const char *const tb_steps[] = {"user-units-reload", "qml-cache-purge", "shell-restart", NULL};

bool tb_valid_step(const char *s) {
    for (int i = 0; tb_steps[i]; i++)
        if (strcmp(s, tb_steps[i]) == 0) return true;
    return false;
}

/* Names and versions become directory names, so: letters, digits, ".", "_",
 * "+", "-", starting with a letter or digit. */
bool tb_valid_name(const char *s) {
    if (!s || !s[0] || strlen(s) > 100) return false;
    for (const char *p = s; *p; p++) {
        char c = *p;
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (p == s && !alnum) return false;
        if (!alnum && c != '.' && c != '_' && c != '+' && c != '-') return false;
    }
    return true;
}

static bool valid_hash(const char *v) {
    if (strncmp(v, "sha256:", 7) != 0 || strlen(v) != TB_HASH_LEN) return false;
    for (const char *p = v + 7; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    return true;
}

static bool parse_mode(const char *s, unsigned *mode) {
    if (strlen(s) < 3 || strlen(s) > 4) return false;
    unsigned v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '7') return false;
        v = v * 8 + (unsigned)(*p - '0');
    }
    if (v > 0777) return false; /* no setuid, setgid or sticky bits */
    *mode = v;
    return true;
}

/* A link target must be relative and, resolved lexically from the link's
 * directory, stay inside the tree. */
static bool link_stays_inside(const char *path, const char *target) {
    if (!target[0] || target[0] == '/') return false;
    int depth = 0;
    for (const char *p = path; *p; p++)
        if (*p == '/') depth++;
    const char *t = target;
    while (*t) {
        const char *end = strchr(t, '/');
        size_t n = end ? (size_t)(end - t) : strlen(t);
        if (n == 2 && t[0] == '.' && t[1] == '.') {
            if (--depth < 0) return false;
        } else if (n > 0 && !(n == 1 && t[0] == '.')) {
            depth++;
        }
        t += n;
        while (*t == '/') t++;
    }
    return true;
}

static bool reserved(const char *path) {
    return strcmp(path, "BUNDLE") == 0 || strcmp(path, "MANIFEST") == 0;
}

static int cmp_entry_path(const void *key, const void *elem) {
    return strcmp((const char *)key, ((const tb_entry_t *)elem)->path);
}

const tb_entry_t *tb_manifest_find(const tb_manifest_t *m, const char *path) {
    if (!m->n) return NULL;
    return bsearch(path, m->v, m->n, sizeof *m->v, cmp_entry_path);
}

typedef struct {
    tb_manifest_t *m;
    const char    *what;
} mparse_t;

static int bad(const char *what, size_t line, const char *why) {
    tb_err("%s line %zu: %s", what, line, why);
    return 1;
}

static int manifest_line(void *user, size_t line, int format, char **f, size_t n) {
    mparse_t *mp = user;
    tb_manifest_t *m = mp->m;
    if (format == 2) {
        if (n != 5 || (strcmp(f[0], "t") && strcmp(f[0], "p")))
            return bad(mp->what, line, "expected: t|p type mode value path");
        bool placed = !strcmp(f[0], "p");
        if (placed && !strcmp(f[1], "l")) return bad(mp->what, line, "placed links are forbidden");
        if (!placed) {
            const char *slash = strchr(f[4], '/');
            size_t top = slash ? (size_t)(slash-f[4]) : strlen(f[4]);
            const char *reserved_tree[] = {"BUNDLE", "MANIFEST", "SETTINGS", ".placed"};
            for (size_t i=0;i<sizeof reserved_tree/sizeof *reserved_tree;i++)
                if (strlen(reserved_tree[i])==top && !strncmp(f[4],reserved_tree[i],top))
                    return bad(mp->what,line,"reserved installed-tree path");
            if (!strcmp(f[1],"l") && !link_stays_inside(f[4],f[3]))
                return bad(mp->what,line,"link leaves the product tree");
        }
        const char *prefix = placed ? "placed" : "tree";
        if (!m->n || strncmp(m->v[m->n-1].path, prefix, strlen(prefix))) {
            char *wrapper[] = {"d", "0755", "-", (char *)prefix};
            if (manifest_line(user, line, 1, wrapper, 4)) return 1;
        }
        char *path = tam_path_join(prefix, f[4]);
        char *fields[] = {f[1], f[2], f[3], path};
        int r = path ? manifest_line(user, line, 1, fields, 4) : 1;
        free(path);
        return r;
    }
    if (n != 4) return bad(mp->what, line, "expected: type mode value path");
    tb_entry_t e = {0};
    if (strlen(f[0]) != 1 || !strchr("dfl", f[0][0])) return bad(mp->what, line, "type must be d, f or l");
    e.type = f[0][0];
    if (e.type == 'l') {
        if (strcmp(f[1], "-") != 0) return bad(mp->what, line, "a link's mode must be -");
    } else if (!parse_mode(f[1], &e.mode)) {
        return bad(mp->what, line, "mode must be octal, at most 0777");
    }
    if (e.type == 'f' && !valid_hash(f[2])) return bad(mp->what, line, "file value must be sha256:<64 hex>");
    if (e.type == 'd' && strcmp(f[2], "-") != 0) return bad(mp->what, line, "directory value must be -");
    if (!tam_path_is_safe_rel(f[3]) || reserved(f[3])) return bad(mp->what, line, "unsafe or reserved path");
    if (e.type == 'l' && !link_stays_inside(f[3], f[2])) return bad(mp->what, line, "link leaves the tree");
    if (m->n && strcmp(m->v[m->n - 1].path, f[3]) >= 0) return bad(mp->what, line, "paths are not sorted or repeat");
    const char *slash = strrchr(f[3], '/');
    if (slash) {
        char *parent = strndup(f[3], (size_t)(slash - f[3]));
        const tb_entry_t *pe = parent ? tb_manifest_find(m, parent) : NULL;
        free(parent);
        if (!pe || pe->type != 'd') return bad(mp->what, line, "parent directory is not listed before it");
    }
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 256;
        tb_entry_t *grown = realloc(m->v, cap * sizeof *grown);
        if (!grown) return bad(mp->what, line, "out of memory");
        m->v = grown;
        m->cap = cap;
    }
    e.value = strdup(f[2]);
    e.path = strdup(f[3]);
    if (!e.value || !e.path) return bad(mp->what, line, "out of memory");
    m->v[m->n++] = e;
    return 0;
}

void tb_manifest_free(tb_manifest_t *m) {
    for (size_t i = 0; i < m->n; i++) {
        free(m->v[i].value);
        free(m->v[i].path);
    }
    free(m->v);
    free(m->text);
    memset(m, 0, sizeof *m);
}

static int rec_failed(const char *what, int r, const tam_rec_error_t *err) {
    if (r == -1) {
        if (err->format > TB_FORMAT)
            tb_err("%s is format %d; this tam-bundle %s reads up to format %d. Use a newer tam-bundle.",
                   what, err->format, TB_VERSION, TB_FORMAT);
        else if (err->line)
            tb_err("%s line %zu: %s", what, err->line, err->reason);
        else
            tb_err("%s: %s", what, err->reason);
    }
    return -1;
}

int tb_manifest_parse(const char *text, size_t len, const char *what, tb_manifest_t *m) {
    memset(m, 0, sizeof *m);
    mparse_t mp = {m, what};
    tam_rec_error_t err;
    m->format = !strncmp(text, "format 2\n", 9) ? 2 : 1;
    int r = tam_rec_parse(text, len, m->format == 2 ? 5 : 4, TB_FORMAT, manifest_line, &mp, &err);
    if (r != 0) {
        tb_manifest_free(m);
        return rec_failed(what, r, &err);
    }
    return 0;
}

/* ---- BUNDLE ------------------------------------------------------------ */

typedef struct {
    tb_bundle_t *b;
    const char  *what;
} bparse_t;

static bool push(char ***list, size_t *n, const char *v) {
    char **grown = realloc(*list, (*n + 1) * sizeof **list);
    if (!grown) return false;
    *list = grown;
    return ((*list)[(*n)++] = strdup(v)) != NULL;
}

static int set_once(char **slot, const char *v, const char *what, size_t line) {
    if (*slot) return bad(what, line, "repeated key");
    return (*slot = strdup(v)) ? 0 : bad(what, line, "out of memory");
}

static int bundle_line(void *user, size_t line, int format, char **f, size_t n) {
    bparse_t *bp = user;
    tb_bundle_t *b = bp->b;
    b->format = format;
    if (n != 2) return bad(bp->what, line, "expected: key value");
    const char *k = f[0], *v = f[1];
    if (strcmp(k, "name") == 0) {
        if (!tb_valid_name(v)) return bad(bp->what, line, "invalid name");
        return set_once(&b->name, v, bp->what, line);
    }
    if (strcmp(k, "version") == 0) {
        if (!tb_valid_name(v)) return bad(bp->what, line, "invalid version");
        return set_once(&b->version, v, bp->what, line);
    }
    if (strcmp(k, "hash") == 0) {
        if (!valid_hash(v)) return bad(bp->what, line, "hash must be sha256:<64 hex>");
        return set_once(&b->hash, v, bp->what, line);
    }
    if(!strcmp(k,"buildenv")) {
        if(!valid_hash(v))return bad(bp->what,line,"buildenv must be sha256:<64 hex>");
        return set_once(&b->buildenv,v,bp->what,line);
    }
    if (format == 2 && strcmp(k, "target") == 0)
        return push(&b->targets, &b->ntargets, v) ? 0 : 1;
    if(format==2 && !strcmp(k,"derived-from"))return set_once(&b->derived,v,bp->what,line);
    if(format==2 && !strcmp(k,"edit")) {
        if(!tam_path_is_safe_rel(v))return bad(bp->what,line,"unsafe edited path");
        return push(&b->edits,&b->nedits,v)?0:1;
    }
    if (strcmp(k, "host") == 0) return set_once(&b->host, v, bp->what, line);
    if (strcmp(k, "built") == 0) return set_once(&b->built, v, bp->what, line);
    if (strcmp(k, "source") == 0) {
        if (!strchr(v, ' ')) return bad(bp->what, line, "expected: source <component> <commit>");
        return push(&b->sources, &b->nsources, v) ? 0 : bad(bp->what, line, "out of memory");
    }
    if (strcmp(k, "tool") == 0)
        return push(&b->tools, &b->ntools, v) ? 0 : bad(bp->what, line, "out of memory");
    if (strcmp(k, "after") == 0) {
        if (!tb_valid_step(v)) return bad(bp->what, line, "unknown post-install step");
        return push(&b->after, &b->nafter, v) ? 0 : bad(bp->what, line, "out of memory");
    }
    if (strcmp(k, "needs") == 0) {
        char *copy = strdup(v);
        if (!copy) return bad(bp->what, line, "out of memory");
        char *kind = strtok(copy, " \t"), *target = strtok(NULL, " \t"), *pkgs = strtok(NULL, "");
        bool ok = kind && target && tb_need_valid(kind,target,pkgs?pkgs:"");
        if(ok && format==1) {
            const char *rest=pkgs?pkgs:"";
            while(*rest==' ' || *rest=='\t')rest++;
            if((strcmp(kind,"file") && strcmp(kind,"command")) || !strncmp(rest,">=",2) || !strncmp(rest,"exact ",6))ok=false;
        }
        if (ok) {
            tb_need_t *grown = realloc(b->needs, (b->nneeds + 1) * sizeof *grown);
            ok = grown != NULL;
            if (ok) {
                b->needs = grown;
                tb_need_t *nd = &b->needs[b->nneeds++];
                nd->kind = strdup(kind);
                nd->target = strdup(target);
                while (pkgs && (*pkgs == ' ' || *pkgs == '\t')) pkgs++;
                nd->pkgs = strdup(pkgs ? pkgs : "");
            }
        }
        free(copy);
        return ok ? 0 : bad(bp->what, line, "expected: needs command|file <name or absolute path> [distro=package ...]");
    }
    return bad(bp->what, line, "unknown key");
}

static void free_list(char **list, size_t n) {
    for (size_t i = 0; i < n; i++) free(list[i]);
    free(list);
}

void tb_bundle_free(tb_bundle_t *b) {
    free(b->name); free(b->version); free(b->hash); free(b->host); free(b->built); free(b->buildenv);
    free_list(b->targets, b->ntargets);
    free(b->derived); free_list(b->edits,b->nedits);
    free_list(b->sources, b->nsources);
    free_list(b->tools, b->ntools);
    free_list(b->after, b->nafter);
    for (size_t i = 0; i < b->nneeds; i++) {
        free(b->needs[i].kind); free(b->needs[i].target); free(b->needs[i].pkgs);
    }
    free(b->needs);
    free(b->text);
    memset(b, 0, sizeof *b);
}

int tb_bundle_parse(const char *text, size_t len, const char *what, tb_bundle_t *b) {
    memset(b, 0, sizeof *b);
    bparse_t bp = {b, what};
    tam_rec_error_t err;
    int r = tam_rec_parse(text, len, 2, TB_FORMAT, bundle_line, &bp, &err);
    if (r != 0) {
        tb_bundle_free(b);
        return rec_failed(what, r, &err);
    }
    if (!b->name || !b->version || !b->hash || !b->host) {
        tb_err("%s: name, version, hash and host are required", what);
        tb_bundle_free(b);
        return -1;
    }
    snprintf(b->id, sizeof b->id, "%s-%s-%.12s", b->name, b->version, b->hash + 7);
    return 0;
}

void tb_hash_settings(const char *btext, size_t blen, const char *mtext, size_t mlen,
                    const char *settings, size_t slen, char out[TB_HASH_LEN + 1]) {
    tam_sha256_t ctx;
    tam_sha256_init(&ctx);
    for (const char *p = btext, *end = btext + blen; p < end;) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *next = nl ? nl + 1 : end;
        if (strncmp(p, "hash ", 5) != 0 && strncmp(p, "built ", 6) != 0)
            tam_sha256_update(&ctx, p, (size_t)(next - p));
        p = next;
    }
    tam_sha256_update(&ctx, mtext, mlen);
    if (settings) tam_sha256_update(&ctx, settings, slen);
    uint8_t d[TAM_SHA256_LEN];
    tam_sha256_final(&ctx, d);
    memcpy(out, "sha256:", 7);
    tam_sha256_hex(d, out + 7);
}

void tb_bundle_hash(const char *b, size_t bn, const char *m, size_t mn, char out[TB_HASH_LEN+1]) {
    tb_hash_settings(b, bn, m, mn, NULL, 0, out);
}

int tb_tree_read(int dirfd, const char *dir, tb_bundle_t *b, tb_manifest_t *m) {
    char bpath[4200], mpath[4200];
    snprintf(bpath, sizeof bpath, "%s/BUNDLE", dir);
    snprintf(mpath, sizeof mpath, "%s/MANIFEST", dir);
    size_t blen = 0, mlen = 0;
    char *btext = tb_read_limit(dirfd, bpath, &blen,4U*1024U*1024U);
    if (!btext) {
        tb_err("cannot read %s: %s", bpath, strerror(errno));
        return -1;
    }
    char *mtext = tb_read_limit(dirfd, mpath, &mlen,32U*1024U*1024U);
    if (!mtext) {
        tb_err("cannot read %s: %s", mpath, strerror(errno));
        free(btext);
        return -1;
    }
    if (tb_bundle_parse(btext, blen, bpath, b) != 0) {
        free(btext); free(mtext);
        return -1;
    }
    if (tb_manifest_parse(mtext, mlen, mpath, m) != 0) {
        tb_bundle_free(b);
        free(btext); free(mtext);
        return -1;
    }
    char hash[TB_HASH_LEN + 1];
    size_t slen = 0;
    char *settings = NULL;
    if (b->format == 2) {
        char spath[4200];
        snprintf(spath, sizeof spath, "%s/SETTINGS", dir);
        settings = tb_read_at(dirfd, spath, &slen);
    }
    tb_hash_settings(btext, blen, mtext, mlen, settings, slen, hash);
    bool formats_ok = b->format == m->format && (b->format == 1 || settings);
    free(settings);
    if (!formats_ok || strcmp(hash, b->hash) != 0) {
        tb_err("%s: BUNDLE and MANIFEST do not match the recorded hash", dir);
        tb_bundle_free(b);
        tb_manifest_free(m);
        free(btext); free(mtext);
        return -1;
    }
    b->text = btext;
    b->len = blen;
    m->text = mtext;
    m->len = mlen;
    return 0;
}
