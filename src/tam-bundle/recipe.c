/* Recipes: what to build, as data.
 *
 * <dir>/bundle names the bundle:
 *   format 1
 *   name tamlinux
 *   host any                                   (optional)
 *   needs command quickshell arch=quickshell   (any number)
 *   index man                                  (optional: build the manual index)
 *   after shell-restart                        (any number)
 *   file notes/version.txt share/tamlinux/version-notes.txt
 *
 * <dir>/<component>.recipe says how to build one component:
 *   format 1
 *   component tam
 *   build make
 *   output bundle | build-only                 (default bundle)
 *   make-vars LIBTAM_PREFIX=@libtam@           (any number; NAME=value ...)
 *   after-build libtam                         (any number)
 *   make-targets all display session            (optional)
 *   install-targets install install-display install-session
 *
 * "@<component>@" in a make variable is that build-only component's private
 * prefix; it must be named in after-build. */
#include "tb.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Set by tam-bundle itself; a recipe may not set them. */
static const char *const reserved_vars[] = {"PREFIX", "DESTDIR", "SOURCE_DATE_EPOCH", "PATH", "SHELL", NULL};

static int bad(const char *what, size_t line, const char *why) {
    tb_err("%s line %zu: %s", what, line, why);
    return 1;
}

static bool push(char ***list, size_t *n, const char *v) {
    char **grown = realloc(*list, (*n + 1) * sizeof **list);
    if (!grown) return false;
    *list = grown;
    return ((*list)[(*n)++] = strdup(v)) != NULL;
}

static void free_list(char **list, size_t n) {
    for (size_t i = 0; i < n; i++) free(list[i]);
    free(list);
}

static bool valid_var(const char *tok) {
    const char *eq = strchr(tok, '=');
    if (!eq || eq == tok) return false;
    for (const char *p = tok; p < eq; p++) {
        char c = *p;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (p > tok && c >= '0' && c <= '9');
        if (!ok) return false;
    }
    for (int i = 0; reserved_vars[i]; i++)
        if ((size_t)(eq - tok) == strlen(reserved_vars[i]) && strncmp(tok, reserved_vars[i], (size_t)(eq - tok)) == 0)
            return false;
    return true;
}

/* ---- The bundle file --------------------------------------------------- */

typedef struct {
    tb_recipes_t *r;
    const char   *what;
} bparse_t;

static int bundle_line(void *user, size_t line, int format, char **f, size_t n) {
    bparse_t *bp = user;
    tb_recipes_t *r = bp->r;
    r->format=format;
    if (n != 2) return bad(bp->what, line, "expected: key value");
    if (strcmp(f[0], "name") == 0) {
        if (r->name) return bad(bp->what, line, "repeated name");
        if (!tb_valid_name(f[1])) return bad(bp->what, line, "invalid name");
        return (r->name = strdup(f[1])) ? 0 : 1;
    }
    if (strcmp(f[0], "host") == 0) {
        if (r->host) return bad(bp->what, line, "repeated host");
        return (r->host = strdup(f[1])) ? 0 : 1;
    }
    if (strcmp(f[0], "needs") == 0) return push(&r->needs, &r->nneeds, f[1]) ? 0 : 1;
    if(format==2 && !strcmp(f[0],"target"))return r->ntargets<16 && push(&r->targets,&r->ntargets,f[1])?0:1;
    if(format==2 && !strcmp(f[0],"place")) {
        if(strncmp(f[1],"usr/",4) || !tam_path_is_safe_rel(f[1]) || r->nplaced>=64)
            return bad(bp->what,line,"place takes a conventional usr/ path");
        return push(&r->placed,&r->nplaced,f[1])?0:1;
    }
    if (strcmp(f[0], "file") == 0) {
        char *space = strchr(f[1], ' ');
        if (!space) return bad(bp->what, line, "expected: file <packaging path> <tree path>");
        *space++ = '\0';
        while (*space == ' ') space++;
        if (!tam_path_is_safe_rel(f[1]) || !tam_path_is_safe_rel(space) ||
            strcmp(space, "BUNDLE") == 0 || strcmp(space, "MANIFEST") == 0)
            return bad(bp->what, line, "file needs safe relative paths, not BUNDLE or MANIFEST");
        if(format==2 && strcmp(space,"SETTINGS") && strncmp(space,"tree/",5) && strncmp(space,"placed/usr/",11))
            return bad(bp->what,line,"format 2 file destinations are SETTINGS, tree/ or placed/usr/");
        tb_recipe_file_t *grown = realloc(r->files, (r->nfiles + 1) * sizeof *grown);
        if (!grown) return 1;
        r->files = grown;
        tb_recipe_file_t *file = &r->files[r->nfiles++];
        file->source = strdup(f[1]);
        file->path = strdup(space);
        return file->source && file->path ? 0 : 1;
    }
    if (strcmp(f[0], "index") == 0) {
        if (strcmp(f[1], "man") != 0) return bad(bp->what, line, "index takes man");
        if (r->index_man) return bad(bp->what, line, "repeated index man");
        r->index_man = true;
        return 0;
    }
    if (strcmp(f[0], "after") == 0) {
        if (!tb_valid_step(f[1])) return bad(bp->what, line, "unknown post-install step");
        return push(&r->after, &r->nafter, f[1]) ? 0 : 1;
    }
    return bad(bp->what, line, "unknown key (name, host, needs, index, after, file)");
}

