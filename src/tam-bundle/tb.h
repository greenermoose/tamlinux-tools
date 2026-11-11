/* tam-bundle: install a named, versioned set of real files into a slot,
 * record it, and switch between versions with rollback.
 * Design: docs/plans/tam-bundle.md. */
#ifndef TB_H
#define TB_H

#include <tam.h>
#include <stdio.h>
#include <sys/types.h>

#define TB_VERSION "0.10.0"
#define TB_FORMAT 2               /* newest record format this program reads */
#define TB_DEFAULT_SLOT "/usr/lib/tamlinux"
#define TB_SYSTEM_STATE "/var/lib/tam-bundle"
#define TB_HASH_LEN (7 + TAM_SHA256_HEX_LEN) /* "sha256:" + hex */

/* Exit codes. */
enum {
    TB_OK = 0,
    TB_FAIL = 1,        /* refused, or a check found problems */
    TB_USAGE = 2,
    TB_AFTER_FAILED = 3, /* switched, but a post-install step failed */
    TB_INTERRUPTED = 4,  /* an interrupted switch needs `recover` */
};

/* ---- Records (meta.c) -------------------------------------------------- */

typedef struct {
    char     type;  /* 'd', 'f' or 'l' */
    unsigned mode;  /* permission bits; 0 for links */
    char    *value; /* "sha256:<hex>" for files, the target for links, "-" */
    char    *path;
} tb_entry_t;

typedef struct {
    int format;
    tb_entry_t *v;
    size_t      n, cap;
    char       *text; /* the MANIFEST bytes as read or written */
    size_t      len;
} tb_manifest_t;

typedef struct {
    char *kind;   /* "command" or "file" */
    char *target;
    char *pkgs;   /* "arch=x void=y", or "" */
} tb_need_t;

typedef struct {
    int format;
    char *buildenv;
    char **targets; size_t ntargets;
    char *derived;
    char **edits; size_t nedits;
    char      *name, *version, *hash, *host, *built;
    char     **sources; size_t nsources; /* "<component> <commit>" */
    char     **tools;   size_t ntools;
    tb_need_t *needs;   size_t nneeds;
    char     **after;   size_t nafter;
    char      *text;    /* the BUNDLE bytes */
    size_t     len;
    char       id[256]; /* <name>-<version>-<hash 12> */
} tb_bundle_t;

void tb_manifest_free(tb_manifest_t *m);
void tb_bundle_free(tb_bundle_t *b);
const tb_entry_t *tb_manifest_find(const tb_manifest_t *m, const char *path);

/* Parse files already read; on failure print why (prefixed by `what`). */
int  tb_manifest_parse(const char *text, size_t len, const char *what, tb_manifest_t *m);
int  tb_bundle_parse(const char *text, size_t len, const char *what, tb_bundle_t *b);
/* Read and parse BUNDLE and MANIFEST in directory `dir` (relative to dirfd). */
int  tb_tree_read(int dirfd, const char *dir, tb_bundle_t *b, tb_manifest_t *m);
/* The bundle hash: SHA-256 over BUNDLE without its hash and built lines,
 * then MANIFEST. */
void tb_bundle_hash(const char *btext, size_t blen, const char *mtext, size_t mlen,
                    char out[TB_HASH_LEN + 1]);
bool tb_valid_name(const char *s);
bool tb_valid_step(const char *s);
extern const char *const tb_steps[];

/* ---- Trees (tree.c) ---------------------------------------------------- */

typedef struct {
    char    *path;
    long long size, ino, mtime_s, mtime_ns, ctime_s, ctime_ns;
} tb_stat_t;

typedef struct {
    tb_stat_t *v;
    size_t     n, cap;
} tb_stats_t;

typedef struct {
    size_t   edited, missing, extra;
    bool     quiet;          /* do not print each problem */
    char   **paths;          /* edited or extra regular files, for saving */
    size_t   npaths;
} tb_check_t;

/* Build MANIFEST for the tree at `root` (BUNDLE and MANIFEST excluded). */
int  tb_manifest_build(const char *root, tb_manifest_t *m);
/* Compare the tree at `root` with `m`. With `stats` (may be NULL) and not
 * `full`, a file whose size, times and inode match is not hashed. Returns 0
 * when the walk completed (problems are counted in `out`), -1 on error. */
int  tb_tree_check(const char *root, const tb_manifest_t *m, const tb_stats_t *stats,
                   bool full, tb_check_t *out);
void tb_check_free(tb_check_t *c);
int  tb_stats_build(const char *root, const tb_manifest_t *m, tb_stats_t *s);
int  tb_stats_write(const char *path, const tb_stats_t *s);
int  tb_stats_read(const char *path, tb_stats_t *s); /* missing file: empty, 0 */
void tb_stats_free(tb_stats_t *s);
long long tb_tree_bytes(const char *root);

