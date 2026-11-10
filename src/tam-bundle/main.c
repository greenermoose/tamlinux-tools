/* tam-bundle command line. */
#include "tb.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char usage_text[] =
    "usage: tam-bundle [options] <command> [arguments]\n"
    "\n"
    "Commands:\n"
    "  install <bundle>             prepare a format 2 directory or .tbundle\n"
    "  apply [--if-pending]         finish the prepared transaction before session use\n"
    "  cancel                       disarm staging or ready work\n"
    "  pack <dir> -o FILE           make a portable format 2 archive\n"
    "  seal <dir> --name N --version V [...]   record a built tree as a bundle\n"
    "  verify <dir>                 check a bundle, kept or live tree against its MANIFEST\n"
    "  needs <dir>                  report missing native commands or files\n"
    "  switch <dir>                 install a bundle and make it live\n"
    "  back [<bundle id>]           make the previous (or named) kept tree live again\n"
    "  status                       live bundle, previous, kept trees, interruptions\n"
    "  check                        find changes in the live tree\n"
    "  where <path>                 which bundle installed a file\n"
    "  keep <bundle id> <reason>    protect a kept tree from prune\n"
    "  unkeep <bundle id>           stop protecting it\n"
    "  prune [--yes]                list removable kept trees; remove them with --yes\n"
    "  recover                      finish or undo an interrupted switch\n"
    "  after                        run the live bundle's post-install steps\n"
    "  build <recipes> <lock> -o <dir> --version V   build a bundle from exact commits\n"
    "  lock <component> <commit>    set one component's commit in a lock file\n"
    "\n"
    "Options:\n"
    "  --root DIR      format 2 installation root (default /)\n"
    "  --format 1|2    seal format (default 1); --target TEXT records a target\n"
    "  --slot DIR      slot directory (default " TB_DEFAULT_SLOT ")\n"
    "  --state DIR     state directory (default by user; see tam-bundle(1))\n"
    "  --dry-run       say what would change, change nothing\n"
    "  --full          hash every file instead of trusting unchanged file stats\n"
    "  --save-edits    save changed live files before switching\n"
    "  --quiet         print only errors\n"
    "  --version, --help\n"
    "\n"
    "seal options: --name N --version V [--host H] [--source \"COMPONENT COMMIT\"]...\n"
    "  [--tool \"NAME VERSION\"]... [--needs \"command|file TARGET [distro=pkg ...]\"]...\n"
    "  [--after STEP]... [--built TIME]\n"
    "build options: -o DIR --version V [--host H] [--repos DIR] [--built TIME];\n"
    "  the bundle is built for --slot's live tree, <slot>/current\n"
    "lock options: [--lock FILE (default tamlinux.lock)] [--url URL] [--repos DIR]\n";

static int usage(const char *why) {
    if (why) tb_err("%s", why);
    fputs(usage_text, why ? stderr : stdout);
    return TB_USAGE;
}

typedef struct {
    const char *slot, *state, *root;
    int format;
    bool if_pending;
    const char *targets[16]; size_t ntargets;
    tb_switch_opts_t sw;
    bool yes;
    /* seal */
    const char *name, *version, *host, *built;
    const char *sources[64], *tools[64], *needs[64], *after[16];
    /* build, lock */
    const char *output, *repos, *lock, *url;
    size_t nsources, ntools, nneeds, nafter;
    /* positional, and where they and --state's value are in argv */
    const char *args[4];
    int argi[4], state_i;
    size_t nargs;
} opts_t;

/* ---- seal -------------------------------------------------------------- */

static int cmd_seal(const opts_t *o) {
    if (o->nargs != 2 || !o->name || !o->version) return usage("seal needs a directory, --name and --version");
    tb_seal_t so = {
        .format = o->format, .targets = o->targets, .ntargets = o->ntargets,
        .name = o->name, .version = o->version, .host = o->host, .built = o->built,
        .sources = o->sources, .nsources = o->nsources, .tools = o->tools, .ntools = o->ntools,
        .needs = o->needs, .nneeds = o->nneeds, .after = o->after, .nafter = o->nafter,
    };
    return tb_seal(o->args[1], &so, NULL);
}

/* ---- build, lock -------------------------------------------------------- */