/* ---- A recipe ---------------------------------------------------------- */

typedef struct {
    tb_recipe_t *rc;
    const char  *what;
    bool         seen_component, seen_build, seen_output;
} rparse_t;

static int each_token(const char *rest, rparse_t *rp, size_t line, bool vars) {
    char *copy = strdup(rest);
    if (!copy) return 1;
    int r = 0;
    for (char *save = NULL, *t = strtok_r(copy, " \t", &save); t && !r; t = strtok_r(NULL, " \t", &save)) {
        if (vars) {
            if (!valid_var(t)) r = bad(rp->what, line, "make-vars takes NAME=value words, not PREFIX, DESTDIR or SOURCE_DATE_EPOCH");
            else if (!push(&rp->rc->vars, &rp->rc->nvars, t)) r = 1;
        } else {
            if (!tb_valid_name(t)) r = bad(rp->what, line, "after-build takes component names");
            else if (!push(&rp->rc->deps, &rp->rc->ndeps, t)) r = 1;
        }
    }
    free(copy);
    return r;
}

static int recipe_line(void *user, size_t line, int format, char **f, size_t n) {
    rparse_t *rp = user;
    (void)format;
    if (n != 2) return bad(rp->what, line, "expected: key value");
    const char *k = f[0], *v = f[1];
    if (strcmp(k, "component") == 0) {
        if (rp->seen_component) return bad(rp->what, line, "repeated component");
        rp->seen_component = true;
        if (strcmp(v, rp->rc->name) != 0) return bad(rp->what, line, "the component must match the file name");
        return 0;
    }
    if (strcmp(k, "build") == 0) {
        if (rp->seen_build) return bad(rp->what, line, "repeated build");
        rp->seen_build = true;
        return strcmp(v, "make") == 0 ? 0 : bad(rp->what, line, "the only build kind is make");
    }
    if (strcmp(k, "output") == 0) {
        if (rp->seen_output) return bad(rp->what, line, "repeated output");
        rp->seen_output = true;
        if (strcmp(v, "bundle") == 0) rp->rc->build_only = false;
        else if (strcmp(v, "build-only") == 0) rp->rc->build_only = true;
        else return bad(rp->what, line, "output is bundle or build-only");
        return 0;
    }
    if (strcmp(k, "make-vars") == 0) return each_token(v, rp, line, true);
    if(!strcmp(k,"build-needs")) {
        char pkg[128],op[8],version[80],extra;
        int n=sscanf(v,"%127s %7s %79s %c",pkg,op,version,&extra);
        if((n!=1 && (n!=3 || strcmp(op,">="))) || !tb_valid_name(pkg) || rp->rc->nbuild_needs>=64)
            return bad(rp->what,line,"build-needs takes a native package name and optional >= version");
        return push(&rp->rc->build_needs,&rp->rc->nbuild_needs,v)?0:1;
    }
    if (strcmp(k, "after-build") == 0) return each_token(v, rp, line, false);
    if (strcmp(k, "make-targets") == 0 || strcmp(k, "install-targets") == 0) {
        bool install = strcmp(k, "install-targets") == 0;
        char *copy = strdup(v);
        if (!copy) return 1;
        int result = 0;
        for (char *save = NULL, *t = strtok_r(copy, " \t", &save); t && !result; t = strtok_r(NULL, " \t", &save)) {
            if (!tb_valid_name(t) || t[0] == '-') result = bad(rp->what, line, "targets must be names, never make options or variable assignments");
            else if (!push(install ? &rp->rc->installs : &rp->rc->targets,
                           install ? &rp->rc->ninstalls : &rp->rc->ntargets, t)) result = 1;
        }
        free(copy);
        return result;
    }
    return bad(rp->what, line, "unknown key (component, build, output, make-vars, after-build, make-targets, install-targets)");
}