/* ---- Slots and state (slot.c) ------------------------------------------ */

typedef struct {
    char *dir;      /* slot directory, absolute */
    int   fd;
    char *name;     /* slot name: the directory's last component */
    char *state;    /* state directory */
    int   lock_fd;
    dev_t dev;
} tb_slot_t;

typedef struct {
    char op[16];      /* "switch" or "back" */
    char from[256];   /* bundle id, or "-" for an empty slot */
    char to[256];
    char source[300]; /* slot-relative path exchanged with current */
} tb_intent_t;

typedef struct {
    char  **ids;
    char  **reasons;
    size_t  n;
} tb_keep_t;

/* Open a slot. `state` NULL picks /var/lib/tam-bundle/<name> for a slot
 * owned by root and $XDG_STATE_HOME/tamlinux/tam-bundle/<name> otherwise. With `create`, make
 * the slot, kept/ and state directories where missing. */
int  tb_slot_open(tb_slot_t *s, const char *dir, const char *state, bool create, bool plan_missing);
/* The state directory tb_slot_open would choose. Caller frees. */
char *tb_state_dir(const char *dir, const char *state);
/* True when `dir`, or its nearest existing parent, belongs to root. */
bool tb_owned_by_root(const char *dir);
void tb_slot_close(tb_slot_t *s);
int  tb_slot_lock(tb_slot_t *s);
int  tb_slot_plan_lock(tb_slot_t *s); /* shared, read-only; no file created */
char *tb_state_path(const tb_slot_t *s, const char *leaf); /* caller frees */
/* Remove a slot-relative tree ("kept/<id>", "staging.<id>"), making its
 * directories writable first. Missing is success. */
int  tb_remove_tree(const tb_slot_t *s, const char *rel);
/* The same for `rel` under any directory: `basefd` open on `base`. */
int  tb_remove_at(int basefd, const char *base, const char *rel);

int  tb_intent_read(const tb_slot_t *s, tb_intent_t *it); /* 1 present, 0 none, -1 error */
int  tb_intent_write(const tb_slot_t *s, const tb_intent_t *it);
int  tb_intent_clear(const tb_slot_t *s);

int  tb_keep_read(const tb_slot_t *s, tb_keep_t *k);
int  tb_keep_write(const tb_slot_t *s, const tb_keep_t *k);
void tb_keep_set(tb_keep_t *k, const char *id, const char *reason);
void tb_keep_drop(tb_keep_t *k, const char *id);
const char *tb_keep_reason(const tb_keep_t *k, const char *id);
const char *tb_keep_previous(const tb_keep_t *k);
void tb_keep_free(tb_keep_t *k);