static int cmd_build(const opts_t *o) {
    if (o->nargs != 3 || !o->output || !o->version)
        return usage("build needs a recipes directory, a lock file, -o DIR and --version");
    if (o->slot[0] != '/') return usage("build needs an absolute --slot");
    char *prefix = tam_path_join(o->slot, "current");
    tb_build_opts_t bo = {
        .recipes = o->args[1], .lock = o->args[2], .output = o->output, .version = o->version,
        .host = o->host, .repos = o->repos, .prefix = prefix, .built = o->built,
    };
    int r = prefix ? tb_build(&bo) : TB_FAIL;
    free(prefix);
    return r;
}

static int cmd_lock(const opts_t *o) {
    if (o->nargs != 3) return usage("lock needs a component and a commit");
    tb_lock_opts_t lo = {
        .file = o->lock ? o->lock : "tamlinux.lock", .url = o->url, .repos = o->repos, .dry_run = o->sw.dry_run,
    };
    return tb_lock_set(o->args[1], o->args[2], &lo);
}

/* ---- Interrupted switches --------------------------------------------- */

/* check, where and after read only the live tree, which an exchange keeps
 * whole, so they answer; but they say that a switch was interrupted. */
static void warn_interrupted(const tb_slot_t *s) {
    tb_intent_t it;
    int have = tb_intent_read(s, &it);
    if (have > 0)
        tb_err("warning: a %s to %s from %s was interrupted; this answer is from the live tree. "
               "Run `tam-bundle recover` to finish or undo it.", it.op, it.to, it.from);
    else if (have < 0)
        tb_err("warning: the switch intent in %s cannot be read; run `tam-bundle status`", s->state);
}

/* ---- verify, needs, check ---------------------------------------------- */

static int cmd_verify(const opts_t *o) {
    if (o->nargs != 2) return usage("verify needs a directory");
    tb_bundle_t b;
    tb_manifest_t m;
    if (tb_tree_read(AT_FDCWD, o->args[1], &b, &m) != 0) return TB_FAIL;
    if (b.format == 2) { tb_bundle_free(&b); tb_manifest_free(&m); return tb_v2_verify(o->args[1]); }
    tb_check_t c = {0};
    int r = tb_tree_check(o->args[1], &m, NULL, true, &c);
    size_t problems = c.edited + c.missing + c.extra;
    if (r == 0 && problems == 0) tb_note("%s: %s verified, %zu entries", o->args[1], b.id, m.n);
    else if (r == 0) tb_err("%s: %zu edited, %zu missing, %zu extra", o->args[1], c.edited, c.missing, c.extra);
    tb_check_free(&c);
    tb_bundle_free(&b);
    tb_manifest_free(&m);
    return r == 0 && problems == 0 ? TB_OK : TB_FAIL;
}

static int cmd_needs(const opts_t *o) {
    if (o->nargs != 2) return usage("needs needs a directory");
    tb_bundle_t b;
    tb_manifest_t m;
    if (tb_tree_read(AT_FDCWD, o->args[1], &b, &m) != 0) return TB_FAIL;
    size_t missing = tb_needs_check(&b, true);
    if (!missing) tb_note("%s: all %zu native needs are present", b.id, b.nneeds);
    tb_bundle_free(&b);
    tb_manifest_free(&m);
    return missing ? TB_FAIL : TB_OK;
}

static int cmd_check(tb_slot_t *s, const opts_t *o) {
    warn_interrupted(s);
    char cur[256];
    if (tb_current_id(s, cur) != 0) return TB_FAIL;
    if (strcmp(cur, "-") == 0) {
        tb_note("%s has no live bundle", s->dir);
        return TB_OK;
    }
    tb_bundle_t b;
    tb_manifest_t m;
    if (tb_tree_read(s->fd, "current", &b, &m) != 0) return TB_FAIL;
    char leaf[300];
    snprintf(leaf, sizeof leaf, "stat/%s", cur);
    char *stat_path = tb_state_path(s, leaf), *live = tam_path_join(s->dir, "current");
    tb_stats_t st;
    tb_stats_read(stat_path, &st);
    tb_check_t c = {0};
    int r = tb_tree_check(live, &m, &st, o->sw.full, &c);
    size_t problems = c.edited + c.missing + c.extra;
    if (r == 0 && !problems) tb_note("%s/current matches %s", s->dir, cur);
    else if (r == 0) tb_err("%s/current: %zu edited, %zu missing, %zu extra", s->dir, c.edited, c.missing, c.extra);
    tb_check_free(&c);
    tb_stats_free(&st);
    free(stat_path);
    free(live);
    tb_bundle_free(&b);
    tb_manifest_free(&m);
    return r == 0 && !problems ? TB_OK : TB_FAIL;
}