static void recipe_free(tb_recipe_t *rc) {
    free(rc->name);
    free(rc->text);
    free_list(rc->vars, rc->nvars);
    free_list(rc->deps, rc->ndeps);
    free_list(rc->targets, rc->ntargets);
    free_list(rc->installs, rc->ninstalls);
    free_list(rc->build_needs,rc->nbuild_needs);
    memset(rc, 0, sizeof *rc);
}

void tb_recipes_free(tb_recipes_t *r) {
    free(r->name);
    free(r->host);
    free(r->text); free_list(r->targets,r->ntargets); free_list(r->placed,r->nplaced);
    free_list(r->needs, r->nneeds);
    free_list(r->after, r->nafter);
    for (size_t i = 0; i < r->nfiles; i++) {
        free(r->files[i].source);
        free(r->files[i].path);
    }
    free(r->files);
    for (size_t i = 0; i < r->n; i++) recipe_free(&r->v[i]);
    free(r->v);
    memset(r, 0, sizeof *r);
}

static int parse_failed(const char *what, int r, const tam_rec_error_t *err) {
    if (r == -1) {
        if (err->format > TB_FORMAT)
            tb_err("%s is format %d; this tam-bundle %s reads up to format %d. Use a newer tam-bundle.",
                   what, err->format, TB_VERSION, TB_FORMAT);
        else if (err->line) tb_err("%s line %zu: %s", what, err->line, err->reason);
        else tb_err("%s: %s", what, err->reason);
    }
    return -1;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static tb_recipe_t *find(tb_recipe_t *v, size_t n, const char *name) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(v[i].name, name) == 0) return &v[i];
    return NULL;
}

/* Every "@name@" in a value must name a build-only component in deps. */
static int check_refs(const tb_recipe_t *rc, tb_recipe_t *all, size_t n) {
    for (size_t i = 0; i < rc->nvars; i++) {
        const char *p = strchr(rc->vars[i], '=') + 1;
        while ((p = strchr(p, '@')) != NULL) {
            const char *end = strchr(p + 1, '@');
            if (!end) {
                tb_err("%s.recipe: unmatched @ in %s", rc->name, rc->vars[i]);
                return -1;
            }
            char ref[128];
            snprintf(ref, sizeof ref, "%.*s", (int)(end - p - 1), p + 1);
            bool listed = false;
            for (size_t d = 0; d < rc->ndeps; d++) listed |= strcmp(rc->deps[d], ref) == 0;
            const tb_recipe_t *dep = find(all, n, ref);
            if (!listed || !dep || !dep->build_only) {
                tb_err("%s.recipe: @%s@ must name a build-only component listed in after-build", rc->name, ref);
                return -1;
            }
            p = end + 1;
        }
    }
    return 0;
}