/* Append one line to the log: `<UTC time> <text>`. */
int  tb_log(const tb_slot_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* session.<uid> in the state directory: the results of session steps that
 * a root run left for that user, who cannot write the log. Only root
 * creates it (tb_session_prepare, owned by `uid`, 0644); the user appends
 * to it with tb_log_session, which never creates it. */
int  tb_session_prepare(const tb_slot_t *s, uid_t uid);
int  tb_log_session(const tb_slot_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* The live bundle id in `out`; "-" when the slot has no current/. */
int  tb_current_id(const tb_slot_t *s, char out[256]);

/* ---- Operations (switch.c) --------------------------------------------- */

typedef struct {
    bool save_edits;
    bool dry_run;
    bool full;
} tb_switch_opts_t;

int  tb_switch(tb_slot_t *s, const char *bundle_dir, const tb_switch_opts_t *o);
int  tb_back(tb_slot_t *s, const char *id, const tb_switch_opts_t *o);
/* Finish or undo an interrupted switch. Returns TB_OK when the slot is clean
 * afterwards; sets *ran_after_due when post-install steps are still due. */
int  tb_recover(tb_slot_t *s, bool dry_run, bool *steps_due);
/* Recover before changing the slot; a dry run only reports. TB_OK when the
 * slot is clean (or a recovered switch's post-install step failed). */
int  tb_recover_first(tb_slot_t *s, bool dry_run);

/* ---- Steps and needs (after.c) ----------------------------------------- */

/* Run the bundle's post-install steps, logging each. Session steps
 * (user-units-reload, qml-cache-purge, shell-restart) need the desktop
 * user: run as root they print a note, and run as root through pkexec they
 * are logged "after <step> deferred to uid <uid>" for the user's own
 * tam-bundle to run with tb_run_steps. */
int  tb_run_after(const tb_slot_t *s, const tb_bundle_t *b);
int  tb_run_steps(const tb_slot_t *s, char *const *steps, size_t n);
bool tb_step_is_session(const char *step);
/* Print missing needs; returns the number missing. */
size_t tb_needs_check(const tb_bundle_t *b, bool print);

/* ---- Privilege (elevate.c) --------------------------------------------- */

/* Run `tam-bundle <args>` as root through pkexec (args end with NULL and
 * hold no relative paths), then the session steps it deferred to this
 * user. Returns the root run's exit code, or TB_AFTER_FAILED when only a
 * session step failed. */
int  tb_pkexec(char **args, const char *slot, const char *state);
/* The test build: $TAM_BUNDLE_PKEXEC names a stand-in for pkexec, used
 * whatever the slot's owner. Always false in the installed build. */
bool tb_test_pkexec(void);

/* ---- Sealing (seal.c) -------------------------------------------------- */

typedef struct {
    int format;
    const char *buildenv;
    const char *derived;
    const char *const *edits; size_t nedits;
    const char *const *targets; size_t ntargets;
    const char *name, *version, *host, *built; /* host NULL: any; built NULL: now */
    const char *const *sources; size_t nsources; /* "<component> <commit>" */
    const char *const *tools;   size_t ntools;   /* "<name> <version>" */
    const char *const *needs;   size_t nneeds;
    const char *const *after;   size_t nafter;
} tb_seal_t;

/* Write BUNDLE and MANIFEST for the tree at `dir`; `id` (may be NULL)
 * receives the bundle id. */
int  tb_seal(const char *dir, const tb_seal_t *o, char id[256]);

/* ---- Git (git.c) ------------------------------------------------------- */

/* A full commit id: 40 (SHA-1) or 64 (SHA-256) lowercase hex digits. */
bool tb_valid_commit(const char *s);
bool tb_git_has(const char *repo, const char *commit);
/* The full commit `rev` names in `repo`; NULL when it names none. */
char *tb_git_resolve(const char *repo, const char *rev);
int  tb_git_commit_time(const char *repo, const char *commit, long long *t);
/* Make sure `mirror` (a bare repository, created if missing) holds `commit`,
 * fetching from `url` only when it does not. */
int  tb_git_fetch(const char *mirror, const char *url, const char *commit);
/* Export the files of `commit` into the existing empty directory `dest`. */
int  tb_git_export(const char *repo, const char *commit, const char *dest);
/* Read a regular file from an exact commit, never from the work tree. */
char *tb_git_file(const char *repo, const char *commit, const char *path, size_t *len);
/* Repository containing dir, or NULL when it is outside a Git work tree. */
char *tb_git_root(const char *dir);

/* ---- Lock files (lock.c) ----------------------------------------------- */

typedef struct {
    char *name, *url, *commit;
} tb_lock_entry_t;

typedef struct {
    tb_lock_entry_t *v;
    size_t           n;
    char *text; size_t len;
} tb_lock_t;

int  tb_lock_read(const char *path, tb_lock_t *l);
void tb_lock_free(tb_lock_t *l);
const tb_lock_entry_t *tb_lock_find(const tb_lock_t *l, const char *name);

typedef struct {
    const char *file;  /* lock file */
    const char *url;   /* needed to add a component */
    const char *repos; /* directory of local clones, to resolve a short rev */
    bool        dry_run;
} tb_lock_opts_t;

/* Set one component's commit; the only change made to the file. */
int  tb_lock_set(const char *component, const char *rev, const tb_lock_opts_t *o);

/* ---- Building (build.c, recipe.c) -------------------------------------- */

typedef struct {
    char  *name;     /* the component */
    char  *text;     /* the recipe bytes, part of the cache key */
    size_t len;
    bool   build_only; /* installs into a private prefix, adds no bundle files */
    char **vars;  size_t nvars;   /* "NAME=value" make variables */
    char **deps;  size_t ndeps;   /* after-build components */
    char **targets; size_t ntargets; /* make-targets; empty means default */
    char **installs; size_t ninstalls; /* install-targets; default install */
    char **build_needs; size_t nbuild_needs; /* native package names */
} tb_recipe_t;

typedef struct {
    char *source, *path; /* packaging repository path, bundle tree path */
} tb_recipe_file_t;

typedef struct {
    int format;
    char *text; size_t len;
    char        *name, *host;
    char **targets; size_t ntargets;
    char **placed; size_t nplaced;
    char       **needs; size_t nneeds;
    char       **after; size_t nafter;
    bool         index_man;        /* `index man`: build share/man's index */
    tb_recipe_file_t *files; size_t nfiles;
    tb_recipe_t *v;     size_t n;  /* in build order */
} tb_recipes_t;

/* Read `<dir>/bundle` and every `<dir>/<component>.recipe`, check them, and order the
 * recipes so each comes after the components it names in after-build. */
int  tb_recipes_read(const char *dir, tb_recipes_t *r);
void tb_recipes_free(tb_recipes_t *r);

typedef struct {
    const char *recipes, *lock, *output;
    const char *version;
    const char *host;    /* overrides the bundle file's host */
    const char *repos;   /* directory of local clones tried before the URL */
    const char *prefix;  /* the live tree the bundle is built for */
    const char *built;   /* NULL: now */
    bool verify;
} tb_build_opts_t;

int  tb_build(const tb_build_opts_t *o);

/* ---- The manual index (manindex.c) ------------------------------------- */

/* Set every entry under <tree>/share/man to `epoch`, then run mandb on it,
 * leaving share/man/index.db. Refuses a tree without share/man, or one
 * where a component installed an index. */
int   tb_man_index(const char *tree, long long epoch);
char *tb_man_index_tool(void); /* "man-db <version>", or NULL; caller frees */

/* ---- Fixed build paths (isolate.c) ------------------------------------- */

/* Builds run with their work tree at TB_BUILD_ROOT/work and each build-only
 * dependency at TB_BUILD_ROOT/deps/<component>, whatever the cache's path. */
#define TB_BUILD_ROOT "/tmp/tam-bundle"
#define TB_MAX_BINDS 64

typedef struct {
    const char *from, *to;
    bool        read_only;
} tb_bind_t;

/* In a child about to exec: enter a new user and mount namespace and mount
 * each `from` at its `to`. 0, or -1 with errno and `why` naming the step. */
int  tb_isolate(const tb_bind_t *binds, size_t n, const char **why);

/* ---- Test fault points (fault.c) --------------------------------------- */

/* In the test build, exit at once when $TAM_BUNDLE_FAULT names `point`;
 * elsewhere it does nothing. */
void tb_fault(const char *point);
void tb_fault_file(size_t index);

/* ---- Helpers (util.c) -------------------------------------------------- */

char *tb_read_at(int dirfd, const char *rel, size_t *len); /* NUL-terminated */
char *tb_read_limit(int dirfd,const char *rel,size_t *len,size_t limit);
/* Open a file for reading strictly beneath dirfd: no symbolic link in any
 * component (openat2 RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS). */
int   tb_open_beneath(int dirfd, const char *rel);
void  tb_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void  tb_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern bool tb_quiet;

/* Format 2 product transactions. Paths are rooted at root (default /).
 * Profile commands are supplied by the rootstock, never the bundle. */
int tb_v2_main(const char *root, const char *cmd, const char *arg, bool dry,
               bool if_pending, bool yes);
int tb_v2_verify(const char *dir);
int tb_archive_unpack(const char *archive, const char *dest);
int tb_archive_pack(const char *dir, const char *archive);
int tb_busy(const char *root, const char *old, const tb_manifest_t *m, bool stop);
int tb_v2_manifest(const char *dir, tb_manifest_t *m);
void tb_hash_settings(const char *b, size_t bn, const char *m, size_t mn,
                      const char *settings, size_t sn, char out[TB_HASH_LEN+1]);
int tb_settings_capture(const char *root);
int tb_settings_host(const char *root, int state);
int tb_settings_main(const char *root, const char *command, const char *arg);
int tb_settings_archive_check(const char *dir, size_t *entries);
int tb_settings_export(const char *root, const char *output, bool save_edits);
int tb_settings_import(const char *root, const char *archive);
bool tb_settings_private_output(const char *root, const char *output);
int tb_settings_diff(const char *root, const char *path);

/* Arch native database snapshot: bounded read-only pacman queries. */
typedef struct { char *path,*package; } tb_native_file;
typedef struct {
    char *root,*versions;
    tb_native_file *files; size_t nfiles;
} tb_native_t;
int tb_native_read(const char *root,bool files,tb_native_t *n);
int tb_native_read_hook(const char *root,tb_native_t *n);
void tb_native_free(tb_native_t *n);
const char *tb_native_owner(const tb_native_t *n,const char *path);
char *tb_native_version(const tb_native_t *n,const char *package);
int tb_native_conflicts(const char *root,const tb_manifest_t *a,const tb_manifest_t *b);
bool tb_need_valid(const char *kind,const char *target,const char *rest);
int tb_contract_check(const char *root,const char *product,const tb_bundle_t *b,bool native_hook);
int tb_link_check(const char *root,const char *product,const tb_bundle_t *b,bool declarations);
int tb_elf_probe(const char *root,const char *product,const tb_bundle_t *b,const char *path);
int tb_runtime_open(int root,const char *absolute);
int tb_version_compare(const char *actual,const char *required);
bool tb_placement_allowed(const char *path,bool directory,char *const *roots,size_t n);
bool tb_buildenv_matches(const char *hash,const tb_manifest_t *m);
#endif