/* ---- status, where ----------------------------------------------------- */

static char **kept_ids(const tb_slot_t *s, size_t *n) {
    *n = 0;
    int kfd = openat(s->fd, "kept", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *dir = kfd >= 0 ? fdopendir(kfd) : NULL;
    if (!dir) {
        if (kfd >= 0) close(kfd);
        return NULL;
    }
    char **ids = NULL;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char **grown = realloc(ids, (*n + 1) * sizeof *ids);
        if (!grown) break;
        ids = grown;
        ids[(*n)++] = strdup(de->d_name);
    }
    closedir(dir);
    return ids;
}

static void free_ids(char **ids, size_t n) {
    for (size_t i = 0; i < n; i++) free(ids[i]);
    free(ids);
}

/* The last line of each session.<uid>: results of session steps that a
 * root run left for that user. */
static void print_sessions(const tb_slot_t *s) {
    DIR *dir = opendir(s->state);
    if (!dir) return;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "session.", 8) != 0) continue;
        size_t len = 0;
        char *text = tb_read_at(dirfd(dir), de->d_name, &len);
        char *last = NULL;
        if (text && len > 1) {
            text[len - 1] = '\0';
            last = strrchr(text, '\n');
            last = last ? last + 1 : text;
        }
        printf("session:   uid %s: %s\n", de->d_name + 8, last ? last : "(no result yet)");
        free(text);
    }
    closedir(dir);
}

static int cmd_status(tb_slot_t *s) {
    char cur[256];
    if (tb_current_id(s, cur) != 0) return TB_FAIL;
    printf("slot:      %s\nstate:     %s\n", s->dir, s->state);
    printf("live:      %s\n", strcmp(cur, "-") == 0 ? "(none)" : cur);
    tb_keep_t k;
    if (tb_keep_read(s, &k) != 0) return TB_FAIL;
    const char *prev = tb_keep_previous(&k);
    printf("previous:  %s\n", prev ? prev : "(none)");
    size_t n;
    char **ids = kept_ids(s, &n);
    for (size_t i = 0; i < n; i++) {
        const char *reason = tb_keep_reason(&k, ids[i]);
        printf("kept:      %s (%s)\n", ids[i], reason ? reason : "not protected; prune removes it");
    }
    free_ids(ids, n);
    tb_keep_free(&k);
    int result = TB_OK;
    tb_intent_t it;
    int have = tb_intent_read(s, &it);
    if (have != 0) {
        if (have > 0)
            printf("interrupted: %s to %s from %s; run `tam-bundle recover`\n", it.op, it.to, it.from);
        result = TB_INTERRUPTED;
    }
    char *log = tb_state_path(s, "log");
    size_t len;
    char *text = log ? tb_read_at(AT_FDCWD, log, &len) : NULL;
    if (text && len > 1) {
        text[len - 1] = '\0';
        char *last = strrchr(text, '\n');
        printf("last:      %s\n", last ? last + 1 : text);
    }
    free(text);
    free(log);
    print_sessions(s);
    return result;
}

static int cmd_where(tb_slot_t *s, const opts_t *o) {
    if (o->nargs != 2) return usage("where needs a path");
    warn_interrupted(s);
    const char *path = o->args[1];
    char *live = tam_path_join(s->dir, "current");
    size_t llen = strlen(live);
    bool inside = strncmp(path, live, llen) == 0 && path[llen] == '/';
    const char *rel = inside ? path + llen + 1 : NULL;
    free(live);
    if (!rel || !tam_path_is_safe_rel(rel)) {
        tb_err("%s is not in the live tree of slot %s", path, s->dir);
        return TB_FAIL;
    }
    tb_bundle_t b;
    tb_manifest_t m;
    if (tb_tree_read(s->fd, "current", &b, &m) != 0) return TB_FAIL;
    const tb_entry_t *e = tb_manifest_find(&m, rel);
    int result = TB_FAIL;
    if (!e) {
        tb_err("%s is in the live tree but not in %s's MANIFEST", path, b.id);
    } else {
        printf("%s\n  bundle:  %s (%s %s)\n  record:  %c %s\n", path, b.id, b.name, b.version, e->type, e->value);
        result = TB_OK;
    }
    tb_bundle_free(&b);
    tb_manifest_free(&m);
    return result;
}

/* ---- keep, unkeep, prune ----------------------------------------------- */