int tb_recipes_read(const char *dir, tb_recipes_t *r) {
    memset(r, 0, sizeof *r);
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) {
        tb_err("cannot open recipes %s: %s", dir, strerror(errno));
        return -1;
    }
    tam_rec_error_t err;
    size_t len;
    char what[4200];

    snprintf(what, sizeof what, "%s/bundle", dir);
    char *text = tb_read_limit(dfd, "bundle", &len,1024U*1024U);
    if (!text) {
        tb_err("cannot read %s: %s", what, strerror(errno));
        close(dfd);
        return -1;
    }
    bparse_t bp = {r, what};
    int pr = tam_rec_parse(text, len, 2, TB_FORMAT, bundle_line, &bp, &err);
    r->text=text; r->len=len;
    if (pr != 0 || !r->name) {
        if (pr == 0) tb_err("%s: name is required", what);
        else parse_failed(what, pr, &err);
        close(dfd);
        tb_recipes_free(r);
        return -1;
    }

    /* The recipe files, in name order. */
    char **files = NULL;
    size_t nfiles = 0;
    DIR *d = fdopendir(dup(dfd));
    struct dirent *de;
    while (d && (de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        if (n > 7 && strcmp(de->d_name + n - 7, ".recipe") == 0) push(&files, &nfiles, de->d_name);
    }
    if (d) closedir(d);
    if (nfiles > 1) qsort(files, nfiles, sizeof *files, cmp_str);

    tb_recipe_t *v = calloc(nfiles ? nfiles : 1, sizeof *v);
    size_t n = 0;
    int result = v ? 0 : -1;
    for (size_t i = 0; i < nfiles && result == 0; i++) {
        tb_recipe_t *rc = &v[n];
        rc->name = strndup(files[i], strlen(files[i]) - 7);
        snprintf(what, sizeof what, "%s/%s", dir, files[i]);
        if (!tb_valid_name(rc->name)) {
            tb_err("%s: the file name is not a valid component name", what);
            recipe_free(rc);
            result = -1;
            break;
        }
        rc->text = tb_read_limit(dfd, files[i], &rc->len,1024U*1024U);
        if (!rc->text) {
            tb_err("cannot read %s: %s", what, strerror(errno));
            recipe_free(rc);
            result = -1;
            break;
        }
        rparse_t rp = {rc, what, false, false, false};
        pr = tam_rec_parse(rc->text, rc->len, 2, TB_FORMAT, recipe_line, &rp, &err);
        if (pr != 0 || !rp.seen_component || !rp.seen_build) {
            if (pr == 0) tb_err("%s: component and build are required", what);
            else parse_failed(what, pr, &err);
            recipe_free(rc);
            result = -1;
            break;
        }
        n++;
    }
    free_list(files, nfiles);
    close(dfd);
    if (result == 0 && n == 0) {
        tb_err("%s has no .recipe files", dir);
        result = -1;
    }
    for (size_t i = 0; i < n && result == 0; i++) {
        for (size_t k = 0; k < v[i].ndeps && result == 0; k++)
            if (!find(v, n, v[i].deps[k])) {
                tb_err("%s.recipe: after-build names %s, which has no recipe", v[i].name, v[i].deps[k]);
                result = -1;
            }
        if (result == 0) result = check_refs(&v[i], v, n);
    }

    /* Build order: repeatedly take the first recipe, by name, whose
     * after-build components are all placed. */
    if (result == 0) {
        r->v = calloc(n, sizeof *r->v);
        bool *placed = calloc(n, sizeof *placed);
        if (!r->v || !placed) result = -1;
        while (result == 0 && r->n < n) {
            size_t pick = n;
            for (size_t i = 0; i < n && pick == n; i++) {
                if (placed[i]) continue;
                bool ready = true;
                for (size_t k = 0; k < v[i].ndeps && ready; k++)
                    ready = placed[find(v, n, v[i].deps[k]) - v];
                if (ready) pick = i;
            }
            if (pick == n) {
                tb_err("%s: after-build lines form a cycle", dir);
                result = -1;
                break;
            }
            placed[pick] = true;
            r->v[r->n++] = v[pick];
        }
        free(placed);
    }
    if (result != 0) {
        /* Recipes moved to r->v are freed there; the rest here. */
        for (size_t i = 0; i < n; i++)
            if (!find(r->v, r->n, v[i].name)) recipe_free(&v[i]);
        free(v);
        tb_recipes_free(r);
        return -1;
    }
    free(v);
    return 0;
}