static bool kept_exists(const tb_slot_t *s, const char *id) {
    char rel[300];
    snprintf(rel, sizeof rel, "kept/%s", id);
    struct stat st;
    return tb_valid_name(id) && fstatat(s->fd, rel, &st, AT_SYMLINK_NOFOLLOW) == 0;
}

static int cmd_keep(tb_slot_t *s, const opts_t *o, bool keep) {
    if ((keep && o->nargs != 3) || (!keep && o->nargs != 2))
        return usage(keep ? "keep needs a bundle id and a reason" : "unkeep needs a bundle id");
    const char *id = o->args[1];
    if (keep && strcmp(o->args[2], "previous") == 0) return usage("\"previous\" is set by switch and back");
    if ((o->sw.dry_run ? tb_slot_plan_lock(s) : tb_slot_lock(s)) != 0) return TB_FAIL;
    int rr = tb_recover_first(s, o->sw.dry_run);
    if (rr != TB_OK) return rr;
    if (keep && !kept_exists(s, id)) {
        tb_err("%s is not kept in %s/kept", id, s->dir);
        return TB_FAIL;
    }
    tb_keep_t k;
    if (tb_keep_read(s, &k) != 0) return TB_FAIL;
    if (keep) tb_keep_set(&k, id, o->args[2]);
    else tb_keep_drop(&k, id);
    int r = o->sw.dry_run ? 0 : tb_keep_write(s, &k);
    tb_keep_free(&k);
    if (r != 0) return TB_FAIL;
    if (!o->sw.dry_run) tb_log(s, keep ? "keep %s %s" : "unkeep %s", id, keep ? o->args[2] : "");
    tb_note("%s%s %s", o->sw.dry_run ? "would " : "", keep ? "keep" : "no longer keep", id);
    return TB_OK;
}

static int cmd_prune(tb_slot_t *s, const opts_t *o) {
    if ((o->sw.dry_run ? tb_slot_plan_lock(s) : tb_slot_lock(s)) != 0) return TB_FAIL;
    int rr = tb_recover_first(s, o->sw.dry_run);
    if (rr != TB_OK) return rr;
    tb_keep_t k;
    if (tb_keep_read(s, &k) != 0) return TB_FAIL;
    size_t n;
    char **ids = kept_ids(s, &n);
    size_t removable = 0;
    int result = TB_OK;
    for (size_t i = 0; i < n; i++) {
        if (tb_keep_reason(&k, ids[i])) continue;
        removable++;
        char rel[300];
        snprintf(rel, sizeof rel, "kept/%s", ids[i]);
        char *dir = tam_path_join(s->dir, rel);
        long long bytes = tb_tree_bytes(dir);
        free(dir);
        if (!o->yes || o->sw.dry_run) {
            printf("removable: %s (%.1f MB)\n", ids[i], (double)bytes / 1e6);
            continue;
        }
        if (tb_remove_tree(s, rel) == 0) {
            char leaf[300];
            snprintf(leaf, sizeof leaf, "stat/%s", ids[i]);
            char *sp = tb_state_path(s, leaf);
            if (sp) unlink(sp);
            free(sp);
            tb_log(s, "pruned %s", ids[i]);
            tb_note("removed %s", ids[i]);
        } else {
            tb_err("cannot remove %s/%s: %s", s->dir, rel, strerror(errno));
            result = TB_FAIL;
        }
    }
    free_ids(ids, n);
    tb_keep_free(&k);
    if (!removable) tb_note("nothing to prune in %s", s->dir);
    else if (!o->yes || o->sw.dry_run)
        tb_note("sizes are the files' allocated size; on Btrfs, extents shared with other trees are "
                "freed only when no tree uses them, so less space may come back. "
                "Run `tam-bundle prune --yes` to remove them.");
    return result;
}

static int cmd_after(tb_slot_t *s) {
    warn_interrupted(s);
    char cur[256];
    if (tb_current_id(s, cur) != 0) return TB_FAIL;
    if (strcmp(cur, "-") == 0) {
        tb_note("%s has no live bundle", s->dir);
        return TB_OK;
    }
    tb_bundle_t b;
    tb_manifest_t m;
    if (tb_tree_read(s->fd, "current", &b, &m) != 0) return TB_FAIL;
    int r = tb_run_after(s, &b);
    tb_bundle_free(&b);
    tb_manifest_free(&m);
    return r;
}

/* ---- privilege ---------------------------------------------------------- */

/* An absolute form of `path`; caller frees. `must_exist` resolves links. */
static char *absolute(const char *path, bool must_exist) {
    if (must_exist) return realpath(path, NULL);
    if (path[0] == '/') return strdup(path);
    char cwd[4096];
    return getcwd(cwd, sizeof cwd) ? tam_path_join(cwd, path) : NULL;
}

/* The same command again through pkexec, which starts it in /root with a
 * cleared environment: paths given relative to here are made absolute. */
static int elevate(const opts_t *o, int argc, char **argv) {
    char **args = calloc((size_t)argc, sizeof *args);
    char *bundle = NULL, *state = NULL;
    int r = TB_FAIL;
    if (!args) return TB_FAIL;
    for (int i = 1; i < argc; i++) args[i - 1] = argv[i];
    if (strcmp(o->args[0], "switch") == 0 && o->nargs == 2) {
        bundle = absolute(o->args[1], true);
        if (!bundle) {
            tb_err("cannot find bundle directory %s: %s", o->args[1], strerror(errno));
            goto out;
        }
        args[o->argi[1] - 1] = bundle;
    }
    if (o->state) {
        state = absolute(o->state, false);
        if (!state) goto out;
        args[o->state_i - 1] = state;
    }
    r = tb_pkexec(args, o->slot, state);
out:
    free(bundle);
    free(state);
    free(args);
    return r;
}

/* ---- main -------------------------------------------------------------- */

static bool add(const char **list, size_t *n, size_t cap, const char *v) {
    if (*n >= cap) return false;
    list[(*n)++] = v;
    return true;
}

int main(int argc, char **argv) {
    opts_t o = {0};
    o.slot = TB_DEFAULT_SLOT;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define VALUE() do { if (!v) return usage("an option is missing its value"); i++; } while (0)
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(NULL);
            return TB_OK;
        }
        /* Alone, --version prints this program's version; with a value it is
         * seal's bundle version. */
        else if (strcmp(a, "--version") == 0 && argc == 2) {
            printf("tam-bundle %s (record format %d)\n", TB_VERSION, TB_FORMAT);
            return TB_OK;
        }
        else if (strcmp(a, "--root") == 0) { VALUE(); o.root = v; }
        else if (strcmp(a, "--format") == 0) { VALUE(); if(strcmp(v,"1") && strcmp(v,"2")) return usage("format must be 1 or 2"); o.format=atoi(v); }
        else if (strcmp(a, "--target") == 0) { VALUE(); if(!add(o.targets,&o.ntargets,16,v)) return usage("too many targets"); }
        else if (strcmp(a, "--if-pending") == 0) o.if_pending=true;
        else if (strcmp(a, "--slot") == 0) { VALUE(); o.slot = v; }
        else if (strcmp(a, "--state") == 0) { VALUE(); o.state = v; o.state_i = i; }
        else if (strcmp(a, "--dry-run") == 0) o.sw.dry_run = true;
        else if (strcmp(a, "--full") == 0) o.sw.full = true;
        else if (strcmp(a, "--save-edits") == 0) o.sw.save_edits = true;
        else if (strcmp(a, "--quiet") == 0) tb_quiet = true;
        else if (strcmp(a, "--yes") == 0) o.yes = true;
        else if (strcmp(a, "--name") == 0) { VALUE(); o.name = v; }
        else if (strcmp(a, "--version") == 0) { VALUE(); o.version = v; }
        else if (strcmp(a, "--host") == 0) { VALUE(); o.host = v; }
        else if (strcmp(a, "--built") == 0) { VALUE(); o.built = v; }
        else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) { VALUE(); o.output = v; }
        else if (strcmp(a, "--repos") == 0) { VALUE(); o.repos = v; }
        else if (strcmp(a, "--lock") == 0) { VALUE(); o.lock = v; }
        else if (strcmp(a, "--url") == 0) { VALUE(); o.url = v; }
        else if (strcmp(a, "--source") == 0) { VALUE(); if (!add(o.sources, &o.nsources, 64, v)) return usage("too many --source"); }
        else if (strcmp(a, "--tool") == 0) { VALUE(); if (!add(o.tools, &o.ntools, 64, v)) return usage("too many --tool"); }
        else if (strcmp(a, "--needs") == 0) { VALUE(); if (!add(o.needs, &o.nneeds, 64, v)) return usage("too many --needs"); }
        else if (strcmp(a, "--after") == 0) { VALUE(); if (!add(o.after, &o.nafter, 16, v)) return usage("too many --after"); }
        else if (a[0] == '-' && a[1] == '-') { tb_err("unknown option %s", a); return TB_USAGE; }
        else if (o.nargs < 4) { o.argi[o.nargs] = i; o.args[o.nargs++] = a; }
        else return usage("too many arguments");
#undef VALUE
    }
    if (!o.nargs) return usage("no command given");
    const char *cmd = o.args[0];

    if (!strcmp(cmd, "pack")) {
        if (o.nargs != 2 || !o.output) return usage("pack needs a directory and -o ARCHIVE");
        if (tb_v2_verify(o.args[1])) return TB_FAIL;
        char *output = absolute(o.output, false);
        int r = output ? tb_archive_pack(o.args[1], output) : -1;
        free(output); return r ? TB_FAIL : TB_OK;
    }
    if (!strcmp(cmd,"seal")) return cmd_seal(&o);
    if (!strcmp(cmd,"verify")) return cmd_verify(&o);
    if (o.root || !strcmp(cmd,"install") || !strcmp(cmd,"prepare") || !strcmp(cmd,"apply") || !strcmp(cmd,"cancel") || !strcmp(cmd,"restore")) {
        if (o.nargs > 2) return usage("too many transaction arguments");
        return tb_v2_main(o.root ? o.root : "/", cmd, o.nargs==2 ? o.args[1] : NULL, o.sw.dry_run, o.if_pending, o.yes);
    }
    if (strcmp(cmd, "seal") == 0) return cmd_seal(&o);
    if (strcmp(cmd, "verify") == 0) return cmd_verify(&o);
    if (strcmp(cmd, "needs") == 0) return cmd_needs(&o);
    if (strcmp(cmd, "build") == 0) return cmd_build(&o);
    if (strcmp(cmd, "lock") == 0) return cmd_lock(&o);

    static const char *const changing[] = {"switch", "back", "recover", "keep", "unkeep", "prune", NULL};
    static const char *const reading[] = {"status", "check", "where", "after", NULL};
    bool changes = false, known = false;
    for (int i = 0; changing[i]; i++) if (strcmp(cmd, changing[i]) == 0) changes = known = true;
    for (int i = 0; reading[i]; i++) if (strcmp(cmd, reading[i]) == 0) known = true;
    if (!known) {
        tb_err("unknown command %s", cmd);
        return TB_USAGE;
    }
    if (changes && !o.sw.dry_run && o.slot[0] == '/' && geteuid() != 0 &&
        (tb_owned_by_root(o.slot) || tb_test_pkexec()))
        return elevate(&o, argc, argv);
    if (changes && !o.sw.dry_run && access(o.slot, F_OK) == 0 && access(o.slot, W_OK) != 0) {
        tb_err("slot %s is not writable by you; run tam-bundle with pkexec or sudo", o.slot);
        return TB_FAIL;
    }

    tb_slot_t s;
    bool create = changes && !o.sw.dry_run && (strcmp(cmd, "switch") == 0);
    if (tb_slot_open(&s, o.slot, o.state, create, o.sw.dry_run && strcmp(cmd, "switch") == 0) != 0) {
        tb_slot_close(&s);
        return TB_FAIL;
    }
    int r;
    if (strcmp(cmd, "switch") == 0) {
        r = o.nargs == 2 ? tb_switch(&s, o.args[1], &o.sw) : usage("switch needs a bundle directory");
    } else if (strcmp(cmd, "back") == 0) {
        r = o.nargs <= 2 ? tb_back(&s, o.nargs == 2 ? o.args[1] : NULL, &o.sw) : usage("back takes at most one bundle id");
    } else if (strcmp(cmd, "recover") == 0) {
        r = (o.sw.dry_run ? tb_slot_plan_lock(&s) : tb_slot_lock(&s)) == 0 ? tb_recover(&s, o.sw.dry_run, NULL) : TB_FAIL;
        if (r == TB_OK) tb_note("%s is clean", s.dir);
    } else if (strcmp(cmd, "keep") == 0) r = cmd_keep(&s, &o, true);
    else if (strcmp(cmd, "unkeep") == 0) r = cmd_keep(&s, &o, false);
    else if (strcmp(cmd, "prune") == 0) r = cmd_prune(&s, &o);
    else if (strcmp(cmd, "status") == 0) r = cmd_status(&s);
    else if (strcmp(cmd, "check") == 0) r = cmd_check(&s, &o);
    else if (strcmp(cmd, "where") == 0) r = cmd_where(&s, &o);
    else r = cmd_after(&s);
    tb_slot_close(&s);
    return r;
}
